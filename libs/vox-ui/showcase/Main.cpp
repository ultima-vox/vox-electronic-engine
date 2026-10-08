#include <juce_gui_extra/juce_gui_extra.h>

#include "VoxComponents.h"
#include "VoxLookAndFeel.h"
#include "Tokens.h"
#include "Typography.h"

#include <array>

namespace {

std::unique_ptr<juce::Drawable> makeDiamondIcon()
{
    auto drawable = std::make_unique<juce::DrawablePath>();
    juce::Path path;
    path.startNewSubPath (8.0f, 0.0f);
    path.lineTo (16.0f, 8.0f);
    path.lineTo (8.0f, 16.0f);
    path.lineTo (0.0f, 8.0f);
    path.closeSubPath();
    drawable->setPath (path);
    drawable->setFill (vox::ui::tokens::colour::textSecondary);
    return drawable;
}

// Draws the whole shared vector icon family in one strip, because "one icon
// grammar" is only verifiable if every glyph is visible at the same weight.
class IconGallery final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        using namespace vox::ui;
        const std::array<std::pair<icons::Icon, const char*>, 14> entries { {
            { icons::Icon::chevronLeft, "chevronLeft" },
            { icons::Icon::chevronRight, "chevronRight" },
            { icons::Icon::chevronDown, "chevronDown" },
            { icons::Icon::power, "power" },
            { icons::Icon::plus, "plus" },
            { icons::Icon::gear, "gear" },
            { icons::Icon::pencil, "pencil" },
            { icons::Icon::heart, "heart" },
            { icons::Icon::dice, "dice" },
            { icons::Icon::cube, "cube" },
            { icons::Icon::keyboardLayout, "keyboard" },
            { icons::Icon::play, "play" },
            { icons::Icon::stop, "stop" },
            { icons::Icon::matrixGrid, "matrixGrid" },
        } };

        const auto cell = getWidth() / static_cast<int> (entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            auto area = juce::Rectangle<int> (static_cast<int> (i) * cell, 0, cell, getHeight());
            auto glyphArea = area.removeFromTop (juce::jmax (0, area.getHeight() - 16))
                                 .toFloat()
                                 .withSizeKeepingCentre (26.0f, 26.0f);

            icons::draw (g, entries[i].first, glyphArea,
                         i % 2 == 0 ? tokens::colour::accent : tokens::colour::text);

            g.setColour (tokens::colour::textMuted);
            g.setFont (typography::valueText());
            g.drawText (entries[i].second, area, juce::Justification::centred, false);
        }
    }
};

class ShowcaseContent final : public juce::Component
{
public:
    ShowcaseContent()
        : primary ("PRIMARY", vox::ui::VoxButton::Type::Primary),
          secondary ("SECONDARY", vox::ui::VoxButton::Type::Secondary),
          toggle ("TOGGLE", vox::ui::VoxButton::Type::Toggle),
          danger ("DANGER", vox::ui::VoxButton::Type::Danger),
          iconButton ({}, vox::ui::VoxButton::Type::Icon),
          disabled ("DISABLED", vox::ui::VoxButton::Type::Secondary),
          smallKnob ("SMALL", vox::ui::VoxKnob::Size::Small),
          normalKnob ("NORMAL", vox::ui::VoxKnob::Size::Normal),
          largeKnob ("LARGE", vox::ui::VoxKnob::Size::Large),
          modKnob ("MOD", vox::ui::VoxKnob::Size::Normal),
          bipolarKnob ("BIPOLAR", vox::ui::VoxKnob::Size::Normal),
          panel ("FOUNDATION"),
          powerHeader ("OSCILLATOR"),
          iconHeader ("AMP ENVELOPE"),
          plainHeader ("PLAIN HEADER"),
          gearButton (vox::ui::icons::Icon::gear),
          heartButton (vox::ui::icons::Icon::heart),
          togglePowerButton (vox::ui::icons::Icon::power, true)
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        setOpaque (true);

        tabs.setTabs ({ { "sound", "Sound", true },
                        { "pattern", "Pattern", true },
                        { "routing", "Routing", true },
                        { "disabled", "Disabled", false } });

        const std::array<juce::Component*, 24> components {
            &tabs, &panel, &primary, &secondary, &toggle, &danger,
            &iconButton, &disabled, &smallKnob, &normalKnob,
            &largeKnob, &modKnob, &bipolarKnob,
            &combo, &scaleBox, &powerHeader, &iconHeader, &plainHeader,
            &iconGallery, &segments, &stepper, &headerSelector,
            &gearButton, &togglePowerButton
        };

        for (auto* component : components)
            addAndMakeVisible (*component);

        iconButton.setIcon (makeDiamondIcon());
        iconButton.setIcon (vox::ui::icons::Icon::dice);
        disabled.setEnabled (false);

        const std::array<vox::ui::VoxKnob*, 5> knobs {
            &smallKnob, &normalKnob, &largeKnob, &modKnob, &bipolarKnob
        };

        for (auto* knob : knobs)
        {
            knob->getSlider().setRange (0.0, 100.0, 0.1);
            knob->getSlider().setValue (55.0, juce::dontSendNotification);
            knob->getSlider().setTextValueSuffix (" %");
            knob->setDoubleClickResetValue (50.0);
        }
        modKnob.setModulationAmount (0.35f);
        bipolarKnob.setStyle (vox::ui::VoxKnob::Style::Bipolar);
        bipolarKnob.setModulationAmount (0.55f);

        combo.addItem ("Low Pass", 1);
        combo.addItem ("High Pass", 2);
        combo.addItem ("Band Pass", 3);
        combo.setSelectedId (1, juce::dontSendNotification);

        scaleBox.addItem ("75%", 75);
        scaleBox.addItem ("100%", 100);
        scaleBox.addItem ("125%", 125);
        scaleBox.addItem ("150%", 150);
        scaleBox.addItem ("200%", 200);
        scaleBox.setSelectedId (100, juce::dontSendNotification);
        scaleBox.onChange = [this]
        {
            const auto selected = scaleBox.getSelectedId();
            uiScale = selected > 0 ? static_cast<float> (selected) / 100.0f : 1.0f;
            resized();
            repaint();
        };

        powerHeader.setPowerVisible (true);
        powerHeader.setPowerState (true);
        powerHeader.setControlComponent (&segments);
        powerHeader.setTrailingPowerVisible (true);

        iconHeader.setIcon (vox::ui::icons::Icon::gear);
        iconHeader.setSubtitle ("(5/16)");
        iconHeader.setSelectorComponent (&headerSelector);
        iconHeader.setActionComponent (&gearButton);

        // The stepper row from the OSCILLATOR panel: previous / value / next.
        segments.setSegments ({ "1", "2", "3" });
        stepper.setItems ({ "Wavetable 01", "Wavetable 02", "Wavetable 03" });
        headerSelector.setReadOnly (true);
        headerSelector.setPlaceholderText ("Low Pass 24 dB");

        togglePowerButton.setToggled (true);

        setSize (vox::ui::tokens::size::referenceWidth,
                 vox::ui::tokens::size::referenceHeight);
    }

    ~ShowcaseContent() override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (vox::ui::tokens::colour::background);

        g.setColour (vox::ui::tokens::colour::text);
        g.setFont (vox::ui::typography::instrumentTitle());
        g.drawText ("VOX UI FOUNDATION SHOWCASE", titleBounds,
                    juce::Justification::centredLeft, false);

        g.setColour (vox::ui::tokens::colour::textMuted);
        g.setFont (vox::ui::typography::valueText());
        g.drawText ("UI-1 / TOKENS / TYPOGRAPHY / STATES / SCALE / ICONS",
                    subtitleBounds, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        using namespace vox::ui;
        const auto s = uiScale;
        const auto px = [s] (int v) { return juce::jmax (1, juce::roundToInt (static_cast<float> (v) * s)); };

        auto area = getLocalBounds().reduced (px (tokens::spacing::xxl));
        auto headerRow = area.removeFromTop (px (64));
        titleBounds = headerRow.removeFromTop (px (34));
        subtitleBounds = headerRow;

        auto scaleArea = titleBounds.removeFromRight (px (120));
        scaleBox.setBounds (scaleArea.withHeight (px (tokens::size::controlHeight)));

        area.removeFromTop (px (tokens::spacing::lg));
        tabs.setBounds (area.removeFromTop (px (tokens::size::tabHeight)));
        area.removeFromTop (px (tokens::spacing::lg));

        panel.setBounds (area);
        // getContentBounds() is PANEL-LOCAL, and the showcase composes the panel
        // from outside it, so the rect has to be brought back into this
        // component's coordinate space before it can position siblings.
        auto content = panel.getContentBounds().translated (panel.getX(), panel.getY());
        const auto gap = px (tokens::spacing::lg);

        powerHeader.setBounds (content.removeFromTop (px (32)));
        content.removeFromTop (px (tokens::spacing::sm));

        auto knobRow = content.removeFromTop (px (120));
        const auto knobCell = knobRow.getWidth() / 5;
        smallKnob.setBounds (knobRow.removeFromLeft (knobCell));
        normalKnob.setBounds (knobRow.removeFromLeft (knobCell));
        largeKnob.setBounds (knobRow.removeFromLeft (knobCell));
        modKnob.setBounds (knobRow.removeFromLeft (knobCell));
        bipolarKnob.setBounds (knobRow);

        content.removeFromTop (gap);
        iconHeader.setBounds (content.removeFromTop (px (32)));
        content.removeFromTop (px (tokens::spacing::sm));

        auto buttonRow = content.removeFromTop (px (tokens::size::controlHeight));
        const auto buttonGap = px (tokens::spacing::sm);
        const auto buttonWidth = juce::jmax (72, (buttonRow.getWidth() - buttonGap * 5) / 6);
        const std::array<vox::ui::VoxButton*, 6> buttons {
            &primary, &secondary, &toggle, &danger, &iconButton, &disabled
        };
        for (auto* button : buttons)
        {
            button->setBounds (buttonRow.removeFromLeft (buttonWidth));
            buttonRow.removeFromLeft (buttonGap);
        }

        content.removeFromTop (gap);
        auto primitiveRow = content.removeFromTop (juce::jmax (px (30), px (tokens::size::controlHeight)));
        const auto square = primitiveRow.getHeight();
        gearButton.setBounds (primitiveRow.removeFromLeft (square));
        primitiveRow.removeFromLeft (buttonGap);
        heartButton.setBounds (primitiveRow.removeFromLeft (square));
        primitiveRow.removeFromLeft (buttonGap);
        togglePowerButton.setBounds (primitiveRow.removeFromLeft (square));
        primitiveRow.removeFromLeft (px (tokens::spacing::lg));
        stepper.setBounds (primitiveRow.removeFromLeft (px (280)));
        primitiveRow.removeFromLeft (buttonGap);
        headerSelector.setBounds (primitiveRow.removeFromLeft (px (196)));

        content.removeFromTop (gap);
        plainHeader.setBounds (content.removeFromTop (px (32)));
        content.removeFromTop (px (tokens::spacing::sm));
        combo.setBounds (content.removeFromTop (px (tokens::size::controlHeight))
                                .removeFromLeft (px (180)));

        content.removeFromTop (gap);
        iconGallery.setBounds (content.removeFromTop (juce::jmax (px (48), area.getHeight() / 6)));
    }

private:
    vox::ui::VoxLookAndFeel lookAndFeel;
    float uiScale = 1.0f;
    juce::Rectangle<int> titleBounds;
    juce::Rectangle<int> subtitleBounds;

    vox::ui::VoxTabBar tabs;
    vox::ui::VoxPanel panel;
    vox::ui::VoxButton primary;
    vox::ui::VoxButton secondary;
    vox::ui::VoxButton toggle;
    vox::ui::VoxButton danger;
    vox::ui::VoxButton iconButton;
    vox::ui::VoxButton disabled;
    vox::ui::VoxKnob smallKnob;
    vox::ui::VoxKnob normalKnob;
    vox::ui::VoxKnob largeKnob;
    vox::ui::VoxKnob modKnob;
    vox::ui::VoxKnob bipolarKnob;
    vox::ui::VoxComboBox combo;
    vox::ui::VoxComboBox scaleBox;
    vox::ui::VoxSectionHeader powerHeader;
    vox::ui::VoxSectionHeader iconHeader;
    vox::ui::VoxSectionHeader plainHeader;
    IconGallery iconGallery;
    vox::ui::VoxSegmentedControl segments;
    vox::ui::VoxInlineSelector stepper { vox::ui::VoxInlineSelector::Form::Stepper };
    vox::ui::VoxInlineSelector headerSelector { vox::ui::VoxInlineSelector::Form::Header };
    vox::ui::VoxIconButton gearButton;
    vox::ui::VoxIconButton heartButton;
    vox::ui::VoxIconButton togglePowerButton;
};

class ShowcaseWindow final : public juce::DocumentWindow
{
public:
    ShowcaseWindow()
        : juce::DocumentWindow ("VOX UI Showcase",
                                vox::ui::tokens::colour::background,
                                juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setContentOwned (new ShowcaseContent(), true);
        centreWithSize (vox::ui::tokens::size::referenceWidth,
                        vox::ui::tokens::size::referenceHeight);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class ShowcaseApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "VOX UI Showcase"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // Headless evidence path: render the showcase once to a PNG and exit, so
        // the shared primitives can be reviewed without a display.
        const auto captureTarget = commandLineFrom (commandLine);

        if (captureTarget.isNotEmpty())
        {
            ShowcaseContent content;
            content.setSize (vox::ui::tokens::size::referenceWidth,
                             vox::ui::tokens::size::referenceHeight);
            const auto image = content.createComponentSnapshot (content.getLocalBounds());

            juce::File file (captureTarget);
            file.deleteFile();
            juce::PNGImageFormat format;
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
                format.writeImageToStream (image, *stream);

            quit();
            return;
        }

        window = std::make_unique<ShowcaseWindow>();
    }

    void shutdown() override
    {
        window.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    static juce::String commandLineFrom (const juce::String& commandLine)
    {
        const auto marker = commandLine.indexOf ("--capture=");
        if (marker < 0)
            return {};

        auto value = commandLine.substring (marker + 10).trim();
        if (value.startsWithChar ('"') && value.endsWithChar ('"') && value.length() > 1)
            value = value.substring (1, value.length() - 1);
        return value.unquoted();
    }

    std::unique_ptr<ShowcaseWindow> window;
};

} // namespace

START_JUCE_APPLICATION (ShowcaseApplication)
