#pragma once
namespace s3d {

// Fixed-timestep accumulator. Rendering runs at display rate while the
// simulation advances in constant 1/rate steps, so physics stays deterministic
// and frame-rate independent. Excess time is dropped instead of spiralling.
class FixedClock {
public:
    explicit FixedClock(double rate = 60.0, int maxStepsPerFrame = 10)
        : step_(1.0 / rate), maxSteps_(maxStepsPerFrame) {}

    // Adds `dt` seconds and returns how many simulation steps to run now.
    int advance(double dt) {
        accumulator_ += dt;
        int steps = 0;
        while (accumulator_ >= step_ && steps < maxSteps_) {
            accumulator_ -= step_;
            ++steps;
        }
        if (steps >= maxSteps_) accumulator_ = 0.0;
        return steps;
    }

    void reset() { accumulator_ = 0.0; }
    double step() const { return step_; }
    int maxSteps() const { return maxSteps_; }

private:
    double step_;
    int maxSteps_;
    double accumulator_ = 0.0;
};

} // namespace s3d