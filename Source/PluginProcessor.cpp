#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/SaturationEngine.h"
#include "Presets/FactoryPresets.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <type_traits>
#include <vector>

namespace
{
constexpr auto stateType = "IvoryDriveState";
constexpr int stateVersion = 1;
enum ParameterIndex
{
    input, output, mix, autoGain, toDry, dual, second, lowCut, lowShelf, highShelf,
    emphasis, extended, classicMode, hpOn, hpFreq, hpRes, lpOn, lpFreq, lpRes,
    bassPre, bassPost, bassComp, p1Pre, p1Post, p1Comp, p1Freq,
    p2Pre, p2Post, p2Comp, p2Freq, treblePre, treblePost, trebleComp,
    oversampling, bypass, parameterCount
};
constexpr std::array<const char*, parameterCount> parameterIDs {
    "input", "output", "mix", "autoGain", "toDry", "dual", "second", "lowCut", "lowShelf", "highShelf",
    "emphasis", "extended", "classicMode", "hpOn", "hpFreq", "hpRes", "lpOn", "lpFreq", "lpRes",
    "bassPre", "bassPost", "bassComp", "p1Pre", "p1Post", "p1Comp", "p1Freq",
    "p2Pre", "p2Post", "p2Comp", "p2Freq", "treblePre", "treblePost", "trebleComp", "oversampling", "bypass"
};

template <typename Sample>
class SampleDelay
{
public:
    void prepare(int numberOfSamples)
    {
        delay = juce::jmax(0, numberOfSamples);
        for (auto& channel : data)
            channel.assign(static_cast<std::size_t>(juce::jmax(1, delay)), Sample {});
        position.fill(0);
    }

    Sample tick(Sample value, int channel) noexcept
    {
        if (delay == 0) return value;
        auto& offset = position[static_cast<std::size_t>(channel)];
        auto& slot = data[static_cast<std::size_t>(channel)][static_cast<std::size_t>(offset)];
        const auto delayed = slot;
        slot = value;
        if (++offset == delay) offset = 0;
        return delayed;
    }

    void reset() noexcept
    {
        for (auto& channel : data) std::fill(channel.begin(), channel.end(), Sample {});
        position.fill(0);
    }

private:
    int delay = 0;
    std::array<int, 2> position {};
    std::array<std::vector<Sample>, 2> data;
};

struct MeterResult
{
    std::array<float, 2> in {}, out {};
    float activity = 0;
};

template <typename Sample>
class ProcessingBank
{
public:
    void prepare(double sampleRate, int channelCount, int blockCapacity,
                 const ivory::Parameters& initialParameters, int initialQuality, bool initiallyBypassed)
    {
        channels = channelCount;
        capacity = blockCapacity;
        original.setSize(channels, capacity);
        original.clear();
        maximumLatency = 0;
        activeQuality = requestedQuality = initialQuality;
        transitionQuality = -1;
        warmupLength = juce::jmax(1, static_cast<int>(std::lround(sampleRate * 0.03)));
        fadeLength = juce::jmax(1, static_cast<int>(std::lround(sampleRate * 0.03)));
        for (std::size_t quality = 0; quality < paths.size(); ++quality)
        {
            auto& path = paths[quality];
            // Linear-phase rate-conversion filters keep the latency-aligned dry
            // and wet fundamentals in phase during parallel saturation mixing.
            path.oversampler = std::make_unique<juce::dsp::Oversampling<Sample>>(
                static_cast<std::size_t>(channels), quality + 1,
                juce::dsp::Oversampling<Sample>::filterHalfBandFIREquiripple, true, false);
            path.oversampler->initProcessing(static_cast<std::size_t>(capacity));
            path.scratch.setSize(channels, capacity);
            path.scratch.clear();
            const auto factor = static_cast<int>(1u << (quality + 1));
            const auto rawLatency = static_cast<double>(path.oversampler->getLatencyInSamples());
            path.latency = static_cast<int>(std::ceil(rawLatency - 1.0e-6));
            const auto highRatePadding = static_cast<int>(std::llround((static_cast<double>(path.latency) - rawLatency) * factor));
            jassert(highRatePadding >= 0 && highRatePadding < factor);
            jassert(std::abs(rawLatency + static_cast<double>(highRatePadding) / factor - path.latency) < 1.0e-5);
            // FIR group delay is an integer at the high rate. Padding there
            // makes base-rate latency integral without a phase-warping allpass.
            path.highRateAlignment.prepare(highRatePadding);
            maximumLatency = juce::jmax(maximumLatency, path.latency);
            path.engine.prepare(sampleRate * static_cast<double>(1u << (quality + 1)), channels);
            path.engine.setParameters(initialParameters);
        }
        for (auto& path : paths) path.alignment.prepare(maximumLatency - path.latency);
        dryDelay.prepare(maximumLatency);
        mixSmoother.reset(sampleRate, 0.02);
        outputSmoother.reset(sampleRate, 0.02);
        dryGainSmoother.reset(sampleRate, 0.02);
        bypassSmoother.reset(sampleRate, 0.01);
        mixSmoother.setCurrentAndTargetValue(static_cast<Sample>(initialParameters.mix));
        outputSmoother.setCurrentAndTargetValue(static_cast<Sample>(juce::Decibels::decibelsToGain(initialParameters.outputDb)));
        dryGainSmoother.setCurrentAndTargetValue(static_cast<Sample>(initialParameters.toDry ? juce::Decibels::decibelsToGain(initialParameters.inputDb) : 1.0));
        bypassSmoother.setCurrentAndTargetValue(initiallyBypassed ? Sample(1) : Sample(0));
    }

    void setTargets(const ivory::Parameters& p, int quality, bool isBypassed) noexcept
    {
        mixSmoother.setTargetValue(static_cast<Sample>(p.mix));
        outputSmoother.setTargetValue(static_cast<Sample>(juce::Decibels::decibelsToGain(p.outputDb)));
        dryGainSmoother.setTargetValue(static_cast<Sample>(p.toDry ? juce::Decibels::decibelsToGain(p.inputDb) : 1.0));
        bypassSmoother.setTargetValue(isBypassed ? Sample(1) : Sample(0));
        requestedQuality = quality;
        paths[static_cast<std::size_t>(activeQuality)].engine.setParameters(p);
        if (transitionQuality >= 0) paths[static_cast<std::size_t>(transitionQuality)].engine.setParameters(p);
    }

    MeterResult processChunk(juce::AudioBuffer<Sample>& buffer, int start, int count,
                             const ivory::Parameters& p) noexcept
    {
        MeterResult meters;
        if (transitionQuality < 0 && requestedQuality != activeQuality)
        {
            transitionQuality = requestedQuality;
            auto& incoming = paths[static_cast<std::size_t>(transitionQuality)];
            // These resets clear preallocated fixed-size states. The new path
            // warms silently before a fade; the audible path remains untouched.
            incoming.oversampler->reset();
            incoming.engine.setParameters(p);
            incoming.engine.reset();
            incoming.highRateAlignment.reset();
            incoming.alignment.reset();
            warmupRemaining = warmupLength + maximumLatency;
            fadePosition = 0;
        }
        const auto inputGain = static_cast<Sample>(juce::Decibels::decibelsToGain(p.inputDb));
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto* source = buffer.getReadPointer(ch, start);
            auto* clean = original.getWritePointer(ch);
            for (int n = 0; n < count; ++n)
            {
                clean[n] = std::isfinite(source[n]) ? juce::jlimit(Sample(-1000000), Sample(1000000), source[n]) : Sample(0);
                meters.in[static_cast<std::size_t>(ch)] = juce::jmax(meters.in[static_cast<std::size_t>(ch)], static_cast<float>(std::abs(clean[n] * inputGain)));
            }
        }

        // Steady-state CPU follows the selected quality. A quality change runs
        // only the outgoing/incoming paths, with fixed latency throughout.
        std::array<float, 3> activities {};
        for (std::size_t q = 0; q < paths.size(); ++q)
        {
            if (static_cast<int>(q) != activeQuality && static_cast<int>(q) != transitionQuality) continue;
            auto& path = paths[q];
            for (int ch = 0; ch < channels; ++ch)
                path.scratch.copyFrom(ch, 0, original, ch, 0, count);
            auto block = juce::dsp::AudioBlock<Sample>(path.scratch).getSubBlock(0, static_cast<std::size_t>(count));
            auto up = path.oversampler->processSamplesUp(block);
            for (int ch = 0; ch < channels; ++ch)
            {
                auto* data = up.getChannelPointer(static_cast<std::size_t>(ch));
                for (std::size_t n = 0; n < up.getNumSamples(); ++n)
                    data[n] = path.highRateAlignment.tick(path.engine.processSample(data[n], ch), ch);
            }
            path.oversampler->processSamplesDown(block);
            activities[q] = path.engine.getActivityAndReset();
        }

        for (int n = 0; n < count; ++n)
        {
            const auto blend = mixSmoother.getNextValue();
            const auto outGain = outputSmoother.getNextValue();
            const auto dryGain = dryGainSmoother.getNextValue();
            const auto bypassBlend = bypassSmoother.getNextValue();
            std::array<Sample, 3> weights {};
            auto fade = Sample(0);
            if (transitionQuality >= 0)
            {
                if (warmupRemaining > 0) --warmupRemaining;
                else fade = static_cast<Sample>(++fadePosition) / static_cast<Sample>(fadeLength);
                weights[static_cast<std::size_t>(transitionQuality)] = fade;
            }
            weights[static_cast<std::size_t>(activeQuality)] = Sample(1) - fade;
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto dry = dryDelay.tick(original.getSample(ch, n), ch);
                Sample wet {};
                for (std::size_t q = 0; q < paths.size(); ++q)
                    if (static_cast<int>(q) == activeQuality || static_cast<int>(q) == transitionQuality)
                        wet += weights[q] * paths[q].alignment.tick(paths[q].scratch.getSample(ch, n), ch);
                const auto effected = (dry * dryGain * (Sample(1) - blend) + wet * blend) * outGain;
                const auto result = effected + (dry - effected) * bypassBlend;
                const auto safe = std::isfinite(result) ? result : Sample(0);
                buffer.setSample(ch, start + n, safe);
                meters.out[static_cast<std::size_t>(ch)] = juce::jmax(meters.out[static_cast<std::size_t>(ch)], static_cast<float>(std::abs(safe)));
            }
            if (transitionQuality >= 0 && fadePosition >= fadeLength)
            {
                activeQuality = transitionQuality;
                transitionQuality = -1;
            }
        }
        const auto fade = transitionQuality >= 0 ? static_cast<float>(fadePosition) / static_cast<float>(fadeLength) : 0.0f;
        meters.activity = activities[static_cast<std::size_t>(activeQuality)] * (1.0f - fade);
        if (transitionQuality >= 0) meters.activity += activities[static_cast<std::size_t>(transitionQuality)] * fade;
        return meters;
    }

    int getLatency() const noexcept { return maximumLatency; }
    int getCapacity() const noexcept { return capacity; }

private:
    struct Path
    {
        std::unique_ptr<juce::dsp::Oversampling<Sample>> oversampler;
        ivory::SaturationEngine<Sample> engine;
        juce::AudioBuffer<Sample> scratch;
        SampleDelay<Sample> highRateAlignment;
        SampleDelay<Sample> alignment;
        int latency = 0;
    };
    int channels = 0, capacity = 0, maximumLatency = 0;
    int activeQuality = 1, requestedQuality = 1, transitionQuality = -1;
    int warmupLength = 1, warmupRemaining = 0, fadeLength = 1, fadePosition = 0;
    std::array<Path, 3> paths;
    juce::AudioBuffer<Sample> original;
    SampleDelay<Sample> dryDelay;
    juce::SmoothedValue<Sample> mixSmoother, outputSmoother, dryGainSmoother, bypassSmoother;
};
} // namespace

struct IvoryDriveAudioProcessor::ProcessingState
{
    explicit ProcessingState(juce::AudioProcessorValueTreeState& state)
    {
        for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = state.getRawParameterValue(parameterIDs[i]);
    }

    float value(ParameterIndex id) const noexcept { return raw[static_cast<std::size_t>(id)]->load(std::memory_order_relaxed); }
    bool enabled(ParameterIndex id) const noexcept { return value(id) >= 0.5f; }
    ivory::Parameters snapshot() const noexcept
    {
        ivory::Parameters p;
        p.inputDb = value(input); p.outputDb = value(output); p.mix = value(mix) * 0.01;
        p.autoGain = enabled(autoGain); p.toDry = enabled(toDry); p.dual = enabled(dual); p.second = enabled(second);
        p.lowCut = enabled(lowCut); p.lowShelf = enabled(lowShelf); p.highShelf = enabled(highShelf);
        p.emphasis = enabled(emphasis); p.extended = enabled(extended); p.classicMode = static_cast<int>(value(classicMode));
        p.hpOn = enabled(hpOn); p.hpHz = value(hpFreq); p.hpRes = value(hpRes);
        p.lpOn = enabled(lpOn); p.lpHz = value(lpFreq); p.lpRes = value(lpRes);
        p.presence1 = static_cast<int>(value(p1Freq)); p.presence2 = static_cast<int>(value(p2Freq));
        p.bands = {{ {value(bassPre), value(bassPost), enabled(bassComp)},
                     {value(p1Pre), value(p1Post), enabled(p1Comp)},
                     {value(p2Pre), value(p2Post), enabled(p2Comp)},
                     {value(treblePre), value(treblePost), enabled(trebleComp)} }};
        return p;
    }
    std::array<std::atomic<float>*, parameterCount> raw {};
    ProcessingBank<float> single;
    ProcessingBank<double> dualPrecision;
    double rate = 48000;
    int channelCount = 2;
    bool prepared = false;
};

juce::AudioProcessorValueTreeState::ParameterLayout IvoryDriveAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto continuous = [&layout](const char* id, const char* name, float minimum, float maximum, float initial, const char* unit = "", float skew = 1.0f)
    {
        juce::NormalisableRange<float> range(minimum, maximum, 0.01f, skew);
        auto attributes = juce::AudioParameterFloatAttributes().withLabel(unit);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID {id, 1}, name, range, initial, attributes));
    };
    auto toggle = [&layout](const char* id, const char* name, bool initial)
    {
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID {id, 1}, name, initial));
    };
    continuous("input", "Input", -24, 36, 0, "dB");
    continuous("output", "Output", -36, 12, 0, "dB");
    continuous("mix", "Mix", 0, 100, 100, "%");
    toggle("autoGain", "Auto Gain", true); toggle("toDry", "To Dry", false);
    toggle("dual", "Dual", false); toggle("second", "Second Harmonic", false);
    toggle("lowCut", "Low Cut", false); toggle("lowShelf", "Low Shelf", false); toggle("highShelf", "High Shelf", false);
    toggle("emphasis", "Emphasis", true); toggle("extended", "Extended", false);
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID {"classicMode", 1}, "Classic Emphasis",
        juce::StringArray {"Classic", "Low Bandwidth", "Low Presence", "High Presence", "Bass Distortion", "Treble Distortion"}, 0));
    toggle("hpOn", "High Pass On", false);
    continuous("hpFreq", "High Pass Frequency", 20, 2000, 50, "Hz", 0.3f);
    continuous("hpRes", "High Pass Resonance", 0, 1, 0);
    toggle("lpOn", "Low Pass On", false);
    continuous("lpFreq", "Low Pass Frequency", 200, 20000, 16000, "Hz", 0.3f);
    continuous("lpRes", "Low Pass Resonance", 0, 1, 0);
    constexpr std::array<const char*, 4> prefixes {"bass", "p1", "p2", "treble"};
    constexpr std::array<const char*, 4> labels {"Bass", "Presence 1", "Presence 2", "Treble"};
    juce::StringArray frequencies;
    for (const auto frequency : ivory::presenceFrequencies)
        frequencies.add(frequency >= 1000 ? juce::String(frequency / 1000.0, 2) + " kHz" : juce::String(frequency, 1) + " Hz");
    for (std::size_t i = 0; i < prefixes.size(); ++i)
    {
        const auto prefix = juce::String(prefixes[i]);
        const auto label = juce::String(labels[i]);
        continuous((prefix + "Pre").toRawUTF8(), (label + " Pre Gain").toRawUTF8(), -12, 12, 0, "dB");
        continuous((prefix + "Post").toRawUTF8(), (label + " Post Gain").toRawUTF8(), -12, 12, 0, "dB");
        toggle((prefix + "Comp").toRawUTF8(), (label + " Compensation").toRawUTF8(), true);
        if (i == 1 || i == 2)
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID {prefix + "Freq", 1}, label + " Frequency", frequencies, i == 1 ? 10 : 14));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID {"oversampling", 1}, "Oversampling", juce::StringArray {"2x", "4x", "8x"}, 1));
    toggle("bypass", "Bypass", false);
    return layout;
}

IvoryDriveAudioProcessor::IvoryDriveAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, createParameterLayout()),
      processing(std::make_unique<ProcessingState>(parameters))
{
    setCurrentProgram(0);
}

IvoryDriveAudioProcessor::~IvoryDriveAudioProcessor() = default;

bool IvoryDriveAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto inputLayout = layout.getMainInputChannelSet();
    return inputLayout == layout.getMainOutputChannelSet()
        && (inputLayout == juce::AudioChannelSet::mono() || inputLayout == juce::AudioChannelSet::stereo());
}

void IvoryDriveAudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    processing->rate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.0;
    processing->channelCount = juce::jlimit(1, 2, getTotalNumInputChannels());
    const auto capacity = juce::jlimit(16, 8192, maximumExpectedSamplesPerBlock);
    const auto p = processing->snapshot();
    const auto quality = juce::jlimit(0, 2, static_cast<int>(processing->value(oversampling)));
    const auto initiallyBypassed = processing->enabled(bypass);
    processing->single.prepare(processing->rate, processing->channelCount, capacity, p, quality, initiallyBypassed);
    processing->dualPrecision.prepare(processing->rate, processing->channelCount, capacity, p, quality, initiallyBypassed);
    jassert(processing->single.getLatency() == processing->dualPrecision.getLatency());
    setLatencySamples(juce::jmax(processing->single.getLatency(), processing->dualPrecision.getLatency()));
    processing->prepared = true;
    for (auto& peak : inputPeaks) peak.store(0, std::memory_order_relaxed);
    for (auto& peak : outputPeaks) peak.store(0, std::memory_order_relaxed);
    saturationActivity.store(0, std::memory_order_relaxed);
}

void IvoryDriveAudioProcessor::releaseResources()
{
    processing->prepared = false;
}

template <typename Sample>
void IvoryDriveAudioProcessor::process(juce::AudioBuffer<Sample>& buffer, bool hostBypassed)
{
    juce::ScopedNoDenormals noDenormals;
    if (!processing->prepared || buffer.getNumChannels() < processing->channelCount)
    {
        buffer.clear();
        return;
    }
    for (int ch = processing->channelCount; ch < buffer.getNumChannels(); ++ch) buffer.clear(ch, 0, buffer.getNumSamples());
    if (buffer.getNumSamples() == 0) return;
    auto& bank = [&]() -> ProcessingBank<Sample>&
    {
        if constexpr (std::is_same_v<Sample, float>) return processing->single;
        else return processing->dualPrecision;
    }();
    const auto p = processing->snapshot();
    bank.setTargets(p, juce::jlimit(0, 2, static_cast<int>(processing->value(oversampling))), hostBypassed || processing->enabled(bypass));
    MeterResult total;
    for (int start = 0; start < buffer.getNumSamples(); start += bank.getCapacity())
    {
        const auto count = juce::jmin(bank.getCapacity(), buffer.getNumSamples() - start);
        const auto chunk = bank.processChunk(buffer, start, count, p);
        for (std::size_t ch = 0; ch < total.in.size(); ++ch)
        {
            total.in[ch] = juce::jmax(total.in[ch], chunk.in[ch]);
            total.out[ch] = juce::jmax(total.out[ch], chunk.out[ch]);
        }
        total.activity = juce::jmax(total.activity, chunk.activity);
    }
    const auto decay = static_cast<float>(std::exp(-static_cast<double>(buffer.getNumSamples()) / (0.3 * processing->rate)));
    for (std::size_t ch = 0; ch < inputPeaks.size(); ++ch)
    {
        inputPeaks[ch].store(juce::jmax(total.in[ch], inputPeaks[ch].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
        outputPeaks[ch].store(juce::jmax(total.out[ch], outputPeaks[ch].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    }
    saturationActivity.store(juce::jlimit(0.0f, 1.0f, juce::jmax(total.activity, saturationActivity.load(std::memory_order_relaxed) * decay)), std::memory_order_relaxed);
}

void IvoryDriveAudioProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { m.clear(); process(b, false); }
void IvoryDriveAudioProcessor::processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer& m) { m.clear(); process(b, false); }
void IvoryDriveAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& b, juce::MidiBuffer& m) { m.clear(); process(b, true); }
void IvoryDriveAudioProcessor::processBlockBypassed(juce::AudioBuffer<double>& b, juce::MidiBuffer& m) { m.clear(); process(b, true); }

juce::AudioProcessorParameter* IvoryDriveAudioProcessor::getBypassParameter() const { return parameters.getParameter("bypass"); }
juce::AudioProcessorEditor* IvoryDriveAudioProcessor::createEditor() { return new IvoryDriveAudioProcessorEditor(*this); }

float IvoryDriveAudioProcessor::getInputPeak(int channel) const noexcept
{
    return inputPeaks[static_cast<std::size_t>(juce::jlimit(0, 1, channel))].load(std::memory_order_relaxed);
}

float IvoryDriveAudioProcessor::getOutputPeak(int channel) const noexcept
{
    return outputPeaks[static_cast<std::size_t>(juce::jlimit(0, 1, channel))].load(std::memory_order_relaxed);
}

int IvoryDriveAudioProcessor::getNumPrograms() { return static_cast<int>(ivory::presets::factory.size()); }
int IvoryDriveAudioProcessor::getCurrentProgram() { return juce::jmax(0, currentProgramIndex.load(std::memory_order_relaxed)); }

const juce::String IvoryDriveAudioProcessor::getProgramName(int index)
{
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? ivory::presets::factory[static_cast<std::size_t>(index)].name : juce::String {};
}

void IvoryDriveAudioProcessor::setCurrentProgram(int index)
{
    if (!juce::isPositiveAndBelow(index, getNumPrograms())) return;
    for (const auto* id : parameterIDs)
    {
        auto* parameter = parameters.getParameter(id);
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
    }
    for (const auto& setting : ivory::presets::factory[static_cast<std::size_t>(index)].settings)
    {
        auto* parameter = parameters.getParameter(setting.id);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(setting.value));
    }
    currentProgramIndex.store(index, std::memory_order_relaxed);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
}

juce::String IvoryDriveAudioProcessor::getCurrentPresetName() const
{
    const auto index = currentProgramIndex.load(std::memory_order_relaxed);
    if (juce::isPositiveAndBelow(index, static_cast<int>(ivory::presets::factory.size())))
        return ivory::presets::factory[static_cast<std::size_t>(index)].name;
    const juce::ScopedLock lock(presetNameLock);
    return userPresetName.isNotEmpty() ? userPresetName : "User Preset";
}

juce::ValueTree IvoryDriveAudioProcessor::createStateSnapshot()
{
    auto state = parameters.copyState();
    state.setProperty("version", stateVersion, nullptr);
    state.setProperty("presetName", getCurrentPresetName(), nullptr);
    state.setProperty("program", currentProgramIndex.load(std::memory_order_relaxed), nullptr);
    return state;
}

juce::Result IvoryDriveAudioProcessor::restoreState(const juce::ValueTree& state)
{
    if (!state.isValid() || !state.hasType(stateType)) return juce::Result::fail("This file is not an Ivory Drive preset.");
    if (static_cast<int>(state.getProperty("version", 0)) != stateVersion) return juce::Result::fail("Unsupported Ivory Drive state version.");
    if (state.getNumChildren() != parameterCount) return juce::Result::fail("The preset parameter set is incomplete.");
    std::array<bool, parameterCount> seen {};
    for (const auto child : state)
    {
        if (!child.hasType("PARAM") || !child.hasProperty("id") || !child.hasProperty("value"))
            return juce::Result::fail("Invalid preset parameter data.");
        const auto id = child["id"].toString();
        const auto found = std::find_if(parameterIDs.begin(), parameterIDs.end(), [&id](const char* candidate) { return id == candidate; });
        if (found == parameterIDs.end()) return juce::Result::fail("The preset contains an unknown parameter.");
        const auto index = static_cast<std::size_t>(std::distance(parameterIDs.begin(), found));
        if (seen[index]) return juce::Result::fail("The preset contains duplicate parameters.");
        seen[index] = true;
        const auto raw = child["value"];
        if (!raw.isDouble() && !raw.isInt() && !raw.isInt64() && !raw.isString()) return juce::Result::fail("Invalid preset parameter value.");
        double number = 0;
        if (raw.isString())
        {
            const auto text = raw.toString().trim();
            const auto* first = text.toRawUTF8();
            const auto* last = first + text.getNumBytesAsUTF8();
            const auto parsed = std::from_chars(first, last, number);
            if (parsed.ec != std::errc {} || parsed.ptr != last)
                return juce::Result::fail("Invalid numeric preset parameter value.");
        }
        else number = static_cast<double>(raw);
        auto* parameter = parameters.getParameter(*found);
        const auto& range = parameter->getNormalisableRange();
        if (!std::isfinite(number) || number < range.start - 0.001 || number > range.end + 0.001)
            return juce::Result::fail("A preset parameter is outside its supported range.");
    }
    const auto program = static_cast<int>(state.getProperty("program", -1));
    if (program < -1 || program >= getNumPrograms()) return juce::Result::fail("Invalid preset program index.");
    parameters.replaceState(state.createCopy());
    {
        const juce::ScopedLock lock(presetNameLock);
        userPresetName = state.getProperty("presetName", "User Preset").toString().substring(0, 160);
    }
    currentProgramIndex.store(program, std::memory_order_relaxed);
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
    return juce::Result::ok();
}

void IvoryDriveAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = createStateSnapshot().createXml()) copyXmlToBinary(*xml, destination);
}

void IvoryDriveAudioProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0 || size > 1024 * 1024) return;
    if (auto xml = getXmlFromBinary(data, size)) (void) restoreState(juce::ValueTree::fromXml(*xml));
}

juce::Result IvoryDriveAudioProcessor::saveUserPreset(const juce::File& file)
{
    if (file == juce::File {}) return juce::Result::fail("Choose a preset file.");
    auto state = createStateSnapshot();
    const auto name = file.getFileNameWithoutExtension().substring(0, 160);
    state.setProperty("presetName", name, nullptr);
    state.setProperty("program", -1, nullptr);
    const auto xml = state.createXml();
    if (xml == nullptr) return juce::Result::fail("Could not encode the preset.");
    const auto parentResult = file.getParentDirectory().createDirectory();
    if (parentResult.failed()) return parentResult;
    juce::TemporaryFile temporary(file);
    if (!xml->writeTo(temporary.getFile())) return juce::Result::fail("Could not write the preset file.");
    if (!temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail("Could not replace the preset file.");
    {
        const juce::ScopedLock lock(presetNameLock);
        userPresetName = name;
    }
    currentProgramIndex.store(-1, std::memory_order_relaxed);
    return juce::Result::ok();
}

juce::Result IvoryDriveAudioProcessor::loadUserPreset(const juce::File& file)
{
    if (!file.existsAsFile()) return juce::Result::fail("The preset file does not exist.");
    if (file.getSize() <= 0 || file.getSize() > 1024 * 1024) return juce::Result::fail("The preset file has an invalid size.");
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return juce::Result::fail("The preset file is not valid XML.");
    auto state = juce::ValueTree::fromXml(*xml);
    state.setProperty("program", -1, nullptr);
    state.setProperty("presetName", file.getFileNameWithoutExtension().substring(0, 160), nullptr);
    return restoreState(state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new IvoryDriveAudioProcessor(); }
