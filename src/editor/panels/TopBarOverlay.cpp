#include "editor/Editor.hpp"

#include "core/Log.hpp"
#include "core/Project.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/GlInfo.hpp"
#include "scene/SceneSerializer.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cstdio>

namespace s3d {
namespace {

constexpr float kRowHeight = 20.0f;
constexpr float kMenuRowHeight = 22.0f;
constexpr float kPanelPadding = 4.0f;
constexpr float kMenuPanelWidthMin = 150.0f;
constexpr float kMenuPanelWidthMax = 240.0f;
constexpr float kLayersPanelWidth = 240.0f;
constexpr float kLayerRowHeight = 20.0f;
constexpr float kLayerToggleSize = 16.0f;
constexpr float kHelpPanelWidth = 460.0f;
constexpr float kHelpLineHeight = 22.0f;

constexpr int kMaxLayerRows = 12;
constexpr int kMaxSearchRows = 8;

// Dialogs: one centred panel, a title, fields, and two buttons at the bottom
// right. Sized here rather than per dialog so every one of them has the same
// frame and the same button positions.
constexpr float kModalWidth = 460.0f;
constexpr float kModalRowHeight = 22.0f;
constexpr int kModalButton = 1700;

// A dialog's explanation has to fit its dialog. One line that runs off the edge is
// a sentence whose last words - usually the ones saying what is about to happen -
// are the ones nobody reads, so the words wrap and the box below makes room.
// Word wrapping, measured twice: once to learn how tall the text will be (the
// dialog has to be sized before it can be drawn) and once to draw it. Both passes
// walk the same words with the same widths, so they cannot disagree.
const std::vector<std::string> wrapWords(const UiRenderer& ui, float maxWidth,
                                         const char* utf8) {
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    const std::string_view all(utf8);
    while (at <= all.size()) {
        const size_t space = all.find(' ', at);
        const std::string_view word = all.substr(
            at, space == std::string_view::npos ? std::string_view::npos : space - at);
        const std::string candidate =
            line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && ui.textWidth(candidate.c_str()) > maxWidth) {
            lines.push_back(line);
            line = std::string(word);
        } else {
            line = candidate;
        }
        if (space == std::string_view::npos) break;
        at = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float wrappedHeight(const UiRenderer& ui, float maxWidth, const char* utf8) {
    return (float)wrapWords(ui, maxWidth, utf8).size() * ((float)theme::textLineHeight + 4.0f);
}

float drawWrapped(UiRenderer& ui, float x, float y, float maxWidth, const char* utf8,
                  const Color& tint) {
    const std::vector<std::string> lines = wrapWords(ui, maxWidth, utf8);
    for (size_t i = 0; i < lines.size(); ++i)
        ui.text(x, y + (float)i * ((float)theme::textLineHeight + 4.0f), lines[i].c_str(), tint);
    return (float)lines.size() * ((float)theme::textLineHeight + 4.0f);
}

// The two small toggles in a layers row. Drawn rather than typed: there is no
// dependable glyph for an eye or a padlock, and a shape cannot go missing from
// the atlas.
enum class Toggle { Visible, Locked };

void drawToggle(UiRenderer& ui, const Rect& rect, Toggle toggle, bool on) {
    const Color tint = on ? theme::accent : theme::textDim;
    const float x = rect.x + (rect.width - kLayerToggleSize) * 0.5f;
    const float y = rect.y + (rect.height - kLayerToggleSize) * 0.5f;
    if (toggle == Toggle::Visible) {
        // Filled when the body is drawn, an empty frame when it is not.
        if (on)
            ui.fillRect(x + 2.0f, y + 2.0f, kLayerToggleSize - 4.0f, kLayerToggleSize - 4.0f,
                        tint);
        ui.fillRect(x, y, kLayerToggleSize, 1.0f, tint);
        ui.fillRect(x, y + kLayerToggleSize - 1.0f, kLayerToggleSize, 1.0f, tint);
        ui.fillRect(x, y, 1.0f, kLayerToggleSize, tint);
        ui.fillRect(x + kLayerToggleSize - 1.0f, y, 1.0f, kLayerToggleSize, tint);
        return;
    }
    // Padlock: body, and a shackle that opens on the left when unlocked - which is
    // the difference between "locked" and "not drawn at all".
    ui.fillRect(x + 2.0f, y + 7.0f, kLayerToggleSize - 4.0f, kLayerToggleSize - 8.0f, tint);
    ui.fillRect(x + 4.0f, y + 2.0f, 1.5f, 5.0f, theme::textDim);
    if (on) {
        ui.fillRect(x + 4.0f, y + 2.0f, kLayerToggleSize - 8.0f, 1.5f, tint);
        ui.fillRect(x + kLayerToggleSize - 5.5f, y + 2.0f, 1.5f, 5.0f, tint);
    }
}

// A tick for a checked row, drawn as two diagonals of one-and-a-half pixel dots.
// A font glyph for it cannot be relied on, a shape cannot go missing from the
// atlas, and stacking rows is the only way this batcher can draw a diagonal.
void drawTick(UiRenderer& ui, const Rect& row, const Color& color) {
    const float x = row.x + row.width - 18.0f;
    const float y = row.y + row.height * 0.5f - 3.0f;
    static const float kShort[][2] = {{0.0f, 3.0f}, {1.5f, 4.5f}, {3.0f, 6.0f}, {4.5f, 4.5f}};
    static const float kLong[][2] = {{4.5f, 4.5f}, {6.0f, 3.0f}, {7.5f, 1.5f}, {9.0f, 0.0f}};
    for (const auto& point : kShort) ui.fillRect(x + point[0], y + point[1], 1.6f, 1.6f, color);
    for (const auto& point : kLong) ui.fillRect(x + point[0], y + point[1], 1.6f, 1.6f, color);
}

} // namespace

// Everything the top bar drops down: the five menus, the search results, the
// layers panel and the help pages.
//
// Drawn last in the frame and after a flush, because all of it hangs over the
// panels below and the batcher emits every rectangle before any text - without
// the flush a dropdown's background would land under the labels of the panel it
// covers, and its own text under nothing at all.
//
// Interaction is resolved here rather than in the bar: the bar decides what is
// open, this pass draws it and reads the presses on its own rows. Two things keep
// it from fighting the panels it covers. The editor claims the pointer for every
// popup before those panels are drawn (Editor::renderUi), so a click on a row never
// reaches the button underneath it - and the capture is released again right before
// this pass, because the rows here are what the user was aiming at. A press that
// opens a panel must not also act on it, which is what overlayOpenedThisFrame_ is
// for.
// Registers every open overlay's rectangle with the guard, in draw order, without
// drawing anything. Runs BEFORE the panels so a click under a dropdown is already
// suppressed when their hit tests run; drawTopBarOverlay() then paints the same
// rectangles. Split from the draw on purpose: the hit test happens in the middle of
// the frame, and what is on top has to be known by then.
//
// The rectangles are measured here rather than reused from the draw pass, because
// the two passes are far apart in the frame. They read the same layout_, so they
// cannot disagree - and a menu that scrolls changes shape between them, which is
// why the scroll is applied to both.
void Editor::declareTopBarOverlays(UiRenderer& ui, int fbWidth, int fbHeight, int bodyCount) {
    ui_.overlays().clear();
    if (openMenu_ < 0 && !layersOpen_ && helpPage_ == 0 && toolbarSearch_.empty()) return;

    if (helpPage_ != 0) {
        const bool about = helpPage_ == 2;
        const int lines = about ? 4 : 7;
        const float height =
            kPanelPadding * 2.0f + 30.0f + (float)lines * kHelpLineHeight + 26.0f;
        ui_.overlays().push((float)fbWidth * 0.5f - kHelpPanelWidth * 0.5f,
                            (float)fbHeight * 0.5f - height * 0.5f, kHelpPanelWidth, height);
    }

    if (openMenu_ >= 0) {
        float captionX = 6.0f;
        for (int i = 0; i < openMenu_; ++i) captionX += ui.textWidth(menuCaption(i)) + 18.0f;

        const int count = menuRowCount(openMenu_);
        float width = kMenuPanelWidthMin;
        for (int i = 0; i < count; ++i) {
            const MenuRow& row = menuRow(openMenu_, i);
            if (row.separator || !row.text) continue;
            width = std::fmax(width, ui.textWidth(row.text) + 34.0f);
        }
        width = std::fmin(width, kMenuPanelWidthMax);

        const float panelY = layout_.menuBar.y + layout_.menuBar.height - 1.0f;
        const float available =
            std::fmax(3.0f * kMenuRowHeight, (float)fbHeight - panelY - 8.0f);
        const int visible =
            std::clamp((int)((available - kPanelPadding * 2.0f) / kMenuRowHeight), 1, count);
        const int shown = std::min(count, visible);
        const int hidden = count - shown;

        // The height is the same whatever row the list is scrolled to - only the
        // contents move - so the rectangle does not need `first` at all. That is
        // what lets this function skip measuring the rows a second time.
        const int rows = shown + (hidden > 0 ? 1 : 0);
        ui_.overlays().push(captionX, panelY, width,
                            kPanelPadding * 2.0f + (float)rows * kMenuRowHeight);
    }

    if (layersOpen_) {
        const int total = std::max(1, bodyCount);
        const int rows = std::min(total, kMaxLayerRows);
        const float footer = total > rows ? kLayerRowHeight : 0.0f;
        const float height = 24.0f + (float)rows * kLayerRowHeight + footer + kPanelPadding;
        const Rect anchor = layersAnchor_.width > 0.0f ? layersAnchor_ : layersButton_;
        const float panelX = std::fmax(
            4.0f, std::fmin(anchor.x, (float)fbWidth - kLayersPanelWidth - 4.0f));
        ui_.overlays().push(panelX, layout_.menuBar.y + layout_.menuBar.height - 1.0f,
                            kLayersPanelWidth, height);
    }

    if (!toolbarSearch_.empty()) {
        // Non-blocking: the search list is a suggestion. A click that misses it
        // belongs to the scene behind it, not to nothing at all.
        ui_.overlays().push(OverlayRect{
            searchField_.x, layout_.menuBar.y + layout_.menuBar.height - 1.0f,
            searchField_.width > 0.0f ? searchField_.width : 160.0f,
            kPanelPadding * 2.0f + 3.0f * kRowHeight, false});
    }
}

void Editor::drawTopBarOverlay(UiRenderer& ui, PhysicsWorld& world, Camera& camera,
                               Window& window, int fbWidth, int fbHeight) {
    if (openMenu_ < 0 && !layersOpen_ && helpPage_ == 0 && toolbarSearch_.empty()) return;

    // True while the pointer is over a panel drawn below, so the closing press at
    // the end does not also eat the press that hit a row.
    bool insidePanel = false;
    // The press that opened whatever is on screen. It is still this frame's press,
    // and the pointer may well be sitting on top of a row already - a menu opening
    // under the cursor must not run the row underneath it on the same click.
    const bool openedNow = overlayOpenedThisFrame_;

    // --- help and about, centred over everything ---------------------------
    if (helpPage_ != 0) {
        const bool about = helpPage_ == 2;
        // Key and description in two columns: the key is what a hand goes for and
        // it has to line up, which a single "key - description" string cannot do.
        static const char* const kHelpKeys[7] = {
            "help.key.move", "help.key.up",    "help.key.boost", "help.key.look",
            "help.key.gravity", "help.key.select", "help.key.tools"};
        static const char* const kHelpDescs[7] = {
            "help.desc.move", "help.desc.up",    "help.desc.boost", "help.desc.look",
            "help.desc.gravity", "help.desc.select", "help.desc.tools"};
        const int lines = about ? 4 : (int)(sizeof(kHelpKeys) / sizeof(kHelpKeys[0]));

        // The key column is as wide as its widest caption - measured, because
        // "Ліва колонка" is twice as wide as "Shift" and a fixed column either
        // clips one or leaves a gap under the other.
        float keyColumn = 0.0f;
        if (!about)
            for (int i = 0; i < lines; ++i)
                keyColumn = std::fmax(keyColumn, ui.textWidth(text(kHelpKeys[i])));
        keyColumn = std::fmax(0.0f, keyColumn) + 14.0f;

        const float height =
            kPanelPadding * 2.0f + 30.0f + (float)lines * kHelpLineHeight + 26.0f;
        const Rect panel{(float)fbWidth * 0.5f - kHelpPanelWidth * 0.5f,
                         (float)fbHeight * 0.5f - height * 0.5f, kHelpPanelWidth, height};
        ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
        ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::separator);
        ui.fillRect(panel.x, panel.y + panel.height - 1.0f, panel.width, 1.0f,
                    theme::separator);
        ui.text(panel.x + 14.0f, panel.y + 9.0f, text(about ? "help.about" : "help.controls"),
                theme::accent);

        float y = panel.y + 32.0f;
        if (about) {
            char line[128];
            std::snprintf(line, sizeof(line), "%s %s", kEngineName, kEngineVersion);
            ui.text(panel.x + 14.0f, y, line, theme::text);
            y += kHelpLineHeight;
            std::snprintf(line, sizeof(line), "OpenGL %s", gl::requestedVersion());
            ui.text(panel.x + 14.0f, y, line, theme::textDim);
            y += kHelpLineHeight;
            ui.text(panel.x + 14.0f, y, text("help.sceneFile"), theme::textDim);
            ui.textFit(panel.x + 14.0f + keyColumn, y, panel.width - keyColumn - 28.0f,
                       scenePath(), theme::textDim);
            y += kHelpLineHeight;
            ui.text(panel.x + 14.0f, y, text("help.language"), theme::textDim);
            ui.text(panel.x + 14.0f + keyColumn, y, loc_.languageName().c_str(),
                    theme::textDim);
        } else {
            for (int i = 0; i < lines; ++i) {
                ui.text(panel.x + 14.0f, y, text(kHelpKeys[i]), theme::accent);
                ui.textFit(panel.x + 14.0f + keyColumn, y, panel.width - keyColumn - 28.0f,
                           text(kHelpDescs[i]), theme::textDim);
                y += kHelpLineHeight;
            }
        }

        const Rect close{panel.x + panel.width - 96.0f, panel.y + panel.height - 26.0f, 82.0f,
                         20.0f};
        if (ui_.button(ui, kHelpClose, close.x, close.y, close.width, close.height,
                       text("help.close")))
            helpPage_ = 0;
        insidePanel |= ui_.hovered(panel);
    }

    // --- the open dropdown --------------------------------------------------
    if (openMenu_ >= 0) {
        // Anchored under its own caption: the menu is the third of the bar
        // whatever the window width is, and the panel follows the label rather
        // than a rectangle stored when the bar was drawn.
        float captionX = 6.0f;
        for (int i = 0; i < openMenu_; ++i) captionX += ui.textWidth(menuCaption(i)) + 18.0f;

        const int count = menuRowCount(openMenu_);
        // As wide as its longest row, so a two-word menu does not come with a
        // hundred pixels of empty panel behind it. Bounded so a very long
        // translation cannot push the rows off the screen.
        float width = kMenuPanelWidthMin;
        for (int i = 0; i < count; ++i) {
            const MenuRow& row = menuRow(openMenu_, i);
            if (row.separator || !row.text) continue;
            width = std::fmax(width, ui.textWidth(row.text) + 34.0f);
        }
        width = std::fmin(width, kMenuPanelWidthMax);

        // A menu is as long as its contents, and Create is every model on disk.
        // The bound is the window: rows that do not fit are reached with the wheel,
        // and the footer says how many are below.
        const float panelY = layout_.menuBar.y + layout_.menuBar.height - 1.0f;
        const float available =
            std::fmax(3.0f * kMenuRowHeight, (float)fbHeight - panelY - 8.0f);
        const int visible =
            std::clamp((int)((available - kPanelPadding * 2.0f) / kMenuRowHeight), 1, count);
        const int maxFirst = std::max(0, count - visible);
        const int shown = std::min(count, visible);
        const int hidden = count - shown;

        auto panelRect = [&](int first) {
            const int rows = shown + (hidden > 0 ? 1 : 0);
            return Rect{captionX, panelY, width, kPanelPadding * 2.0f + (float)rows * kMenuRowHeight};
        };

        int first = std::clamp((int)(menuScroll_ / kMenuRowHeight), 0, maxFirst);
        Rect panel = panelRect(first);
        // Two rows per notch, the same as the scene tree.
        if (ui_.hovered(panel) && ui_.scrollDelta() != 0.0) {
            menuScroll_ = clampf(menuScroll_ - (float)ui_.scrollDelta() * 2.0f * kMenuRowHeight,
                                 0.0f, (float)maxFirst * kMenuRowHeight);
            first = std::clamp((int)(menuScroll_ / kMenuRowHeight), 0, maxFirst);
            panel = panelRect(first);
        }
        insidePanel |= ui_.hovered(panel);

        ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
        ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::separator);

        // A dropdown longer than the window gets a bar on the right edge, so the
        // length of the list is visible and not merely scrollable, and the rows
        // stop short of it.
        const bool scrollable = count > visible;
        const float rowInset = scrollable ? 9.0f : 4.0f;
        if (scrollable) {
            const float trackTop = panel.y + kPanelPadding;
            const float trackHeight = (float)shown * kMenuRowHeight;
            const float thumbHeight =
                std::fmax(20.0f, trackHeight * (float)shown / (float)count);
            const float maxOffset = (float)maxFirst * kMenuRowHeight;
            const float travel = trackHeight - thumbHeight;
            const float thumbY =
                trackTop + travel * (maxOffset > 0.0f ? clampf(menuScroll_ / maxOffset, 0.0f, 1.0f) : 0.0f);
            const float barX = panel.x + panel.width - 5.0f;
            ui.fillRect(barX, trackTop, 3.0f, trackHeight, theme::panelDark);
            ui.fillRect(barX, thumbY, 3.0f, thumbHeight, theme::textDim.scaled(0.55f));
        }

        for (int i = first; i < first + shown; ++i) {
            const MenuRow& row = menuRow(openMenu_, i);
            if (row.separator) continue;
            const Rect rowRect{panel.x + 2.0f,
                               panel.y + kPanelPadding + (float)(i - first) * kMenuRowHeight,
                               panel.width - rowInset, kMenuRowHeight};
            const bool hovered = ui_.hovered(rowRect);
            if (hovered) {
                ui_.markHovered(kMenuRow + i);
                ui.fillRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height,
                            theme::buttonHover);
            }
            ui.textFit(rowRect.x + 8.0f,
                       rowRect.y + (rowRect.height - theme::textLineHeight) * 0.5f,
                       rowRect.width - 28.0f, row.text, theme::text);
            if (row.checked) drawTick(ui, rowRect, theme::accent);
            if (hovered && ui_.rawPointerPressed() && !openedNow) {
                runAction(row.action, row.param, world, camera, window);
                closeOverlays();
                break;
            }
        }

        if (hidden > 0) {
            char more[24];
            std::snprintf(more, sizeof(more), "+%d", hidden);
            ui.text(panel.x + 8.0f,
                    panel.y + kPanelPadding + (float)shown * kMenuRowHeight + 2.0f, more,
                    theme::textDim);
        }
    }

    // --- layers -------------------------------------------------------------
    if (layersOpen_) {
        const int total = (int)world.bodies.size();
        const int rows = std::min(total, kMaxLayerRows);
        const float headerHeight = 24.0f;
        const float footer = total > rows ? kLayerRowHeight : 0.0f;
        const float height =
            headerHeight + (float)rows * kLayerRowHeight + footer + kPanelPadding;
        // Clamped to the window: the button that opened it can sit at either end of
        // the top bar, and a full-width panel anchored to the far one would hang off
        // the screen - which is exactly what it did before this line.
        const Rect anchor = layersAnchor_.width > 0.0f ? layersAnchor_ : layersButton_;
        const float panelX = std::fmax(
            4.0f, std::fmin(anchor.x, (float)fbWidth - kLayersPanelWidth - 4.0f));
        const Rect panel{panelX, layout_.menuBar.y + layout_.menuBar.height - 1.0f,
                         kLayersPanelWidth, height};
        ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
        ui.fillRect(panel.x, panel.y, panel.width, headerHeight, theme::panelDark);
        ui.fillRect(panel.x, panel.y + headerHeight - 1.0f, panel.width, 1.0f,
                    theme::separator);
        ui.text(panel.x + 8.0f, panel.y + 5.0f, text("toolbar.layers"), theme::text);
        char count[24];
        std::snprintf(count, sizeof(count), "%d", total);
        ui.text(panel.x + panel.width - 8.0f - ui.textWidth(count), panel.y + 5.0f, count,
                theme::textDim);
        insidePanel |= ui_.hovered(panel);

        for (int i = 0; i < rows; ++i) {
            const RigidBody& body = world.bodies[(size_t)i];
            const Rect row{panel.x + 2.0f, panel.y + headerHeight + (float)i * kLayerRowHeight,
                           panel.width - 4.0f, kLayerRowHeight};
            const bool hovered = ui_.hovered(row);
            if (hovered) ui.fillRect(row.x, row.y, row.width, row.height, theme::buttonHover);
            if (i == activeBody_) ui.fillRect(row.x, row.y, 2.0f, row.height, theme::accent);

            const Rect visibleRect{row.x + row.width - 2.0f * kLayerToggleSize - 12.0f, row.y,
                                   kLayerToggleSize, kLayerRowHeight};
            const Rect lockedRect{row.x + row.width - kLayerToggleSize - 6.0f, row.y,
                                  kLayerToggleSize, kLayerRowHeight};
            drawToggle(ui, visibleRect, Toggle::Visible, body.visible);
            drawToggle(ui, lockedRect, Toggle::Locked, body.locked);
            if (hovered) ui_.markHovered(kLayerRow + i);

            char label[64];
            formatIndexed(label, sizeof(label), bodyKind(world, i), ' ', i);
            ui.textFit(row.x + 10.0f, row.y + (row.height - theme::textLineHeight) * 0.5f,
                       row.width - 10.0f - 2.0f * kLayerToggleSize - 16.0f, label,
                       body.visible ? theme::text : theme::textDim);

            if (ui_.rawPointerPressed() && hovered && !openedNow) {
                if (ui_.hovered(visibleRect))
                    runAction(Action::LayerVisible, i, world, camera, window);
                else if (ui_.hovered(lockedRect))
                    runAction(Action::LayerLocked, i, world, camera, window);
                else
                    runAction(Action::LayerSelect, i, world, camera, window);
            }
        }

        float footerY = panel.y + headerHeight + (float)rows * kLayerRowHeight;
        if (total > rows) {
            char more[24];
            std::snprintf(more, sizeof(more), "+%d", total - rows);
            ui.text(panel.x + 10.0f, footerY + 3.0f, more, theme::textDim);
        } else if (total == 0) {
            ui.text(panel.x + 10.0f, footerY + 3.0f, text("layers.empty"), theme::textDim);
        }
    }

// --- search results -----------------------------------------------------
    if (!toolbarSearch_.empty()) {
        MenuHit hits[kMaxSearchRows];
        const int found = findMenuRows(toolbarSearch_.text(), hits, kMaxSearchRows);
        const int shown = std::min(found, kMaxSearchRows);
        const int lines = shown + (found > shown ? 1 : 0);
        const Rect panel{searchField_.x, layout_.menuBar.y + layout_.menuBar.height - 1.0f,
                         searchField_.width,
                         kPanelPadding * 2.0f + (float)std::max(1, lines) * kRowHeight};
        ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
        ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::separator);
        insidePanel |= ui_.hovered(panel);

        int pressed = -1;
        for (int i = 0; i < shown; ++i) {
            const MenuRow& row = menuRow(hits[i].menu, hits[i].index);
            const Rect rowRect{panel.x + 2.0f, panel.y + kPanelPadding + (float)i * kRowHeight,
                               panel.width - 4.0f, kRowHeight};
            const bool hovered = ui_.hovered(rowRect);
            if (hovered) ui.fillRect(rowRect.x, rowRect.y, rowRect.width, rowRect.height,
                                     theme::buttonHover);
            ui.textFit(rowRect.x + 8.0f,
                       rowRect.y + (rowRect.height - theme::textLineHeight) * 0.5f,
                       rowRect.width - 78.0f, row.text, theme::text);
            // The menu it lives in, so two rows with the same wording ("Reload"
            // under File and under the toolbar) are told apart.
            const char* caption = menuCaption(hits[i].menu);
            ui.text(rowRect.x + rowRect.width - 8.0f - ui.textWidth(caption),
                    rowRect.y + (rowRect.height - theme::textLineHeight) * 0.5f, caption,
                    theme::textDim);
            if (hovered && ui_.rawPointerPressed() && !openedNow) pressed = i;
        }

        if (found == 0) {
            ui.text(panel.x + 8.0f, panel.y + kPanelPadding + 2.0f, text("toolbar.noMatches"),
                    theme::textDim);
        } else if (found > shown) {
            char more[24];
            std::snprintf(more, sizeof(more), "+%d", found - shown);
            ui.text(panel.x + 8.0f, panel.y + kPanelPadding + (float)shown * kRowHeight + 2.0f,
                    more, theme::textDim);
        }

        // Enter runs the first match: the field is a command line, not a list that
        // has to be clicked into.
        if (pressed >= 0 || (window.takeKeyPress(GLFW_KEY_ENTER) && found > 0)) {
            const MenuRow& row = menuRow(hits[pressed >= 0 ? pressed : 0].menu,
                                         hits[pressed >= 0 ? pressed : 0].index);
            runAction(row.action, row.param, world, camera, window);
            toolbarSearch_.clear();
            closeOverlays();
        }
    }

    // A press outside every panel closes them - but not on the frame one opened,
    // which is what overlayOpenedThisFrame_ is for. What was typed into the search
    // goes with them: a suggestion list that outlives the click that dismissed it
    // is a panel nobody can get rid of.
    if (ui_.rawPointerPressed() && !insidePanel && !openedNow) {
        closeOverlays();
        toolbarSearch_.clear();
    }

    // Escape, same job by keyboard. The numeric field in the inspector gets first
    // refusal, because it is drawn before this pass.
    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) {
        closeOverlays();
        toolbarSearch_.clear();
    }
}

// --- dialogs ---------------------------------------------------------------
//
// Unity's File menu is mostly dialogs, so these are dialogs too rather than
// messages that only say "ok": each one has the fields its command needs, and
// its primary button does the command instead of acknowledging it.
void Editor::drawModal(UiRenderer& ui, PhysicsWorld& world, Window& window, int fbWidth,
                       int fbHeight) {
    if (modal_ == Modal::None) return;

    const int listed = std::min(8, (int)modalFiles_.size());
    // Heights are measured, not guessed: the wrapped hints differ between the two
    // languages, and a dialog sized for one of them clips the other. Each case
    // adds up what it draws and every case adds the same button strip below.
    constexpr float kModalTop = 38.0f;       // title
    constexpr float kModalButtons = 48.0f;  // button row plus the air above it
    constexpr float kModalField = 44.0f;    // a labelled text field
    const float wrapWidth = kModalWidth - 28.0f;
    const auto hintHeight = [&](const char* key) { return wrappedHeight(ui, wrapWidth, text(key)); };

    float height = kModalTop + kModalButtons;
    std::string title, primary, secondary;
    switch (modal_) {
    case Modal::NewScene:
        title = text("modal.newScene");
        height += hintHeight("modal.newSceneHint");
        break;
    case Modal::OpenScene:
        title = text("modal.openScene");
        height += hintHeight("modal.openSceneHint") + (float)listed * kModalRowHeight + 16.0f +
                  kModalField;
        break;
    case Modal::BuildSettings:
        title = text("modal.buildSettings");
        height += hintHeight("modal.buildHint") + kModalField + hintHeight("modal.buildStages");
        break;
    case Modal::SaveFirst:
        title = text("modal.saveFirst");
        height += hintHeight("modal.saveFirstHint");
        break;
    case Modal::None:
        return;
    }

    const float width = kModalWidth;
    const Rect panel{(float)fbWidth * 0.5f - width * 0.5f, (float)fbHeight * 0.5f - height * 0.5f,
                     width, height};
    // The editor behind it is dimmed, not blocked by the pixels: a dialog that does
    // not look modal is one the user keeps clicking past.
    ui.fillRect(0.0f, 0.0f, (float)fbWidth, (float)fbHeight, Color{0.0f, 0.0f, 0.0f, 0.32f});
    ui.fillRect(panel.x, panel.y, panel.width, panel.height, theme::panel);
    ui.fillRect(panel.x, panel.y, panel.width, 1.0f, theme::accent);
    ui.fillRect(panel.x, panel.y + panel.height - 1.0f, panel.width, 1.0f, theme::separator);
    ui.text(panel.x + 14.0f, panel.y + 12.0f, title.c_str(), theme::accent);

    float y = panel.y + kModalTop;
    bool accept = false, cancel = false, discard = false;

    // Label over an editable box: the same field the inspector uses, so typing,
    // paste and caret behaviour are identical everywhere.
    auto field = [&](const char* label) {
        ui.text(panel.x + 14.0f, y + 2.0f, label, theme::textDim);
        const Rect box{panel.x + 14.0f, y + 18.0f, panel.width - 28.0f, theme::fieldHeight};
        ui_.pumpTextField(window, modalField_, ui_.hovered(box));
        ui.fillRect(box.x, box.y, box.width, box.height,
                    modalField_.editing ? theme::accent.scaled(0.3f) : theme::field);
        ui.textFit(box.x + 6.0f, box.y + (box.height - theme::textLineHeight) * 0.5f,
                   box.width - 12.0f, modalField_.text(), theme::text);
        y += 44.0f;
    };

    const float textWidth = panel.width - 28.0f;
    switch (modal_) {
    case Modal::NewScene:
        y += drawWrapped(ui, panel.x + 14.0f, y, textWidth, text("modal.newSceneHint"),
                         theme::textDim);
        primary = text("btn.create");
        break;
    case Modal::OpenScene: {
        y += drawWrapped(ui, panel.x + 14.0f, y, textWidth, text("modal.openSceneHint"),
                         theme::textDim);
        for (int i = 0; i < listed; ++i) {
            const std::string& path = modalFiles_[(size_t)i];
            const Rect row{panel.x + 14.0f, y, panel.width - 28.0f, kModalRowHeight};
            const bool hovered = ui_.hovered(row);
            if (hovered) ui.fillRect(row.x, row.y, row.width, row.height, theme::header);
            ui.textFit(row.x + 6.0f, row.y + 2.0f, row.width - 12.0f,
                       str::fileNameView(path).data(), theme::text);
            // Choosing a file fills the field and marks the row; opening it is the
            // button, so a mis-click cannot replace the scene.
            if (hovered && ui_.pointerPressed()) {
                modalField_.set(str::fileName(path).c_str());
                modalRow_ = i;
            }
            y += kModalRowHeight;
        }
        if (modalFiles_.empty())
            ui.text(panel.x + 14.0f, y, text("modal.noScenes"), theme::textDim);
        y += 16.0f;
        field(text("modal.file"));
        primary = text("btn.open");
        break;
    }
    case Modal::BuildSettings:
        y += drawWrapped(ui, panel.x + 14.0f, y, textWidth, text("modal.buildHint"),
                         theme::textDim);
        field(text("modal.buildPath"));
        drawWrapped(ui, panel.x + 14.0f, y, textWidth, text("modal.buildStages"), theme::textDim);
        primary = text("btn.build");
        break;
    case Modal::SaveFirst:
        drawWrapped(ui, panel.x + 14.0f, y, textWidth, text("modal.saveFirstHint"),
                    theme::textDim);
        primary = text("btn.save");
        secondary = text("btn.dontSave");
        break;
    case Modal::None:
        return;
    }

    if (window.takeKeyPress(GLFW_KEY_ENTER)) accept = true;
    if (window.takeKeyPress(GLFW_KEY_ESCAPE)) cancel = true;

    const float buttonY = panel.y + panel.height - 32.0f;
    if (!secondary.empty()) {
        const Rect no{panel.x + panel.width - 226.0f, buttonY, 100.0f, 24.0f};
        if (ui_.button(ui, kModalButton + 2, no.x, no.y, no.width, no.height, secondary.c_str()))
            discard = true;
    } else if (ui_.button(ui, kModalButton, panel.x + 14.0f, buttonY, 96.0f, 24.0f,
                          text("btn.cancel"))) {
        cancel = true;
    }
    const Rect ok{panel.x + panel.width - 112.0f, buttonY, 96.0f, 24.0f};
    if (ui_.button(ui, kModalButton + 1, ok.x, ok.y, ok.width, ok.height, primary.c_str()))
        accept = true;

    if (cancel) {
        modal_ = Modal::None;
        return;
    }
    if (!accept && !discard) return;

    switch (modal_) {
    case Modal::NewScene:
        world.clear();
        activeBody_ = -1;
        selectedBodies_.clear();
        sceneDirty_ = true;
        Log::info("Scene: new and empty");
        break;
    case Modal::OpenScene: {
        std::string name = modalField_.text();
        if (name.empty() && listed > 0) name = str::fileName(modalFiles_[(size_t)modalRow_]);
        if (name.empty()) break;
        scenePath_ = str::join(projectDir(), name);
        loadScene(world);
        break;
    }
    case Modal::BuildSettings:
        // Remembered, so the shell can stage the build wherever this dialog said.
        buildFolder_ = modalField_.text();
        shellRequest_ = ShellRequest::Build;
        break;
case Modal::SaveFirst: {
        // Save and "Don't save" both mean "go ahead with what you asked"; they
        // differ only in whether the scene reaches the disk on the way out.
        //
        // Through saveScene(), not scenes::save() directly: that is where the
        // scene file's name and the project that owns it are kept in step, and
        // going around it here is how the scene ends up in a file the project
        // does not know about.
        const bool wanted = !discard;
        if (wanted) saveScene(world);

        const Pending what = pendingAction_;
        modal_ = Modal::None;
        switch (what) {
        case Pending::NewScene:
            world.clear();
            activeBody_ = -1;
            selectedBodies_.clear();
            sceneDirty_ = true;
            Log::info("Scene: new and empty");
            break;
        case Pending::OpenScene:
            openModal(Modal::OpenScene);
            break;
        case Pending::LeaveProject:
            shellRequest_ = pendingShell_;
            pendingShell_ = ShellRequest::None;
            break;
        }
return;
    }
    case Modal::None:
        break;
    }
    modal_ = Modal::None;
}

} // namespace s3d