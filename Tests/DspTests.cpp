#include "DSP/SaturationEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr auto pi = std::numbers::pi_v<double>;
constexpr std::size_t fftSize = 32768;
constexpr int toneBin = 683;
constexpr double sampleRate = 48000.0;
using Samples = std::vector<double>;
using Engine = ivory::SaturationEngine<double>;

struct Results
{
    explicit Results(const std::filesystem::path& directory) : output(directory)
    {
        std::filesystem::create_directories(output);
        csv.open(output / "metrics.csv");
        csv << "test,metric,value,unit\n" << std::setprecision(12);
    }

    void metric(const std::string& test, const std::string& name, double value, const std::string& unit)
    {
        csv << test << ',' << name << ',' << value << ',' << unit << '\n';
    }

    void check(const std::string& name, const std::function<void()>& action)
    {
        try
        {
            action();
            ++passed;
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }

    std::filesystem::path output;
    std::ofstream csv;
    int passed = 0;
    int failed = 0;
};

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

double rms(const Samples& samples)
{
    const double square = std::inner_product(samples.begin(), samples.end(), samples.begin(), 0.0);
    return std::sqrt(square / static_cast<double>(samples.size()));
}

double decibels(double amplitude)
{
    return 20.0 * std::log10(std::max(std::abs(amplitude), 1.0e-15));
}

ivory::Parameters neutral()
{
    ivory::Parameters p;
    p.emphasis = false;
    p.autoGain = false;
    return p;
}

Samples tone(ivory::Parameters p, double amplitude, double frequency, double rate = sampleRate,
             std::size_t count = fftSize)
{
    Engine engine;
    engine.prepare(rate, 1);
    engine.setParameters(p);
    const auto settling = static_cast<std::size_t>(rate * 0.25);
    Samples output(count);
    for (std::size_t i = 0; i < settling + count; ++i)
    {
        const auto x = amplitude * std::sin(2.0 * pi * frequency * static_cast<double>(i) / rate);
        const auto y = engine.processSample(x, 0);
        require(std::isfinite(y), "tone produced a non-finite sample");
        if (i >= settling)
            output[i - settling] = y;
    }
    return output;
}

std::array<double, 21> harmonics(const Samples& samples, int bin = toneBin)
{
    std::array<double, 21> amplitudes {};
    for (std::size_t h = 1; h < amplitudes.size(); ++h)
    {
        if (h * static_cast<std::size_t>(bin) >= samples.size() / 2)
            break;
        const auto angle = -2.0 * pi * static_cast<double>(h * static_cast<std::size_t>(bin))
                           / static_cast<double>(samples.size());
        const std::complex<double> advance(std::cos(angle), std::sin(angle));
        std::complex<double> phase(1.0, 0.0), sum(0.0, 0.0);
        for (const auto value : samples)
        {
            sum += value * phase;
            phase *= advance;
        }
        amplitudes[h] = 2.0 * std::abs(sum) / static_cast<double>(samples.size());
    }
    return amplitudes;
}

double thd(const std::array<double, 21>& h)
{
    double power = 0.0;
    for (std::size_t i = 2; i < h.size(); ++i)
        power += h[i] * h[i];
    return std::sqrt(power) / std::max(h[1], 1.0e-15);
}

double evenOdd(const std::array<double, 21>& h)
{
    double even = 0.0, odd = 0.0;
    for (std::size_t i = 2; i < h.size(); ++i)
        (i % 2 == 0 ? even : odd) += h[i] * h[i];
    return std::sqrt(even / std::max(odd, 1.0e-30));
}

double relativeDifference(const Samples& a, const Samples& b)
{
    require(a.size() == b.size(), "different sample counts");
    double difference = 0.0, energy = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        difference += (a[i] - b[i]) * (a[i] - b[i]);
        energy += a[i] * a[i];
    }
    return std::sqrt(difference / std::max(energy, 1.0e-30));
}

void writeWord(std::ofstream& file, std::uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        file.put(static_cast<char>((value >> (8 * i)) & 0xffu));
}

// PCM24 is readable by Python's standard wave module and common audio editors.
// Fixtures are normalized only if they exceed full scale; the CSV preserves level.
void writeWave(const std::filesystem::path& path, const Samples& samples, int rate)
{
    std::ofstream file(path, std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 3);
    file.write("RIFF", 4); writeWord(file, 36u + bytes, 4); file.write("WAVEfmt ", 8);
    writeWord(file, 16, 4); writeWord(file, 1, 2); writeWord(file, 1, 2);
    writeWord(file, static_cast<std::uint32_t>(rate), 4); writeWord(file, static_cast<std::uint32_t>(rate * 3), 4);
    writeWord(file, 3, 2); writeWord(file, 24, 2); file.write("data", 4); writeWord(file, bytes, 4);
    const auto peak = std::abs(*std::max_element(samples.begin(), samples.end(),
        [](double a, double b) { return std::abs(a) < std::abs(b); }));
    const double scale = peak > 1.0 ? 0.98 / peak : 1.0;
    for (double x : samples)
    {
        const auto value = static_cast<std::int32_t>(std::llround(std::clamp(x * scale, -1.0, 1.0) * 8388607.0));
        writeWord(file, static_cast<std::uint32_t>(value), 3);
    }
}

void harmonicTests(Results& results)
{
    auto p = neutral();
    const double frequency = static_cast<double>(toneBin) * sampleRate / static_cast<double>(fftSize);
    const auto quiet = harmonics(tone(p, 0.02, frequency));
    const auto baseAudio = tone(p, 0.8, frequency);
    const auto base = harmonics(baseAudio);
    const auto loud = harmonics(tone(p, 1.8, frequency));
    results.metric("saturation", "quiet_thd", thd(quiet) * 100.0, "percent");
    results.metric("saturation", "moderate_thd", thd(base) * 100.0, "percent");
    results.metric("saturation", "loud_thd", thd(loud) * 100.0, "percent");
    require(thd(base) > thd(quiet) * 5.0, "drive must increase harmonic distortion materially");
    require(thd(loud) > thd(base), "aggressive drive must develop more harmonic distortion");
    p.second = true;
    const auto secondAudio = tone(p, 0.8, frequency);
    const auto second = harmonics(secondAudio);
    results.metric("second", "normal_even_odd_ratio", evenOdd(base), "ratio");
    results.metric("second", "enabled_even_odd_ratio", evenOdd(second), "ratio");
    require(evenOdd(second) > evenOdd(base) * 1.5,
            "2ND must increase even versus odd harmonic energy by at least 3.5 dB");
    p.second = false;
    p.dual = true;
    const auto dualAudio = tone(p, 0.8, frequency);
    const auto dual = harmonics(dualAudio);
    double shapeDifference = 0.0;
    for (std::size_t h = 2; h < base.size(); ++h)
        shapeDifference += std::pow(dual[h] / dual[1] - base[h] / base[1], 2.0);
    shapeDifference = std::sqrt(shapeDifference);
    results.metric("dual", "normalized_harmonic_difference", shapeDifference, "ratio");
    results.metric("dual", "second_harmonic_change", decibels((dual[2] / dual[1]) / (base[2] / base[1])), "dB");
    require(shapeDifference > 0.005, "DUAL must change harmonic shape beyond a simple output gain");
    require(dual[2] / dual[1] > base[2] / base[1], "DUAL must increase second-harmonic content at nominal drive");
    std::ofstream csv(results.output / "harmonics.csv");
    csv << "harmonic,frequency_hz,normal_dbfs,second_dbfs,dual_dbfs\n" << std::setprecision(12);
    for (std::size_t h = 1; h < base.size(); ++h)
        csv << h << ',' << frequency * static_cast<double>(h) << ',' << decibels(base[h]) << ','
            << decibels(second[h]) << ',' << decibels(dual[h]) << '\n';
    writeWave(results.output / "sine_normal.wav", baseAudio, 48000);
    writeWave(results.output / "sine_second.wav", secondAudio, 48000);
    writeWave(results.output / "sine_dual.wav", dualAudio, 48000);
}

void emphasisTests(Results& results)
{
    std::array<Samples, 6> audio;
    auto p = neutral();
    p.emphasis = true;
    p.inputDb = 12.0;
    p.autoGain = true;
    for (int mode = 0; mode < 6; ++mode)
    {
        p.classicMode = mode;
        Engine engine;
        engine.prepare(sampleRate, 1);
        engine.setParameters(p);
        for (int i = 0; i < 60000; ++i)
        {
            const double t = static_cast<double>(i) / sampleRate;
            const double x = 0.14 * (std::sin(2.0 * pi * 87.0 * t) + std::sin(2.0 * pi * 997.0 * t)
                                     + std::sin(2.0 * pi * 7319.0 * t));
            const double y = engine.processSample(x, 0);
            if (i >= 12000)
                audio[static_cast<std::size_t>(mode)].push_back(y);
        }
        writeWave(results.output / ("classic_mode_" + std::to_string(mode + 1) + ".wav"),
                  audio[static_cast<std::size_t>(mode)], 48000);
    }
    double minimum = 1.0;
    for (std::size_t a = 0; a < audio.size(); ++a)
        for (std::size_t b = a + 1; b < audio.size(); ++b)
            minimum = std::min(minimum, relativeDifference(audio[a], audio[b]));
    results.metric("classic_modes", "minimum_pairwise_waveform_difference", minimum, "ratio");
    require(minimum > 0.01, "every Classic emphasis mode must produce a materially distinct result");

    // A linear EQ-only preset would retain the same relative response at all levels.
    // A pre/nonlinear/post profile must change harmonic content with mode and drive.
    const double f = static_cast<double>(toneBin) * sampleRate / static_cast<double>(fftSize);
    double lowestTHD = 1.0, highestTHD = 0.0;
    for (int mode = 0; mode < 6; ++mode)
    {
        p.classicMode = mode;
        const auto h = harmonics(tone(p, 0.3, f));
        lowestTHD = std::min(lowestTHD, thd(h));
        highestTHD = std::max(highestTHD, thd(h));
        results.metric("classic_modes", "mode_" + std::to_string(mode + 1) + "_thd", thd(h) * 100.0, "percent");
    }
    require(highestTHD > lowestTHD * 1.5, "emphasis profiles must alter nonlinear harmonic generation");
}

void extendedTests(Results& results)
{
    auto flat = neutral();
    flat.extended = true;
    flat.emphasis = true;
    auto compensated = flat;
    compensated.presence1 = 8;
    compensated.presence2 = 14;
    for (auto& band : compensated.bands)
    {
        band.preDb = 9.0;
        band.compensate = true;
    }
    double worst = 0.0;
    std::ofstream csv(results.output / "frequency_response.csv");
    csv << "frequency_hz,compensated_relative_db,hp_db,lp_db\n";
    for (const double frequency : {100.0, 150.0, 300.0, 600.0, 1000.0, 3000.0, 7500.0, 12000.0, 18000.0})
    {
        const double baseline = rms(tone(flat, 1.0e-5, frequency));
        const double error = decibels(rms(tone(compensated, 1.0e-5, frequency)) / baseline);
        worst = std::max(worst, std::abs(error));
        auto hp = flat, lp = flat;
        hp.hpOn = true; hp.hpHz = 500.0;
        lp.lpOn = true; lp.lpHz = 4000.0;
        csv << frequency << ',' << error << ',' << decibels(rms(tone(hp, 1.0e-5, frequency)) / baseline)
            << ',' << decibels(rms(tone(lp, 1.0e-5, frequency)) / baseline) << '\n';
    }
    results.metric("extended_comp", "worst_linear_response_error", worst, "dB");
    require(worst < 0.05, "complementary pre/post EQ must cancel within 0.05 dB at small signal");

    auto independent = compensated;
    for (auto& band : independent.bands)
    {
        band.compensate = false;
        band.postDb = 0.0;
    }
    require(relativeDifference(tone(compensated, 0.25, 1000.0), tone(independent, 0.25, 1000.0)) > 0.1,
            "disabling COMP must release independent post gain");

    for (std::size_t bandIndex = 0; bandIndex < flat.bands.size(); ++bandIndex)
    {
        auto pre = flat, post = flat;
        pre.bands[bandIndex].compensate = post.bands[bandIndex].compensate = false;
        pre.bands[bandIndex].preDb = 12.0;
        post.bands[bandIndex].postDb = 12.0;
        const std::array frequencies {100.0, 1000.0, ivory::presenceFrequencies[14], 10000.0};
        const double difference = relativeDifference(tone(pre, 0.8, frequencies[bandIndex]),
                                                     tone(post, 0.8, frequencies[bandIndex]));
        results.metric("extended_bands", "band_" + std::to_string(bandIndex + 1) + "_pre_post_difference", difference, "ratio");
        require(difference > 0.02, "each band's pre and post controls must affect different sides of saturation");
    }

    auto hp = flat, lp = flat;
    hp.hpOn = true; hp.hpHz = 500.0;
    lp.lpOn = true; lp.lpHz = 2000.0;
    const auto gain = [&](const ivory::Parameters& p, double f)
    { return decibels(rms(tone(p, 1.0e-5, f)) / rms(tone(flat, 1.0e-5, f))); };
    const double hpLow = gain(hp, 125.0), hpHigh = gain(hp, 2000.0);
    const double lpLow = gain(lp, 500.0), lpHigh = gain(lp, 8000.0);
    results.metric("filters", "hp_two_octaves_below_cutoff", hpLow, "dB");
    results.metric("filters", "lp_two_octaves_above_cutoff", lpHigh, "dB");
    require(hpLow < -22.0 && hpHigh > -1.0, "HP must reject bass with a second-order slope");
    require(lpHigh < -22.0 && lpLow > -1.0, "LP must reject treble with a second-order slope");
    hp.hpRes = 1.0; lp.lpRes = 1.0;
    const double hpResonance = gain(hp, 500.0), lpResonance = gain(lp, 2000.0);
    results.metric("filters", "hp_max_resonance_at_cutoff", hpResonance, "dB");
    results.metric("filters", "lp_max_resonance_at_cutoff", lpResonance, "dB");
    require(hpResonance > 1.0 && hpResonance < 5.0 && lpResonance > 1.0 && lpResonance < 5.0,
            "maximum resonance must provide a bounded, audible boost");

    // The official Extended manual makes EMPHASIS the master enable for both
    // the configurable EQ and the two uncompensated input filters.
    auto disabled = compensated;
    disabled.hpOn = disabled.lpOn = true;
    disabled.hpHz = 2000.0; disabled.lpHz = 200.0;
    disabled.emphasis = false;
    auto baseline = flat;
    baseline.emphasis = false;
    const double disabledDifference = relativeDifference(tone(baseline, 0.1, 1000.0),
                                                        tone(disabled, 0.1, 1000.0));
    results.metric("extended_master_enable", "disabled_filters_and_eq_residual", disabledDifference, "ratio");
    require(disabledDifference < 1.0e-8,
            "Extended EMPHASIS off must bypass HP/LP as well as pre/post EQ");

    // The three white input switches remain common to both interfaces and are
    // independent from the lower Extended section's EMPHASIS master switch.
    for (const bool extended : {false, true})
    {
        auto common = neutral();
        common.extended = extended;
        auto changed = common;
        changed.lowCut = true;
        require(decibels(rms(tone(changed, 1.0e-5, 12.5)) / rms(tone(common, 1.0e-5, 12.5))) < -20.0,
                "common LOW CUT must work in both views with EMPHASIS off");
        changed = common; changed.lowShelf = true;
        require(decibels(rms(tone(changed, 1.0e-5, 20.0)) / rms(tone(common, 1.0e-5, 20.0))) > 1.8,
                "common LOW SHELF must work in both views with EMPHASIS off");
        changed = common; changed.highShelf = true;
        require(decibels(rms(tone(changed, 1.0e-5, 12000.0)) / rms(tone(common, 1.0e-5, 12000.0))) > 1.8,
                "common HIGH SHELF must work in both views with EMPHASIS off");
    }
}

void gainAndTransientTests(Results& results)
{
    auto flat = neutral();
    auto driven = flat;
    driven.inputDb = 12.0;
    const double baseline = rms(tone(flat, 0.0001, 1000.0));
    const double raw = rms(tone(driven, 0.0001, 1000.0));
    driven.autoGain = true;
    const double compensated = rms(tone(driven, 0.0001, 1000.0));
    const double rawGain = decibels(raw / baseline), compensatedGain = decibels(compensated / baseline);
    results.metric("gain", "input_12db_small_signal_gain", rawGain, "dB");
    results.metric("gain", "autogain_12db_small_signal_gain", compensatedGain, "dB");
    require(std::abs(rawGain - 12.0) < 0.01, "INPUT must provide the selected small-signal gain");
    require(std::abs(compensatedGain) < 0.01, "AUTO GAIN must cancel the corresponding linear input gain");

    Engine engine;
    engine.prepare(sampleRate, 1);
    driven.dual = driven.second = true;
    engine.setParameters(driven);
    // Measure a steady setting; otherwise the first transient arrives during
    // the input/auto-gain ramp and is not comparable with the later pulses.
    for (int i = 0; i < 12000; ++i)
        engine.processSample(0.0, 0);
    Samples input(48000), output(48000);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double age = static_cast<double>(i % 4800) / sampleRate;
        input[i] = 0.12 * std::sin(2.0 * pi * 123.0 * t)
                 + 1.5 * std::exp(-age / 0.002) * std::sin(2.0 * pi * 1700.0 * age);
        output[i] = engine.processSample(input[i], 0);
    }
    const auto peak = [](const Samples& audio)
    { return std::abs(*std::max_element(audio.begin(), audio.end(), [](double a, double b) { return std::abs(a) < std::abs(b); })); };
    const double inputCrest = decibels(peak(input) / rms(input));
    const double outputCrest = decibels(peak(output) / rms(output));
    results.metric("transient", "input_crest", inputCrest, "dB");
    results.metric("transient", "wet_crest", outputCrest, "dB");
    require(outputCrest < inputCrest - 1.0, "saturation must round transient peaks relative to average level");
    writeWave(results.output / "transient_input.wav", input, 48000);
    writeWave(results.output / "transient_wet.wav", output, 48000);
}

template <typename Sample>
void rateAndAutomationTests(Results& results, const char* precision)
{
    double maximum = 0.0;
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    {
        ivory::SaturationEngine<Sample> engine;
        engine.prepare(rate, 2);
        auto p = neutral();
        p.extended = true;
        p.emphasis = true;
        for (int i = 0; i < static_cast<int>(rate / 2); ++i)
        {
            if (i % 127 == 0)
            {
                const bool high = (i / 127) % 2 == 0;
                p.inputDb = high ? 36.0 : -24.0;
                p.dual = high; p.second = !high;
                p.hpOn = p.lpOn = true;
                p.hpHz = high ? 2000.0 : 20.0;
                p.lpHz = high ? 200.0 : 20000.0;
                p.hpRes = p.lpRes = high ? 1.0 : 0.0;
                p.presence1 = high ? 0 : 20; p.presence2 = high ? 20 : 0;
                for (auto& band : p.bands)
                {
                    band.preDb = high ? 18.0 : -18.0;
                    band.postDb = high ? -18.0 : 18.0;
                    band.compensate = high;
                }
                engine.setParameters(p);
            }
            const double phase = 2.0 * pi * 997.0 * static_cast<double>(i) / rate;
            const auto input = static_cast<Sample>((i % 13 == 0 ? 10000.0 : 0.7) * std::sin(phase));
            for (int channel = 0; channel < 2; ++channel)
            {
                const auto y = engine.processSample(channel == 0 ? input : -input, channel);
                require(std::isfinite(y), "extreme signal/automation generated non-finite output");
                maximum = std::max(maximum, std::abs(static_cast<double>(y)));
                require(std::abs(y) < static_cast<Sample>(10000), "automation generated an unbounded output");
            }
        }
        engine.reset();
        for (int i = 0; i < 4096; ++i)
        {
            require(engine.processSample(static_cast<Sample>(0), 0) == static_cast<Sample>(0),
                    "reset must restore exact silence");
            require(engine.processSample(static_cast<Sample>(0), 1) == static_cast<Sample>(0),
                    "reset must restore exact silence on both channels");
        }
    }
    results.metric(std::string("automation_") + precision, "maximum_absolute_output", maximum, "linear");
}

void impulseAndStereoTests(Results& results)
{
    Engine stereo, mono;
    stereo.prepare(sampleRate, 2); mono.prepare(sampleRate, 1);
    auto p = neutral();
    p.dual = true; p.second = true;
    stereo.setParameters(p); mono.setParameters(p);
    Samples impulse(48000);
    for (std::size_t i = 0; i < impulse.size(); ++i)
    {
        const double x = i == 12000 ? 0.8 : 0.0;
        const auto left = stereo.processSample(x, 0);
        const auto right = stereo.processSample(0.0, 1);
        const auto reference = mono.processSample(x, 0);
        require(right == 0.0, "left-channel signal leaked into a silent right channel");
        require(std::abs(left - reference) < 1.0e-12, "mono and stereo left signal paths disagree");
        require(std::isfinite(left), "impulse generated non-finite output");
        impulse[i] = left;
    }
    results.metric("impulse", "tail_at_750ms", std::abs(impulse.back()), "linear");
    require(std::abs(impulse.back()) < 1.0e-8, "impulse response must decay to silence");
    writeWave(results.output / "impulse.wav", impulse, 48000);
}

template <typename Sample>
void denormalTests()
{
    ivory::SaturationEngine<Sample> engine;
    engine.prepare(sampleRate, 1);
    const auto tiny = std::numeric_limits<Sample>::denorm_min() * static_cast<Sample>(8);
    for (int i = 0; i < 10000; ++i)
    {
        const auto y = engine.processSample(tiny, 0);
        require(std::isfinite(y), "subnormal input generated non-finite output");
        require(std::fpclassify(y) != FP_SUBNORMAL, "subnormal output requires explicit denormal protection");
    }
}

void activityTests(Results& results)
{
    Engine engine;
    engine.prepare(sampleRate, 1);
    engine.setParameters(neutral());
    for (int i = 0; i < 12000; ++i)
        engine.processSample(0.0, 0);
    require(engine.getActivityAndReset() == 0.0f, "silence must not light nonlinear activity");
    for (int i = 0; i < 48000; ++i)
        engine.processSample(0.005 * std::sin(2.0 * pi * 997.0 * i / sampleRate), 0);
    const auto quiet = engine.getActivityAndReset();
    engine.reset();
    for (int i = 0; i < 48000; ++i)
        engine.processSample(1.5 * std::sin(2.0 * pi * 997.0 * i / sampleRate), 0);
    const auto loud = engine.getActivityAndReset();
    results.metric("activity", "quiet", quiet, "linear");
    results.metric("activity", "driven", loud, "linear");
    require(loud > quiet * 10.0f && loud > 0.01f, "activity must follow actual nonlinear bending");
    require(engine.getActivityAndReset() == 0.0f, "activity reads must reset accumulated activity");
}

template <typename Sample>
void benchmark(Results& results, const char* precision)
{
    ivory::SaturationEngine<Sample> engine;
    engine.prepare(sampleRate, 2);
    auto p = neutral();
    p.extended = p.emphasis = p.dual = p.second = p.hpOn = p.lpOn = true;
    for (auto& band : p.bands)
        band.preDb = 6.0;
    engine.setParameters(p);
    constexpr int frames = 480000;
    double checksum = 0.0;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i)
    {
        const auto x = static_cast<Sample>(0.4 * std::sin(2.0 * pi * 997.0 * i / sampleRate));
        checksum += static_cast<double>(engine.processSample(x, 0));
        checksum += static_cast<double>(engine.processSample(-x, 1));
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    require(std::isfinite(checksum), "benchmark output was not finite");
    results.metric(std::string("cpu_") + precision, "stereo_base_rate_realtime_percent", seconds / 10.0 * 100.0, "percent_one_core");
    results.metric(std::string("cpu_") + precision, "nanoseconds_per_channel_sample", seconds * 1.0e9 / (frames * 2.0), "ns");
    results.metric(std::string("cpu_") + precision, "checksum", checksum, "linear");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Results results(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("validation"));
        results.check("harmonic drive, 2ND and DUAL", [&] { harmonicTests(results); });
        results.check("six nonlinear emphasis profiles", [&] { emphasisTests(results); });
        results.check("Extended compensation, independent bands and filters", [&] { extendedTests(results); });
        results.check("input/auto gain calibration and transient peak rounding", [&] { gainAndTransientTests(results); });
        results.check("all requested rates and extreme automation / float", [&] { rateAndAutomationTests<float>(results, "float"); });
        results.check("all requested rates and extreme automation / double", [&] { rateAndAutomationTests<double>(results, "double"); });
        results.check("impulse, mono equivalence and stereo independence", [&] { impulseAndStereoTests(results); });
        results.check("subnormal handling / float", [] { denormalTests<float>(); });
        results.check("subnormal handling / double", [] { denormalTests<double>(); });
        results.check("nonlinear activity metering", [&] { activityTests(results); });
        results.check("CPU measurements / float", [&] { benchmark<float>(results, "float"); });
        results.check("CPU measurements / double", [&] { benchmark<double>(results, "double"); });
        std::cout << results.passed << " passed, " << results.failed << " failed; measurements: " << results.output << '\n';
        return results.failed == 0 ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Validation setup failed: " << error.what() << '\n';
        return 2;
    }
}
