#include "PluginProcessor.h"
#include "DSP/SaturationEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
int checks = 0;
void require(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void parameter(IvoryDriveAudioProcessor& processor, const char* id, float value)
{
    auto* p = processor.parameters.getParameter(id);
    require(p != nullptr, std::string("Missing parameter: ") + id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

template <typename Sample>
void prepare(IvoryDriveAudioProcessor& processor, double sampleRate, int channels = 2, int block = 128)
{
    processor.setPlayConfigDetails(channels, channels, sampleRate, block);
    processor.setProcessingPrecision(std::is_same_v<Sample, double>
        ? juce::AudioProcessor::doublePrecision : juce::AudioProcessor::singlePrecision);
    processor.prepareToPlay(sampleRate, block);
}

template <typename Sample>
std::vector<double> render(IvoryDriveAudioProcessor& processor, const std::vector<double>& input,
                           int channels = 2, int blockSize = 257, bool bypassed = false)
{
    std::vector<double> result(input.size());
    juce::MidiBuffer midi;
    for (std::size_t start = 0; start < input.size(); start += static_cast<std::size_t>(blockSize))
    {
        const auto count = static_cast<int>(std::min<std::size_t>(blockSize, input.size() - start));
        juce::AudioBuffer<Sample> block(channels, count);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < count; ++i)
                block.setSample(ch, i, static_cast<Sample>(input[start + static_cast<std::size_t>(i)]));
        if (bypassed) processor.processBlockBypassed(block, midi);
        else processor.processBlock(block, midi);
        for (int i = 0; i < count; ++i)
        {
            const auto value = static_cast<double>(block.getSample(0, i));
            require(std::isfinite(value), "Non-finite processor output");
            result[start + static_cast<std::size_t>(i)] = value;
            if (channels == 2)
                require(std::abs(value - static_cast<double>(block.getSample(1, i))) < 1.0e-6,
                        "Identical stereo inputs diverged");
        }
    }
    return result;
}

std::vector<double> sine(double sr, double hz, std::size_t count, double amplitude = 0.2)
{
    std::vector<double> values(count);
    for (std::size_t i = 0; i < count; ++i)
        values[i] = amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sr);
    return values;
}

template <typename Sample>
void latencyChecks()
{
    int fixedLatency = -1;
    for (int os = 0; os < 3; ++os)
    {
        for (int mode = 0; mode < 3; ++mode)
        {
            IvoryDriveAudioProcessor processor;
            parameter(processor, "oversampling", static_cast<float>(os));
            parameter(processor, "mix", mode == 0 ? 0.0f : 65.0f);
            parameter(processor, "bypass", mode == 1 ? 1.0f : 0.0f);
            prepare<Sample>(processor, 48000.0);
            const auto latency = processor.getLatencySamples();
            require(latency > 0 && latency < 512, "Invalid oversampling latency");
            if (fixedLatency < 0) fixedLatency = latency;
            require(latency == fixedLatency, "Latency changes across quality settings");
            render<Sample>(processor, std::vector<double>(4800, 0.0), 2, 127, mode == 2);
            auto input = sine(48000.0, 733.0, 5000, 0.15);
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] += 0.013 * std::sin(0.371 * static_cast<double>(i));
            const auto output = render<Sample>(processor, input, 2, 4097, mode == 2);
            double maximumError = 0.0;
            for (std::size_t i = static_cast<std::size_t>(latency); i < output.size(); ++i)
                maximumError = std::max(maximumError, std::abs(output[i] - input[i - static_cast<std::size_t>(latency)]));
            require(maximumError < (std::is_same_v<Sample, float> ? 2.0e-6 : 1.0e-10),
                    "Dry/parameter-bypass/host-bypass latency mismatch: " + std::to_string(maximumError));
        }
    }
    std::cout << "Latency: " << fixedLatency << " samples (all qualities, dry and both bypass paths)\n";
}

void stateAndPresets()
{
    IvoryDriveAudioProcessor processor;
    require(processor.getNumPrograms() == 12, "Factory preset count must be 12");
    std::vector<double> signatures;
    for (int i = 0; i < processor.getNumPrograms(); ++i)
    {
        processor.setCurrentProgram(i);
        require(processor.getCurrentProgram() == i, "Factory program index not recalled");
        require(processor.getProgramName(i).isNotEmpty(), "Unnamed factory preset");
        prepare<float>(processor, 48000.0);
        const auto output = render<float>(processor, sine(48000.0, 503.0, 12000, 0.4));
        double energy = 0.0;
        for (std::size_t j = 6000; j < output.size(); ++j) energy += output[j] * output[j];
        require(energy > 1.0e-8, "Factory preset is silent");
        signatures.push_back(energy);
        processor.releaseResources();
    }
    const auto [smallest, largest] = std::minmax_element(signatures.begin(), signatures.end());
    require(*largest - *smallest > 0.01, "Factory presets have no different processing");

    parameter(processor, "extended", 1.0f);
    parameter(processor, "p1Freq", 17.0f);
    parameter(processor, "p1Pre", 7.0f);
    parameter(processor, "p1Comp", 0.0f);
    parameter(processor, "p1Post", -3.0f);
    parameter(processor, "oversampling", 2.0f);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    require(state.getSize() > 100, "Empty host state");
    IvoryDriveAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (auto* base : processor.getParameters())
    {
        auto* identified = dynamic_cast<juce::AudioProcessorParameterWithID*>(base);
        require(identified != nullptr, "Unidentified automation parameter");
        const auto* other = restored.parameters.getParameter(identified->paramID);
        require(other != nullptr && std::abs(base->getValue() - other->getValue()) < 1.0e-6f,
                "Host state parameter mismatch: " + identified->paramID.toStdString());
    }
    require(restored.getCurrentPresetName() == processor.getCurrentPresetName(), "Preset name not restored");
    const auto path = juce::File::getCurrentWorkingDirectory().getChildFile("ivory-test-user-preset.xml");
    require(!path.existsAsFile(), "Refusing to overwrite existing user-preset test file");
    require(processor.saveUserPreset(path).wasOk(), "User preset save failed");
    require(restored.loadUserPreset(path).wasOk(), "User preset load failed");
    require(path.deleteFile(), "User preset test cleanup failed");
    const auto before = restored.parameters.getRawParameterValue("p1Pre")->load();
    const char garbage[] = "not a plugin state";
    restored.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
    require(restored.parameters.getRawParameterValue("p1Pre")->load() == before,
            "Malformed host state corrupted parameters");
}

void formatAndAutomationChecks()
{
    for (double sampleRate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (int channels : {1, 2})
        {
            IvoryDriveAudioProcessor processor;
            prepare<double>(processor, sampleRate, channels, 64);
            require(processor.supportsDoublePrecisionProcessing(), "Double processing unsupported");
            parameter(processor, "input", 30.0f);
            parameter(processor, "dual", 1.0f);
            parameter(processor, "second", 1.0f);
            auto output = render<double>(processor, sine(sampleRate, 997.0, 7000, 0.7), channels, 1);
            require(processor.getSaturationActivity() > 0.05f, "Saturation activity does not follow nonlinear processing");
            require(processor.getInputPeak(0) > 0.1f && processor.getOutputPeak(0) > 0.0f,
                    "Input/output metering inactive");
            for (int quality : {2, 0, 1})
            {
                parameter(processor, "oversampling", static_cast<float>(quality));
                parameter(processor, "classicMode", static_cast<float>(quality * 2));
                render<double>(processor, sine(sampleRate, 311.0, 1000, 0.3), channels, 7);
            }
            render<double>(processor, std::vector<double>(1000, 0.0), channels, 1025);
        }
}

// Measure the folded third harmonic against an unoversampled render of the
// same circuit. This exercises the actual wrapper filters. It is one defined
// stress signal, not a universal alias-rejection guarantee.
void oversamplingEvidence()
{
    std::ofstream csv("plugin_oversampling.csv");
    csv << "quality,alias_3rd_dbfs,fundamental_dbfs\n";
    constexpr double sr = 48000.0, fundamental = 10000.0, alias = 18000.0;
    const auto amplitudeAt = [](const std::vector<double>& signal, double frequency)
    {
        double re = 0.0, im = 0.0;
        constexpr std::size_t start = 4800, length = 4800;
        for (std::size_t n = 0; n < length; ++n)
        {
            const auto angle = 2.0 * std::numbers::pi * frequency * static_cast<double>(n) / sr;
            re += signal[start + n] * std::cos(angle);
            im -= signal[start + n] * std::sin(angle);
        }
        return 2.0 * std::hypot(re, im) / static_cast<double>(length);
    };
    ivory::SaturationEngine<double> rawEngine;
    ivory::Parameters rawParameters;
    rawParameters.inputDb = 18;
    rawParameters.autoGain = false;
    rawParameters.emphasis = false;
    rawEngine.prepare(sr, 1);
    rawEngine.setParameters(rawParameters);
    rawEngine.reset();
    auto unoversampled = sine(sr, fundamental, 14400, 0.6);
    for (auto& value : unoversampled) value = rawEngine.processSample(value, 0);
    const auto baselineAlias = amplitudeAt(unoversampled, alias);
    const auto toDb = [](double a) { return 20.0 * std::log10(std::max(1.0e-15, a)); };
    csv << "1," << toDb(baselineAlias) << ',' << toDb(amplitudeAt(unoversampled, fundamental)) << '\n';
    std::array<double, 3> aliases {};
    for (int quality = 0; quality < 3; ++quality)
    {
        IvoryDriveAudioProcessor processor;
        parameter(processor, "oversampling", static_cast<float>(quality));
        parameter(processor, "input", 18.0f);
        parameter(processor, "autoGain", 0.0f);
        parameter(processor, "emphasis", 0.0f);
        prepare<double>(processor, sr);
        auto output = render<double>(processor, sine(sr, fundamental, 14400, 0.6), 2, 256);
        aliases[static_cast<std::size_t>(quality)] = amplitudeAt(output, alias);
        csv << (2 << quality) << ',' << toDb(aliases[static_cast<std::size_t>(quality)])
            << ',' << toDb(amplitudeAt(output, fundamental)) << '\n';
        require(toDb(aliases[static_cast<std::size_t>(quality)]) < -45.0,
                "Oversampled third harmonic alias exceeds -45 dBFS");
        require(toDb(baselineAlias) - toDb(aliases[static_cast<std::size_t>(quality)]) > 20.0,
                "Oversampling improves the folded third harmonic by less than 20 dB");
    }
    require(aliases[2] < 0.01, "8x oversampling alias rejection regressed");
    require(csv.good(), "Could not write oversampling evidence");
}

void editorSmoke()
{
    IvoryDriveAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    require(editor != nullptr && editor->getWidth() >= 700 && editor->getHeight() >= 300,
            "Editor creation or size invalid");
    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics graphics(image);
    editor->paintEntireComponent(graphics, true);
    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("IvoryDrive-Classic.png");
    if (auto stream = file.createOutputStream())
    {
        require(stream->setPosition(0) && stream->truncate().wasOk(), "Classic screenshot truncate failed");
        require(juce::PNGImageFormat().writeImageToStream(image, *stream), "Classic screenshot encode failed");
    }
    else require(false, "Could not save editor screenshot");
    parameter(processor, "extended", 1.0f);
    // Timer-driven layout updates need message dispatch; the synchronous
    // processor state is still verified independently by state tests.
    juce::Thread::sleep(100);
    juce::Timer::callPendingTimersSynchronously();
    juce::Image extended(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics extendedGraphics(extended);
    editor->paintEntireComponent(extendedGraphics, true);
    if (auto stream = juce::File::getCurrentWorkingDirectory().getChildFile("IvoryDrive-Extended.png").createOutputStream())
    {
        require(stream->setPosition(0) && stream->truncate().wasOk(), "Extended screenshot truncate failed");
        require(juce::PNGImageFormat().writeImageToStream(extended, *stream), "Extended screenshot encode failed");
    }
    else require(false, "Could not save Extended editor screenshot");
    editor.reset();
}

void parallelPhaseAndPerformance()
{
    std::ofstream csv("plugin_phase_cpu.csv");
    csv << "quality,frequency_hz,wet_db,mix_db,wet_phase_error_deg,realtime_cpu_percent\n";
    for (int quality = 0; quality < 3; ++quality)
    {
        for (double hz : {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0})
        {
            std::array<double, 2> gain {}, phase {};
            for (int blend = 0; blend < 2; ++blend)
            {
                IvoryDriveAudioProcessor processor;
                parameter(processor, "oversampling", static_cast<float>(quality));
                parameter(processor, "mix", blend == 0 ? 100.0f : 50.0f);
                parameter(processor, "emphasis", 0.0f);
                prepare<double>(processor, 48000.0);
                const auto output = render<double>(processor, sine(48000.0, hz, 9600, 0.0001), 2, 256);
                double real = 0.0, imag = 0.0;
                for (std::size_t n = 4800; n < 9600; ++n)
                {
                    const auto a = 2.0 * std::numbers::pi * hz
                        * (static_cast<double>(n) - processor.getLatencySamples()) / 48000.0;
                    real += output[n] * std::sin(a);
                    imag += output[n] * std::cos(a);
                }
                gain[static_cast<std::size_t>(blend)] = 20.0 * std::log10(2.0 * std::hypot(real, imag) / 0.48);
                phase[static_cast<std::size_t>(blend)] = std::atan2(imag, real) * 180.0 / std::numbers::pi;
            }
            require(std::abs(gain[1] - gain[0]) < 0.5, "Parallel mixing introduces more than 0.5 dB comb loss");
            require(std::abs(phase[0]) < 12.0, "Wet path is not phase aligned to reported latency");
            csv << (2 << quality) << ',' << hz << ',' << gain[0] << ',' << gain[1] << ',' << phase[0] << ",\n";
        }
        IvoryDriveAudioProcessor processor;
        parameter(processor, "oversampling", static_cast<float>(quality));
        parameter(processor, "dual", 1.0f);
        parameter(processor, "second", 1.0f);
        parameter(processor, "input", 12.0f);
        prepare<float>(processor, 48000.0);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        // Preallocation and fixture creation are outside the measured region.
        const auto begin = std::chrono::steady_clock::now();
        for (int block = 0; block < 188; ++block)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 256; ++n)
                    buffer.setSample(ch, n, 0.25f * std::sin(static_cast<float>((block * 256 + n) * 0.130506)));
            processor.processBlock(buffer, midi);
        }
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        const auto percentage = seconds / (188.0 * 256.0 / 48000.0) * 100.0;
        require(std::isfinite(percentage), "Invalid CPU benchmark");
        csv << (2 << quality) << ",0,,,," << percentage << '\n';
        std::cout << (2 << quality) << "x full stereo float wrapper: " << percentage << "% of one cloud CPU (wall clock)\n";
    }
    require(csv.good(), "Could not save phase/CPU evidence");
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        latencyChecks<float>();
        latencyChecks<double>();
        stateAndPresets();
        formatAndAutomationChecks();
        oversamplingEvidence();
        parallelPhaseAndPerformance();
        editorSmoke();
        std::cout << "PASS: " << checks << " processor assertions; float/double, mono/stereo, 5 rates, 12 presets, state, bypass, latency, oversampling, editor.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL after " << checks << " assertions: " << error.what() << '\n';
        return 1;
    }
}
