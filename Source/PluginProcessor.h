/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class ExchangeBandAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    ExchangeBandAudioProcessor();
    ~ExchangeBandAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    
    juce::CriticalSection bufferLock;  // 用于保护缓冲区的线程安全
    bool isSidechainInputActive() const;//检查side chain是否激活
    
    // 输入缓冲区
    juce::AudioBuffer<float> inputFifo;
    int inputFifoIndex = 0; // 当前缓冲了多少样本

    // 侧链输入缓冲区
    juce::AudioBuffer<float> sidechainInputFifo;
    int sidechainInputFifoIndex = 0; // 侧链缓冲了多少样本

    // 输出缓冲区
    juce::AudioBuffer<float> outputFifo;
    int outputFifoIndex = 0; // 当前输出缓冲区中可供输出的样本数
    
    float sampleRateOverFftSize = 0.0f;
    
    // 用于存储窗函数数据
    std::vector<float> windowingTable;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> windowFunction;
    
    //FFT相关数据
    static constexpr int fftOrder = 11; // FFT的阶数，2^11 = 2048点FFT
    static constexpr int fftSize = 1 << fftOrder;
    double sampleRate = 0.0;  // 用于存储采样率
    juce::dsp::FFT fft;
    void processFFTBlock();

    // 准备进行处理的数据缓冲区
    std::vector<float> mainchainFFTData;
    std::vector<float> sidechainFFTData;
    
    // 储存fft后幅度和相位信息
    std::vector<float> mainMagnitude;
    std::vector<float> mainPhase;

    std::vector<float> sidechainMagnitude;
    std::vector<float> sidechainPhase;
    
    // 混合后的 FFT 结果-band1
    std::vector<float> mixedMagnitude1;
    std::vector<float> mixedPhase1;

    // 混合后的 FFT 结果-band2
    std::vector<float> mixedMagnitude2;
    std::vector<float> mixedPhase2;
    
    // 用于存储逆 FFT 的数据
    std::vector<float> outputFFTData;
    //std::vector<float> outputMagnitude;
    std::vector<float> fftFrequency; // 频率向量
    // 公共成员以访问ValueTreeState
    juce::AudioProcessorValueTreeState parameters;
    

private:
    //==============================================================================
    //管理音频格式
    juce::AudioFormatManager formatManager;
    // overlap-add buffer
    juce::AudioBuffer<float> overlapAddBuffer;
    int overlapWriteIndex = 0;
    int hopSize = fftSize / 2; // 50% 重叠
    // 保护 FFT 数据的互斥锁
    juce::CriticalSection fftDataLock;

    // 频率向量
    std::vector<float> mainchainFrequency;
    std::vector<float> sidechainFrequency;
    std::vector<float> mixedFrequency;
    
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // 辅助方法
    void performFFT(const juce::AudioBuffer<float>& buffer, std::vector<float>& fftData, std::vector<float>& magnitude, std::vector<float>& phase, bool isMainchain);
    void crossSynthesis();
    void performIFFT(juce::AudioBuffer<float>& buffer);
    
    
    juce::NormalisableRange<float> createFrequencyRange();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExchangeBandAudioProcessor)
};
