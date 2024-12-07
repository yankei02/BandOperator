/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AudioFifo.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <JuceHeader.h>
#include <juce_core/juce_core.h>  // 添加这个头文件以使用 jlimit

//==============================================================================
// PluginProcessor.cpp
ExchangeBandAudioProcessor::ExchangeBandAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput ("Input", juce::AudioChannelSet::stereo(), true)       // 主输入
                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)   // 侧链输入，false代表他不是总线，即为辅助总线。
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),//输出
windowFunction(nullptr),    // 初始化输入缓冲区，假设是立体声和2048点FFT
sampleRate(0.0), // 初始化侧链输入缓冲区
fft(fftOrder),    // 初始化输出缓冲区
fftFrequency(fftSize / 2),           // 初始化采样率
sampleRateOverFftSize(0.0f), // 初始化为默认值
parameters (*this, nullptr, juce::Identifier ("Parameters"),
            {
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cutFrequencyFrom1",1), "CutFrequencyFrom1", createFrequencyRange(), 2000.0f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("cutFrequencyFrom2",1), "CutFrequencyFrom2", createFrequencyRange(), 2000.0f),
    //band可以是中心的
    //slider非线性
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("FrequencyBandLength",1), "FrequencyBandLength",  juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f),//length之于cutofffrequency的比率 q值 比例关系
    //0-200%*cutofffrequency
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("ExchangeBandValue",1), "ExchangeBandValueOrNot", 0.0f, 1.0f, 1.0f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("band1Mix",1), "Band1Mix", 0.0f, 1.0f, 0.01f),
    std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("band2Mix",1), "Band2Mix", 0.0f, 1.0f, 0.01f),
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

    // 直接将侧链输入总线的通道数设置为 2
    getBus(true, 1)->setNumberOfChannels(2);

    formatManager.registerBasicFormats(); // 注册基本音频格式

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
    mainRingBuffer.resize(fftSize, 0.0f);
    sidechainRingBuffer.resize(fftSize, 0.0f);
    // 初始化代码
    sampleRateOverFftSize = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    jassert(mainRingBuffer.size() == fftSize);  // 确保主链环形缓冲区大小一致
    jassert(sidechainRingBuffer.size() == fftSize);  // 确保侧链环形缓冲区大小一致
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
        // 限制输入值在合法范围内
        value = juce::jlimit(start, end, value);

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
    DBG("Create frequency range");
}

void ExchangeBandAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // 获取输入和输出通道数量
    int mainBusNumInputChannels = getBus(true, 0)->getNumberOfChannels(); // 主输入总线的通道数
    int sidechainBusNumInputChannels = getBus(true, 1)->getNumberOfChannels();  // 默认值
    DBG("sidechainBusNumInputChannels: " << sidechainBusNumInputChannels);

    int numOutputChannels = getTotalNumOutputChannels(); // 输出通道的总数
    DBG("prepareToPlay Called");
    DBG("Sample Rate: " << sampleRate);
    DBG("Samples Per Block: " << samplesPerBlock);
    // 检查 FFT 大小和 FIFO 初始化参数
    jassert(fftSize > 0);
    jassert(mainBusNumInputChannels > 0);
    jassert(getBusCount(true) > 0);
    DBG("FIFOs Initialized");
    fft = juce::dsp::FFT (std::log2 (fftSize)); // 初始化 FFT 处理器，使用 FFT 大小的对数值（以 2 为底）

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
    // 如果采样率变化，需要重新初始化 FFT 或缓冲区
    jassert(fftSize == (1 << fftOrder));
    DBG("fftSize Changed");
}

void ExchangeBandAudioProcessor::releaseResources()
{
    // 重置所有缓冲区和处理器
    windowFunction.reset();
    overlapAddBuffer.clear();
    mainRingBuffer.clear();
    sidechainRingBuffer.clear();
    mainMagnitude.clear();
    mainPhase.clear();
    sidechainMagnitude.clear();
    sidechainPhase.clear();
    mixedMagnitude1.clear();
    mixedPhase1.clear();
    mixedMagnitude2.clear();
    mixedPhase2.clear();
    outputFFTData.clear();
    printf("Release Sources");
}


bool ExchangeBandAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    DBG("Main Input Channels: " << layouts.getMainInputChannelSet().size());
    DBG("Main Output Channels: " << layouts.getMainOutputChannelSet().size());
    DBG("Input Buses Count: " << layouts.inputBuses.size());
    // 检查主输出是否为立体声
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    {
        DBG("Main output is not stereo");
        return false;
    }

    // 检查主输入是否为立体声
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
    {
        DBG("Main input is not stereo");
        return false;
    }
    int sidechainBusNumInputChannels = getBus(true, 1)->getNumberOfChannels();  // 默认值
    if (layouts.inputBuses.size() > 1)
    {
        auto sidechainInput = layouts.getChannelSet(true, 1);
        DBG("Sidechain Channels: " << sidechainInput.size());
        DBG("sidechainInput: " << sidechainInput.getDescription());

        // 检查侧链输入是否禁用或为单声道/立体声
        if (!sidechainInput.isDisabled())
        {
            if (sidechainInput != juce::AudioChannelSet::mono() &&
                sidechainInput != juce::AudioChannelSet::stereo())
            {
                DBG("Sidechain input is neither mono nor stereo");
                return false;
            }
        }
        else
        {
            DBG("Sidechain input is disabled");
        }
    }

    // 如果所有检查通过，则支持该布局
    DBG("Layout supported: Mono and Stereo");
    return true;
}


//检查sideChain input是否被激活
bool ExchangeBandAudioProcessor::isSidechainInputActive() const
{
    if (getTotalNumInputChannels() < 1) // 至少需要一个输入总线
    {
        DBG("No SidechainInput.");
    }
        return false;

    const juce::AudioProcessor::Bus* sidechainBus = getBus(true, 1); // 输入总线索引 1 为侧链
    if (sidechainBus)
    {
        int sidechainChannels = sidechainBus->getNumberOfChannels();

        // 如果侧链通道数小于2，则填充为2
        if (sidechainChannels != 2)
        {
            DBG("Sidechain input channel count is not 2, adjusting to stereo.");
            // 在此处执行调整逻辑，强制为 2 通道（通常这是通过音频处理中的填充实现的）
            // 例如，你可以将其信号路由到两个通道，确保它是立体声
            return false; // 或者执行其他行为，取决于你的需求
        }

        // 确保侧链总线已启用且通道数大于0
        return sidechainBus->isEnabled() && sidechainChannels > 0;
    }

    return false;
}



//FFT操作
void ExchangeBandAudioProcessor::performFFT(float* inputData, std::vector<float>& magnitude, std::vector<float>& phase, bool isMainchain)
{
    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全
    // 执行 FFT
    fft.performRealOnlyForwardTransform(inputData);

    // 计算幅度和相位
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        float real = inputData[2 * bin];
        float imag = inputData[2 * bin + 1];
        magnitude[bin] = std::sqrt(real * real + imag * imag);
        phase[bin] = std::atan2(imag, real);
    }
}

void ExchangeBandAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // 这里手动检查布局是否有效
    if (!isBusesLayoutSupported(getBusesLayout()))
    {
        // 如果不支持，可能需要采取其他措施，如发送错误或禁用某些功能
        DBG("Unsupported buses layout!");
    }

    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全
    auto numSamples = buffer.getNumSamples();

    // 确保 FFT 相关缓冲区大小正确
    jassert(mainMagnitude.size() == fftSize / 2 + 1);
    jassert(mainPhase.size() == fftSize / 2 + 1);
    jassert(sidechainMagnitude.size() == fftSize / 2 + 1);
    jassert(sidechainPhase.size() == fftSize / 2 + 1);
    jassert(mixedMagnitude1.size() == fftSize / 2 + 1);
    jassert(mixedPhase1.size() == fftSize / 2 + 1);
    jassert(mixedMagnitude2.size() == fftSize / 2 + 1);
    jassert(mixedPhase2.size() == fftSize / 2 + 1);
    jassert(outputFFTData.size() == 2 * fftSize);
    jassert(overlapAddBuffer.getNumChannels() >= buffer.getNumChannels());
    jassert(overlapAddBuffer.getNumSamples() >= fftSize * 2);
    DBG("fftBufferGreat");
    // 侧链输入激活时，处理主链和侧链
    if (isSidechainInputActive())
    {
        int mainNumChannels = getBus(true, 0)->getNumberOfChannels();
        int sidechainNumChannels = getBus(true, 1)->getNumberOfChannels();
        DBG("Main input channels: " << mainNumChannels);
        DBG("Sidechain input channels: " << sidechainNumChannels);
        
        for (int channel = 0; channel < mainNumChannels; ++channel)
        {
            const float* readPtr = buffer.getReadPointer(channel);
            for (int sample = 0; sample < numSamples; ++sample)
            {
                mainRingBuffer[mainRingBufferWriteIdx] = readPtr[sample];
                mainRingBufferWriteIdx = (mainRingBufferWriteIdx + 1) % fftSize;  // 使用模运算确保写入不越界
                mainSampleCount++;
            }
        }
        DBG("mainRingBufferGet");

        // 将侧链数据写入侧链环形缓冲区
        for (int channel = 0; channel < sidechainNumChannels; ++channel)
        {
            const float* readPtr_1 = buffer.getReadPointer(channel + mainNumChannels);  // 获取侧链通道数据
            for (int sample = 0; sample < numSamples; ++sample)
            {
                sidechainRingBuffer[sidechainRingBufferWriteIdx] = readPtr_1[sample];
                sidechainRingBufferWriteIdx = (sidechainRingBufferWriteIdx + 1) % fftSize;  // 确保不越界
                sidechainSampleCount++;
            }
        }
        DBG("sidechainRingBufferGet");
        jassert(mainRingBufferWriteIdx >= 0 && mainRingBufferWriteIdx < fftSize);
        jassert(sidechainRingBufferWriteIdx >= 0 && sidechainRingBufferWriteIdx < fftSize);

        // 判断是否有足够的数据进行 FFT
        if (mainSampleCount >= fftSize && sidechainSampleCount >= fftSize)
        {
            DBG("Enough data for FFT processing");

            std::vector<float> mainFFTData(fftSize);
            std::vector<float> sidechainFFTData(fftSize);

            for (int i = 0; i < fftSize; ++i)
            {
                int readIdx = (mainRingBufferWriteIdx + i - fftSize) % mainRingBuffer.size();
                if (readIdx < 0)
                    readIdx += mainRingBuffer.size();

                //mainFFTData[i] = mainRingBuffer[readIdx];
                //sidechainFFTData[i] = sidechainRingBuffer[readIdx];
                mainFFTData[i] = mainRingBuffer[(ringBufferReadIdx + i) % fftSize];
                sidechainFFTData[i] = sidechainRingBuffer[(ringBufferReadIdx + i) % fftSize];
            }

            juce::String mainFFTString;
            
            for (int i = 0; i < fftSize; ++i)
            {
                mainFFTString += juce::String(mainFFTData[i], 2) + " "; // 保留2位小数，空格分隔
            }
            DBG("Main FFT Data: " << mainFFTString);

            juce::String sidechainFFTString;
            for (int i = 0; i < fftSize; ++i)
            {
                sidechainFFTString += juce::String(sidechainFFTData[i], 2) + " "; // 保留2位小数，空格分隔
            }
            DBG("Sidechain FFT Data: " << sidechainFFTString);
            jassert(mainFFTData.size() == fftSize);
            jassert(sidechainFFTData.size() == fftSize);
            DBG("mainFFTData size: " << mainFFTData.size());
            DBG("sidechainFFTData size: " << sidechainFFTData.size());

            // 执行主链和侧链的 FFT
            performFFT(mainFFTData.data(), mainMagnitude, mainPhase, true);  // 主链FFT
            performFFT(sidechainFFTData.data(), sidechainMagnitude, sidechainPhase, false);  // 侧链FFT
            DBG("performFFT");
            // 执行交叉合成和 IFFT
            crossSynthesis();
            DBG("crossSynthesis");
            jassert(buffer.getNumSamples() >= fftSize);
            performIFFT(buffer);
            DBG("performIFFT");

            // 更新计数器
            mainSampleCount -= fftSize;
            sidechainSampleCount -= fftSize;
            DBG("updateCounter");

        }
    }
    else
    {
        // 侧链未激活时，直接将输入数据复制到输出
        int mainNumChannels = getBus(true, 0)->getNumberOfChannels();
        for (int channel = 0; channel < mainNumChannels; ++channel)
        {
            buffer.copyFrom(channel, 0, buffer.getReadPointer(channel), numSamples);
        }
        DBG("outputDirecly");
    }
//    else // 如果侧链未激活
//    {
//        int mainNumChannels = getBus(true, 0)->getNumberOfChannels();
//        int sidechainNumChannels = getBus(true, 1)->getNumberOfChannels();
//        // 直接将所有通道的音量设置为 0，输出静音
//        for (int channel = 0; channel < mainNumChannels; ++channel)
//        {
//            float* writePtr = buffer.getWritePointer(channel); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        for (int channel = 0; channel < sidechainNumChannels; ++channel)
//        {
//            float* writePtr_1 = buffer.getWritePointer(channel + mainNumChannels); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr_1[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        DBG("Sidechain not activated: All output muted (volume * 0)");
//    }
    
}
//void ExchangeBandAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
//{
//    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全
//    auto numSamples = buffer.getNumSamples();
//
//    // 获取主链和侧链的通道数量
//    int mainNumChannels = getBus(true, 0)->getNumberOfChannels();
//    int sidechainNumChannels = getBus(true, 1)->getNumberOfChannels();
//
//    if (isSidechainInputActive()) // 如果侧链激活
//    {
//        // 处理主链的环形缓冲区并将其音量设置为0（静音）
//        for (int channel = 0; channel < mainNumChannels; ++channel)
//        {
//            float* writePtr = buffer.getWritePointer(channel); // 获取主链输出通道数据
//
//            // 将主链的音量设置为 0
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        // 处理侧链的环形缓冲区并增大音量
//        for (int channel = 0; channel < sidechainNumChannels; ++channel)
//        {
//            const float* readPtr_1 = buffer.getReadPointer(channel + mainNumChannels);  // 获取侧链通道数据
//            float* writePtr_1 = buffer.getWritePointer(channel + mainNumChannels);      // 获取侧链输出通道数据
//
//            // 对侧链音量进行放大（比如倍增音量）
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr_1[sample] = readPtr_1[sample] * 2.0f;  // 假设增大音量倍数为 2
//            }
//        }
//        DBG("Sidechain activated: Main output (volume * 0), Sidechain output (volume * 2)");
//    }
//    else // 如果侧链未激活
//    {
//        // 直接将所有通道的音量设置为 0，输出静音
//        for (int channel = 0; channel < mainNumChannels; ++channel)
//        {
//            float* writePtr = buffer.getWritePointer(channel); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr[sample] = 0.0f;  // 设置为静音
//            }
//        }
//
//        for (int channel = 0; channel < sidechainNumChannels; ++channel)
//        {
//            float* writePtr_1 = buffer.getWritePointer(channel + mainNumChannels); // 获取输出通道数据
//            for (int sample = 0; sample < numSamples; ++sample)
//            {
//                writePtr_1[sample] = 0.0f;  // 设置为静音
//            }
//        }
//        DBG("Sidechain not activated: All output muted (volume * 0)");
//    }
//}

void ExchangeBandAudioProcessor::crossSynthesis()
{

    // 获取参数值
    float cutFrequencyFrom1 = parameters.getParameterAsValue("cutFrequencyFrom1").getValue();
    float cutFrequencyFrom2 = parameters.getParameterAsValue("cutFrequencyFrom2").getValue();
    float bandLength = parameters.getParameterAsValue("FrequencyBandLength").getValue();
    float exchangeBandValue = parameters.getParameterAsValue("ExchangeBandValue").getValue();
    float band1Mix = parameters.getParameterAsValue("band1Mix").getValue();
    float band2Mix = parameters.getParameterAsValue("band2Mix").getValue();

    // 计算 bandLength 对应的频段宽度
    float bandWidth = juce::jmap(bandLength, 0.0f, 2.0f, 20.0f, 2000.0f); // 可以调整映射范围

    // 确保频段宽度在合理范围内
    bandWidth = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, bandWidth);

    // 确保 cutFrequency 在合理范围内
    cutFrequencyFrom1 = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, cutFrequencyFrom1);
    cutFrequencyFrom2 = juce::jlimit(20.0f, static_cast<float>(sampleRate) / 2.0f, cutFrequencyFrom2);

    // 计算中心 bin 索引
    int centerBin1 = static_cast<int>(cutFrequencyFrom1 * fftSize / static_cast<float>(sampleRate));
    int centerBin2 = static_cast<int>(cutFrequencyFrom2 * fftSize / static_cast<float>(sampleRate));

    // 计算半带宽对应的 bin 数
    int halfBandBins1 = static_cast<int>((bandWidth / 2.0f) * fftSize / static_cast<float>(sampleRate));
    int halfBandBins2 = static_cast<int>((bandWidth / 2.0f) * fftSize / static_cast<float>(sampleRate));

    // 确保半带宽至少为 1
    halfBandBins1 = std::max(1, halfBandBins1);
    halfBandBins2 = std::max(1, halfBandBins2);

    // 计算索引范围
    int startBin1 = juce::jmax(0, centerBin1 - halfBandBins1);
    int endBin1 = juce::jmin(fftSize / 2, centerBin1 + halfBandBins1);

    int startBin2 = juce::jmax(0, centerBin2 - halfBandBins2);
    int endBin2 = juce::jmin(fftSize / 2, centerBin2 + halfBandBins2);

    // 添加断言以确保索引合法
    jassert(startBin1 >= 0 && startBin1 <= fftSize / 2);
    jassert(endBin1 >= 0 && endBin1 <= fftSize / 2);
    jassert(startBin2 >= 0 && startBin2 <= fftSize / 2);
    jassert(endBin2 >= 0 && endBin2 <= fftSize / 2);
    DBG("startBinAndEndBinGreat");
    
    // 初始化混合后的幅度和相位为主链和侧链的原始值
    mixedMagnitude1 = mainMagnitude;
    mixedPhase1 = mainPhase;
    mixedMagnitude2 = sidechainMagnitude;
    mixedPhase2 = sidechainPhase;

    // 在 band1 频段内混合主链和侧链
    for (int i = startBin1; i <= endBin1; ++i)
    {
        try
        {
            // 混合主链和侧链的幅度
            mixedMagnitude1.at(i) = (1.0f - band1Mix) * mainMagnitude.at(i) + band1Mix * sidechainMagnitude.at(i);

            // 混合主链和侧链的相位
            mixedPhase1.at(i) = (1.0f - band1Mix) * mainPhase.at(i) + band1Mix * sidechainPhase.at(i);
        }
        catch (const std::out_of_range& e)
        {
            DBG("Out of range access in band1 loop at index " << i << ": " << e.what());
        }
    }

    // 在 band2 频段内混合主链和侧链
    for (int i = startBin2; i <= endBin2; ++i)
    {
        try
        {
            // 混合主链和侧链的幅度
            mixedMagnitude1.at(i) = (1.0f - band2Mix) * mainMagnitude.at(i) + band2Mix * sidechainMagnitude.at(i);

            // 混合主链和侧链的相位
            mixedPhase1.at(i) = (1.0f - band2Mix) * mainPhase.at(i) + band2Mix * sidechainPhase.at(i);

        }
        catch (const std::out_of_range& e)
        {
            DBG("Out of range access in band2 loop at index " << i << ": " << e.what());
        }
    }

    // 根据 exchangeBandValue 进行更复杂的频段交换
    if (exchangeBandValue != 0.0f)
    {
        // 完全交换 band1 和 band2 的频域数据
        for (int i = startBin1, j = startBin2; i <= endBin1 && j <= endBin2; ++i, ++j)
        {
            try
            {
                std::swap(mixedMagnitude1.at(i), mixedMagnitude2.at(j));
                std::swap(mixedPhase1.at(i), mixedPhase2.at(j));
            }
            catch (const std::out_of_range& e)
            {
                DBG("Out of range access in exchangeBandValue != 0.0f loop at indices (" << i << ", " << j << "): " << e.what());
            }
        }
    }
}


void ExchangeBandAudioProcessor::performIFFT(juce::AudioBuffer<float>& buffer)
{
    std::lock_guard<std::mutex> lock(vectorMutex); // 确保线程安全

    // 为每个通道单独处理
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        // 清空输出频域数据，确保没有垃圾数据
        std::fill(outputFFTData.begin(), outputFFTData.end(), 0.0f);

        // 构建频域数据（实部和虚部）并存储到 outputFFTData
        for (int i = 0; i <= fftSize / 2; ++i)
        {
            int index = 2 * i;
            jassert(index + 1 < outputFFTData.size());

            // 使用 mixMagnitude 和 mixPhase 替代 mainMagnitude 和 mainPhase
            float magnitude = (i < mixedMagnitude1.size()) ? mixedMagnitude1[i] : mainMagnitude[i];  // 用 mixMagnitude 替换 mainMagnitude
            float phase = (i < mixedPhase1.size()) ? mixedPhase1[i] : mainPhase[i];  // 用 mixPhase 替换 mainPhase

            // 计算正频率部分的实部和虚部
            outputFFTData[index]     = magnitude * std::cos(phase);  // 使用修改后的幅度和相位
            outputFFTData[index + 1] = magnitude * std::sin(phase);  // 使用修改后的幅度和相位
        }


        // 处理负频率部分
        for (int i = 1; i < fftSize / 2; ++i)
        {
            int index = 2 * (fftSize - i);
            jassert(index + 1 < outputFFTData.size());

            // 使用 mixMagnitude 和 mixPhase 替代 mainMagnitude 和 mainPhase
            float magnitude = (i < mixedMagnitude1.size()) ? mixedMagnitude1[i] : mainMagnitude[i];  // 用 mixMagnitude 替换 mainMagnitude
            float phase = (i < mixedPhase1.size()) ? mixedPhase1[i] : mainPhase[i];  // 用 mixPhase 替换 mainPhase

            // 计算负频率部分的实部和虚部
            outputFFTData[index]     = magnitude * std::cos(-phase);  // 使用修改后的幅度和相位
            outputFFTData[index + 1] = magnitude * std::sin(-phase);  // 使用修改后的幅度和相位
        }


        // 确保 DC 和 Nyquist 频率的虚部为 0
        outputFFTData[1] = 0.0f; // DC 虚部
        outputFFTData[2 * (fftSize / 2) + 1] = 0.0f; // Nyquist 虚部

        // 手动对虚部取负以实现共轭
        for (int i = 0; i < 2 * fftSize; i += 2)
        {
            outputFFTData[i + 1] = -outputFFTData[i + 1];
        }

        // 执行逆FFT
        fft.performRealOnlyForwardTransform(outputFFTData.data());

        // 再次手动对虚部取负以完成逆 FFT
        for (int i = 0; i < 2 * fftSize; i += 2)
        {
            outputFFTData[i + 1] = -outputFFTData[i + 1];
        }

        // 归一化逆 FFT 结果
        for (int i = 0; i < fftSize; ++i)
        {
            outputFFTData[i] /= fftSize;
        }

        // 将逆 FFT 结果拷贝到重叠相加缓冲区
        overlapAddBuffer.addFrom(channel, overlapWriteIndex, outputFFTData.data(), fftSize);
    }

    // 将重叠相加缓冲区的数据写回输出缓冲区
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        float* outputBufferData = buffer.getWritePointer(channel);
        const float* overlapData = overlapAddBuffer.getReadPointer(channel, overlapWriteIndex);

        for (int sample = 0; sample < fftSize; ++sample)
        {
            if (sample < overlapAddBuffer.getNumSamples())
                outputBufferData[sample] += overlapData[sample];
            else
                DBG("Index out of range in output buffer loop at sample " << sample);
        }
    }

    // 更新重叠写入索引
    overlapWriteIndex = (overlapWriteIndex + fftSize) % (2 * fftSize);
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
    // 使用 AudioProcessorValueTreeState 的 copyState 方法复制当前状态
    juce::ValueTree state = parameters.copyState();
    
    // 将 ValueTree 转换为 XML
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    
    // 如果 XML 有效，写入到内存块
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void ExchangeBandAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // 从二进制数据中解析出 XML 元素
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    
    if (xmlState != nullptr)
    {
        // 确保 XML 标签名与当前状态的类型匹配
        if (xmlState->hasTagName (parameters.state.getType()))
        {
            // 从 XML 中恢复状态
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ExchangeBandAudioProcessor();
} 
