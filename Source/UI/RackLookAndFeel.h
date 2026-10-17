#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

namespace ivory::ui
{
namespace palette
{
inline juce::Colour panel()      { return juce::Colour (0xffd6d3b8); }
inline juce::Colour panelLight() { return juce::Colour (0xffeeeddc); }
inline juce::Colour panelShade() { return juce::Colour (0xffb5b39a); }
inline juce::Colour ink()        { return juce::Colour (0xff292c25); }
inline juce::Colour muted()      { return juce::Colour (0xff666957); }
inline juce::Colour accent()     { return juce::Colour (0xff53e78c); }
inline juce::Colour switchFace() { return juce::Colour (0xfff4f3e9); }
inline juce::Colour outline()    { return juce::Colour (0xff92957e); }
} // namespace palette

/** Original, vector-drawn studio hardware controls. No external image assets. */
class RackLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    RackLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, palette::panel());
        setColour (juce::Slider::thumbColourId, palette::ink());
        setColour (juce::Slider::textBoxTextColourId, palette::ink());
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, palette::accent().withAlpha (0.25f));
        setColour (juce::Label::textColourId, palette::ink());
        setColour (juce::TextButton::buttonColourId, palette::switchFace());
        setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4d7256));
        setColour (juce::TextButton::textColourOffId, palette::ink());
        setColour (juce::TextButton::textColourOnId, palette::switchFace());
        setColour (juce::ToggleButton::textColourId, palette::ink());
        setColour (juce::ComboBox::backgroundColourId, palette::panelLight());
        setColour (juce::ComboBox::textColourId, palette::ink());
        setColour (juce::ComboBox::outlineColourId, palette::outline());
        setColour (juce::ComboBox::arrowColourId, palette::ink());
        setColour (juce::PopupMenu::backgroundColourId, palette::panelLight());
        setColour (juce::PopupMenu::textColourId, palette::ink());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff405a43));
        setColour (juce::PopupMenu::highlightedTextColourId, palette::switchFace());
        setColour (juce::TextEditor::backgroundColourId, palette::panelLight());
        setColour (juce::TextEditor::textColourId, palette::ink());
        setColour (juce::TextEditor::outlineColourId, palette::outline());
        setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff49634b));
        setColour (juce::TooltipWindow::backgroundColourId, palette::panelLight());
        setColour (juce::TooltipWindow::textColourId, palette::ink());
        setColour (juce::TooltipWindow::outlineColourId, palette::outline());
    }

    static juce::Font lettering (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        return label.getFont();
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return lettering (juce::jlimit (9.0f, 12.0f, static_cast<float> (buttonHeight) * 0.42f), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return lettering (juce::jlimit (10.0f, 12.0f, static_cast<float> (box.getHeight()) * 0.45f));
    }

    juce::Font getPopupMenuFont() override
    {
        return lettering (13.0f);
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (lettering (10.5f));
        label->setJustificationType (juce::Justification::centred);
        label->setBorderSize (juce::BorderSize<int> (0));
        label->setColour (juce::Label::textColourId, palette::ink());
        label->setColour (juce::Label::backgroundColourId, palette::panelLight().withAlpha (0.38f));
        label->setColour (juce::Label::outlineColourId, palette::outline().withAlpha (0.72f));
        label->setColour (juce::TextEditor::textColourId, palette::ink());
        label->setColour (juce::TextEditor::backgroundColourId, palette::panelLight());
        label->setColour (juce::TextEditor::outlineColourId, palette::outline());
        label->setColour (juce::TextEditor::highlightColourId, palette::accent().withAlpha (0.25f));
        label->setColour (juce::TextEditor::highlightedTextColourId, palette::ink());
        return label;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosition, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto size = static_cast<float> (juce::jmin (width, height));
        if (size < 8.0f)
            return;

        const auto cx = static_cast<float> (x) + static_cast<float> (width) * 0.5f;
        const auto cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
        const auto radius = size * 0.326f;
        const auto scaleInner = size * 0.399f;
        const auto scaleOuter = size * 0.449f;
        const auto opacity = slider.isEnabled() ? 1.0f : 0.42f;
        const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
        const auto pointAt = [cx, cy] (float r, float a)
        {
            return juce::Point<float> (cx + std::sin (a) * r, cy - std::cos (a) * r);
        };

        // Fine engraved scale and longer quarter marks remain legible on the
        // small extended-panel controls as well as on the main gain controls.
        g.setColour (palette::ink().withAlpha (0.72f * opacity));
        constexpr int tickCount = 40;
        for (int i = 0; i <= tickCount; ++i)
        {
            const auto a = rotaryStartAngle + static_cast<float> (i) / tickCount
                                               * (rotaryEndAngle - rotaryStartAngle);
            const auto isMajor = i % 10 == 0;
            const auto p1 = pointAt (isMajor ? scaleInner - 1.8f : scaleInner, a);
            const auto p2 = pointAt (isMajor ? scaleOuter + 1.0f : scaleOuter, a);
            g.drawLine ({ p1, p2 }, isMajor ? 1.0f : 0.65f);
        }

        if (size >= 74.0f)
        {
            const auto fontHeight = juce::jlimit (7.5f, 9.0f, size * 0.09f);
            g.setFont (lettering (fontHeight));
            for (int i = 0; i <= 4; ++i)
            {
                const auto proportion = static_cast<double> (i) / 4.0;
                const auto a = rotaryStartAngle + static_cast<float> (proportion)
                                                   * (rotaryEndAngle - rotaryStartAngle);
                auto position = pointAt (size * 0.525f, a);
                position.x = juce::jlimit (static_cast<float> (x) + 12.0f,
                                          static_cast<float> (x + width) - 12.0f, position.x);
                position.y = juce::jlimit (static_cast<float> (y) + 5.0f,
                                          static_cast<float> (y + height) - 5.0f, position.y);
                const auto value = slider.proportionOfLengthToValue (proportion);
                const auto text = compactScaleNumber (value);
                g.drawText (text, juce::Rectangle<float> (position.x - 13.0f, position.y - 5.0f,
                                                         26.0f, 10.0f),
                            juce::Justification::centred, false);
            }
        }

        // Recessed metal collar under a twelve-lobed Bakelite body.
        const auto collar = juce::Rectangle<float> (cx - radius - 1.7f, cy - radius - 1.7f,
                                                    2.0f * radius + 3.4f, 2.0f * radius + 3.4f);
        g.setColour (juce::Colours::black.withAlpha (0.2f * opacity));
        g.fillEllipse (collar.translated (1.2f, 2.2f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff85877b).withAlpha (opacity),
                                               cx, cy - radius,
                                               juce::Colour (0xff343832).withAlpha (opacity),
                                               cx, cy + radius, false));
        g.fillEllipse (collar);
        g.setColour (juce::Colours::black.withAlpha (0.58f * opacity));
        g.drawEllipse (collar, 0.8f);

        juce::Path knob;
        constexpr int vertices = 120;
        for (int i = 0; i < vertices; ++i)
        {
            const auto theta = static_cast<float> (i) / vertices * juce::MathConstants<float>::twoPi;
            const auto r = radius * (0.957f + 0.043f * std::cos (theta * 12.0f));
            const auto p = pointAt (r, theta);
            if (i == 0)
                knob.startNewSubPath (p);
            else
                knob.lineTo (p);
        }
        knob.closeSubPath();

        juce::ColourGradient bakelite (juce::Colour (0xff373b36).withAlpha (opacity),
                                      cx - radius * 0.7f, cy - radius,
                                      juce::Colour (0xff080b09).withAlpha (opacity),
                                      cx + radius * 0.55f, cy + radius, false);
        bakelite.addColour (0.45, juce::Colour (0xff1c201c).withAlpha (opacity));
        g.setGradientFill (bakelite);
        g.fillPath (knob);
        g.setColour (juce::Colours::black.withAlpha (opacity));
        g.strokePath (knob, juce::PathStrokeType (0.8f));

        // Shallow moulded finger grooves catch the light individually.
        for (int i = 0; i < 12; ++i)
        {
            const auto a = static_cast<float> (i) / 12.0f * juce::MathConstants<float>::twoPi;
            const auto start = pointAt (radius * 0.79f, a - 0.035f);
            const auto end = pointAt (radius * 0.93f, a - 0.035f);
            g.setColour (juce::Colour (0xffaeb8aa).withAlpha ((i < 4 || i > 9 ? 0.13f : 0.04f) * opacity));
            g.drawLine ({ start, end }, 1.0f);
        }

        const auto faceRadius = radius * 0.72f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff30352e).withAlpha (opacity),
                                               cx - faceRadius, cy - faceRadius,
                                               juce::Colour (0xff111610).withAlpha (opacity),
                                               cx + faceRadius, cy + faceRadius, false));
        g.fillEllipse (cx - faceRadius, cy - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f);
        g.setColour (juce::Colour (0xff4a5146).withAlpha (0.55f * opacity));
        g.drawEllipse (cx - faceRadius, cy - faceRadius, faceRadius * 2.0f, faceRadius * 2.0f, 0.6f);

        const auto pointerStart = pointAt (radius * 0.32f, angle);
        const auto pointerEnd = pointAt (radius * 0.85f, angle);
        g.setColour (juce::Colours::black.withAlpha (0.65f * opacity));
        g.drawLine ({ pointerStart.translated (0.6f, 0.8f), pointerEnd.translated (0.6f, 0.8f) }, 2.5f);
        g.setColour (palette::switchFace().withAlpha (opacity));
        g.drawLine ({ pointerStart, pointerEnd }, juce::jmax (1.5f, size * 0.023f));

        if (slider.isMouseOverOrDragging() && slider.isEnabled())
        {
            g.setColour (palette::panelLight().withAlpha (0.16f));
            g.strokePath (knob, juce::PathStrokeType (1.0f));
        }
        if (slider.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff466a4e));
            g.drawEllipse (collar.expanded (1.7f), 1.0f);
        }
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const auto opacity = button.isEnabled() ? 1.0f : 0.42f;
        const auto hasText = button.getButtonText().isNotEmpty();
        const auto switchHeight = juce::jlimit (12.0f, 25.0f, bounds.getHeight() - 6.0f);
        const auto switchWidth = switchHeight * 0.51f;
        const auto sx = hasText ? 3.0f : bounds.getCentreX() - switchWidth * 0.5f - 3.0f;
        const auto sy = bounds.getCentreY() - switchHeight * 0.5f;
        const juce::Rectangle<float> housing (sx, sy, switchWidth, switchHeight);
        g.setColour (juce::Colours::black.withAlpha (0.21f * opacity));
        g.fillRoundedRectangle (housing.expanded (1.7f).translated (0.4f, 1.3f), 1.5f);
        g.setColour (palette::ink().withAlpha (0.85f * opacity));
        g.fillRoundedRectangle (housing.expanded (0.6f), 1.0f);

        const auto on = button.getToggleState();
        auto face = housing.reduced (0.7f);
        face = face.withTrimmedTop (on ? 0.0f : 2.2f).withTrimmedBottom (on ? 2.2f : 0.0f);
        const auto faceTop = (highlighted || down ? juce::Colours::white : palette::switchFace()).withAlpha (opacity);
        g.setGradientFill (juce::ColourGradient (faceTop, face.getCentreX(), face.getY(),
                                               juce::Colour (0xffbdbdaf).withAlpha (opacity),
                                               face.getCentreX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 0.6f);
        g.setColour (juce::Colours::white.withAlpha (0.7f * opacity));
        g.drawLine (face.getX() + 1.0f, face.getY() + 1.2f,
                    face.getRight() - 1.0f, face.getY() + 1.2f, 0.7f);
        g.setColour (palette::outline().withAlpha (0.65f * opacity));
        g.drawLine (face.getX() + 1.0f, face.getCentreY(),
                    face.getRight() - 1.0f, face.getCentreY(), 0.65f);

        const auto ledX = housing.getRight() + 5.0f;
        const auto ledY = bounds.getCentreY();
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (palette::accent().withAlpha (0.38f * opacity),
                                                   ledX, ledY, juce::Colours::transparentBlack,
                                                   ledX + 5.0f, ledY, true));
            g.fillEllipse (ledX - 5.0f, ledY - 5.0f, 10.0f, 10.0f);
        }
        g.setColour (juce::Colour (0xff18291a).withAlpha (opacity));
        g.fillEllipse (ledX - 2.7f, ledY - 2.7f, 5.4f, 5.4f);
        g.setColour ((on ? palette::accent() : juce::Colour (0xff3f5942)).withAlpha (opacity));
        g.fillEllipse (ledX - 1.8f, ledY - 1.8f, 3.6f, 3.6f);
        if (on)
        {
            g.setColour (juce::Colour (0xffdcffe4).withAlpha (opacity));
            g.fillEllipse (ledX - 1.0f, ledY - 1.3f, 1.4f, 1.4f);
        }

        if (hasText)
        {
            const auto textX = static_cast<int> (ledX + 7.0f);
            g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (opacity));
            g.setFont (lettering (10.4f, true));
            g.drawFittedText (button.getButtonText(), textX, 0,
                              juce::jmax (0, button.getWidth() - textX - 1), button.getHeight(),
                              juce::Justification::centredLeft, 2, 0.8f);
        }

        if (button.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff466a4e));
            g.drawRoundedRectangle (bounds.reduced (0.7f), 2.0f, 1.0f);
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f, 1.0f);
        const auto opacity = button.isEnabled() ? 1.0f : 0.4f;
        const auto active = down || button.getToggleState();
        auto colour = backgroundColour;
        if (highlighted)
            colour = colour.brighter (0.08f);
        if (down)
            colour = colour.darker (0.15f);
        g.setColour (juce::Colours::black.withAlpha (0.18f * opacity));
        g.fillRoundedRectangle (bounds.translated (0.0f, active ? 0.6f : 1.5f), 2.0f);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.13f).withMultipliedAlpha (opacity),
                                               bounds.getX(), bounds.getY(),
                                               colour.darker (0.08f).withMultipliedAlpha (opacity),
                                               bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 2.0f);
        g.setColour (palette::ink().withAlpha (0.6f * opacity));
        g.drawRoundedRectangle (bounds, 2.0f, 0.8f);
        if (! active)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f * opacity));
            g.drawLine (bounds.getX() + 2.0f, bounds.getY() + 1.2f,
                        bounds.getRight() - 2.0f, bounds.getY() + 1.2f, 0.7f);
        }
        if (button.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff466a4e));
            g.drawRoundedRectangle (bounds.reduced (2.0f), 1.0f, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool down) override
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId)
                           .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.42f));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (5, 2).translated (0, down ? 1 : 0),
                          juce::Justification::centred, 1, 0.8f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        const auto opacity = box.isEnabled() ? 1.0f : 0.42f;
        const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                   static_cast<float> (width) - 1.0f,
                                                   static_cast<float> (height) - 1.0f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (bounds, 1.5f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (bounds, 1.5f, 0.8f);
        const auto arrowX = static_cast<float> (width) - 11.0f;
        const auto arrowY = static_cast<float> (height) * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (arrowX - 3.0f, arrowY - 1.6f);
        arrow.lineTo (arrowX, arrowY + 1.6f);
        arrow.lineTo (arrowX + 3.0f, arrowY - 1.6f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (opacity));
        g.strokePath (arrow, juce::PathStrokeType (1.1f));
        if (box.hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff466a4e));
            g.drawRoundedRectangle (bounds.reduced (1.0f), 1.0f, 1.0f);
        }
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (6, 1, juce::jmax (0, box.getWidth() - 23), juce::jmax (0, box.getHeight() - 2));
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

private:
    static juce::String compactScaleNumber (double value)
    {
        const auto absolute = std::abs (value);
        if (absolute >= 1000.0)
            return juce::String (value / 1000.0, absolute < 10000.0 ? 1 : 0) + "k";
        if (absolute < 2.0 && absolute > 0.001)
            return juce::String (value, 1);
        return juce::String (juce::roundToInt (value));
    }
};
} // namespace ivory::ui
