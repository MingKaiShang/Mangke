#pragma once
#include <CaptureLib/CaptureCommon.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>
#include <deque>
#include <mutex>

namespace mangke {
using Microsoft::WRL::ComPtr;

class D3D11Renderer {
public:
    D3D11Renderer();
    ~D3D11Renderer();
    D3D11Renderer(const D3D11Renderer&)=delete;
    D3D11Renderer& operator=(const D3D11Renderer&)=delete;

    bool Initialize(ComPtr<ID3D11Device> d3dDevice, uint32_t w, uint32_t h);
    void RenderFrame(ID3D11Texture2D* frame, const SmoothedMouseState& ms,
                     const std::vector<KeyboardShortcut>& keys, ID3D11Texture2D** out=nullptr);
    void UpdateZoomTarget(float x, float y, float zoom);
    void SetHighlightConfig(const MouseHighlightConfig& c) { m_highlightConfig = c; }
    void SetKeyboardConfig(const KeyboardVisConfig& c) { m_keyboardConfig = c; }
    void SetConfig(float cursorScale, float sharpness, bool ripple, bool keyboard) {
        std::lock_guard lock(m_mutex);
        m_cursorScale = cursorScale; m_sharpness = sharpness;
        m_clickRippleEnabled = ripple; m_keyboardConfig.enabled = keyboard;
        if (!ripple) for (auto& r : m_ripples) r.active = false;
    }
    ID3D11Device* GetDevice() const { return m_d3dDevice.Get(); }
    void SetPreviewWindow(HWND hwnd);
    void Release();

private:
    bool CreateRenderTargets();
    bool CreateSamplers();
    bool CompileShaders();
    void UpdateCamera(float dt);
    void RenderZoomedFrame(ID3D11Texture2D* frame, const SmoothedMouseState& ms);
    bool TrackIME();
    bool CaptureSystemCursor();
    void RenderCursor(float mx, float my);
    void RenderKeyboard(const std::vector<KeyboardShortcut>& keys);
    void RenderToPreview();
    void UpdateClickRipples(const SmoothedMouseState& ms, float now);

    struct Ripple { float x,y,startTime,duration; bool active=false; };
    Ripple m_ripples[8];
    bool m_prevLeftDown = false, m_clickRippleEnabled = true;

    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<ID3D11Texture2D> m_renderTarget, m_outputTexture;
    ComPtr<ID3D11RenderTargetView> m_rtv;
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_mainPS, m_cursorPS;
    ComPtr<ID3D11Buffer> m_cb;

    ComPtr<ID3D11Texture2D> m_cursorTex;
    ComPtr<ID3D11ShaderResourceView> m_cursorSRV;
    bool m_cursorLoaded=false;
    int m_cursorHotspotX=0,m_cursorHotspotY=0;
    uint32_t m_cursorWidth=0,m_cursorHeight=0;
    float m_cursorScale=2.0f;

    ComPtr<ID3D11SamplerState> m_bilinearSampler;
    ComPtr<ID3D11BlendState> m_cursorBlendState; // 缓存，避免每帧创建
    ComPtr<ID3D11ShaderResourceView> m_frameSRV; // 缓存帧SRV
    ComPtr<IDXGISwapChain1> m_previewSwapChain;
    HWND m_previewHwnd=nullptr;

    struct { float x=0.5f,y=0.5f,zoom=1.5f; } m_camera, m_cameraTarget;
    float m_zoomTarget=1.5f, m_sharpness=0.5f;
    KeyboardVisConfig m_keyboardConfig;
    MouseHighlightConfig m_highlightConfig;
    uint32_t m_width=0,m_height=0;
    bool m_initialized=false;
    uint64_t m_frameIndex=0;
    float m_imeCooldown=0;
    std::mutex m_mutex;
};
} // namespace mangke
