/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ExchangeBandAudioProcessor::ExchangeBandAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput ("Input", juce::AudioChannelSet::stereo(), true)       // 主输入
                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)   // 侧链输入，false代表他不是总线，即为辅助总线。
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),//输出
inputFifo(2, fftSize),
sidechainInputFifo(2, fftSize),
outputFifo(2, fftSize),
windowFunction(nullptr),    // 初始化输入缓冲区，假设是立体声和2048点FFT
sampleRate(0.0), // 初始化侧链输入缓冲区
fft(fftOrder),    // 初始化输出缓冲区
fftFrequency(fftSize / 2),           // 初始化采样率
parameters (*this, nullptr, juce::Identifier ("Parameters"),
            {
    std::make_unique<juce::AudioParameterFloat>("cutFrequencyFrom1", "CutFrequencyFrom1", createFrequencyRange(), 2000.0f),
    std::make_unique<juce::AudioParameterFloat>("cutFrequencyFrom2", "CutFrequencyFrom2", createFrequencyRange(), 2000.0f),
    //band可以是中心的
    //slider非线性
    std::make_unique<juce::AudioParameterFloat>("FrequencyBandLength", "FrequencyBandLength",  juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f),//length之于cutofffrequency的比率 q值 比例关系
    //0-200%*cutofffrequency
    std::make_unique<juce::AudioParameterFloat>("ExchangeBandValue?", "ExchangeBandValueOrNot", 0.0f, 1.0f, 1.0f),
    std::make_unique<juce::AudioParameterFloat>("band1Mix", "Band1Mix", 0.0f, 1.0f, 0.01f),
    std::make_unique<juce::AudioParameterFloat>("band2Mix", "Band2Mix", 0.0f, 1.0f, 0.01f),
}),  // 假设频率数据只需要一半
      formatManager(),
      overlapAddBuffer(2, fftSize),  // overlap-add buffer，假设是立体声
      mainchainFrequency(fftSize / 2),
      sidechainFrequency(fftSize / 2),
      mixedFrequency(fftSize / 2)
{
    // 设置默认总线布局
    BusesLayout defaultLayout;
    defaultLayout.inputBuses.add(juce::AudioChannelSet::stereo());
    defaultLayout.inputBuses.add(juce::AudioChannelSet::stereo()); // 侧链输入
    defaultLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    formatManager.registerBasicFormats(); // 注册基本音频格式
    // 初始化 buffer1 和 buffer2 的大小

    // 初始化FFT数据缓冲区
    mainchainFFTData.resize(fftSize, 0.0f);
    sidechainFFTData.resize(fftSize, 0.0f);

    // 初始化频率、幅度和相位缓冲区
    mainMagnitude.resize(fftSize / 2 + 1);
    mainPhase.resize(fftSize / 2 + 1);

    sidechainMagnitude.resize(fftSize / 2 + 1);
    sidechainPhase.resize(fftSize / 2 + 1);
    
    mixedMagnitude1.resize(fftSize / 2 + 1);
    mixedPhase1.resize(fftSize / 2 + 1);
    
    mixedMagnitude2.resize(fftSize / 2 + 1);
    mixedPhase2.resize(fftSize / 2 + 1);

    outputFFTData.resize(fftSize, 0.0f); // 逆 FFT 存储 fftSize 个元素
}

ExchangeBandAudioProcessor::~ExchangeBandAudioProcessor()
{
}

//==============================================================================
const juce::String ExchangeBandAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ExchangeBandAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ExchangeBandAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ExchangeBandAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ExchangeBandAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ExchangeBandAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ExchangeBandAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ExchangeBandAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String ExchangeBandAudioProcessor::getProgramName (int index)
{
    return {};
}

void ExchangeBandAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
juce::NormalisableRange<float> ExchangeBandAudioProcessor::createFrequencyRange()
{
    // 定义频率范围的最小值和最大值
    float minFreq = 20.0f;       // 最小频率为 20Hz
    float maxFreq = 20000.0f;    // 最大频率为 20000Hz

    // 自定义的从 [0,1] 到实际频率的映射函数
    auto convertFrom0To1 = [](float start, float end, float normalisedValue) -> float
    {
        if (normalisedValue < 0.5f)
        {
            // 低频段（start 到 2000Hz），使用指数映射
            float lowMin = start;             // 低频段的最小值
            float lowMax = 2000.0f;           // 低频段的最大值
            float fraction = normalisedValue / 0.5f;  // 将 normalisedValue 归一化到 [0,1]
            return lowMin * std::pow(lowMax / lowMin, fraction);  // 指数映射
        }
        else
        {
            // 高频段（2000Hz 到 end），使用平方映射
            float highMin = 2000.0f;          // 高频段的最小值
            float highMax = end;              // 高频段的最大值
            float fraction = (normalisedValue - 0.5f) / 0.5f;  // 将 normalisedValue 归一化到 [0,1]
            return highMin + (highMax - highMin) * fraction * fraction;  // 平方映射，加速高频段
        }
    };

    // 自定义的从实际频率到 [0,1] 的逆映射函数
    auto convertTo0To1 = [](float start, float end, float value) -> float
    {
        if (value < 2000.0f)
        {
            // 低频段，使用对数逆映射
            float lowMin = start;             // 低频段的最小值
            float lowMax = 2000.0f;           // 低频段的最大值
            return 0.5f * std::log(value / lowMin) / std::log(lowMax / lowMin);  // 对数逆映射
        }
        else
        {
            // 高频段，使用平方根逆映射
            float highMin = 2000.0f;          // 高频段的最小值
            float highMax = end;              // 高频段的最大值
            float fraction = (value - highMin) / (highMax - highMin);  // 归一化到 [0,1]
            return 0.5f + 0.5f * std::sqrt(fraction);  // 平方根逆映射
        }
    };

    // 定义 snapToLegalValueFunc 函数，用于将值捕捉到合法范围
    auto snapToLegalValueFunc = [](float start, float end, float value) -> float
    {
        // 不对值进行修改，直接返回原值
        return value;
    };

    // 创建 NormalisableRange 对象，包含自定义的映射函数和捕捉函数
    return juce::NormalisableRange<float>(
        minFreq,                   // 起始值 20Hz
        maxFreq,                   // 结束值 20000Hz
        convertFrom0To1,           // 自定义的从 [0,1] 到实际频率的映射函数
        convertTo0To1,             // 自定义的从实际频率到 [0,1] 的逆映射函数
        snapToLegalValueFunc       // 捕捉函数，直接返回原值
    );
}



void ExchangeBandAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 获取输入和输出通道数量
    int mainBusNumInputChannels = getBus(true, 0)->getNumberOfChannels(); // 主输入总线的通道数
    int sidechainBusNumInputChannels = isSidechainInputActive() ? getBus(true, 1)->getNumberOfChannels() : 0; // 侧链输入总线的通道数，如果侧链激活
    int numOutputChannels = getTotalNumOutputChannels(); // 输出通道的总数

    fft = juce::dsp::FFT (std::log2 (fftSize)); // 初始化 FFT 处理器，使用 FFT 大小的对数值（以 2 为底）

    // 初始化输入缓冲区
    inputFifo.setSize(mainBusNumInputChannels, fftSize); // 设置输入缓冲区大小为主输入通道数和 FFT 大小
    inputFifo.clear(); // 清空输入缓冲区
    inputFifoIndex = 0; // 初始化输入缓冲区索引为 0

    // 初始化侧链输入缓冲区
    sidechainInputFifo.setSize(sidechainBusNumInputChannels, fftSize);
    sidechainInputFifo.clear();
    sidechainInputFifoIndex = 0;
    
    // 初始化输出缓冲区
    outputFifo.setSize(numOutputChannels, fftSize * 2);
    outputFifo.clear();
    outputFifoIndex = 0;

    overlapAddBuffer.setSize (getTotalNumOutputChannels(), fftSize * 2); // 设置存储重叠部分的缓冲区大小
    overlapAddBuffer.clear(); // 清空重叠缓冲区
    overlapWriteIndex = 0; // 初始化重叠缓冲区写入索引为 0

    // 初始化 FFT 相关的缓冲区
    mainchainFFTData.resize(fftSize * 2, 0.0f);
    mainMagnitude.resize (fftSize / 2 + 1, 0.0f);
    mainPhase.resize(fftSize / 2 + 1);
    sidechainFFTData.resize(fftSize * 2, 0.0f);
    sidechainMagnitude.resize (fftSize / 2 + 1, 0.0f);
    sidechainPhase.resize(fftSize / 2 + 1);
    mixedMagnitude1.resize(fftSize / 2 + 1);
    mixedPhase1.resize(fftSize / 2 + 1);
    mixedMagnitude2.resize(fftSize / 2 + 1);
    mixedPhase2.resize(fftSize / 2 + 1);
    
    outputFFTData.resize(fftSize * 2, 0.0f);
    // 初始化 windowFunction 窗函数
    windowFunction = std::make_unique<juce::dsp::WindowingFunction<float>>(fftSize, juce::dsp::WindowingFunction<float>::hann); // 使用 Hann 窗函数初始化

    this->sampleRate = sampleRate; // 存储采样率
    // 预计算值
    sampleRateOverFftSize = static_cast<float> (sampleRate) / static_cast<float> (fftSize); // 计算采样率除以 FFT 大小的值
    
    // 计算频率向量
    mainchainFrequency.resize(fftSize / 2 + 1);
    sidechainFrequency.resize(fftSize / 2 + 1);
    mixedFrequency.resize(fftSize / 2 + 1);

    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        float freq = bin * sampleRateOverFftSize; // 计算每个 bin 的频率值
        mainchainFrequency[bin] = freq; // 存储到主链频率缓冲区
        sidechainFrequency[bin] = freq; // 存储到侧链频率缓冲区
        mixedFrequency[bin] = freq; // 存储到混合频率缓冲区
    }
}


void ExchangeBandAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

//插件是否支持stereo/mono
bool ExchangeBandAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    DBG("Main Input Channels: " << layouts.getMainInputChannelSet().size());
    DBG("Main Output Channels: " << layouts.getMainOutputChannelSet().size());

    // 检查主输出是否为立体声
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // 检查主输入是否与主输出匹配
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // 检查是否存在侧链输入
    if (layouts.inputBuses.size() > 1)
    {
        auto sidechainInput = layouts.getChannelSet(true, 1);
        DBG("Sidechain Channels: " << sidechainInput.size());

        if (!sidechainInput.isDisabled() &&
            sidechainInput != juce::AudioChannelSet::mono() &&
            sidechainInput != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//检查sideChain input是否被激活
bool ExchangeBandAudioProcessor::isSidechainInputActive() const
{
    if (const juce::AudioProcessor::Bus* sidechainBus = getBus(true, 1)) // 输入总线索引 1 为侧链
    {
        return sidechainBus->isEnabled() && sidechainBus->getNumberOfChannels() > 0;
    }
    return false;
}

//FFT操作
void ExchangeBandAudioProcessor::performFFT(const juce::AudioBuffer<float>& buffer, std::vector<float>& fftData, std::vector<float>& magnitude, std::vector<float>& phase, bool isMainchain)
{
    // 获取输入通道数据
    const float* channelData = buffer.getReadPointer(isMainchain ? 0 : 1);

    // 拷贝数据到 fftData 并应用窗口函数
    std::copy(channelData, channelData + fftSize, fftData.begin());
    windowFunction->multiplyWithWindowingTable(fftData.data(), fftSize);

    // 执行 FFT
    fft.performRealOnlyForwardTransform(fftData.data());

    // 计算幅度和相位
    for (int i = 0; i < fftSize / 2 + 1; ++i)
    {
        float real = fftData[2 * i];
        float imag = fftData[2 * i + 1];
        magnitude[i] = std::sqrt(real * real + imag * imag);
        phase[i] = std::atan2(imag, real);
    }
}

void ExchangeBandAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // 清除未使用的输出通道
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // 检查侧链是否激活
    if (!isSidechainInputActive())
    {
        // 如果侧链未激活，直接将主输入复制到输出
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.copyFrom(channel, 0, buffer.getReadPointer(channel), buffer.getNumSamples());
        }
        return; // 跳过后续处理
    }

    // 对主输入和侧链输入进行 FFT 处理
    performFFT(buffer, mainchainFFTData, mainMagnitude, mainPhase, /* isMainchain = */ true);
    performFFT(buffer, sidechainFFTData, sidechainMagnitude, sidechainPhase, /* isMainchain = */ false);

    // 交叉合成逻辑
    crossSynthesis();

    // 对处理后的数据进行逆 FFT
    performIFFT(buffer);
}


//cross-synthesis part
void ExchangeBandAudioProcessor::crossSynthesis()
{
    if (!isSidechainInputActive())
        {
            // 侧链未激活，无需处理
            return;
        }
    float cutFrequencyFrom1 = parameters.getParameterAsValue("cutFrequencyFrom1").getValue();
    float cutFrequencyFrom2 = parameters.getParameterAsValue("cutFrequencyFrom2").getValue();
    float bandLength = parameters.getParameterAsValue("FrequencyBandLength").getValue();
    float exchangeBandValue = parameters.getParameterAsValue("ExchangeBandValue").getValue();
    float band1Mix = parameters.getParameterAsValue("band1Mix").getValue();
    float band2Mix = parameters.getParameterAsValue("band2Mix").getValue();
    // 计算实际的 band length
    float bandLength1 = cutFrequencyFrom1 * bandLength;
    float bandLength2 = cutFrequencyFrom2 * bandLength;
    
    // 确保 band length 在合理范围内
    float minBandLength = 20.0f;
    float maxBandLength = sampleRate / 2.0f;
    bandLength1 = juce::jlimit(minBandLength, maxBandLength, bandLength1);
    bandLength2 = juce::jlimit(minBandLength, maxBandLength, bandLength2);
    //make sure that sampleRate and fftsize are not zero
    if (sampleRate == 0 || fftSize == 0)
    {
        // 处理错误，例如记录日志或设置默认值
        return;
    }

    // 计算中心 bin 索引
    int centerBin1 = static_cast<int>(cutFrequencyFrom1 / sampleRate * fftSize);
    int centerBin2 = static_cast<int>(cutFrequencyFrom2 / sampleRate * fftSize);

    // 计算半带宽对应的 bin 数
    int halfBandBins1 = static_cast<int>((bandLength1 / 2.0f) / sampleRate * fftSize);
    int halfBandBins2 = static_cast<int>((bandLength2 / 2.0f) / sampleRate * fftSize);

    // 确保索引在有效范围内
    int startBin1 = juce::jmax(0, centerBin1 - halfBandBins1);
    int endBin1 = juce::jmin(fftSize / 2, centerBin1 + halfBandBins1);

    int startBin2 = juce::jmax(0, centerBin2 - halfBandBins2);
    int endBin2 = juce::jmin(fftSize / 2, centerBin2 + halfBandBins2);

    if (exchangeBandValue == 0.0f)
    {
        // 根据 mix 值进行部分交换
        for (int i = startBin1; i <= endBin1; ++i)
        {
            // 插值：根据 mix 值控制交换的比例
            mainMagnitude[i] = mainMagnitude[i] * (1.0f - band1Mix) + sidechainMagnitude[i] * band1Mix;
            mainPhase[i] = mainPhase[i] * (1.0f - band1Mix) + sidechainPhase[i] * band1Mix;

            sidechainMagnitude[i] = sidechainMagnitude[i] * (1.0f - band1Mix) + mainMagnitude[i] * band1Mix;
            sidechainPhase[i] = sidechainPhase[i] * (1.0f - band1Mix) + mainPhase[i] * band1Mix;
        }

        for (int i = startBin2; i <= endBin2; ++i)
        {
            // 插值：根据 mix 值控制交换的比例
            mainMagnitude[i] = mainMagnitude[i] * (1.0f - band2Mix) + sidechainMagnitude[i] * band2Mix;
            mainPhase[i] = mainPhase[i] * (1.0f - band2Mix) + sidechainPhase[i] * band2Mix;

            sidechainMagnitude[i] = sidechainMagnitude[i] * (1.0f - band2Mix) + mainMagnitude[i] * band2Mix;
            sidechainPhase[i] = sidechainPhase[i] * (1.0f - band2Mix) + mainPhase[i] * band2Mix;
        }
    }
    else
    {
        // 交换指定频带的幅度、相位和频率，带有 mix 插值
        for (int i = startBin1, j = startBin2; i <= endBin1 && j <= endBin2; ++i, ++j)
        {
            // 插值：根据 mix 值控制交换的比例
            mainMagnitude[i] = mainMagnitude[i] * (1.0f - band1Mix) + sidechainMagnitude[j] * band1Mix;
            mainPhase[i] = mainPhase[i] * (1.0f - band1Mix) + sidechainPhase[j] * band1Mix;

            sidechainMagnitude[j] = sidechainMagnitude[j] * (1.0f - band2Mix) + mainMagnitude[i] * band2Mix;
            sidechainPhase[j] = sidechainPhase[j] * (1.0f - band2Mix) + mainPhase[i] * band2Mix;
        }
    }
}

void ExchangeBandAudioProcessor::performIFFT(juce::AudioBuffer<float>& buffer)
{
    // 构建逆 FFT 数据
    for (int i = 0; i < fftSize / 2 + 1; ++i)
    {
        outputFFTData[2 * i] = mainMagnitude[i] * std::cos(mainPhase[i]);
        outputFFTData[2 * i + 1] = mainMagnitude[i] * std::sin(mainPhase[i]);
    }

    // 执行逆 FFT
    fft.performRealOnlyInverseTransform(outputFFTData.data());

    // 归一化逆 FFT 结果
    for (int i = 0; i < fftSize; ++i)
    {
        outputFFTData[i] /= fftSize;
    }

    // 应用窗口函数
    windowFunction->multiplyWithWindowingTable(outputFFTData.data(), fftSize);

    // 将逆 FFT 结果拷贝到输出缓冲区
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);
        std::copy(outputFFTData.begin(), outputFFTData.begin() + fftSize, channelData);
    }
}


//==============================================================================
bool ExchangeBandAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ExchangeBandAudioProcessor::createEditor()
{
    return new ExchangeBandAudioProcessorEditor (*this);
}

//==============================================================================
void ExchangeBandAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void ExchangeBandAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExchangeBandAudioProcessor();
}
