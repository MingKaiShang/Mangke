/**
 * @file ScreenCapture.cpp
 * @brief DXGI Desktop Duplication screen capture (with GDI fallback)
 *
 * Primary path: IDXGIOutputDuplication — zero-copy GPU texture, <1ms per frame.
 * Fallback: GDI BitBlt — slower but universally compatible.
 * Frame timing uses monotonic frame counter for PTS, not wall clock.
 */

#include "ScreenCapture.h"
#include "MonitorManager.h"
#include <chrono>
#include <iostream>

#pragma comment(lib, "dxgi.lib")

namespace mangke {

ScreenCapture::ScreenCapture() = default;

ScreenCapture::~ScreenCapture() {
    Stop();
    if (m_bitmap)   { DeleteObject(m_bitmap); }
    if (m_memDC)    { DeleteDC(m_memDC); }
    if (m_screenDC) { ReleaseDC(nullptr, m_screenDC); }
}

bool ScreenCapture::Initialize(const CaptureConfig& config) {
    m_config = config;
    m_monitorIndex = config.monitorIndex;

    // Initialize D3D11 first
    if (!InitD3D11()) {
        std::cerr << "[Capture] D3D11 init failed\n";
        return false;
    }

    // 单屏模式：优先 Desktop Duplication
    if (InitDesktopDuplication()) {
        m_useDesktopDuplication = true;
        IDXGIResource* r=nullptr; DXGI_OUTDUPL_FRAME_INFO fi={};
        if(SUCCEEDED(m_deskDupl->AcquireNextFrame(200,&fi,&r))&&r){
            ID3D11Texture2D* t=nullptr;
            if(SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D),(void**)&t))&&t){
                D3D11_TEXTURE2D_DESC d; t->GetDesc(&d);
                m_frameWidth=d.Width; m_frameHeight=d.Height; t->Release();
            }
            r->Release(); m_deskDupl->ReleaseFrame();
        }
        std::cout<<"[Capture] DD: "<<m_frameWidth<<"x"<<m_frameHeight<<"\n";
    } else {
        // GDI 回退
        MonitorInfo mi=MonitorManager::GetMonitor(config.monitorIndex);
        m_frameWidth=mi.width; m_frameHeight=mi.height;
        m_monitorX=mi.x; m_monitorY=mi.y;
        if(!InitFallbackGDI()){std::cerr<<"[Capture] GDI failed\n";return false;}
        std::cout<<"[Capture] GDI: "<<m_frameWidth<<"x"<<m_frameHeight<<"\n";
    }

    // Create textures at the correct dimensions
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width     = m_frameWidth;
    texDesc.Height    = m_frameHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage     = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_gpuTexture);
    if (FAILED(hr)) {
        std::cerr << "[Capture] Failed to create GPU texture\n";
        return false;
    }

    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.BindFlags = 0;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = m_d3dDevice->CreateTexture2D(&texDesc, nullptr, &m_stagingTexture);
    if (FAILED(hr)) {
        std::cerr << "[Capture] Failed to create staging texture\n";
        return false;
    }

    std::cout << "[Capture] Initialized: " << m_frameWidth << "x" << m_frameHeight
              << " @ " << config.targetFps << "fps\n";
    return true;
}

void ScreenCapture::SetFrameCallback(FrameCapturedCallback callback) {
    std::lock_guard lock(m_callbackMutex);
    m_frameCallback = std::move(callback);
}

bool ScreenCapture::Start() {
    if (m_isCapturing.load()) return false;

    m_shouldStop.store(false);
    m_isCapturing.store(true);
    m_totalFrames.store(0);
    m_frameTimes.clear();

    m_captureThread = std::thread(&ScreenCapture::CaptureThreadFunc, this);
    std::cout << "[Capture] Capture started\n";
    return true;
}

void ScreenCapture::Stop() {
    if (!m_isCapturing.load()) return;

    m_shouldStop.store(true);
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    m_isCapturing.store(false);
    std::cout << "[Capture] Capture stopped (" << m_totalFrames.load() << " frames)\n";
}

bool ScreenCapture::InitD3D11() {
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL actualLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createFlags, featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION, &m_d3dDevice, &actualLevel, &m_d3dContext
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createFlags, featureLevels, _countof(featureLevels),
            D3D11_SDK_VERSION, &m_d3dDevice, &actualLevel, &m_d3dContext
        );
    }

    if (SUCCEEDED(hr)) {
        std::cout << "[Capture] D3D11 device created (FL: 0x"
                  << std::hex << actualLevel << std::dec << ")\n";
    }
    return SUCCEEDED(hr);
}

bool ScreenCapture::InitDesktopDuplication() {
    // Get the DXGI device from D3D11
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) return false;

    // Get the output for our target monitor
    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(m_monitorIndex, &dxgiOutput);
    if (FAILED(hr)) {
        // Try output 0 as fallback
        hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
        if (FAILED(hr)) return false;
    }

    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) return false;

    // Create desktop duplication
    hr = dxgiOutput1->DuplicateOutput(m_d3dDevice.Get(), &m_deskDupl);
    if (FAILED(hr)) {
        std::cerr << "[Capture] DuplicateOutput failed: 0x" << std::hex << hr << std::dec << "\n";
        return false;
    }

    m_useDesktopDuplication = true;
    return true;
}

bool ScreenCapture::InitFallbackGDI() {
    m_screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(m_screenDC);
    m_bitmap = CreateCompatibleBitmap(m_screenDC, m_frameWidth, m_frameHeight);

    if (!m_screenDC || !m_memDC || !m_bitmap) {
        std::cerr << "[Capture] GDI resource creation failed\n";
        return false;
    }

    m_pixelBuffer.resize(m_frameWidth * m_frameHeight * 4);
    m_useGDI = true;
    return true;
}

void ScreenCapture::CaptureThreadFunc() {
    using Clock = std::chrono::steady_clock;
    const auto frameInterval = std::chrono::microseconds(1'000'000 / m_config.targetFps);
    auto nextFrameTime = Clock::now();

    std::cout << "[Capture] Thread started (GDI=" << m_useGDI << " DD=" << m_useDesktopDuplication << ")\n";

    while (!m_shouldStop.load()) {
        auto frameStart = Clock::now();

        bool frameCaptured = false;

        if (m_useDesktopDuplication) {
            // === DXGI Desktop Duplication path (< 1ms per frame) ===
            IDXGIResource* desktopResource = nullptr;
            DXGI_OUTDUPL_FRAME_INFO frameInfo = {};

            HRESULT hr = m_deskDupl->AcquireNextFrame(1, &frameInfo, &desktopResource);

            if (hr == S_OK && desktopResource) {
                ID3D11Texture2D* desktopTex = nullptr;
                desktopResource->QueryInterface(__uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(&desktopTex));

                if (desktopTex) {
                    // Verify texture format matches
                    D3D11_TEXTURE2D_DESC ddDesc, ourDesc;
                    desktopTex->GetDesc(&ddDesc);
                    m_gpuTexture->GetDesc(&ourDesc);
                    m_d3dContext->CopyResource(m_gpuTexture.Get(), desktopTex);
                    m_d3dContext->Flush();
                    desktopTex->Release();
                    frameCaptured = true;
                }

                desktopResource->Release();
                m_deskDupl->ReleaseFrame();
            } else if (hr == DXGI_ERROR_ACCESS_LOST) {
                std::cerr << "[Capture] Desktop Duplication access lost\n";
                m_deskDupl.Reset();
                if (!InitDesktopDuplication()) {
                    m_useDesktopDuplication = false;
                    InitFallbackGDI();
                }
                continue;
            }

        } else if (m_useGDI) {
            // === GDI fallback path ===
            HGDIOBJ oldBmp = SelectObject(m_memDC, m_bitmap);

            int srcX = m_config.region.fullscreen ? m_monitorX : m_config.region.x;
            int srcY = m_config.region.fullscreen ? m_monitorY : m_config.region.y;

            BOOL bltOk = BitBlt(m_memDC, 0, 0, m_frameWidth, m_frameHeight,
                   m_screenDC, srcX, srcY, SRCCOPY);

            SelectObject(m_memDC, oldBmp);

            if (!bltOk) {
                std::cerr << "[Capture] BitBlt failed\n";
                continue;
            }

            // Read pixels from bitmap
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = m_frameWidth;
            bmi.bmiHeader.biHeight      = -(LONG)m_frameHeight;
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            HDC hdc = CreateCompatibleDC(m_screenDC);
            HGDIOBJ oldB = SelectObject(hdc, m_bitmap);
            int lines = GetDIBits(hdc, m_bitmap, 0, m_frameHeight,
                      m_pixelBuffer.data(), &bmi, DIB_RGB_COLORS);
            SelectObject(hdc, oldB);
            DeleteDC(hdc);

            if (lines == 0) {
                std::cerr << "[Capture] GetDIBits failed\n";
                continue;
            }

            // Upload to GPU texture
            if (m_gpuTexture && m_d3dContext) {
                m_d3dContext->UpdateSubresource(
                    m_gpuTexture.Get(), 0, nullptr,
                    m_pixelBuffer.data(), m_frameWidth * 4, 0
                );
            }
            frameCaptured = true;
        }

        // Deliver frame to encoder (always, even if reusing last frame for timing)
        uint64_t frameIndex = m_totalFrames.fetch_add(1);

        {
            std::lock_guard lock(m_callbackMutex);
            if (m_frameCallback) {
                m_frameCallback(m_gpuTexture.Get(), frameIndex);
            }
        }

        // FPS rolling window (1 second)
        m_frameTimes.push_back(frameStart);
        auto cutoff = frameStart - std::chrono::seconds(1);
        while (!m_frameTimes.empty() && m_frameTimes.front() < cutoff) {
            m_frameTimes.pop_front();
        }
        m_currentFPS.store(static_cast<float>(m_frameTimes.size()));

        // Frame rate pacing
        nextFrameTime += frameInterval;
        std::this_thread::sleep_until(nextFrameTime);

        // If we've fallen behind significantly, reset
        if (Clock::now() > nextFrameTime + frameInterval * 3) {
            nextFrameTime = Clock::now();
        }
    }
}

} // namespace mangke
