/**
 * @file VideoEncoder.h
 * @brief Media Foundation H.264 video encoder
 *
 * Frame-counter based PTS ensures consistent playback speed
 * regardless of actual capture timing.
 */

#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <atomic>
#include <mutex>

struct IMFMediaSink;
struct IMFSinkWriter;
struct IMFMediaType;

namespace mangke {

using Microsoft::WRL::ComPtr;

enum class EncoderState : uint8_t {
    Idle, Encoding, Finishing, Error
};

struct EncoderConfig {
    uint32_t width           = 1920;
    uint32_t height          = 1080;
    uint32_t fps             = 60;
    uint32_t bitrateKbps     = 15000;   // 15 Mbps for 1080p60
    uint32_t audioSampleRate = 44100;
    uint16_t audioChannels   = 2;
    uint16_t audioBits       = 16;

    enum class Codec { H264, H265 } codec = Codec::H264;
    std::wstring outputPath;
};

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    static bool InitializeMF();
    static void ShutdownMF();

    bool Configure(const EncoderConfig& config);
    bool Start();

    /// Encode a video frame. frameIndex is used for PTS calculation.
    bool EncodeVideoFrame(ID3D11Texture2D* texture, uint64_t frameIndex);

    bool EncodeAudioData(const uint8_t* data, uint32_t size, uint64_t timestampUs);
    bool Finish();

    EncoderState GetState() const { return m_state.load(); }
    uint64_t GetEncodedFrames() const { return m_encodedFrames.load(); }
    uint64_t GetOutputFileSize() const;

private:
    bool CreateVideoMediaType(IMFMediaType** mediaType);
    bool CreateAudioMediaType(IMFMediaType** mediaType);

    EncoderConfig m_config;
    IMFSinkWriter* m_sinkWriter = nullptr;
    DWORD m_videoStreamIndex = 0;
    DWORD m_audioStreamIndex = 0;

    std::atomic<EncoderState> m_state{EncoderState::Idle};
    std::atomic<uint64_t>     m_encodedFrames{0};
    mutable std::mutex        m_mutex;

    // Frame-counter PTS (100ns units for Media Foundation)
    int64_t m_frameDuration = 0; // = 10_000_000 / fps

    // D3D11 for texture readback
    ComPtr<ID3D11Device>        m_d3dDevice;
    ComPtr<ID3D11DeviceContext>  m_d3dContext;
    ComPtr<ID3D11Texture2D>      m_stagingTex; // persistent staging texture
};

} // namespace mangke
