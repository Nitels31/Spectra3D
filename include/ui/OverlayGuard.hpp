#pragma once

namespace s3d {

// What a surface declares about itself: where it is, and whether a click that
// lands on it belongs to it.
struct OverlayRect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    // A blocking overlay takes the press, so clicking the scene behind an open
    // menu closes the menu instead of grabbing an object. A non-blocking one
    // (a tooltip, a status strip) lights up under the pointer but lets the click
    // through to whatever is under it.
    bool blocks = true;
};

// Decides which surface owns the pointer, in one place.
//
// This exists because the rule was being enforced by hand at every call site -
// each popup had to remember to claim the pointer before the panels drew and
// release it before its own pass, and a site that forgot produced a button that
// worked on some frames and not others depending on which pass ran first. That is
// the whole bug class this file closes.
//
// The rule itself is one line: among every surface the pointer is inside, the one
// drawn LAST is on top and owns the click. There is no z-order to declare and none
// to keep in sync - surfaces are pushed in draw order, so "last pushed" and "drawn
// last" are the same thing and cannot disagree.
//
// Two questions are answered, and they are not the same question:
//   - is the pointer over anything at all?  -> overAny
//   - does the press belong to the top surface? -> pressTaken
// A non-blocking surface answers the first and not the second, which is what lets a
// hover highlight sit over a live button without stealing its clicks.
class OverlayGuard {
public:
    // Declares a surface. Push in draw order: every later push is above every
    // earlier one.
    void push(const OverlayRect& rect) { surfaces_.push_back(rect); }
    // Convenience for the common case of a blocking surface.
    void push(float x, float y, float width, float height) {
        surfaces_.push_back(OverlayRect{x, y, width, height, true});
    }

    void clear() { surfaces_.clear(); }
    bool empty() const { return surfaces_.empty(); }
    int count() const { return (int)surfaces_.size(); }

    // True when the pointer is inside at least one surface, blocking or not.
    bool overAny(float x, float y) const { return topmost(x, y) >= 0; }

    // True when a press at (x,y) belongs to a surface rather than to whatever was
    // drawn underneath it. Only a blocking surface can take a press, and only the
    // topmost one - a blocking surface under a non-blocking one is still visible
    // but is not what the user is aiming at.
    bool pressTaken(float x, float y) const {
        const int top = topmost(x, y);
        return top >= 0 && surfaces_[(size_t)top].blocks;
    }

    // The surface a press would go to, for a caller that wants to route the event
    // itself: -1 when the press belongs to the scene underneath.
    int topmost(float x, float y) const {
        // Reverse scan: the last match is the last drawn, which is the one on top.
        for (int i = (int)surfaces_.size() - 1; i >= 0; --i) {
            const OverlayRect& r = surfaces_[(size_t)i];
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height) return i;
        }
        return -1;
    }

private:
    std::vector<OverlayRect> surfaces_;
};

} // namespace s3d