#pragma once

#include "EmphasisEngine.h"
#include "Filters.h"
#include "NonlinearStage.h"
#include "Parameters.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

namespace ivory
{
// Wet-path engine; the host wrapper owns oversampling, the latency-matched dry
// path, MIX, TO DRY and OUTPUT. All state is preallocated for mono/stereo.
template <typename Sample>
class SaturationEngine
{
    static_assert(std::is_same_v<Sample, float> || std::is_same_v<Sample, double>);
public:
    void prepare(double sampleRate, int channels)
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
        channels_ = std::clamp(channels, 1, 2);
        for (auto& channel : state_)
        {
            channel.emphasis.prepare(sampleRate_);
            channel.commonHighPass.prepare(sampleRate_);
            channel.highPass.prepare(sampleRate_);
            channel.lowPass.prepare(sampleRate_);
            channel.lowShelf.prepare(sampleRate_);
            channel.highShelf.prepare(sampleRate_);
            channel.dc.prepare(sampleRate_, 7.0);
            channel.primary.prepare(sampleRate_, primaryStageCalibration);
            channel.secondary.prepare(sampleRate_, secondaryStageCalibration);
            channel.inputDb.prepare(sampleRate_, 16.0);
            channel.autoGain.prepare(sampleRate_, 16.0);
            channel.dual.prepare(sampleRate_, 10.0);
            channel.second.prepare(sampleRate_, 10.0);
            channel.commonHighPassMix.prepare(sampleRate_, 10.0);
            channel.highPassMix.prepare(sampleRate_, 10.0);
            channel.lowPassMix.prepare(sampleRate_, 10.0);
        }
        configure();
        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : state_)
        {
            channel.emphasis.reset();
            channel.commonHighPass.reset();
            channel.highPass.reset();
            channel.lowPass.reset();
            channel.lowShelf.reset();
            channel.highShelf.reset();
            channel.dc.reset();
            channel.primary.reset();
            channel.secondary.reset();
            channel.inputDb.reset(safeInputDb());
            channel.autoGain.reset(parameters_.autoGain ? 1.0 : 0.0);
            channel.dual.reset(parameters_.dual ? 1.0 : 0.0);
            channel.second.reset(parameters_.second ? 1.0 : 0.0);
            channel.commonHighPassMix.reset(parameters_.lowCut ? 1.0 : 0.0);
            channel.highPassMix.reset(highPassEnabled() ? 1.0 : 0.0);
            channel.lowPassMix.reset(lowPassEnabled() ? 1.0 : 0.0);
        }
        activity_ = 0.0f;
    }

    void setParameters(const Parameters& parameters) noexcept
    {
        parameters_ = parameters;
        configure();
    }

    Sample processSample(Sample sample, int channelIndex) noexcept
    {
        if (channelIndex < 0 || channelIndex >= channels_)
            return Sample{};
        auto& channel = state_[static_cast<std::size_t>(channelIndex)];
        double value = std::isfinite(sample)
            ? std::clamp(static_cast<double>(sample), -128.0, 128.0) : 0.0;
        const auto inputDb = channel.inputDb.next();
        const auto inputGain = std::exp(inputDb * dbExponent);

        // Confirmed reference block-diagram routing: the three common white
        // input filters precede INPUT gain; the Extended lower-section filters
        // and pre-emphasis follow it. The wrapper splits dry audio before all
        // of these stages. This ordering also matters during gain automation.
        const auto commonFilteredHigh = channel.commonHighPass.processSample(static_cast<Sample>(value));
        value += channel.commonHighPassMix.next() * (static_cast<double>(commonFilteredHigh) - value);
        value = static_cast<double>(channel.highShelf.processSample(static_cast<Sample>(value)));
        value = static_cast<double>(channel.lowShelf.processSample(static_cast<Sample>(value)));
        value *= inputGain;
        const auto filteredHigh = channel.highPass.processSample(static_cast<Sample>(value));
        value += channel.highPassMix.next() * (static_cast<double>(filteredHigh) - value);
        const auto filteredLow = channel.lowPass.processSample(static_cast<Sample>(value));
        value += channel.lowPassMix.next() * (static_cast<double>(filteredLow) - value);
        auto wet = channel.emphasis.processPre(static_cast<Sample>(value));

        const auto second = channel.second.next();
        wet = channel.primary.processSample(wet, second);
        // Evaluate the second stage continuously to keep its memory warm.
        // DUAL automation crossfades its contribution without a state reset.
        const auto dualWet = channel.secondary.processSample(wet, second);
        wet = static_cast<Sample>(wet + channel.dual.next() * (dualWet - wet));
        const auto primaryActivity = channel.primary.activityAndReset();
        const auto secondaryActivity = channel.secondary.activityAndReset();
        activity_ = std::max(activity_, static_cast<float>(std::max(primaryActivity,
            parameters_.dual ? secondaryActivity : 0.0)));

        wet = channel.emphasis.processPost(wet);
        wet = channel.dc.processSample(wet);
        // Input and compensation share the same smoothed dB value; AUTO GAIN
        // removes linear input gain while preserving the nonlinear transfer.
        const auto output = static_cast<double>(wet)
                            * std::exp(-inputDb * channel.autoGain.next() * dbExponent);
        return std::isfinite(output) && std::abs(output) >= 1.0e-30
            ? static_cast<Sample>(std::clamp(output, -1.0e6, 1.0e6)) : Sample{};
    }

    float getActivityAndReset() noexcept
    {
        const auto activity = activity_;
        activity_ = 0.0f;
        return activity;
    }

private:
    static constexpr double dbExponent = 0.1151292546497022842;
    struct ChannelState
    {
        EmphasisEngine<Sample> emphasis;
        SmoothBiquad<Sample> commonHighPass, highPass, lowPass, lowShelf, highShelf;
        DCBlocker<Sample> dc;
        NonlinearStage<Sample> primary, secondary;
        ControlSmoother inputDb, autoGain, dual, second, commonHighPassMix, highPassMix, lowPassMix;
    };

    double safeInputDb() const noexcept
    {
        return std::isfinite(parameters_.inputDb) ? std::clamp(parameters_.inputDb, -48.0, 48.0) : 0.0;
    }
    bool highPassEnabled() const noexcept
    {
        return parameters_.extended && parameters_.emphasis && parameters_.hpOn;
    }
    bool lowPassEnabled() const noexcept
    {
        return parameters_.extended && parameters_.emphasis && parameters_.lpOn;
    }
    static double resonanceQ(double resonance) noexcept
    {
        const auto amount = std::isfinite(resonance) ? std::clamp(resonance, 0.0, 1.0) : 0.0;
        // A second-order Butterworth response at zero, rising to an approx.
        // +3 dB resonant peak at maximum (Q = 1.3).
        return 0.7071067811865476 + (1.3 - 0.7071067811865476) * amount;
    }
    static double safeFrequency(double value, double low, double high, double fallback) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
    void configure() noexcept
    {
        const auto hpFrequency = safeFrequency(parameters_.hpHz, 20.0, 2000.0, 50.0);
        const auto lpFrequency = safeFrequency(parameters_.lpHz, 200.0, 20000.0, 16000.0);
        for (auto& channel : state_)
        {
            channel.emphasis.setParameters(parameters_);
            // The three white input controls are common to both interfaces.
            // The independently gated Extended lower-section HP can therefore
            // cascade with the common 50 Hz low cut when both are selected.
            channel.commonHighPass.set(FilterType::highPass, 50.0, 0.88, 0.0);
            channel.highPass.set(FilterType::highPass, hpFrequency, resonanceQ(parameters_.hpRes), 0.0);
            channel.lowPass.set(FilterType::lowPass, lpFrequency, resonanceQ(parameters_.lpRes), 0.0);
            channel.lowShelf.set(FilterType::lowShelf, 100.0, 0.7071067811865476,
                                 parameters_.lowShelf ? 2.0 : 0.0);
            channel.highShelf.set(FilterType::highShelf, 2000.0, 0.7071067811865476,
                                  parameters_.highShelf ? 2.0 : 0.0);
            channel.inputDb.setTarget(safeInputDb());
            channel.autoGain.setTarget(parameters_.autoGain ? 1.0 : 0.0);
            channel.dual.setTarget(parameters_.dual ? 1.0 : 0.0);
            channel.second.setTarget(parameters_.second ? 1.0 : 0.0);
            channel.commonHighPassMix.setTarget(parameters_.lowCut ? 1.0 : 0.0);
            channel.highPassMix.setTarget(highPassEnabled() ? 1.0 : 0.0);
            channel.lowPassMix.setTarget(lowPassEnabled() ? 1.0 : 0.0);
        }
    }

    double sampleRate_ = 48000.0;
    int channels_ = 2;
    Parameters parameters_ {};
    std::array<ChannelState, 2> state_ {};
    float activity_ = 0.0f;
};
} // namespace ivory
