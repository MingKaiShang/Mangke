/**
 * @file VideoEncoder.cpp
 * @brief Media Foundation H.264 encoder with frame-counter PTS
 */

#include "VideoEncoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <iostream>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

namespace mangke {

static bool s_mfInitialized = false;

VideoEncoder::VideoEncoder() = default;

VideoEncoder::~VideoEncoder() {
    if (m_state.load() == EncoderState::Encoding) Finish();
    if (m_sinkWriter) { m_sinkWriter->Release(); m_sinkWriter = nullptr; }
}

bool VideoEncoder::InitializeMF() {
    if (s_mfInitialized) return true;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "[Encoder] MF init failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }
    s_mfInitialized = true;
    return true;
}

void VideoEncoder::ShutdownMF() {
    if (s_mfInitialized) { MFShutdown(); s_mfInitialized = false; }
}

bool VideoEncoder::Configure(const EncoderConfig& config) {
    std::lock_guard lock(m_mutex);
    if (m_state.load() == EncoderState::Encoding) return false;
    m_config = config;
    if (m_config.outputPath.empty()) return false;
    if (!s_mfInitialized && !InitializeMF()) return false;


    m_frameDuration = 10'000'000 / m_config.fps;

    std::wcout << L"[Encoder] Config: " << m_config.outputPath
               << L" (" << m_config.width << L"x" << m_config.height
               << L" @ " << m_config.fps << L"fps, " << m_config.bitrateKbps << L"kbps)\n";
    return true;
}

bool VideoEncoder::Start() {
    std::lock_guard lock(m_mutex);
    if (m_state.load() == EncoderState::Encoding) return false;

    // Create Sink Writer
    HRESULT hr = MFCreateSinkWriterFromURL(
        m_config.outputPath.c_str(), nullptr, nullptr, &m_sinkWriter);
    if (FAILED(hr)) {
        std::cerr << "[Encoder] SinkWriter creation failed: 0x" << std::hex << hr << std::dec << "\n";
        m_state.store(EncoderState::Error);
        return false;
    }

    // Video output type (H.264 compressed)
    IMFMediaType* videoOutType = nullptr;
    MFCreateMediaType(&videoOutType);
    videoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    videoOutType->SetUINT32(MF_MT_AVG_BITRATE, m_config.bitrateKbps * 1000);
    videoOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(videoOutType, MF_MT_FRAME_SIZE, m_config.width, m_config.height);
    MFSetAttributeRatio(videoOutType, MF_MT_FRAME_RATE, m_config.fps, 1);
    MFSetAttributeRatio(videoOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = m_sinkWriter->AddStream(videoOutType, &m_videoStreamIndex);
    videoOutType->Release();
    if (FAILED(hr)) {
        std::cerr << "[Encoder] AddStream failed: 0x" << std::hex << hr << std::dec << "\n";
        m_state.store(EncoderState::Error);
        return false;
    }

    // Video input type (uncompressed BGRA)
    IMFMediaType* videoInType = nullptr;
    MFCreateMediaType(&videoInType);
    videoInType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    videoInType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    videoInType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(videoInType, MF_MT_FRAME_SIZE, m_config.width, m_config.height);
    MFSetAttributeRatio(videoInType, MF_MT_FRAME_RATE, m_config.fps, 1);
    videoInType->SetUINT32(MF_MT_DEFAULT_STRIDE, m_config.width * 4);

    hr = m_sinkWriter->SetInputMediaType(m_videoStreamIndex, videoInType, nullptr);
    videoInType->Release();
    if (FAILED(hr)) {
        std::cerr << "[Encoder] SetInputMediaType failed: 0x" << std::hex << hr << std::dec
                  << "\n  (check if MF H.264 codec is installed)\n";
        m_state.store(EncoderState::Error);
        return false;
    }

    hr = m_sinkWriter->BeginWriting();
    if (FAILED(hr)) {
        std::cerr << "[Encoder] BeginWriting failed: 0x" << std::hex << hr << std::dec << "\n";
        m_state.store(EncoderState::Error);
        return false;
    }

    m_state.store(EncoderState::Encoding);
    m_encodedFrames.store(0);
    std::cout << "[Encoder] Encoding started (frameDuration=" << m_frameDuration << " * 100ns)\n";
    return true;
}

bool VideoEncoder::EncodeVideoFrame(ID3D11Texture2D* texture, uint64_t frameIndex) {
    if (m_state.load() != EncoderState::Encoding || !texture) return false;

    // Lazy-init D3D11 context and staging texture
    if (!m_d3dDevice) {
        texture->GetDevice(&m_d3dDevice);
        m_d3dDevice->GetImmediateContext(&m_d3dContext);

        D3D11_TEXTURE2D_DESC srcDesc;
        texture->GetDesc(&srcDesc);

        D3D11_TEXTURE2D_DESC stagingDesc = srcDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = m_d3dDevice->CreateTexture2D(&stagingDesc, nullptr, &m_stagingTex);
        if (FAILED(hr)) {
            std::cerr << "[Encoder] Staging texture creation failed: 0x" << std::hex << hr << std::dec << "\n";
            return false;
        }
        std::cout << "[Encoder] Staging texture: " << srcDesc.Width << "x" << srcDesc.Height << "\n";
    }

    // GPU copy to staging
    m_d3dContext->CopyResource(m_stagingTex.Get(), texture);

    // Map staging texture (CPU read)
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_d3dContext->Map(m_stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        if (frameIndex < 5) std::cerr << "[Encoder] Map failed on frame " << frameIndex << "\n";
        return false;
    }

    // Create MF sample
    IMFMediaBuffer* buffer = nullptr;
    DWORD bufSize = m_config.width * m_config.height * 4;
    hr = MFCreateMemoryBuffer(bufSize, &buffer);
    if (FAILED(hr)) {
        m_d3dContext->Unmap(m_stagingTex.Get(), 0);
        return false;
    }

    BYTE* bufData = nullptr;
    hr = buffer->Lock(&bufData, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        for (uint32_t row = 0; row < m_config.height; ++row) {
            memcpy(bufData + row * m_config.width * 4,
                   static_cast<BYTE*>(mapped.pData) + row * mapped.RowPitch,
                   m_config.width * 4);
        }
        buffer->Unlock();
    }
    m_d3dContext->Unmap(m_stagingTex.Get(), 0);
    buffer->SetCurrentLength(bufSize);

    IMFSample* sample = nullptr;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer);

    // Frame-counter PTS (guaranteed consistent timing)
    LONGLONG pts = static_cast<LONGLONG>(frameIndex) * m_frameDuration;
    sample->SetSampleTime(pts);
    sample->SetSampleDuration(m_frameDuration);

    hr = m_sinkWriter->WriteSample(m_videoStreamIndex, sample);

    sample->Release();
    buffer->Release();

    if (SUCCEEDED(hr)) {
        m_encodedFrames.fetch_add(1);
    } else if (frameIndex < 5) {
        std::cerr << "[Encoder] WriteSample failed on frame " << frameIndex
                  << ": 0x" << std::hex << hr << std::dec << "\n";
    }

    return SUCCEEDED(hr);
}

bool VideoEncoder::EncodeAudioData(const uint8_t* data, uint32_t size, uint64_t timestampUs) {
    // Audio disabled in MVP
    return true;
}

bool VideoEncoder::Finish() {
    std::lock_guard lock(m_mutex);
    if (m_state.load() != EncoderState::Encoding) return false;

    m_state.store(EncoderState::Finishing);

    HRESULT hr = S_OK;
    if (m_sinkWriter) {
        hr = m_sinkWriter->Finalize();
        if (FAILED(hr)) {
            std::cerr << "[Encoder] Finalize failed: 0x" << std::hex << hr << std::dec << "\n";
        }
        m_sinkWriter->Release();
        m_sinkWriter = nullptr;
    }

    m_state.store(EncoderState::Idle);
    std::cout << "[Encoder] Finished: " << m_encodedFrames.load() << " frames, "
              << (GetOutputFileSize() / 1024) << " KB\n";
    return SUCCEEDED(hr);
}

uint64_t VideoEncoder::GetOutputFileSize() const {
    if (m_config.outputPath.empty()) return 0;
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(m_config.outputPath.c_str(), GetFileExInfoStandard, &data)) {
        return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    return 0;
}

bool VideoEncoder::CreateVideoMediaType(IMFMediaType** mediaType) {
    MFCreateMediaType(mediaType);
    (*mediaType)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    (*mediaType)->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    (*mediaType)->SetUINT32(MF_MT_AVG_BITRATE, m_config.bitrateKbps * 1000);
    (*mediaType)->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(*mediaType, MF_MT_FRAME_SIZE, m_config.width, m_config.height);
    MFSetAttributeRatio(*mediaType, MF_MT_FRAME_RATE, m_config.fps, 1);
    MFSetAttributeRatio(*mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    return true;
}

bool VideoEncoder::CreateAudioMediaType(IMFMediaType** mediaType) {
    MFCreateMediaType(mediaType);
    (*mediaType)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    (*mediaType)->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    (*mediaType)->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_config.audioChannels);
    (*mediaType)->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_config.audioSampleRate);
    (*mediaType)->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, m_config.audioBits);
    return true;
}

} // namespace mangke
