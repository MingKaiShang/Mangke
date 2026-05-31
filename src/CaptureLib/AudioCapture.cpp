#include "AudioCapture.h"
#include <iostream>
#include <chrono>

#pragma comment(lib, "ole32.lib")

namespace mangke {

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() { Stop(); }

bool AudioCapture::Initialize(uint32_t sr, uint16_t ch, uint16_t bps) {
    m_sampleRate = sr; m_channels = ch; m_bitsPerSample = bps;
    return true;
}

void AudioCapture::SetDataCallback(AudioDataCallback cb) {
    std::lock_guard lock(m_callbackMutex);
    m_dataCallback = std::move(cb);
}

bool AudioCapture::Start() {
    if (m_isCapturing.load()) return false;
    m_shouldStop.store(false);
    m_isCapturing.store(true);
    m_captureThread = std::thread(&AudioCapture::CaptureThreadFunc, this);
    // 非阻塞：不等待初始化完成，避免卡 IPC 线程
    return true;
}

void AudioCapture::Stop() {
    m_shouldStop.store(true);
    if (m_captureClient) { m_captureClient->Release(); m_captureClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_deviceEnumerator) { m_deviceEnumerator->Release(); m_deviceEnumerator = nullptr; }
    if (m_captureThread.joinable()) m_captureThread.join();
    m_isCapturing.store(false);
    m_initialized.store(false);
}

void AudioCapture::CaptureThreadFunc() {
    bool comOk = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&enumerator)))
        { if(comOk) CoUninitialize(); return; }

    IMMDevice* device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
        { enumerator->Release(); if(comOk) CoUninitialize(); return; }

    IAudioClient* audioClient = nullptr;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient)))
        { device->Release(); enumerator->Release(); if(comOk) CoUninitialize(); return; }

    WAVEFORMATEX* mixFormat = nullptr;
    if (FAILED(audioClient->GetMixFormat(&mixFormat)))
        { audioClient->Release(); device->Release(); enumerator->Release(); if(comOk) CoUninitialize(); return; }

    HRESULT hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                          100000, 0, mixFormat, nullptr);
    if (FAILED(hr)) {
        std::cerr << "[Audio] Init failed: 0x" << std::hex << hr << std::dec << "\n";
        CoTaskMemFree(mixFormat); audioClient->Release(); device->Release();
        enumerator->Release(); if(comOk) CoUninitialize(); return;
    }

    m_sampleRate = mixFormat->nSamplesPerSec;
    m_channels = mixFormat->nChannels;
    m_bitsPerSample = mixFormat->wBitsPerSample;
    CoTaskMemFree(mixFormat);

    IAudioCaptureClient* captureClient = nullptr;
    if (FAILED(audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient)))
        { audioClient->Release(); device->Release(); enumerator->Release(); if(comOk) CoUninitialize(); return; }

    m_deviceEnumerator = enumerator;
    m_device = device;
    m_audioClient = audioClient;
    m_captureClient = captureClient;

    if (FAILED(audioClient->Start()))
        { std::cerr << "[Audio] Start failed\n"; if(comOk) CoUninitialize(); return; }

    m_initialized.store(true);

    while (!m_shouldStop.load()) {
        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr) || packetLength == 0) { Sleep(1); continue; }
        while (packetLength > 0 && !m_shouldStop.load()) {
            BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0; UINT64 pos = 0;
            if (SUCCEEDED(captureClient->GetBuffer(&data, &frames, &flags, &pos, nullptr)) && data && frames > 0) {
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    uint32_t dataSize = frames * m_channels * (m_bitsPerSample / 8);
                    auto now = std::chrono::high_resolution_clock::now();
                    uint64_t ts = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
                    std::lock_guard lock(m_callbackMutex);
                    if (m_dataCallback) m_dataCallback(data, dataSize, ts);
                }
                captureClient->ReleaseBuffer(frames);
            }
            if (FAILED(captureClient->GetNextPacketSize(&packetLength))) break;
        }
    }

    audioClient->Stop();
    if(comOk) CoUninitialize();
}

} // namespace mangke
