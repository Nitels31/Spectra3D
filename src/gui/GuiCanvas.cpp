#include "gui/GuiCanvas.hpp"

#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <algorithm>
#include <cmath>

namespace s3d {
namespace gui {

const char* elementKindName(ElementKind kind) {
    switch (kind) {
        case ElementKind::Panel: return "panel";
        case ElementKind::Image: return "image";
        case ElementKind::Text: return "text";
        case ElementKind::Button: return "button";
    }
    return "panel";
}

bool elementKindFromName(const std::string& name, ElementKind& out) {
    struct Entry {
        const char* name;
        ElementKind kind;
    };
    static const Entry kEntries[] = {
        {"panel", ElementKind::Panel},   {"image", ElementKind::Image},
        {"text", ElementKind::Text},     {"button", ElementKind::Button},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.kind;
            return true;
        }
    return false;
}

int Canvas::find(const std::string& name) const {
    if (name.empty()) return -1;
    for (size_t i = 0; i < elements.size(); ++i)
        if (elements[i].name == name) return (int)i;
    return -1;
}

Element* Canvas::at(int index) {
    if (index < 0 || index >= (int)elements.size()) return nullptr;
    return &elements[(size_t)index];
}

const Element* Canvas::at(int index) const {
    if (index < 0 || index >= (int)elements.size()) return nullptr;
    return &elements[(size_t)index];
}

Rect Canvas::absoluteRect(int index) const {
    const Element* element = at(index);
    if (!element) return Rect{};

    Rect rect{element->x, element->y, element->width, element->height};
    // The offsets of the parents, from the closest one outwards. The walk is
    // bounded: a canvas with a cycle in it is a file that was edited by hand,
    // and a walk without a bound would never come back from it.
    int parent = element->parent;
    int guard = 0;
    while (parent >= 0 && parent < (int)elements.size() && guard++ < 64) {
        const Element& above = elements[(size_t)parent];
        rect.x += above.x;
        rect.y += above.y;
        parent = above.parent;
    }
    return rect;
}

void Canvas::fit(const Rect& area, float& scale, float& offsetX, float& offsetY) const {
    // One scale for both directions: a canvas that was stretched in one
    // direction only would have letters that are wider than they are tall.
    const float w = width > 1.0f ? width : 1.0f;
    const float h = height > 1.0f ? height : 1.0f;
    scale = 1.0f;
    if (area.width > 0.0f && area.height > 0.0f)
        scale = std::fmin(area.width / w, area.height / h);
    // And centred in what is left, so a wide screen has the canvas in the
    // middle of it rather than in a corner.
    offsetX = area.x + (area.width - w * scale) * 0.5f;
    offsetY = area.y + (area.height - h * scale) * 0.5f;
}

Rect Canvas::screenRect(int index, const Rect& area) const {
    const Rect own = absoluteRect(index);
    float scale = 1.0f, offsetX = 0.0f, offsetY = 0.0f;
    fit(area, scale, offsetX, offsetY);
    return Rect{offsetX + own.x * scale, offsetY + own.y * scale, own.width * scale,
                own.height * scale};
}

int Canvas::pickAt(const Rect& area, float px, float py) const {
    // The last one drawn over the point: the canvas is drawn in the order of
    // the list, and what is drawn later is what is seen.
    int found = -1;
    for (size_t i = 0; i < elements.size(); ++i) {
        const Element& element = elements[i];
        if (!element.visible) continue;
        if (!screenRect((int)i, area).contains(px, py)) continue;
        found = (int)i;
    }
    return found;
}

int Canvas::add(const Element& element) {
    elements.push_back(element);
    return (int)elements.size() - 1;
}

void Canvas::remove(int index) {
    if (index < 0 || index >= (int)elements.size()) return;

    // Everything that goes with the element: the element itself and every
    // descendant of it. A panel that is taken away takes the things inside it
    // with it - a label that is left behind with no panel to be in is a label
    // floating in the middle of the screen.
    //
    // The whole set is collected first and removed afterwards, from the back:
    // removing one element shifts the indices of the ones after it, and a
    // second removal by index would take the wrong one.
    std::vector<int> doomed;
    doomed.push_back(index);
    // Everything whose parent is on the list goes on the list as well, and the
    // list grows until nothing new is found: a child of a child is as gone as
    // a child.
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (std::find(doomed.begin(), doomed.end(), (int)i) != doomed.end()) continue;
            if (elements[i].parent < 0) continue;
            if (std::find(doomed.begin(), doomed.end(), elements[i].parent) == doomed.end())
                continue;
            doomed.push_back((int)i);
            changed = true;
        }
    }
    std::sort(doomed.begin(), doomed.end());
    doomed.erase(std::unique(doomed.begin(), doomed.end()), doomed.end());

    // The ones that pointed at something that is going away now point at
    // nothing, and the ones that pointed past it point one place too far.
    for (Element& element : elements) {
        if (element.parent < 0) continue;
        if (std::find(doomed.begin(), doomed.end(), element.parent) != doomed.end()) {
            element.parent = -1;
            continue;
        }
        // How many of the removed ones were below it.
        int below = 0;
        for (int gone : doomed)
            if (gone < element.parent) ++below;
        element.parent -= below;
    }

    for (int i = (int)doomed.size() - 1; i >= 0; --i)
        elements.erase(elements.begin() + doomed[(size_t)i]);
}

bool Canvas::wouldCycle(int parent, int child) const {
    if (parent < 0) return false;
    if (parent == child) return true;
    // The parent must not be a descendant of the child: making a thing its own
    // grandparent is a cycle, and the offsets would grow without end.
    int walk = parent;
    int guard = 0;
    while (walk >= 0 && walk < (int)elements.size() && guard++ < 64) {
        if (walk == child) return true;
        walk = elements[(size_t)walk].parent;
    }
    return false;
}

std::string resolveTexturePath(const std::string& relative) {
    if (relative.empty()) return std::string();
    // A path that is already absolute is used as it is: the scene may name a
    // file outside the project, and a file that is there is a file that is
    // there.
    if (relative.size() > 1 && (relative[0] == '/' || relative[1] == ':')) return relative;

    for (const std::string& root : fs::assetRoots()) {
        if (root.empty()) continue;
        const std::string candidate = str::join(root, relative);
        if (fs::exists(candidate)) return candidate;
    }
    // Nothing there: the name is returned as it was given, so that the caller
    // can say which picture was missing.
    return relative;
}

} // namespace gui
} // namespace s3d
