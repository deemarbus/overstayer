#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace ivory
{

enum class FilterType
{
    bypass,
    lowPass,
    highPass,
    lowShelf,
    highShelf,
    peak
};

/**
 * A modulation-safe, second-order filter with a topology-preserving state-variable
 * core. The two integrator states and coefficient calculations use double even
 * when Sample is float, keeping low-frequency poles well behaved at high rates.
 *
 * Frequency and Q smooth logarithmically, gain smooths in dB, and expensive design
 * calculations run at most once every 16 samples. The resulting positive g/k
 * values and output mix also smooth each sample. Thus filter-type changes share
 * the same states without switching between incompatible direct-form histories.
 * No processing method allocates, locks, or touches the floating-point mode.
 * Like most DSP objects, control and audio calls must be made on the same thread.
 */
template <typename Sample>
class SmoothBiquad
{
    static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>);

public:
    // Support both ivory::FilterType and SmoothBiquad<Sample>::FilterType.
    using FilterType = ivory::FilterType;

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = validSampleRate(sampleRate);
        parameterStep_ = -std::expm1(-50.0 / sampleRate_); // 20 ms time constant.
        coefficientStep_ = -std::expm1(-(1.0 / 0.003) / sampleRate_);
        frequency_ = clampFrequency(frequency_);
        targetLogFrequency_ = std::log(frequency_);
        reset();
    }

    void reset() noexcept
    {
        integrator1_ = 0.0;
        integrator2_ = 0.0;
        hasProcessed_ = false;
        snapToTargets();
    }

    void set(FilterType type, double frequency, double q, double gainDb) noexcept
    {
        type = validType(type);
        frequency = clampFrequency(finiteOr(frequency, frequency_));
        q = std::clamp(finiteOr(q, q_), 0.1, 18.0);
        gainDb = std::clamp(finiteOr(gainDb, gainDb_), -24.0, 24.0);

        // Hosts commonly send the same parameters once per block. Do not restart
        // a transition, or recalculate logarithms, for an unchanged parameter set.
        if (type == type_ && sameCachedValue(frequency, frequency_)
                          && sameCachedValue(q, q_) && sameCachedValue(gainDb, gainDb_))
            return;

        type_ = type;
        frequency_ = frequency;
        q_ = q;
        gainDb_ = gainDb;
        targetLogFrequency_ = std::log(frequency_);
        targetLogQ_ = std::log(q_);
        parametersMoving_ = true;
        designPending_ = true;

        // Initial settings (including settings immediately after reset) should
        // take effect before the first audio sample, with no startup sweep.
        if (!hasProcessed_)
            snapToTargets();
    }

    [[nodiscard]] Sample processSample(Sample input) noexcept
    {
        hasProcessed_ = true;
        advanceParameters();

        const auto x = static_cast<double>(input);
        if (!std::isfinite(x))
            return clearInvalidState();

        const double v3 = x - integrator2_;
        const double v1 = a1_ * integrator1_ + a2_ * v3;
        const double v2 = integrator2_ + a2_ * integrator1_ + a3_ * v3;
        integrator1_ = zapDenormal(2.0 * v1 - integrator1_);
        integrator2_ = zapDenormal(2.0 * v2 - integrator2_);

        const double output = coefficients_[m0] * x
                            + coefficients_[m1] * v1
                            + coefficients_[m2] * v2;
        if (!std::isfinite(output) || std::abs(output) > std::numeric_limits<Sample>::max()
                                   || !std::isfinite(integrator1_)
                                   || !std::isfinite(integrator2_))
            return clearInvalidState();
        return static_cast<Sample>(zapDenormal(output));
    }

private:
    enum Coefficient : unsigned { g, k, m0, m1, m2, coefficientCount };
    static constexpr unsigned controlInterval = 16;
    using Coefficients = std::array<double, coefficientCount>;

    static double validSampleRate(double rate) noexcept
    {
        return std::isfinite(rate) && rate >= 1.0 ? rate : 44100.0;
    }

    static double finiteOr(double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
    }

    static bool sameCachedValue(double left, double right) noexcept
    {
        // Deliberately exact cache comparison, not a numerical convergence
        // test: every changed host value must eventually reach the target.
        return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
    }

    static double zapDenormal(double value) noexcept
    {
        return std::abs(value) < 1.0e-30 ? 0.0 : value;
    }

    static FilterType validType(FilterType type) noexcept
    {
        switch (type)
        {
            case FilterType::bypass:
            case FilterType::lowPass:
            case FilterType::highPass:
            case FilterType::lowShelf:
            case FilterType::highShelf:
            case FilterType::peak: return type;
        }
        return FilterType::bypass;
    }

    double clampFrequency(double frequency) const noexcept
    {
        const double upper = 0.45 * sampleRate_;
        return std::clamp(frequency, std::min(5.0, upper * 0.1), upper);
    }

    Coefficients design() const noexcept
    {
        // The shelf formulas place frequency at the half-gain point. Q controls
        // the damping of the underlying pole pair; Q = 1/sqrt(2) is a gentle shelf.
        const double amplitude = std::pow(10.0, smoothedGainDb_ / 40.0);
        const double gain = amplitude * amplitude;
        double tuning = std::tan(std::numbers::pi_v<double>
                               * std::exp(smoothedLogFrequency_) / sampleRate_);
        double damping = std::exp(-smoothedLogQ_);
        double direct = 1.0;
        double band = 0.0;
        double low = 0.0;

        switch (type_)
        {
            case FilterType::bypass: break;
            case FilterType::lowPass:
                direct = 0.0;
                low = 1.0;
                break;
            case FilterType::highPass:
                band = -damping;
                low = -1.0;
                break;
            case FilterType::lowShelf:
                tuning /= std::sqrt(amplitude);
                band = damping * (amplitude - 1.0);
                low = gain - 1.0;
                break;
            case FilterType::highShelf:
                tuning *= std::sqrt(amplitude);
                direct = gain;
                band = damping * (1.0 - amplitude) * amplitude;
                low = 1.0 - gain;
                break;
            case FilterType::peak:
                damping /= amplitude;
                band = damping * (gain - 1.0);
                break;
        }
        return { tuning, damping, direct, band, low };
    }

    void updateIntegrators() noexcept
    {
        a1_ = 1.0 / (1.0 + coefficients_[g] * (coefficients_[g] + coefficients_[k]));
        a2_ = coefficients_[g] * a1_;
        a3_ = coefficients_[g] * a2_;
    }

    void snapToTargets() noexcept
    {
        smoothedLogFrequency_ = targetLogFrequency_;
        smoothedLogQ_ = targetLogQ_;
        smoothedGainDb_ = gainDb_;
        targetCoefficients_ = design();
        coefficients_ = targetCoefficients_;
        parametersMoving_ = false;
        coefficientsMoving_ = false;
        designPending_ = false;
        controlCounter_ = 0;
        updateIntegrators();
    }

    void advanceParameters() noexcept
    {
        if (parametersMoving_)
        {
            smoothedLogFrequency_ += parameterStep_ * (targetLogFrequency_ - smoothedLogFrequency_);
            smoothedLogQ_ += parameterStep_ * (targetLogQ_ - smoothedLogQ_);
            smoothedGainDb_ += parameterStep_ * (gainDb_ - smoothedGainDb_);
            if (std::abs(targetLogFrequency_ - smoothedLogFrequency_) < 1.0e-8
                && std::abs(targetLogQ_ - smoothedLogQ_) < 1.0e-8
                && std::abs(gainDb_ - smoothedGainDb_) < 1.0e-7)
            {
                smoothedLogFrequency_ = targetLogFrequency_;
                smoothedLogQ_ = targetLogQ_;
                smoothedGainDb_ = gainDb_;
                parametersMoving_ = false;
            }
            designPending_ = true;
        }

        if (++controlCounter_ >= controlInterval)
        {
            controlCounter_ = 0;
            if (designPending_)
            {
                targetCoefficients_ = design();
                designPending_ = false;
                coefficientsMoving_ = true;
            }
        }

        if (coefficientsMoving_)
        {
            coefficientsMoving_ = false;
            for (unsigned i = 0; i < coefficientCount; ++i)
            {
                const double difference = targetCoefficients_[i] - coefficients_[i];
                if (std::abs(difference) < 1.0e-12)
                    coefficients_[i] = targetCoefficients_[i];
                else
                {
                    coefficients_[i] += coefficientStep_ * difference;
                    coefficientsMoving_ = true;
                }
            }
            // Positive tuning/damping remain positive under this interpolation.
            updateIntegrators();
        }
    }

    Sample clearInvalidState() noexcept
    {
        integrator1_ = 0.0;
        integrator2_ = 0.0;
        return Sample {};
    }

    double sampleRate_ = 44100.0;
    FilterType type_ = FilterType::bypass;
    double frequency_ = 1000.0;
    double q_ = 0.7071067811865476;
    double gainDb_ = 0.0;
    double targetLogFrequency_ = std::log(1000.0);
    double targetLogQ_ = std::log(0.7071067811865476);
    double smoothedLogFrequency_ = targetLogFrequency_;
    double smoothedLogQ_ = targetLogQ_;
    double smoothedGainDb_ = 0.0;
    double parameterStep_ = -std::expm1(-50.0 / 44100.0);
    double coefficientStep_ = -std::expm1(-(1.0 / 0.003) / 44100.0);
    Coefficients coefficients_ { std::tan(std::numbers::pi_v<double> * 1000.0 / 44100.0),
                                 1.4142135623730951, 1.0, 0.0, 0.0 };
    Coefficients targetCoefficients_ = coefficients_;
    double a1_ = 1.0 / (1.0 + coefficients_[g] * (coefficients_[g] + coefficients_[k]));
    double a2_ = coefficients_[g] * a1_;
    double a3_ = coefficients_[g] * a2_;
    double integrator1_ = 0.0;
    double integrator2_ = 0.0;
    unsigned controlCounter_ = 0;
    bool hasProcessed_ = false;
    bool parametersMoving_ = false;
    bool coefficientsMoving_ = false;
    bool designPending_ = false;
};

/** One-pole DC rejection, normalised to unity gain at Nyquist. */
template <typename Sample>
class DCBlocker
{
    static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>);

public:
    void prepare(double sampleRate, double cutoff = 7.0) noexcept
    {
        sampleRate = std::isfinite(sampleRate) && sampleRate >= 1.0 ? sampleRate : 44100.0;
        cutoff = std::isfinite(cutoff) ? cutoff : 7.0;
        cutoff = std::clamp(cutoff, std::min(0.01, sampleRate * 0.1), sampleRate * 0.45);
        pole_ = std::exp(-2.0 * std::numbers::pi_v<double> * cutoff / sampleRate);
        normalisation_ = 0.5 * (1.0 + pole_);
        reset();
    }

    void reset() noexcept
    {
        previousInput_ = 0.0;
        previousOutput_ = 0.0;
    }

    [[nodiscard]] Sample processSample(Sample input) noexcept
    {
        const double x = static_cast<double>(input);
        if (!std::isfinite(x))
        {
            reset();
            return Sample {};
        }
        double output = normalisation_ * (x - previousInput_) + pole_ * previousOutput_;
        if (std::abs(output) < 1.0e-30)
            output = 0.0;
        if (!std::isfinite(output) || std::abs(output) > std::numeric_limits<Sample>::max())
        {
            reset();
            return Sample {};
        }
        previousInput_ = x;
        previousOutput_ = output;
        return static_cast<Sample>(output);
    }

private:
    double pole_ = std::exp(-2.0 * std::numbers::pi_v<double> * 7.0 / 44100.0);
    double normalisation_ = 0.5 * (1.0 + pole_);
    double previousInput_ = 0.0;
    double previousOutput_ = 0.0;
};

} // namespace ivory
