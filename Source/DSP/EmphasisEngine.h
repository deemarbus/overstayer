#pragma once

#include "Filters.h"
#include "Parameters.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ivory
{
struct EmphasisProfile
{
    std::array<double, 4> frequencies;
    std::array<double, 4> gains;
    std::array<double, 4> q;
};

// Original pre/post profiles, inferred from the supplied functional goals.
// Their inverse post curves preserve small-signal balance while the nonlinear
// stage retains newly generated frequency-dependent harmonic content.
inline constexpr std::array<EmphasisProfile, 6> classicProfiles {{
    {{{150.0, 1600.0, 6000.0, 7500.0}}, {{-3.0, 4.0, 2.0, -1.0}}, {{0.707, 0.65, 0.7, 0.707}}},
    {{{150.0, 1000.0, 8000.0, 7500.0}}, {{5.0, -5.0, 1.0, 5.0}}, {{0.707, 0.55, 0.7, 0.707}}},
    {{{150.0, 2500.0, 4500.0, 7500.0}}, {{-1.0, 7.0, 2.0, -1.0}}, {{0.707, 0.7, 0.8, 0.707}}},
    {{{150.0, 4000.0, 8500.0, 7500.0}}, {{-2.0, 3.0, 7.0, 1.0}}, {{0.707, 0.7, 0.8, 0.707}}},
    {{{150.0, 150.0, 1600.0, 7500.0}}, {{9.0, 3.0, -3.0, -5.0}}, {{0.707, 0.7, 0.6, 0.707}}},
    {{{150.0, 2500.0, 6000.0, 7500.0}}, {{-4.0, 3.0, 5.0, 5.0}}, {{0.707, 0.6, 0.65, 0.707}}}
}};

template <typename Sample>
class EmphasisEngine
{
public:
    void prepare(double sampleRate)
    {
        for (auto& band : pre_) band.prepare(sampleRate);
        for (auto& band : post_) band.prepare(sampleRate);
    }
    void reset() noexcept
    {
        for (auto& band : pre_) band.reset();
        for (auto& band : post_) band.reset();
    }
    void setParameters(const Parameters& p) noexcept
    {
        auto profile = classicProfiles[static_cast<std::size_t>(std::clamp(p.classicMode, 0, 5))];
        if (p.extended)
        {
            profile.frequencies = {150.0,
                presenceFrequencies[static_cast<std::size_t>(std::clamp(p.presence1, 0, 20))],
                presenceFrequencies[static_cast<std::size_t>(std::clamp(p.presence2, 0, 20))],
                7500.0};
            profile.q = {0.7071067811865476, 0.8, 0.8, 0.7071067811865476};
        }
        constexpr std::array<FilterType, 4> types {
            FilterType::lowShelf, FilterType::peak, FilterType::peak, FilterType::highShelf};
        for (std::size_t i = 0; i < pre_.size(); ++i)
        {
            const auto preGain = p.emphasis ? (p.extended ? finiteGain(p.bands[i].preDb)
                                                                        : profile.gains[i]) : 0.0;
            const auto postGain = !p.emphasis ? 0.0
                : (p.extended && !p.bands[i].compensate ? finiteGain(p.bands[i].postDb) : -preGain);
            pre_[i].set(types[i], profile.frequencies[i], profile.q[i], preGain);
            post_[i].set(types[i], profile.frequencies[i], profile.q[i], postGain);
        }
    }
    Sample processPre(Sample sample) noexcept
    {
        for (auto& band : pre_) sample = band.processSample(sample);
        return sample;
    }
    Sample processPost(Sample sample) noexcept
    {
        // Reverse filter order makes the complementary small-signal transfer
        // easier to inspect, although LTI filter multiplication commutes.
        for (auto it = post_.rbegin(); it != post_.rend(); ++it)
            sample = it->processSample(sample);
        return sample;
    }
private:
    static double finiteGain(double gain) noexcept
    {
        return std::isfinite(gain) ? std::clamp(gain, -24.0, 24.0) : 0.0;
    }
    std::array<SmoothBiquad<Sample>, 4> pre_, post_;
};
} // namespace ivory
