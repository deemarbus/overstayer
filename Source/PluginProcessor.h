#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>

class IvoryDriveAudioProcessor final : public juce::AudioProcessor
{
public:
    IvoryDriveAudioProcessor();
    ~IvoryDriveAudioProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ivory Drive"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    juce::String getCurrentPresetName() const;
    juce::Result saveUserPreset(const juce::File&);
    juce::Result loadUserPreset(const juce::File&);

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    float getInputPeak(int channel) const noexcept;
    float getOutputPeak(int channel) const noexcept;
    float getSaturationActivity() const noexcept { return saturationActivity.load(std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState parameters;

private:
    struct ProcessingState;
    std::unique_ptr<ProcessingState> processing;
    std::array<std::atomic<float>, 2> inputPeaks {}, outputPeaks {};
    std::atomic<float> saturationActivity { 0.0f };
    std::atomic<int> currentProgramIndex { 0 };
    mutable juce::CriticalSection presetNameLock;
    juce::String userPresetName;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::ValueTree createStateSnapshot();
    juce::Result restoreState(const juce::ValueTree&);
    template <typename Sample> void process(juce::AudioBuffer<Sample>&, bool hostBypassed);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IvoryDriveAudioProcessor)
};
