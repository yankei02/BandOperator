/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class ExchangeBandAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                   public juce::Slider::Listener 
{
public:
    ExchangeBandAudioProcessorEditor (ExchangeBandAudioProcessor&);
    ~ExchangeBandAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void timerCallback() ;
    // 检查侧链输入是否激活
    // 实现 ChangeListener 的回调
    void changeListenerCallback(juce::ChangeBroadcaster* source) ;
    void sliderValueChanged(juce::Slider* slider) override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    ExchangeBandAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExchangeBandAudioProcessorEditor)

    // 添加一个标签用于提示用户设置侧链输入
    juce::Label sidechainInstructionLabel;
    
    // 定义九个滑块
    juce::Slider cutFrequencyFrom1Slider; //第一个交换的band的frequency开始值
    juce::Slider cutFrequencyFrom2Slider;//第二个交换的band的frequency开始值
    juce::Slider frequencyBandLengthSlider; //band的frequency长度的值
    juce::Slider exchangeBandOrNotSlider; //两个band是否交换值
    juce::Slider band1MixSlider;
    juce::Slider band2MixSlider;

    //滑块附件
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutFrequencyFrom1Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cutFrequencyFrom2Attachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyBandLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> exchangeBandOrNotAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> band1MixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> band2MixAttachment;

    // 定义标签（可选）
    juce::Label cutFrequencyFrom1Label;
    juce::Label cutFrequencyFrom2Label;
    juce::Label frequencyBandLengthLabel;
    juce::Label exchangeBandOrNotLabel;

    juce::Label band1MixLabel;
    juce::Label band2MixLabel;

    juce::Label band1Label;
    juce::Label band2Label;
    
    // 帮助函数，用于设置滑块
    void setupSlider(juce::Slider& slider, const juce::String& name);
    // 帮助函数，用于设置标签
    void setupLabel(juce::Label& label, const juce::String& text);
    
    // 添加频率边界滑块
    juce::Slider lowFreqMaxSlider;
    juce::Slider midFreqMaxSlider;

    // 添加滑块与参数的附件（Attachments）
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowFreqMaxAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> midFreqMaxAttachment;
    juce::NormalisableRange<float> frequencyRange();
};
