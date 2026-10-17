#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "DSP/Parameters.h"
#include <cmath>
#include <functional>

namespace
{
using namespace ivory::ui;
juce::Font rackFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

void engraving (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> bounds,
                float size, juce::Justification justification = juce::Justification::centred,
                bool bold = false)
{
    g.setFont (rackFont (size, bold));
    g.setColour (juce::Colours::white.withAlpha (0.48f));
    g.drawFittedText (text, bounds.translated (0, 1), justification, 1);
    g.setColour (palette::ink());
    g.drawFittedText (text, bounds, justification, 1);
}

void screw (juce::Graphics& g, float x, float y)
{
    const juce::Rectangle<float> area (x - 7.0f, y - 7.0f, 14.0f, 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.20f));
    g.fillEllipse (area.expanded (1.6f).translated (0.6f, 1.4f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff686960), x - 5, y - 6,
                                           juce::Colour (0xff242720), x + 4, y + 6, false));
    g.fillEllipse (area);
    g.setColour (juce::Colour (0xff98998a));
    g.drawEllipse (area.reduced (0.6f), 0.7f);
    g.setColour (juce::Colour (0xff13160f));
    g.drawLine (x - 3.4f, y + 3.0f, x + 3.4f, y - 3.0f, 2.1f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (x - 2.8f, y + 3.9f, x + 4.0f, y - 2.1f, 0.8f);
}

juce::String frequencyText (double hz)
{
    return hz >= 1000.0 ? juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1) + " kHz"
                        : juce::String (hz, 0) + " Hz";
}
}

class IvoryDriveAudioProcessorEditor::Surface final : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> onPaint;
    void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
};

class IvoryDriveAudioProcessorEditor::Knob final : public juce::Component
{
public:
    Knob (const juce::String& name, const juce::String& tip)
    {
        setName (name);
        slider.setName (name);
        slider.setDescription (tip);
        title.setText (name, juce::dontSendNotification);
        title.setFont (rackFont (12.0f, true));
        title.setJustificationType (juce::Justification::centred);
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.24f,
                                    juce::MathConstants<float>::pi * 2.76f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 19);
        slider.setMouseDragSensitivity (220);
        slider.setVelocityModeParameters (0.30, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
        slider.setScrollWheelEnabled (true);
        slider.setTooltip (tip + "\nDrag to adjust; Shift-drag for fine control. Double-click resets. Mouse wheel supported.");
        addAndMakeVisible (slider);
        complement.setJustificationType (juce::Justification::centred);
        complement.setFont (rackFont (13.0f, true));
        complement.setColour (juce::Label::textColourId, palette::ink());
        complement.setInterceptsMouseClicks (false, false);
        addChildComponent (complement);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (18));
        slider.setBounds (area);
        complement.setBounds (area.reduced (3, 10));
    }

    void setComplement (bool active, float preDb)
    {
        if (compensated != active)
        {
            compensated = active;
            slider.setVisible (! active);
            complement.setVisible (active);
            repaint();
        }
        if (active)
            complement.setText ("AUTO\n" + juce::String (std::abs (preDb) < 0.05f ? 0.0f : -preDb, 1) + " dB",
                                juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        if (! compensated) return;
        const float diameter = juce::jmin (static_cast<float> (getWidth() - 18),
                                          static_cast<float> (getHeight() - 34));
        const auto ring = juce::Rectangle<float> (diameter, diameter)
                              .withCentre ({ getWidth() * 0.5f, getHeight() * 0.5f + 4.0f });
        g.setColour (palette::ink().withAlpha (0.10f));
        g.fillEllipse (ring);
        g.setColour (palette::ink().withAlpha (0.24f));
        g.drawEllipse (ring, 1.0f);
    }

    juce::Slider slider;
private:
    juce::Label title, complement;
    bool compensated = false;
};

class IvoryDriveAudioProcessorEditor::LevelMeter final : public juce::Component,
                                                         public juce::SettableTooltipClient
{
public:
    LevelMeter() { setTooltip ("Stereo peak level in dBFS. Amber: above -6 dBFS. Red: clipping. Short peak hold."); }
    void update (float left, float right)
    {
        const std::array<float, 2> inputs { left, right };
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const float db = juce::Decibels::gainToDecibels (juce::jmax (0.0f, inputs[ch]), -72.0f);
            levels[ch] = juce::jmax (db, levels[ch] - 1.1f);
            if (levels[ch] >= holds[ch]) { holds[ch] = levels[ch]; holdFrames[ch] = 36; }
            else if (--holdFrames[ch] <= 0) holds[ch] = juce::jmax (levels[ch], holds[ch] - 0.65f);
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff111810));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (0.4f), 2.0f, 1.0f);
        g.setFont (rackFont (8.0f, true));
        g.setColour (juce::Colour (0xffa1ab91));
        g.drawText ("L  R", getLocalBounds().removeFromTop (14), juce::Justification::centred);
        constexpr int segments = 22;
        const float segmentHeight = (getHeight() - 35.0f) / segments;
        const float columnWidth = (getWidth() - 13.0f) * 0.5f;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (int segment = 0; segment < segments; ++segment)
            {
                const float threshold = -60.0f + segment * (60.0f / (segments - 1));
                const auto colour = threshold > -1.0f ? juce::Colour (0xffe35431)
                                  : threshold > -6.0f ? juce::Colour (0xffddbc4c)
                                                      : juce::Colour (0xff4cef67);
                const auto led = juce::Rectangle<float> (5.0f + static_cast<float> (ch) * (columnWidth + 3.0f),
                    15.0f + static_cast<float> (segments - 1 - segment) * segmentHeight,
                    columnWidth, juce::jmax (2.0f, segmentHeight - 2.0f));
                const bool lit = levels[ch] >= threshold;
                const bool hold = holds[ch] >= threshold && holds[ch] < threshold + 60.0f / (segments - 1);
                g.setColour (lit || hold ? colour : colour.withAlpha (0.12f));
                g.fillRoundedRectangle (led, 0.8f);
                if (lit)
                {
                    g.setColour (colour.withAlpha (0.08f));
                    g.fillRoundedRectangle (led.expanded (1.2f), 1.2f);
                }
            }
        }
        const auto maxLevel = juce::jmax (levels[0], levels[1]);
        g.setColour (maxLevel >= 0.0f ? juce::Colour (0xffed7950) : juce::Colour (0xffb8c5aa));
        g.setFont (rackFont (9.0f));
        g.drawText (maxLevel < -60.0f ? "-inf" : juce::String (maxLevel, 0),
                    getLocalBounds().removeFromBottom (17), juce::Justification::centred);
    }
private:
    std::array<float, 2> levels { -72.0f, -72.0f }, holds { -72.0f, -72.0f };
    std::array<int, 2> holdFrames { 0, 0 };
};

class IvoryDriveAudioProcessorEditor::ActivityMeter final : public juce::Component,
                                                            public juce::SettableTooltipClient
{
public:
    ActivityMeter() { setTooltip ("Actual nonlinear peak bending, measured inside the saturation stages. More LEDs indicate stronger saturation."); }
    void update (float newActivity)
    {
        const float target = std::sqrt (juce::jlimit (0.0f, 1.0f, newActivity));
        activity = target > activity ? target : activity * 0.90f;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        constexpr int segments = 40;
        const float step = getWidth() / static_cast<float> (segments);
        for (int i = 0; i < segments; ++i)
        {
            const float x = (static_cast<float> (i) + 0.5f) * step;
            const bool lit = activity > (static_cast<float> (i) + 0.25f) / segments;
            g.setColour (juce::Colour (0xff515746));
            g.fillEllipse (x - 3.9f, 3.0f, 7.8f, 7.8f);
            if (lit)
            {
                g.setColour (palette::accent().withAlpha (0.15f));
                g.fillEllipse (x - 6.1f, 0.8f, 12.2f, 12.2f);
                g.setColour (palette::accent());
                g.fillEllipse (x - 2.7f, 4.1f, 5.4f, 5.4f);
                g.setColour (juce::Colour (0xffd4ffbd).withAlpha (0.8f));
                g.fillEllipse (x - 1.5f, 4.5f, 2.0f, 1.4f);
            }
            else
            {
                g.setColour (juce::Colour (0xff263522));
                g.fillEllipse (x - 2.7f, 4.1f, 5.4f, 5.4f);
            }
        }
    }
private:
    float activity = 0.0f;
};

IvoryDriveAudioProcessorEditor::IvoryDriveAudioProcessorEditor (IvoryDriveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), surface (std::make_unique<Surface>()),
      inputMeter (std::make_unique<LevelMeter>()), outputMeter (std::make_unique<LevelMeter>()),
      activityMeter (std::make_unique<ActivityMeter>())
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    addAndMakeVisible (*surface);
    surface->onPaint = [this] (juce::Graphics& g) { paintRack (g); };
    surface->addAndMakeVisible (*inputMeter);
    surface->addAndMakeVisible (*outputMeter);
    surface->addAndMakeVisible (*activityMeter);

    addKnob ("input", "INPUT", "Input drive into the nonlinear stages, in dB.", "dB");
    addKnob ("mix", "MIX", "Dry/wet balance. 0% is dry; 100% is processed. Both paths are latency aligned.", "%");
    addKnob ("output", "OUTPUT", "Final output gain, in dB.", "dB");
    addSwitch ("autoGain", "AUTO GAIN", "Compensates input drive at the output while retaining saturation.");
    addSwitch ("toDry", "TO DRY", "Apply the input drive gain to the dry signal as well.");
    addSwitch ("dual", "DUAL", "Engage a second interacting nonlinear saturation stage in series.");
    addSwitch ("second", "2ND", "Shift the nonlinear response toward a stronger second harmonic.");
    addSwitch ("lowCut", "LOW CUT", "Enable a resonant 12 dB/octave high-pass near 50 Hz before saturation. Available in both views, independently of EMPHASIS.");
    addSwitch ("lowShelf", "LOW SHELF", "Add approximately 2 dB at 100 Hz before saturation. Available in both views, independently of EMPHASIS.");
    addSwitch ("highShelf", "HIGH SHELF", "Add approximately 2 dB at 2 kHz before saturation. Available in both views, independently of EMPHASIS.");
    addSwitch ("emphasis", "EMPHASIS", "Enable the selected Classic pre/post emphasis or the Extended emphasis EQ.");
    addSwitch ("extended", "CLASSIC / EXTENDED", "Off: Classic six-position emphasis. On: independently configurable Extended emphasis EQ and resonant filters.");
    addSwitch ("bypass", "BYPASS", "Bypass processing, retaining the reported latency for dry-path alignment.");
    addChoice ("oversampling", { "2x", "4x", "8x" }, "Oversampling quality. Higher settings reduce aliasing and use more CPU. Latency updates with this setting.");

    addSwitch ("hpOn", "HP ON", "Enable the Extended resonant 12 dB/octave high-pass filter.");
    addKnob ("hpFreq", "FREQUENCY", "High-pass cutoff, 20 Hz to 2 kHz.", "Hz");
    addKnob ("hpRes", "RESONANCE", "High-pass resonance. 0% is neutral; 100% gives approximately 3 dB of resonant boost.", "res");
    addSwitch ("lpOn", "LP ON", "Enable the Extended resonant 12 dB/octave low-pass filter.");
    addKnob ("lpFreq", "FREQUENCY", "Low-pass cutoff, 200 Hz to 20 kHz.", "Hz");
    addKnob ("lpRes", "RESONANCE", "Low-pass resonance. 0% is neutral; 100% gives approximately 3 dB of resonant boost.", "res");

    const std::array<juce::String, 4> bandIds { "bass", "p1", "p2", "treble" };
    for (const auto& band : bandIds)
    {
        addKnob (band + "Pre", "PRE GAIN", "EQ gain before saturation, in dB. Changes the frequencies driven into the nonlinear stages.", "dB");
        addKnob (band + "Post", "POST GAIN", "Independent EQ gain after saturation, in dB. COMP replaces this with the complementary pre-gain curve.", "dB");
        addSwitch (band + "Comp", "COMP", "Automatically apply the inverse pre-EQ curve after saturation. The independent POST GAIN value is preserved and restored when COMP is off.");
    }
    juce::StringArray frequencies;
    for (const double hz : ivory::presenceFrequencies) frequencies.add (frequencyText (hz));
    addChoice ("p1Freq", frequencies, "Presence 1 centre frequency: 21 logarithmic positions, 50 Hz to 20 kHz.");
    addChoice ("p2Freq", frequencies, "Presence 2 centre frequency: 21 logarithmic positions, 50 Hz to 20 kHz.");

    const std::array<juce::String, 6> modes { "CLASSIC", "LOW BANDWIDTH", "LOW PRESENCE", "HIGH PRESENCE", "BASS DISTORTION", "TREBLE DISTORTION" };
    const std::array<juce::String, 6> descriptions {
        "Midrange and upper-frequency saturation with a comparatively preserved fundamental.",
        "Stronger low- and high-frequency coloration with comparatively preserved mids.",
        "Upper-midrange presence saturation.",
        "Upper-presence harmonics and controlled brightness.",
        "Transformer-inspired low-frequency coloration with comparatively preserved highs.",
        "Tape-inspired upper-frequency saturation and transient rounding." };
    for (size_t i = 0; i < modeButtons.size(); ++i)
    {
        auto& button = modeButtons[i];
        button.setButtonText (modes[i]);
        button.setTooltip (descriptions[i] + " Classic mode only; the selection is retained in Extended mode.");
        button.onClick = [this, i] { if (modeAttachment) modeAttachment->setValueAsCompleteGesture (static_cast<float> (i)); };
        surface->addAndMakeVisible (button);
    }
    if (auto* parameter = processor.parameters.getParameter ("classicMode"))
    {
        modeAttachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float index)
        {
            const auto selected = juce::roundToInt (index);
            for (size_t i = 0; i < modeButtons.size(); ++i)
                modeButtons[i].setToggleState (selected == static_cast<int> (i), juce::dontSendNotification);
        }, nullptr);
        modeAttachment->sendInitialUpdate();
    }

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetMenu.addItem (processor.getProgramName (i), i + 1);
    presetMenu.setTooltip ("Factory and current user preset. All controls and the Classic/Extended selection are recalled.");
    presetMenu.onChange = [this]
    {
        const auto selected = presetMenu.getSelectedId();
        if (selected >= 1 && selected <= processor.getNumPrograms()) processor.setCurrentProgram (selected - 1);
        refreshPresetName();
    };
    surface->addAndMakeVisible (presetMenu);
    surface->addAndMakeVisible (saveButton);
    surface->addAndMakeVisible (loadButton);
    saveButton.setTooltip ("Save every parameter and the current interface mode as an Ivory Drive user preset.");
    loadButton.setTooltip ("Load an Ivory Drive user preset.");
    saveButton.onClick = [this] { choosePresetFile (true); };
    loadButton.onClick = [this] { choosePresetFile (false); };
    refreshPresetName();
    setResizable (true, true);
    setResizeLimits (800, 400, 1664, 1482);
    isExtended = value ("extended") >= 0.5f;
    if (auto* sizeConstrainer = getConstrainer())
        sizeConstrainer->setFixedAspectRatio (static_cast<double> (designWidth) / (isExtended ? extendedHeight : classicHeight));
    setSize (designWidth, isExtended ? extendedHeight : classicHeight);
    updateMode (isExtended);
    startTimerHz (40);
}

IvoryDriveAudioProcessorEditor::~IvoryDriveAudioProcessorEditor()
{
    stopTimer();
    fileChooser.reset();
    modeAttachment.reset();
    comboAttachments.clear();
    buttonAttachments.clear();
    sliderAttachments.clear();
    setLookAndFeel (nullptr);
}

IvoryDriveAudioProcessorEditor::Knob& IvoryDriveAudioProcessorEditor::addKnob (
    const juce::String& id, const juce::String& name, const juce::String& tooltip, const juce::String& unit)
{
    auto control = std::make_unique<Knob> (name, tooltip);
    control->setComponentID (id);
    auto& result = *control;
    surface->addAndMakeVisible (result);
    // Slider caches its value Label when first constructed, before this knob
    // joins the rack. Bind explicitly so it recreates that Label with rack ink.
    result.slider.setLookAndFeel (&lookAndFeel);
    if (auto* parameter = processor.parameters.getParameter (id))
    {
        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.parameters, id, result.slider));
        result.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }
    result.slider.textFromValueFunction = [unit] (double v)
    {
        if (unit == "Hz") return frequencyText (v);
        if (unit == "res") return juce::String (v * 100.0, 0) + "%";
        if (unit == "%") return juce::String (v, 0) + "%";
        return juce::String (std::abs (v) < 0.05 ? 0.0 : v, 1) + " dB";
    };
    result.slider.valueFromTextFunction = [unit] (const juce::String& text)
    {
        auto number = text.retainCharacters ("-+.0123456789").getDoubleValue();
        if (unit == "Hz" && text.containsIgnoreCase ("k")) number *= 1000.0;
        if (unit == "res") number *= 0.01;
        return number;
    };
    result.slider.updateText();
    knobs.push_back (std::move (control));
    return result;
}

juce::ToggleButton& IvoryDriveAudioProcessorEditor::addSwitch (
    const juce::String& id, const juce::String& name, const juce::String& tooltip)
{
    auto control = std::make_unique<juce::ToggleButton> (name);
    control->setComponentID (id);
    control->setTooltip (tooltip);
    auto& result = *control;
    surface->addAndMakeVisible (result);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.parameters, id, result));
    switches.push_back (std::move (control));
    return result;
}

juce::ComboBox& IvoryDriveAudioProcessorEditor::addChoice (
    const juce::String& id, const juce::StringArray& items, const juce::String& tooltip)
{
    auto control = std::make_unique<juce::ComboBox>();
    control->setComponentID (id);
    control->setTooltip (tooltip);
    control->addItemList (items, 1);
    auto& result = *control;
    surface->addAndMakeVisible (result);
    comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.parameters, id, result));
    choices.push_back (std::move (control));
    return result;
}

IvoryDriveAudioProcessorEditor::Knob* IvoryDriveAudioProcessorEditor::knob (const juce::String& id) const
{
    for (const auto& control : knobs) if (control->getComponentID() == id) return control.get();
    return nullptr;
}
juce::ToggleButton* IvoryDriveAudioProcessorEditor::switchFor (const juce::String& id) const
{
    for (const auto& control : switches) if (control->getComponentID() == id) return control.get();
    return nullptr;
}
juce::ComboBox* IvoryDriveAudioProcessorEditor::choice (const juce::String& id) const
{
    for (const auto& control : choices) if (control->getComponentID() == id) return control.get();
    return nullptr;
}
float IvoryDriveAudioProcessorEditor::value (const char* id) const
{
    if (const auto* parameter = processor.parameters.getRawParameterValue (id)) return parameter->load();
    return 0.0f;
}

void IvoryDriveAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff292b25)); }

void IvoryDriveAudioProcessorEditor::resized()
{
    const int logicalHeight = isExtended ? extendedHeight : classicHeight;
    const float scale = juce::jmin (getWidth() / static_cast<float> (designWidth),
                                    getHeight() / static_cast<float> (logicalHeight));
    surface->setTransform (juce::AffineTransform());
    surface->setBounds (0, 0, designWidth, logicalHeight);
    surface->setTransform (juce::AffineTransform::scale (scale));
    presetMenu.setBounds (524, 22, 294, 26);
    saveButton.setBounds (829, 22, 72, 26);
    loadButton.setBounds (910, 22, 72, 26);
    switchFor ("extended")->setBounds (292, 39, 205, 31);
    choice ("oversampling")->setBounds (785, 57, 75, 25);
    switchFor ("bypass")->setBounds (884, 55, 101, 28);
    knob ("input")->setBounds (111, 108, 194, 177);
    knob ("mix")->setBounds (373, 133, 154, 151);
    knob ("output")->setBounds (738, 108, 194, 177);
    inputMeter->setBounds (53, 118, 35, 165);
    outputMeter->setBounds (957, 118, 35, 165);
    switchFor ("dual")->setBounds (588, 122, 122, 28);
    switchFor ("second")->setBounds (588, 167, 122, 28);
    switchFor ("emphasis")->setBounds (588, 212, 122, 28);
    switchFor ("toDry")->setBounds (350, 287, 111, 28);
    switchFor ("autoGain")->setBounds (470, 287, 133, 28);
    switchFor ("lowCut")->setBounds (106, 300, 134, 28);
    switchFor ("lowShelf")->setBounds (106, 334, 145, 28);
    switchFor ("highShelf")->setBounds (746, 300, 145, 28);
    activityMeter->setBounds (292, 342, 674, 19);
    for (size_t i = 0; i < modeButtons.size(); ++i)
        modeButtons[i].setBounds (58 + static_cast<int> (i) * 156, 418, 145, 39);

    const std::array<juce::String, 4> bandIds { "bass", "p1", "p2", "treble" };
    for (size_t i = 0; i < bandIds.size(); ++i)
    {
        const int x = 219 + static_cast<int> (i) * 156;
        knob (bandIds[i] + "Pre")->setBounds (x, 610, 126, 107);
        switchFor (bandIds[i] + "Comp")->setBounds (x + 20, 721, 104, 27);
        knob (bandIds[i] + "Post")->setBounds (x, 751, 126, 107);
    }
    choice ("p1Freq")->setBounds (376, 578, 124, 25);
    choice ("p2Freq")->setBounds (532, 578, 124, 25);
    switchFor ("hpOn")->setBounds (76, 578, 100, 27);
    knob ("hpFreq")->setBounds (63, 610, 126, 107);
    knob ("hpRes")->setBounds (63, 751, 126, 107);
    switchFor ("lpOn")->setBounds (856, 578, 100, 27);
    knob ("lpFreq")->setBounds (843, 610, 126, 107);
    knob ("lpRes")->setBounds (843, 751, 126, 107);
}

void IvoryDriveAudioProcessorEditor::updateMode (bool extended)
{
    const bool changed = isExtended != extended;
    isExtended = extended;
    const std::array<const char*, 12> extendedKnobs {
        "hpFreq", "hpRes", "lpFreq", "lpRes", "bassPre", "bassPost", "p1Pre", "p1Post", "p2Pre", "p2Post", "treblePre", "treblePost" };
    for (const auto* id : extendedKnobs) knob (id)->setVisible (extended);
    const std::array<const char*, 6> extendedSwitches { "hpOn", "lpOn", "bassComp", "p1Comp", "p2Comp", "trebleComp" };
    for (const auto* id : extendedSwitches) switchFor (id)->setVisible (extended);
    choice ("p1Freq")->setVisible (extended);
    choice ("p2Freq")->setVisible (extended);
    for (auto& button : modeButtons) button.setEnabled (! extended);
    if (changed)
    {
        const int logicalHeight = extended ? extendedHeight : classicHeight;
        if (auto* sizeConstrainer = getConstrainer())
            sizeConstrainer->setFixedAspectRatio (static_cast<double> (designWidth) / logicalHeight);
        setSize (getWidth(), juce::roundToInt (getWidth() * static_cast<double> (logicalHeight) / designWidth));
    }
    resized();
    surface->repaint();
}

void IvoryDriveAudioProcessorEditor::timerCallback()
{
    const bool extended = value ("extended") >= 0.5f;
    if (isExtended != extended) updateMode (extended);
    inputMeter->update (processor.getInputPeak (0), processor.getInputPeak (1));
    outputMeter->update (processor.getOutputPeak (0), processor.getOutputPeak (1));
    activityMeter->update (processor.getSaturationActivity());
    const std::array<const char*, 4> bands { "bass", "p1", "p2", "treble" };
    for (const auto* band : bands)
    {
        const juce::String prefix (band);
        const bool comp = processor.parameters.getRawParameterValue (prefix + "Comp")->load() >= 0.5f;
        const float pre = processor.parameters.getRawParameterValue (prefix + "Pre")->load();
        knob (prefix + "Post")->setComplement (comp, pre);
    }
    refreshPresetName();
    surface->repaint (48, 387, 944, 25);
    surface->repaint (44, isExtended ? 884 : 486, 950, 27);
}

void IvoryDriveAudioProcessorEditor::refreshPresetName()
{
    const auto current = processor.getCurrentPresetName();
    if (current == presetName) return;
    presetName = current;
    presetMenu.setText (presetName, juce::dontSendNotification);
}

void IvoryDriveAudioProcessorEditor::choosePresetFile (bool save)
{
    if (fileChooser) return;
    const auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto initialFile = save ? folder.getChildFile ("Ivory Drive.ivorypreset") : folder;
    fileChooser = std::make_unique<juce::FileChooser> (save ? "Save Ivory Drive preset" : "Load Ivory Drive preset",
                                                    initialFile, "*.ivorypreset", true);
    const auto chooserFlags = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    const juce::Component::SafePointer<IvoryDriveAudioProcessorEditor> safeThis (this);
    fileChooser->launchAsync (chooserFlags, [safeThis, save] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr) return;
        auto file = chooser.getResult();
        if (file != juce::File())
        {
            if (save) file = file.withFileExtension ("ivorypreset");
            const auto result = save ? safeThis->processor.saveUserPreset (file) : safeThis->processor.loadUserPreset (file);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                        "Ivory Drive preset", result.getErrorMessage());
            safeThis->refreshPresetName();
        }
        // Destruction is deferred until the callback returns; native choosers may still be dispatching.
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->fileChooser.reset(); });
    });
}

void IvoryDriveAudioProcessorEditor::paintRack (juce::Graphics& g)
{
    const auto height = static_cast<float> (isExtended ? extendedHeight : classicHeight);
    g.setGradientFill (juce::ColourGradient (palette::panelLight(), 0.0f, 0.0f,
                                           palette::panelShade(), 1040.0f, height, false));
    g.fillAll();
    // Fine machining lines are deterministic vector artwork, never audio indicators.
    for (int y = 2; y < static_cast<int> (height); y += 4)
    {
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        g.drawHorizontalLine (y, 22.0f, 1018.0f);
    }
    g.setColour (juce::Colour (0xff46483d));
    g.fillRect (0.0f, 0.0f, 25.0f, height);
    g.fillRect (1015.0f, 0.0f, 25.0f, height);
    g.setColour (juce::Colours::white.withAlpha (0.33f));
    g.drawVerticalLine (26, 0.0f, height);
    g.drawVerticalLine (1013, 0.0f, height);
    g.setColour (juce::Colour (0xff777968).withAlpha (0.6f));
    g.drawRect (27.0f, 0.5f, 986.0f, height - 1.0f, 0.9f);
    screw (g, 13.0f, 23.0f); screw (g, 1027.0f, 23.0f);
    screw (g, 13.0f, height - 23.0f); screw (g, 1027.0f, height - 23.0f);

    engraving (g, "IVORY DRIVE", { 54, 18, 227, 31 }, 29.0f, juce::Justification::centredLeft, true);
    engraving (g, "H A R M O N I C   C O L O U R   P R O C E S S O R", { 56, 51, 235, 17 }, 8.2f, juce::Justification::centredLeft);
    engraving (g, "OVERSAMPLE", { 680, 58, 96, 23 }, 10.5f, juce::Justification::centredRight, true);
    g.setColour (palette::ink().withAlpha (0.37f));
    g.drawHorizontalLine (94, 47.0f, 993.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawHorizontalLine (95, 47.0f, 993.0f);

    engraving (g, "IN", { 49, 289, 44, 19 }, 10.0f, juce::Justification::centred, true);
    engraving (g, "OUT", { 953, 289, 44, 19 }, 10.0f, juce::Justification::centred, true);
    engraving (g, "dBFS", { 47, 308, 49, 16 }, 8.5f);
    engraving (g, "dBFS", { 950, 308, 49, 16 }, 8.5f);
    engraving (g, "NONLINEAR ACTIVITY", { 289, 323, 375, 17 }, 9.5f, juce::Justification::centredLeft, true);
    engraving (g, "SUBTLE", { 291, 363, 91, 16 }, 8.0f, juce::Justification::centredLeft);
    engraving (g, "PEAK BENDING", { 568, 363, 134, 16 }, 8.0f);
    engraving (g, "DENSE", { 895, 363, 70, 16 }, 8.0f, juce::Justification::centredRight);
    g.setColour (palette::ink().withAlpha (0.26f));
    g.drawHorizontalLine (386, 49.0f, 991.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawHorizontalLine (387, 49.0f, 991.0f);
    engraving (g, isExtended ? "CLASSIC EMPHASIS  /  STORED SELECTION" : "CLASSIC EMPHASIS  /  PRE & POST SATURATION",
               { 59, 392, 690, 18 }, 10.5f, juce::Justification::centredLeft, true);
    if (isExtended)
        engraving (g, "EXTENDED EQ ACTIVE", { 788, 392, 193, 18 }, 9.0f, juce::Justification::centredRight, true);
    engraving (g, isExtended ? "The six Classic profiles are recalled when returning to Classic. Extended EQ controls the active emphasis."
                             : "Frequency-selective drive: complementary EQ curves surround the nonlinear stages.",
               { 58, 462, 922, 18 }, 10.5f, juce::Justification::centred);

    if (isExtended)
    {
        g.setColour (palette::ink().withAlpha (0.3f));
        g.drawHorizontalLine (493, 28.0f, 1013.0f);
        g.setColour (juce::Colours::white.withAlpha (0.48f));
        g.drawHorizontalLine (494, 28.0f, 1013.0f);
        engraving (g, "EXTENDED EMPHASIS", { 56, 506, 400, 20 }, 15.0f, juce::Justification::centredLeft, true);
        engraving (g, "PRE EQ  >  SATURATION  >  POST EQ", { 587, 508, 396, 18 }, 10.0f, juce::Justification::centredRight);
        const std::array<juce::String, 6> headings { "HIGH PASS", "BASS", "PRESENCE 1", "PRESENCE 2", "TREBLE", "LOW PASS" };
        for (size_t i = 0; i < headings.size(); ++i)
        {
            const int x = 55 + static_cast<int> (i) * 156;
            const juce::Rectangle<float> group (static_cast<float> (x), 537.0f, 142.0f, 334.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (group, 2.0f);
            g.setColour (palette::ink().withAlpha (0.30f));
            g.drawRoundedRectangle (group, 2.0f, 0.7f);
            engraving (g, headings[i], { x + 5, 546, 132, 20 }, 11.5f, juce::Justification::centred, true);
            g.setColour (palette::ink().withAlpha (0.15f));
            g.drawHorizontalLine (571, static_cast<float> (x + 10), static_cast<float> (x + 132));
        }
        engraving (g, "150 Hz SHELF", { 224, 580, 119, 20 }, 10.5f);
        engraving (g, "7.5 kHz SHELF", { 691, 580, 119, 20 }, 10.5f);
        engraving (g, "12 dB / OCT", { 70, 727, 112, 17 }, 9.0f);
        engraving (g, "12 dB / OCT", { 850, 727, 112, 17 }, 9.0f);
    }
    const int footerY = isExtended ? 889 : 490;
    engraving (g, "IV / 01", { 57, footerY, 120, 19 }, 10.0f, juce::Justification::centredLeft, true);
    engraving (g, "SHIFT: FINE  /  DOUBLE CLICK: RESET", { 277, footerY, 478, 19 }, 9.0f);
    const bool bypassed = value ("bypass") >= 0.5f;
    const bool emphasisOn = value ("emphasis") >= 0.5f;
    const auto state = bypassed ? "PROCESSING BYPASSED" : (! emphasisOn ? "EMPHASIS OFF" : (isExtended ? "EXTENDED / ACTIVE" : "CLASSIC / ACTIVE"));
    g.setColour (bypassed ? juce::Colour (0xff79551e) : juce::Colour (0xff326137));
    g.setFont (rackFont (9.5f, true));
    g.drawText (state, 786, footerY, 193, 19, juce::Justification::centredRight);
}
