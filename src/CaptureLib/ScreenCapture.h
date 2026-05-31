/**
 * @file ScreenCapture.h
 * @brief Screen capture using DXGI Desktop Duplication API
 *
 * Uses IDXGIOutputDuplication for zero-copy GPU texture capture.
 * Falls back to GDI BitBlt if Desktop Duplication is unavailable.
 * Pre-allocates textures to avoid per-frame allocation overhead.
 */

#pragma once

#include "CaptureCommon.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>

namespace mangke {

using Microsoft::WRL::ComPtr;

/// Frame callback: receives texture pointer and frame index
using FrameCapturedCallback = std::function<void(ID3D11Texture2D* frame, uint64_t frameIndex)>;

class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    bool Initialize(const CaptureConfig& config);
    void SetFrameCallback(FrameCapturedCallback callback);
    bool Start();
    void Stop();

    bool IsCapturing() const { return m_isCapturing.load(); }
    ID3D11Device* GetD3DDevice() const { return m_d3dDevice.Get(); }
    float GetCurrentFPS() const { return m_currentFPS.load(); }

    void GetFrameSize(uint32_t& width, uint32_t& height) const {
        width = m_frameWidth;
        height = m_frameHeight;
    }

    /// Get total frames delivered to encoder
    uint64_t GetTotalFrames() const { return m_totalFrames.load(); }

private:
    void CaptureThreadFunc();
    bool InitD3D11();
    bool InitDesktopDuplication();
    bool InitFallbackGDI();

    CaptureConfig m_config;

    // D3D11
    ComPtr<ID3D11Device>        m_d3dDevice;
    ComPtr<ID3D11DeviceContext>  m_d3dContext;

    // Pre-allocated textures (created once)
    ComPtr<ID3D11Texture2D> m_gpuTexture;     // GPU-readable output texture
    ComPtr<ID3D11Texture2D> m_stagingTexture;  // CPU-readable staging

    // Desktop Duplication
    ComPtr<IDXGIOutputDuplication> m_deskDupl;
    bool m_useDesktopDuplication = false;

    // GDI fallback
    bool m_useGDI = false;
    HDC  m_screenDC = nullptr;
    HDC  m_memDC    = nullptr;
    HBITMAP m_bitmap = nullptr;
    std::vector<uint8_t> m_pixelBuffer;

    // Thread
    std::thread       m_captureThread;
    std::atomic<bool> m_isCapturing{false};
    std::atomic<bool> m_shouldStop{false};

    // Callback
    FrameCapturedCallback m_frameCallback;
    std::mutex            m_callbackMutex;

    // Stats
    std::atomic<float>   m_currentFPS{0.0f};
    std::atomic<uint64_t> m_totalFrames{0};
    std::deque<std::chrono::steady_clock::time_point> m_frameTimes;

    uint32_t m_frameWidth  = 0;
    uint32_t m_frameHeight = 0;

    // Monitor offset (for multi-monitor)
    int m_monitorX = 0;
    int m_monitorY = 0;
    int m_monitorIndex = 0;
};

} // namespace mangke
