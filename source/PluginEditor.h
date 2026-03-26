#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class CleanVowelSynthAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit CleanVowelSynthAudioProcessorEditor (CleanVowelSynthAudioProcessor&);
    ~CleanVowelSynthAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void addSlider(juce::Slider& slider, juce::Label& label, const juce::String& text);

    CleanVowelSynthAudioProcessor& audioProcessor;

    juce::ToggleButton femaleButton { "Female" };

    juce::Slider morphSlider, formantScaleSlider, brightnessSlider, breathSlider, aspirationSlider;
    juce::Slider roughnessSlider, vibratoRateSlider, vibratoDepthSlider, jitterSlider, shimmerSlider, formantDriftSlider;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider, portamentoSlider;
    juce::Slider outputGainSlider;

    juce::Label morphLabel, formantScaleLabel, brightnessLabel, breathLabel, aspirationLabel;
    juce::Label roughnessLabel, vibratoRateLabel, vibratoDepthLabel, jitterLabel, shimmerLabel, formantDriftLabel;
    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel, portamentoLabel;
    juce::Label outputGainLabel;

    std::unique_ptr<SliderAttachment> morphAttachment, formantScaleAttachment, brightnessAttachment, breathAttachment, aspirationAttachment;
    std::unique_ptr<SliderAttachment> roughnessAttachment, vibratoRateAttachment, vibratoDepthAttachment, jitterAttachment, shimmerAttachment, formantDriftAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment, decayAttachment, sustainAttachment, releaseAttachment, portamentoAttachment;
    std::unique_ptr<SliderAttachment> outputGainAttachment;
    std::unique_ptr<ButtonAttachment> femaleAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CleanVowelSynthAudioProcessorEditor)
};
