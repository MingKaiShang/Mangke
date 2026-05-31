/**
 * @file AudioCapture.h
 * @brief 基于 WASAPI 的系统音频环回捕获
 *
 * 使用 WASAPI 环回模式捕获系统输出音频（扬声器播放的声音）。
 * 低延迟模式，与视频流同步。
 */

#pragma once

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>

namespace mangke {

/// 音频数据回调（PCM 格式）
using AudioDataCallback = std::function<void(const uint8_t* data, uint32_t size, uint64_t timestampUs)>;

/**
 * @class AudioCapture
 * @brief WASAPI 环回音频捕获
 */
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    /**
     * @brief 初始化音频捕获
     * @param sampleRate 采样率 (默认 44100)
     * @param channels 声道数 (默认 2)
     * @param bitsPerSample 位深 (默认 16)
     */
    bool Initialize(uint32_t sampleRate = 44100, uint16_t channels = 2, uint16_t bitsPerSample = 16);

    /**
     * @brief 设置音频数据回调
     */
    void SetDataCallback(AudioDataCallback callback);

    /**
     * @brief 开始捕获
     */
    bool Start();

    /**
     * @brief 停止捕获
     */
    void Stop();

    /**
     * @brief 获取音频格式信息
     */
    uint32_t GetSampleRate() const { return m_sampleRate; }
    uint16_t GetChannels() const { return m_channels; }
    uint16_t GetBitsPerSample() const { return m_bitsPerSample; }

private:
    void CaptureThreadFunc();

private:
    // WASAPI 接口
    IMMDeviceEnumerator* m_deviceEnumerator = nullptr;
    IMMDevice*           m_device           = nullptr;
    IAudioClient*        m_audioClient      = nullptr;
    IAudioCaptureClient* m_captureClient    = nullptr;

    // 音频格式
    uint32_t m_sampleRate    = 44100;
    uint16_t m_channels      = 2;
    uint16_t m_bitsPerSample = 16;
    uint32_t m_bufferFrames  = 0;

    // 捕获线程
    std::thread       m_captureThread;
    std::atomic<bool> m_isCapturing{false};
    std::atomic<bool> m_shouldStop{false};
    std::atomic<bool> m_initialized{false};

    // 回调
    AudioDataCallback m_dataCallback;
    std::mutex        m_callbackMutex;
};

} // namespace mangke
