#pragma once
namespace s3d {

// Fixed-timestep accumulator. Rendering runs at display rate while the
// simulation advances in constant 1/rate steps, so physics stays deterministic
// and frame-rate independent. Excess time is dropped instead of spiralling.
//
// The step cap is 2, not 10, and that number is the whole point. `dt` is clamped
// upstream (kMaxFrameTime), so at 60 Hz a fully clamped frame can fund six 16.7 ms
// steps - which means a high cap never binds and the clamp decides instead. The
// cost of one slow step is then paid up to six times per frame, so the frame rate
// stops being proportional to the step cost and pins itself to a plateau: a step
// that got slower reports the same 3 FPS as one that got twice as slow, and the
// plateau cannot be left because clearing the accumulator throws the debt away
// instead of paying it down. With the cap binding first, frame time is
// cap * step + render, i.e. proportional to the real cost again.
class FixedClock {
public:
    explicit FixedClock(double rate = 60.0, int maxStepsPerFrame = 2)
        : step_(1.0 / rate), maxSteps_(maxStepsPerFrame) {}

    // Adds `dt` seconds and returns how many simulation steps to run now.
    int advance(double dt) {
        accumulator_ += dt;
        int steps = 0;
        while (accumulator_ >= step_ && steps < maxSteps_) {
            accumulator_ -= step_;
            ++steps;
        }
        // Still behind after the cap: the machine cannot keep up, and running the
        // rest would only deepen the hole. Keep at most one step of debt so a
        // single stall is absorbed instead of compounding, and so the simulation
        // neither spins nor silently loses unbounded time.
        if (accumulator_ > step_) accumulator_ = step_;
        return steps;
    }

    void reset() { accumulator_ = 0.0; }
    double step() const { return step_; }
    int maxSteps() const { return maxSteps_; }

    // How far into the NEXT step the accumulator is, 0..1. This is the
    // interpolation factor: a renderer that draws `lerp(prev, curr, alpha)`
    // instead of `curr` hides the fact that physics runs at 60 Hz while the
    // display refreshes at whatever rate it likes. Without it, every frame
    // where the accumulator does not advance a full step shows the same physics
    // state twice, and the one where it advances two shows a double jump - a
    // visible hitch whose period is 1/|refresh - 60| seconds. On a 59.8 Hz
    // panel that is once every five seconds.
    double alpha() const { return accumulator_ / step_; }

private:
    double step_;
    int maxSteps_;
    double accumulator_ = 0.0;
};

} // namespace s3d