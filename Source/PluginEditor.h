#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "UI/RackLookAndFeel.h"
#include <array>
#include <memory>
#include <vector>

class IvoryDriveAudioProcessor;

/** Original vector rack artwork. Every control belongs to the shared APVTS. */
class IvoryDriveAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit IvoryDriveAudioProcessorEditor (IvoryDriveAudioProcessor&);
    ~IvoryDriveAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Surface;
    class Knob;
    class LevelMeter;
    class ActivityMeter;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    Knob& addKnob (const juce::String& id, const juce::String& name,
                   const juce::String& tooltip, const juce::String& unit);
    juce::ToggleButton& addSwitch (const juce::String& id, const juce::String& name,
                                   const juce::String& tooltip);
    juce::ComboBox& addChoice (const juce::String& id, const juce::StringArray& choices,
                              const juce::String& tooltip);
    Knob* knob (const juce::String& id) const;
    juce::ToggleButton* switchFor (const juce::String& id) const;
    juce::ComboBox* choice (const juce::String& id) const;
    float value (const char* id) const;
    void timerCallback() override;
    void updateMode (bool extended);
    void paintRack (juce::Graphics&);
    void choosePresetFile (bool save);
    void refreshPresetName();

    IvoryDriveAudioProcessor& processor;
    ivory::ui::RackLookAndFeel lookAndFeel;
    std::unique_ptr<Surface> surface;
    juce::TooltipWindow tooltips { this, 550 };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<juce::ToggleButton>> switches;
    std::vector<std::unique_ptr<juce::ComboBox>> choices;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::array<juce::TextButton, 6> modeButtons;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    juce::ComboBox presetMenu;
    juce::TextButton saveButton { "SAVE" }, loadButton { "LOAD" };
    std::unique_ptr<LevelMeter> inputMeter, outputMeter;
    std::unique_ptr<ActivityMeter> activityMeter;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::String presetName;
    bool isExtended = false;
    static constexpr int designWidth = 1040;
    static constexpr int classicHeight = 520;
    static constexpr int extendedHeight = 926;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IvoryDriveAudioProcessorEditor)
};
