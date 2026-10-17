#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ivory
{
// Deliberately public calibration coefficients: fit these against lawful user
// supplied reference renders, without entangling calibration with the UI.
struct StageCalibration
{
    double drive = 0.78;
    double feedback = 0.075;
    double memoryHz = 2400.0;
    double evenBase = 0.008;
    double evenSecond = 0.205;
    double algebraicWeight = 0.36;
};

inline constexpr StageCalibration primaryStageCalibration {};
inline constexpr StageCalibration secondaryStageCalibration {
    1.12, 0.13, 680.0, 0.020, 0.21, 0.62
};

// A scalar target smoother. Each channel owns its own copy so interleaved
// mono/stereo sample processing has identical time constants.
class ControlSmoother
{
public:
    void prepare(double sampleRate, double milliseconds = 12.0) noexcept
    {
        coefficient_ = std::exp(-1.0 / (std::max(1.0, sampleRate)
                                      * std::max(0.001, milliseconds * 0.001)));
    }
    void reset(double value) noexcept { current_ = target_ = value; }
    void setTarget(double value) noexcept { target_ = value; }
    double next() noexcept
    {
        current_ = target_ + coefficient_ * (current_ - target_);
        if (std::abs(current_ - target_) < 1.0e-12)
            current_ = target_;
        return current_;
    }
private:
    double coefficient_ = 0.0, current_ = 0.0, target_ = 0.0;
};

template <typename Sample>
class NonlinearStage
{
public:
    void prepare(double sampleRate, StageCalibration calibration) noexcept
    {
        calibration_ = calibration;
        memoryPole_ = std::exp(-2.0 * std::numbers::pi * calibration_.memoryHz
                               / std::max(1.0, sampleRate));
        reset();
    }
    void reset() noexcept { memory_ = 0.0; activity_ = 0.0; }

    Sample processSample(Sample input, double secondAmount) noexcept
    {
        const auto x = std::clamp(static_cast<double>(input), -128.0, 128.0);
        // Feedback is derived from the clipping residual, not from a level
        // detector. It therefore changes the transfer's history and spectral
        // tilt without introducing compressor attack/release behaviour.
        const auto driven = std::clamp(x * calibration_.drive
                                       + calibration_.feedback * memory_, -64.0, 64.0);
        const auto square = driven * driven;
        const auto bounded = std::clamp(driven, -3.0, 3.0);
        const auto boundedSquare = bounded * bounded;
        // Cubic rational soft knee has zero slope at +/-3; the algebraic
        // branch has a gentler asymptote. Both have unit derivative at zero.
        const auto rational = bounded * (27.0 + boundedSquare)
                              / (27.0 + 9.0 * boundedSquare);
        const auto algebraic = driven / std::sqrt(1.0 + square);
        const auto odd = rational + calibration_.algebraicWeight * (algebraic - rational);
        const auto evenAmount = calibration_.evenBase
                                + calibration_.evenSecond * std::clamp(secondAmount, 0.0, 1.0);
        // Even residual starts quadratically, increasing H2 while leaving the
        // infinitesimal gain unchanged. A downstream DC blocker removes bias.
        const auto shaped = odd + evenAmount * square / (1.0 + square);
        const auto residual = shaped - driven;
        memory_ = memoryPole_ * memory_ + (1.0 - memoryPole_) * residual;
        if (std::abs(memory_) < 1.0e-24)
            memory_ = 0.0;
        const auto output = shaped / calibration_.drive;
        activity_ = std::max(activity_, std::min(1.0, std::abs(output - x) / (0.05 + std::abs(x))));
        return static_cast<Sample>(output);
    }

    double activityAndReset() noexcept
    {
        const auto result = activity_;
        activity_ = 0.0;
        return result;
    }

private:
    StageCalibration calibration_ {};
    double memoryPole_ = 0.0, memory_ = 0.0, activity_ = 0.0;
};
} // namespace ivory
