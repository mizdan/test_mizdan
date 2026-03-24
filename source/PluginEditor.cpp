#include "PluginEditor.h"

namespace IDs
{
    static constexpr auto morph = "morph";
    static constexpr auto formantScale = "formantScale";
    static constexpr auto brightness = "brightness";
    static constexpr auto breath = "breath";
    static constexpr auto aspiration = "aspiration";
    static constexpr auto vibratoRate = "vibratoRate";
    static constexpr auto vibratoDepth = "vibratoDepth";
    static constexpr auto jitter = "jitter";
    static constexpr auto shimmer = "shimmer";
    static constexpr auto formantDrift = "formantDrift";
    static constexpr auto attackMs = "attackMs";
    static constexpr auto decayMs = "decayMs";
    static constexpr auto sustain = "sustain";
    static constexpr auto releaseMs = "releaseMs";
    static constexpr auto female = "female";
    static constexpr auto portamento = "portamento";
}

CleanVowelSynthAudioProcessorEditor::CleanVowelSynthAudioProcessorEditor (CleanVowelSynthAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (820, 560);

    addAndMakeVisible(femaleButton);
    femaleAttachment = std::make_unique<ButtonAttachment>(audioProcessor.apvts, IDs::female, femaleButton);

    addSlider(morphSlider, morphLabel, "Morph");
    addSlider(formantScaleSlider, formantScaleLabel, "Formant Scale");
    addSlider(brightnessSlider, brightnessLabel, "Brightness");
    addSlider(breathSlider, breathLabel, "Breath");
    addSlider(aspirationSlider, aspirationLabel, "Aspiration");
    addSlider(vibratoRateSlider, vibratoRateLabel, "Vibrato Rate");
    addSlider(vibratoDepthSlider, vibratoDepthLabel, "Vibrato Depth");
    addSlider(jitterSlider, jitterLabel, "Jitter");
    addSlider(shimmerSlider, shimmerLabel, "Shimmer");
    addSlider(formantDriftSlider, formantDriftLabel, "Formant Drift");
    addSlider(attackSlider, attackLabel, "Attack");
    addSlider(decaySlider, decayLabel, "Decay");
    addSlider(sustainSlider, sustainLabel, "Sustain");
    addSlider(releaseSlider, releaseLabel, "Release");
    addSlider(portamentoSlider, portamentoLabel, "Portamento");

    auto& state = audioProcessor.apvts;
    morphAttachment = std::make_unique<SliderAttachment>(state, IDs::morph, morphSlider);
    formantScaleAttachment = std::make_unique<SliderAttachment>(state, IDs::formantScale, formantScaleSlider);
    brightnessAttachment = std::make_unique<SliderAttachment>(state, IDs::brightness, brightnessSlider);
    breathAttachment = std::make_unique<SliderAttachment>(state, IDs::breath, breathSlider);
    aspirationAttachment = std::make_unique<SliderAttachment>(state, IDs::aspiration, aspirationSlider);
    vibratoRateAttachment = std::make_unique<SliderAttachment>(state, IDs::vibratoRate, vibratoRateSlider);
    vibratoDepthAttachment = std::make_unique<SliderAttachment>(state, IDs::vibratoDepth, vibratoDepthSlider);
    jitterAttachment = std::make_unique<SliderAttachment>(state, IDs::jitter, jitterSlider);
    shimmerAttachment = std::make_unique<SliderAttachment>(state, IDs::shimmer, shimmerSlider);
    formantDriftAttachment = std::make_unique<SliderAttachment>(state, IDs::formantDrift, formantDriftSlider);
    attackAttachment = std::make_unique<SliderAttachment>(state, IDs::attackMs, attackSlider);
    decayAttachment = std::make_unique<SliderAttachment>(state, IDs::decayMs, decaySlider);
    sustainAttachment = std::make_unique<SliderAttachment>(state, IDs::sustain, sustainSlider);
    releaseAttachment = std::make_unique<SliderAttachment>(state, IDs::releaseMs, releaseSlider);
    portamentoAttachment = std::make_unique<SliderAttachment>(state, IDs::portamento, portamentoSlider);
}

void CleanVowelSynthAudioProcessorEditor::addSlider(juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    addAndMakeVisible(slider);

    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(label);
}

void CleanVowelSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff181a20));
    g.setColour(juce::Colours::white);
    g.setFont(24.0f);
    g.drawText("Clean Vowel Synth", 20, 15, getWidth() - 40, 30, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff2a2d36));
    g.fillRoundedRectangle(12.0f, 56.0f, getWidth() - 24.0f, getHeight() - 68.0f, 14.0f);
}

void CleanVowelSynthAudioProcessorEditor::resized()
{
    femaleButton.setBounds(24, 64, 100, 24);

    const int startX = 24;
    const int startY = 100;
    const int colW = 150;
    const int rowH = 115;

    std::array<juce::Slider*, 15> sliders = {
        &morphSlider, &formantScaleSlider, &brightnessSlider, &breathSlider, &aspirationSlider,
        &vibratoRateSlider, &vibratoDepthSlider, &jitterSlider, &shimmerSlider, &formantDriftSlider,
        &attackSlider, &decaySlider, &sustainSlider, &releaseSlider, &portamentoSlider
    };

    std::array<juce::Label*, 15> labels = {
        &morphLabel, &formantScaleLabel, &brightnessLabel, &breathLabel, &aspirationLabel,
        &vibratoRateLabel, &vibratoDepthLabel, &jitterLabel, &shimmerLabel, &formantDriftLabel,
        &attackLabel, &decayLabel, &sustainLabel, &releaseLabel, &portamentoLabel
    };

    for (size_t i = 0; i < sliders.size(); ++i)
    {
        const int col = (int) (i % 5);
        const int row = (int) (i / 5);
        const int x = startX + col * colW;
        const int y = startY + row * rowH;
        labels[i]->setBounds(x, y, 120, 20);
        sliders[i]->setBounds(x, y + 20, 120, 90);
    }
}
