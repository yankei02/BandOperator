/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ExchangeBandAudioProcessorEditor::ExchangeBandAudioProcessorEditor (ExchangeBandAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    
    setSize (400, 300);
    // Define the frequency range limits as double
        double minFreq = 20.0;
        double maxFreq = 20000.0;

        // Mapping from normalised value [0,1] to frequency
        auto convertFrom0To1 = [](double start, double end, double normalisedValue) -> double
        {
            if (normalisedValue < 0.5)
            {
                double lowMin = start;
                double lowMax = 2000.0;
                double fraction = normalisedValue / 0.5;
                return lowMin * std::pow(lowMax / lowMin, fraction);
            }
            else
            {
                double highMin = 2000.0;
                double highMax = end;
                double fraction = (normalisedValue - 0.5) / 0.5;
                return highMin + (highMax - highMin) * fraction * fraction;
            }
        };

        // Inverse mapping from frequency to normalised value [0,1]
        auto convertTo0To1 = [](double start, double end, double value) -> double
        {
            if (value < 2000.0)
            {
                double lowMin = start;
                double lowMax = 2000.0;
                return 0.5 * std::log(value / lowMin) / std::log(lowMax / lowMin);
            }
            else
            {
                double highMin = 2000.0;
                double highMax = end;
                double fraction = (value - highMin) / (highMax - highMin);
                return 0.5 + 0.5 * std::sqrt(fraction);
            }
        };

        // Snap function (optional)
        auto snapToLegalValueFunc = [](double start, double end, double value) -> double
        {
            return value; // No snapping needed
        };

        // Create NormalisableRange<double>
        juce::NormalisableRange<double> frequencyRange(
            minFreq,
            maxFreq,
            convertFrom0To1,
            convertTo0To1,
            snapToLegalValueFunc
        );

    // 设置所有滑块
    setupSlider(cutFrequencyFrom1Slider, "CutFrom?1");
    setupSlider(cutFrequencyFrom2Slider, "CutFrom?2");
    setupSlider(frequencyBandLengthSlider, "BandLength?");
    setupSlider(exchangeBandOrNotSlider, "Exchange?");

    setupSlider(band1MixSlider, "Band1Mix?");
    setupSlider(band2MixSlider, "Band1Mix2");
    

    // 连接滑块到处理器参数
    cutFrequencyFrom1Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "cutFrequencyFrom1", cutFrequencyFrom1Slider);
    cutFrequencyFrom2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "cutFrequencyFrom2", cutFrequencyFrom2Slider);
    frequencyBandLengthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "FrequencyBandLength", frequencyBandLengthSlider);
    exchangeBandOrNotAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "ExchangeBandValue?", exchangeBandOrNotSlider);
    

    band1MixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "band1Mix", band1MixSlider);
    band2MixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.parameters, "band2Mix", band2MixSlider);

    // 将滑块添加到编辑器
    addAndMakeVisible(cutFrequencyFrom1Slider);
    cutFrequencyFrom1Slider.setNormalisableRange(frequencyRange);
    cutFrequencyFrom1Slider.setValue(2000.0f); // Default value
    cutFrequencyFrom1Slider.setSkewFactorFromMidPoint(1000.0f); // Optional: Set skew for logarithmic scaling
    cutFrequencyFrom1Slider.textFromValueFunction = [](double value) {
          return juce::String(value, 1) + " Hz";
      };
    addAndMakeVisible(cutFrequencyFrom2Slider);
    cutFrequencyFrom2Slider.setRange(20.0f, 20000.0f); // 最小值、最大值、步长
    cutFrequencyFrom2Slider.setSkewFactorFromMidPoint(1000.0f); // Optional: Set skew for logarithmic scaling
    cutFrequencyFrom2Slider.textFromValueFunction = [](double value) {
          return juce::String(value, 1) + " Hz";
      };
    addAndMakeVisible(frequencyBandLengthSlider);
    frequencyBandLengthSlider.setRange(0.0f, 2.0f); // Min, Max without step size
    frequencyBandLengthSlider.setValue(1.0f); // Set default value to 100%
    frequencyBandLengthSlider.textFromValueFunction = [](double value) {
        return juce::String(value * 100.0, 1) + " %";
    };
    addAndMakeVisible(exchangeBandOrNotSlider);
    exchangeBandOrNotSlider.setRange(0.0f, 1.0f, 1.0f); // 最小值、最大值、步长
    
    addAndMakeVisible(band1MixSlider);
    band1MixSlider.setRange(0.0f, 100.0f, 1.0f);
    band1MixSlider.textFromValueFunction = [](double value) {
        return juce::String(value, 1) + " %";
    };
    addAndMakeVisible(band2MixSlider);
    band2MixSlider.setRange(0.0f, 100.0f, 1.0f);
    band2MixSlider.textFromValueFunction = [](double value) {
        return juce::String(value, 1) + " %";
    };
    
    // 设置标签
    cutFrequencyFrom1Label.setText("Band1Cutfrom", juce::dontSendNotification);
    addAndMakeVisible(cutFrequencyFrom1Label);
    cutFrequencyFrom1Label.attachToComponent(&cutFrequencyFrom1Slider, false); // 将标签附加到滑块上方
    
    cutFrequencyFrom2Label.setText("Band2Cutfrom", juce::dontSendNotification);
    addAndMakeVisible(cutFrequencyFrom2Label);
    cutFrequencyFrom2Label.attachToComponent(&cutFrequencyFrom2Slider, false); // 将标签附加到滑块上方
    
    frequencyBandLengthLabel.setText("CutLength", juce::dontSendNotification);
    addAndMakeVisible(frequencyBandLengthSlider);
    frequencyBandLengthLabel.attachToComponent(&frequencyBandLengthSlider, false); // 将标签附加到滑块上方
    
    exchangeBandOrNotLabel.setText("ExchangeBandOrNot", juce::dontSendNotification);
    addAndMakeVisible(exchangeBandOrNotSlider);
    exchangeBandOrNotLabel.attachToComponent(&exchangeBandOrNotSlider, false); // 将标签附加到滑块上方
    
    
    band1MixLabel.setText("Band1Mix", juce::dontSendNotification);
    band1MixLabel.attachToComponent(&band1MixSlider, false);
    addAndMakeVisible(band1MixLabel);
    band1MixLabel.attachToComponent(&band1MixSlider, false); // 将标签附加到滑块上方
    
    band2MixLabel.setText("Band2Mix", juce::dontSendNotification);
    band2MixLabel.attachToComponent(&band2MixSlider, false);
    addAndMakeVisible(band2MixLabel);
    band2MixLabel.attachToComponent(&band2MixSlider, false); // 将标签附加到滑块上方
    
    sidechainInstructionLabel.setText("Please Set your SideChain in your DAW.", juce::dontSendNotification);
    addAndMakeVisible(sidechainInstructionLabel);
    
    band1Label.setText("Band1", juce::dontSendNotification);
    addAndMakeVisible(band1Label);
    band2Label.setText("Band2", juce::dontSendNotification);
    addAndMakeVisible(band2Label);


}

ExchangeBandAudioProcessorEditor::~ExchangeBandAudioProcessorEditor()
{
}

//==============================================================================


void ExchangeBandAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::pink);
}

void ExchangeBandAudioProcessorEditor::setupSlider(juce::Slider& slider, const juce::String& name)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::Rotary); // 设置为圆形滑块
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    addAndMakeVisible(slider);
}

void ExchangeBandAudioProcessorEditor::resized()
{
    // Set sliders to be round
    cutFrequencyFrom1Slider.setSliderStyle(juce::Slider::Rotary);
    frequencyBandLengthSlider.setSliderStyle(juce::Slider::Rotary);
    band1MixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    cutFrequencyFrom2Slider.setSliderStyle(juce::Slider::Rotary);
    exchangeBandOrNotSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    band2MixSlider.setSliderStyle(juce::Slider::Rotary);

    // Set label text color to black
    cutFrequencyFrom1Label.setColour(juce::Label::textColourId, juce::Colours::black);
    frequencyBandLengthLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    band1MixLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    cutFrequencyFrom2Label.setColour(juce::Label::textColourId, juce::Colours::black);
    exchangeBandOrNotLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    band2MixLabel.setColour(juce::Label::textColourId, juce::Colours::black);

    // Set sidechain instruction label properties
    sidechainInstructionLabel.setText("Please set your SideChain in your DAW.", juce::dontSendNotification);
    sidechainInstructionLabel.setColour(juce::Label::textColourId, juce::Colours::black);
    sidechainInstructionLabel.setColour(juce::Label::outlineColourId, juce::Colours::white); // Set white border

    auto area = getLocalBounds();

    // Define page margins
    int margin = 10; // Page margin
    area.reduce(margin, margin);

    // Calculate the width of the instruction label based on its text
    int labelHeight = 20; // Height of the instruction label
    
    int labelWidth = sidechainInstructionLabel.getFont().getStringWidth(sidechainInstructionLabel.getText()) + margin * 2;
    // Set the width of the label based on the text width
    sidechainInstructionLabel.setBounds(area.removeFromTop(labelHeight).removeFromLeft(labelWidth));

    // Add extra vertical space after the label
    int verticalSpacingAfterLabel = 20; // Increase this value for more spacing
    area.removeFromTop(verticalSpacingAfterLabel);

    // Vertical spacing between rows
    int rowSpacing = 10;

    // Horizontal spacing between sliders
    int columnSpacing = 10;

    // Define label height
    int sliderLabelHeight = 20;

    // Number of rows and columns
    int numRows = 2;
    int numColumns = 3;

    // Calculate row height (including label)
    int totalRowSpacing = rowSpacing * (numRows - 1);
    int rowHeight = (area.getHeight() - totalRowSpacing) / numRows;

    // Effective slider height (excluding label)
    int sliderHeight = rowHeight - sliderLabelHeight;

    // Calculate slider width
    int totalColumnSpacing = columnSpacing * (numColumns - 1);
    int sliderWidth = (area.getWidth() - totalColumnSpacing) / numColumns;

    // First row area
    auto rowArea = area.removeFromTop(rowHeight);

    // Position sliders in first row
    {
        auto sliderArea = rowArea;

        // First slider (cutFrequencyFrom1Slider)
        auto bounds = sliderArea.removeFromLeft(sliderWidth);
        cutFrequencyFrom1Label.setBounds(bounds.removeFromTop(sliderLabelHeight));
        cutFrequencyFrom1Slider.setBounds(bounds);
        sliderArea.removeFromLeft(columnSpacing);

        // Second slider (frequencyBandLengthSlider)
        bounds = sliderArea.removeFromLeft(sliderWidth);
        frequencyBandLengthLabel.setBounds(bounds.removeFromTop(sliderLabelHeight));
        frequencyBandLengthSlider.setBounds(bounds);
        sliderArea.removeFromLeft(columnSpacing);

        // Third slider (band1MixSlider)
        bounds = sliderArea.removeFromLeft(sliderWidth);
        band1MixLabel.setBounds(bounds.removeFromTop(sliderLabelHeight));
        band1MixSlider.setBounds(bounds);
    }

    // Remove row spacing
    area.removeFromTop(rowSpacing);

    // Second row area
    rowArea = area.removeFromTop(rowHeight);

    // Position sliders in second row
    {
        auto sliderArea = rowArea;

        // First slider (cutFrequencyFrom2Slider)
        auto bounds = sliderArea.removeFromLeft(sliderWidth);
        cutFrequencyFrom2Label.setBounds(bounds.removeFromTop(sliderLabelHeight));
        cutFrequencyFrom2Slider.setBounds(bounds);
        sliderArea.removeFromLeft(columnSpacing);

        // Second slider (exchangeBandOrNotSlider)
        bounds = sliderArea.removeFromLeft(sliderWidth);
        exchangeBandOrNotLabel.setBounds(bounds.removeFromTop(sliderLabelHeight));
        exchangeBandOrNotSlider.setBounds(bounds);
        sliderArea.removeFromLeft(columnSpacing);

        // Third slider (band2MixSlider)
        bounds = sliderArea.removeFromLeft(sliderWidth);
        band2MixLabel.setBounds(bounds.removeFromTop(sliderLabelHeight));
        band2MixSlider.setBounds(bounds);
    }
}
