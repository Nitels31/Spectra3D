#pragma once
#include <vector>

namespace s3d {

// What happened, as an edge. `code` is a GLFW key or button; `value` carries the
// scroll ticks and is 0 for everything else.
enum class InputEventType { Key, KeyUp, Mouse, MouseUp, Scroll };

struct InputEvent {
    InputEventType type = InputEventType::Key;
    int code = 0;
    double value = 0.0;
};

// The ordered edges of one frame, in the order they arrived.
//
// Window keeps its state as a bool per key and a bool per button, which is right
// for "is this held" and wrong for "what happened": a bool cannot say a key was
// pressed and released inside the same frame, and it cannot say which of three
// presses came first. Both are what a script means by an event, and both are lost
// by the state arrays, so this queue exists next to them rather than instead of
// them - takeKeyPress() and mousePressed() keep working exactly as before and
// neither of them touches this.
//
// No GLFW in here, on purpose. The callbacks in Window are the only place that
// needs a window, and everything worth testing - the ordering, the cap, what
// clear() does to a frame - is this file. FixedClock.hpp is the same argument for
// the same reason.
class InputEvents {
public:
    // A frame's worth of input is a handful of edges; 256 is two orders of
    // magnitude more than a person produces in 16 ms. The cap exists so a stuck
    // input device or a window someone is hammering cannot grow the vector without
    // bound over a long session.
    static constexpr int kMaxEvents = 256;

    void push(InputEventType type, int code, double value) {
        if ((int)events_.size() >= kMaxEvents) {
            // Drop the newest, not the oldest: the most recent edge is the one the
            // user just produced and the one whose absence will be noticed.
            overflowed_ = true;
            return;
        }
        events_.push_back(InputEvent{type, code, value});
    }

    int count() const { return (int)events_.size(); }
    bool empty() const { return events_.empty(); }

    // By out-parameter rather than by reference return, so an index from a script
    // cannot produce a pointer into the middle of this vector.
    bool at(int index, InputEvent& out) const {
        if (index < 0 || index >= (int)events_.size()) return false;
        out = events_[(size_t)index];
        return true;
    }

    // End of frame: every edge dies with the frame that had its chance to read it.
    // An event that arrives while the loop is still waiting belongs to the next
    // frame, which is why this is called at the bottom of the loop and not the top.
    //
    // The overflow flag is scoped to a frame too, so it goes here as well. A reader
    // that cleared the queue and kept the flag would report the same overflow on
    // every following frame, and the warning would stop meaning anything. Read it
    // before calling this if you want to say something about it.
    void clear() {
        events_.clear();
        overflowed_ = false;
    }

    // True when push() had to throw something away this frame. Reported rather
    // than swallowed, and read once by Window at the end of the frame so the
    // message lands next to the rest of that frame's diagnostics.
    bool overflowed() const { return overflowed_; }

private:
    std::vector<InputEvent> events_;
    bool overflowed_ = false;
};

} // namespace s3d