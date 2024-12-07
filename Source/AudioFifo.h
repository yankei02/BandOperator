// AudioFifo.h
#pragma once
#include <JuceHeader.h>

class AudioFifo
{
public:
    AudioFifo(int numChannels, int bufferSize)
        : fifo(numChannels), buffer(bufferSize, numChannels)
    {
        buffer.clear();
    }

    // 写入数据到 FIFO
    void write(const float* const* data, int numSamples)  // 修改参数类型
    {
        const int blockSize = numSamples;
        int start1, size1, start2, size2;
        fifo.prepareToWrite(blockSize, start1, size1, start2, size2);

        // 写入第一部分
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.copyFrom(channel, start1, data[channel], size1);
        }

        // 写入第二部分（如果有）
        if (size2 > 0)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                buffer.copyFrom(channel, start2, data[channel] + size1, size2);
            }
        }

        fifo.finishedWrite(size1 + size2);
    }

    // 从 FIFO 中读取数据
    bool read(float* const* data, int numSamples)  // 修改参数类型
    {
        const int blockSize = numSamples;
        int start1, size1, start2, size2;
        fifo.prepareToRead(blockSize, start1, size1, start2, size2);

        if (size1 + size2 < blockSize)
            return false; // 数据不足

        // 读取第一部分
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            buffer.copyFrom(channel, 0, buffer.getReadPointer(channel, start1), size1);
            std::copy(buffer.getReadPointer(channel, start1),
                      buffer.getReadPointer(channel, start1) + size1,
                      data[channel]);
        }

        // 读取第二部分（如果有）
        if (size2 > 0)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                std::copy(buffer.getReadPointer(channel, start2),
                          buffer.getReadPointer(channel, start2) + size2,
                          data[channel] + size1);
            }
        }

        fifo.finishedRead(blockSize);
        return true;
    }

    // 获取可用的样本数
    int getNumSamplesAvailable() const
    {
        return fifo.getNumReady();
    }

private:
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> buffer;
};
