/**
 * @file D3D11Renderer.cpp
 * @brief D3D11 renderer — zoom + cursor + ripple + keyboard
 */

#include "D3D11Renderer.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace mangke {

// ========== Shaders ==========

static const char* VS_HLSL = R"(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };
VSOut main(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
)";

// Sharp zoom + click ripple + keyboard pixel shader
// CB layout: [cx,cy,zoom,pad, cursorX,cursorY, cursorW,cursorH, rippleCount, time, sharpness, pad]
static const char* MAIN_PS_HLSL = R"(
Texture2D tex : register(t0);
SamplerState samp : register(s0);

cbuffer CB : register(b0) {
    float4 p0; // camX, camY, zoom, sharpness
    float4 p1; // mouseX, mouseY, mouseScale, _pad
    float4 p2; // ripple0_xy, ripple0_radius, ripple0_alpha
    float4 p3; // ripple1_xy, ripple1_radius, ripple1_alpha
    float4 p4; // ripple2_xy, ripple2_radius, ripple2_alpha
    float4 p5; // ripple3_xy, ripple3_radius, ripple3_alpha
    float4 p6; // key0_text, key1_text (unused in shader, used for CPU debug)
    float4 p7; // _unused
};

// Sharp bilinear: blend between nearest-pixel and linear
float4 SampleSharp(float2 uv, float sharp) {
    float4 c = tex.Sample(samp, uv);
    // Simple unsharp mask via luma adjustment
    float luma = dot(c.rgb, float3(0.299, 0.587, 0.114));
    float sharpness = 1.0 + sharp * 0.5;
    return float4(lerp(luma.xxx, c.rgb, sharpness), c.a);
}

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target {
    float2 center = p0.xy;
    float zoom = p0.z;
    float sharp = p0.w;
    float2 zoomedUV = (uv - center) / zoom + center;
    if (any(zoomedUV < 0) || any(zoomedUV > 1))
        return float4(0, 0, 0, 1);
    float4 color = SampleSharp(zoomedUV, sharp);

    // Click ripples (overlay)
    float3 rippleColor = float3(1, 1, 1);
    float4 ripples[4] = { p2, p3, p4, p5 };
    for (int i = 0; i < 4; i++) {
        float4 r = ripples[i];
        if (r.w <= 0) continue;
        float d = length(uv - r.xy);
        float ring = 1.0 - abs(d - r.z);
        ring = smoothstep(0.03, 0, abs(d - r.z)) * r.w;
        color.rgb = lerp(color.rgb, rippleColor, ring * 0.4);
    }

    return color;
}
)";

// Cursor pixel shader
static const char* CURSOR_PS_HLSL = R"(
Texture2D cursorTex : register(t0);
SamplerState samp : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target {
    float4 c = cursorTex.Sample(samp, uv);
    if (c.a < 0.01) discard;
    return c;
}
)";

D3D11Renderer::D3D11Renderer() = default;
D3D11Renderer::~D3D11Renderer() { Release(); }

bool D3D11Renderer::Initialize(ComPtr<ID3D11Device> d3dDevice, uint32_t width, uint32_t height) {
    std::lock_guard lock(m_mutex);
    m_d3dDevice = d3dDevice;
    m_d3dDevice->GetImmediateContext(&m_d3dContext);
    m_width = width;
    m_height = height;

    if (!CreateRenderTargets()) return false;
    if (!CreateSamplers()) return false;
    if (!CompileShaders()) return false;

    m_initialized = true;
    std::cout << "[Renderer] Ready (" << width << "x" << height << ")\n";
    return true;
}

void D3D11Renderer::RenderFrame(
    ID3D11Texture2D* inputFrame,
    const SmoothedMouseState& mouseState,
    const std::vector<KeyboardShortcut>& shortcuts,
    ID3D11Texture2D** outputTex)
{
    if (!m_initialized || !inputFrame) return;
    std::lock_guard lock(m_mutex);

    m_frameIndex++;
    float dt = 1.0f / 60.0f;
    float now = m_frameIndex * dt;

    // 直接用 GetCursorPos 获取实时鼠标位置（消除延时）
    POINT curPos;
    GetCursorPos(&curPos);
    float mx = (float)curPos.x, my = (float)curPos.y;
    // 钳位到主屏范围 [0,1]（多屏时鼠标可能在副屏）
    float nx = std::clamp(mx / (float)m_width, 0.0f, 1.0f);
    float ny = std::clamp(my / (float)m_height, 0.0f, 1.0f);

    // Camera follows mouse (实时追踪)
    if (!TrackIME()) {
        m_cameraTarget.x = nx;
        m_cameraTarget.y = ny;
    }
    UpdateCamera(dt);

    // 实时检测鼠标点击（涟漪）
    bool leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (m_clickRippleEnabled && leftDown && !m_prevLeftDown) {
        for (auto& r : m_ripples) {
            if (!r.active) { r = {nx, ny, now, 0.5f, true}; break; }
        }
    }
    m_prevLeftDown = leftDown;

    // Clear + render zoomed view
    float clear[4] = {0,0,0,1};
    m_d3dContext->ClearRenderTargetView(m_rtv.Get(), clear);
    m_d3dContext->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    RenderZoomedFrame(inputFrame, mouseState);

    // Cursor overlay (用实时鼠标位置)
    if (m_highlightConfig.style != MouseHighlightStyle::None) {
        RenderCursor(mx, my);
    }

    // Preview
    if (m_previewHwnd) RenderToPreview();

    // Output for encoder
    if (outputTex) {
        if (!m_outputTexture) {
            D3D11_TEXTURE2D_DESC desc;
            inputFrame->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;
            m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_outputTexture);
        }
        m_d3dContext->CopyResource(m_outputTexture.Get(), m_renderTarget.Get());
        *outputTex = m_outputTexture.Get();
    }
}

void D3D11Renderer::UpdateZoomTarget(float x, float y, float targetZoom) {
    m_cameraTarget.x = x;
    m_cameraTarget.y = y;
    m_zoomTarget = std::clamp(targetZoom, 1.0f, 5.0f);
}

void D3D11Renderer::Release() {
    std::lock_guard lock(m_mutex);
    m_vs.Reset(); m_mainPS.Reset(); m_cursorPS.Reset();
    m_cb.Reset(); m_cursorTex.Reset(); m_cursorSRV.Reset();
    m_bilinearSampler.Reset(); m_previewSwapChain.Reset();
    m_rtv.Reset(); m_renderTarget.Reset(); m_outputTexture.Reset();
    m_d3dContext.Reset(); m_initialized = false; m_cursorLoaded = false;
}

void D3D11Renderer::SetPreviewWindow(HWND hwnd) {
    m_previewHwnd = hwnd;
    if (!hwnd || !m_d3dDevice) return;
    ComPtr<IDXGIFactory2> factory;
    CreateDXGIFactory1(__uuidof(IDXGIFactory2), &factory);
    DXGI_SWAP_CHAIN_DESC1 sc = {};
    sc.Width = m_width; sc.Height = m_height;
    sc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 2;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    factory->CreateSwapChainForHwnd(m_d3dDevice.Get(), hwnd, &sc, nullptr, nullptr, &m_previewSwapChain);
}

// ========== Camera ==========

void D3D11Renderer::UpdateCamera(float dt) {
    float s = 0.08f;
    float f = 1.0f - powf(1.0f - s, dt * 60.0f);
    m_camera.x += (m_cameraTarget.x - m_camera.x) * f;
    m_camera.y += (m_cameraTarget.y - m_camera.y) * f;
    float zf = 1.0f - powf(1.0f - 0.12f, dt * 60.0f);
    m_camera.zoom += (m_zoomTarget - m_camera.zoom) * zf;
}

// ========== Main Render ==========

void D3D11Renderer::RenderZoomedFrame(ID3D11Texture2D* frame, const SmoothedMouseState& ms) {
    if (!m_mainPS || !m_cb) { m_d3dContext->CopyResource(m_renderTarget.Get(), frame); return; }

    // Set viewport
    D3D11_VIEWPORT vp = {0,0,(float)m_width,(float)m_height,0,1};
    m_d3dContext->RSSetViewports(1, &vp);
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->IASetInputLayout(nullptr);
    m_d3dContext->VSSetShader(m_vs.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_mainPS.Get(), nullptr, 0);

    // Create SRV for source frame
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_d3dDevice->CreateShaderResourceView(frame, &sd, &srv))) {
        m_d3dContext->CopyResource(m_renderTarget.Get(), frame);
        return;
    }

    // Build constant buffer (8 x float4 = 128 bytes)
    // p0: camX, camY, zoom, sharpness
    // p1: mouseX, mouseY, mouseScale, pad
    // p2-p5: ripples (xy, radius, alpha)
    // p6-p7: unused
    float cb[32] = {};
    cb[0] = m_camera.x; cb[1] = m_camera.y; cb[2] = m_camera.zoom; cb[3] = m_sharpness;
    cb[4] = ms.x / m_width; cb[5] = ms.y / m_height; cb[6] = m_cursorScale; cb[7] = 0;
    // Copy active ripples to constant buffer
    int ri = 0;
    float time = m_frameIndex / 60.0f;
    for (auto& r : m_ripples) {
        if (!r.active || ri >= 4) continue;
        float age = time - r.startTime;
        float progress = age / r.duration;
        if (progress > 1) { r.active = false; continue; }
        float radius = progress * 0.15f; // expand to 15% of screen
        float alpha = 1.0f - progress;
        int base = 8 + ri * 4; // p2 = cb[8..11], p3 = cb[12..15], etc.
        cb[base+0] = r.x; cb[base+1] = r.y; cb[base+2] = radius; cb[base+3] = alpha;
        ri++;
    }

    m_d3dContext->UpdateSubresource(m_cb.Get(), 0, nullptr, cb, 0, 0);

    ID3D11ShaderResourceView* s = srv.Get();
    m_d3dContext->PSSetShaderResources(0, 1, &s);
    m_d3dContext->PSSetSamplers(0, 1, m_bilinearSampler.GetAddressOf());
    m_d3dContext->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_d3dContext->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
    m_d3dContext->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_d3dContext->PSSetShaderResources(0, 1, &nullSrv);
}

// ========== Click Ripples ==========

void D3D11Renderer::UpdateClickRipples(const SmoothedMouseState& ms, float now) {
    if (!m_clickRippleEnabled) return;
    if (ms.leftDown && !m_prevLeftDown) {
        float nx = ms.x / m_width;
        float ny = ms.y / m_height;
        for (auto& r : m_ripples) {
            if (!r.active) {
                r = {nx, ny, now, 0.5f, true};
                break;
            }
        }
    }
    m_prevLeftDown = ms.leftDown;
    // Clean expired ripples
    for (auto& r : m_ripples) {
        if (r.active && now - r.startTime > r.duration) r.active = false;
    }
}

// ========== Cursor ==========

bool D3D11Renderer::CaptureSystemCursor() {
    if (m_cursorLoaded) return true;
    CURSORINFO ci = {sizeof(CURSORINFO)};
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING)) return false;
    ICONINFO ii;
    if (!GetIconInfo(ci.hCursor, &ii)) return false;
    int w=32,h=32; BITMAP b;
    if (ii.hbmColor) { GetObject(ii.hbmColor,sizeof(BITMAP),&b); w=b.bmWidth; h=b.bmHeight; }
    if (w<=0||h<=0){w=32;h=32;}
    HDC sdc=GetDC(nullptr), mdc=CreateCompatibleDC(sdc);
    HBITMAP b2=CreateCompatibleBitmap(sdc,w,h);
    SelectObject(mdc,b2);
    DrawIconEx(mdc,0,0,ci.hCursor,w,h,0,nullptr,DI_NORMAL);
    BITMAPINFO bi={}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    std::vector<uint8_t> px(w*h*4);
    GetDIBits(mdc,b2,0,h,px.data(),&bi,DIB_RGB_COLORS);
    DeleteObject(b2);DeleteDC(mdc);ReleaseDC(nullptr,sdc);
    DeleteObject(ii.hbmColor);DeleteObject(ii.hbmMask);
    m_cursorHotspotX=ii.xHotspot; m_cursorHotspotY=ii.yHotspot;
    m_cursorWidth=w; m_cursorHeight=h;
    D3D11_TEXTURE2D_DESC td={}; td.Width=w; td.Height=h;
    td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd={px.data(),(UINT)w*4,0};
    if(FAILED(m_d3dDevice->CreateTexture2D(&td,&sd,&m_cursorTex))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv={}; sv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels=1;
    if(FAILED(m_d3dDevice->CreateShaderResourceView(m_cursorTex.Get(),&sv,&m_cursorSRV))) return false;
    ID3DBlob *b3=nullptr,*e=nullptr;
    D3DCompile(CURSOR_PS_HLSL,strlen(CURSOR_PS_HLSL),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&b3,&e);
    if(e)e->Release();
    if(b3){m_d3dDevice->CreatePixelShader(b3->GetBufferPointer(),b3->GetBufferSize(),nullptr,&m_cursorPS);b3->Release();}
    m_cursorLoaded=true;
    return true;
}

void D3D11Renderer::RenderCursor(float mx, float my) {
    if(!m_cursorLoaded) CaptureSystemCursor();
    if(!m_cursorSRV||!m_cursorPS) return;
    float nx=mx/m_width, ny=my/m_height;
    if(nx<0||nx>1||ny<0||ny>1) return;
    float cx=(nx*m_width-m_cursorHotspotX*m_cursorScale)/m_width;
    float cy=(ny*m_height-m_cursorHotspotY*m_cursorScale)/m_height;
    float cw=(float)m_cursorWidth/m_width*m_cursorScale;
    float ch=(float)m_cursorHeight/m_height*m_cursorScale;
    D3D11_VIEWPORT vp={cx*m_width,cy*m_height,cw*m_width,ch*m_height,0,1};
    m_d3dContext->RSSetViewports(1,&vp);
    if (!m_cursorBlendState) {
        D3D11_BLEND_DESC bd={}; bd.RenderTarget[0].BlendEnable=TRUE;
        bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        m_d3dDevice->CreateBlendState(&bd,&m_cursorBlendState);
    }
    float bf[4]={1,1,1,1};
    m_d3dContext->OMSetBlendState(m_cursorBlendState.Get(),bf,0xFFFFFFFF);
    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->IASetInputLayout(nullptr);
    m_d3dContext->VSSetShader(m_vs.Get(),nullptr,0);
    m_d3dContext->PSSetShader(m_cursorPS.Get(),nullptr,0);
    m_d3dContext->PSSetSamplers(0,1,m_bilinearSampler.GetAddressOf());
    ID3D11ShaderResourceView* s=m_cursorSRV.Get();
    m_d3dContext->PSSetShaderResources(0,1,&s);
    m_d3dContext->Draw(3,0);
    D3D11_VIEWPORT fvp={0,0,(float)m_width,(float)m_height,0,1};
    m_d3dContext->RSSetViewports(1,&fvp);
    m_d3dContext->OMSetBlendState(nullptr,bf,0xFFFFFFFF);
    ID3D11ShaderResourceView* n=nullptr;
    m_d3dContext->PSSetShaderResources(0,1,&n);
}

// ========== Keyboard Overlay ==========

void D3D11Renderer::RenderKeyboard(const std::vector<KeyboardShortcut>& shortcuts) {
    if (!m_keyboardConfig.enabled || shortcuts.empty()) return;

    // 用 GDI 绘制按键文本到内存 DC，上传为纹理叠在右下角
    // 获取最新按键（最多显示 3 个）
    wchar_t text[128] = L"";
    int n = 0;
    for (int i = (int)shortcuts.size() - 1; i >= 0 && i >= (int)shortcuts.size() - 3; i--) {
        if (n > 0) wcscat_s(text, L" ");
        wcscat_s(text, shortcuts[i].displayText);
        n++;
    }
    if (n == 0) return;

    // 用 GDI 渲染按键文本
    HDC sdc = GetDC(nullptr), mdc = CreateCompatibleDC(sdc);
    int tw = (int)wcslen(text) * 14 + 24, th = 36;
    if (tw > m_width) tw = m_width - 40;
    HBITMAP bmp = CreateCompatibleBitmap(sdc, tw, th);
    SelectObject(mdc, bmp);
    // 半透明黑底
    HBRUSH bg = CreateSolidBrush(RGB(0,0,0));
    RECT rc = {0, 0, tw, th};
    FillRect(mdc, &rc, bg);
    DeleteObject(bg);
    // 白色文字
    SetTextColor(mdc, RGB(255,255,255));
    SetBkMode(mdc, TRANSPARENT);
    SelectObject(mdc, GetStockObject(DEFAULT_GUI_FONT));
    rc.left = 10; rc.top = 8;
    DrawTextW(mdc, text, -1, &rc, DT_LEFT);

    // 读像素
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = tw; bi.bmiHeader.biHeight = -th;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> px(tw * th * 4);
    GetDIBits(mdc, bmp, 0, th, px.data(), &bi, DIB_RGB_COLORS);

    DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);

    // 创建纹理并渲染
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = tw; td.Height = th; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = { px.data(), (UINT)tw * 4, 0 };
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(m_d3dDevice->CreateTexture2D(&td, &sd, &tex))) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(m_d3dDevice->CreateShaderResourceView(tex.Get(), &sv, &srv))) return;

    // 右下角显示
    float kx = (float)(m_width - tw - 16) / m_width;
    float ky = (float)(m_height - th - 16) / m_height;
    float kw = (float)tw / m_width;
    float kh = (float)th / m_height;

    // 启用透明度混合
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ComPtr<ID3D11BlendState> bs;
    m_d3dDevice->CreateBlendState(&bd, &bs);
    float bf[4] = {1,1,1,1};
    m_d3dContext->OMSetBlendState(bs.Get(), bf, 0xFFFFFFFF);

    // 设置视口到按键区域
    D3D11_VIEWPORT vp = { kx*m_width, ky*m_height, kw*m_width, kh*m_height, 0, 1 };
    m_d3dContext->RSSetViewports(1, &vp);

    m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_d3dContext->VSSetShader(m_vs.Get(), nullptr, 0);
    m_d3dContext->PSSetShader(m_cursorPS.Get(), nullptr, 0); // reuse cursor PS for text
    ID3D11ShaderResourceView* s = srv.Get();
    m_d3dContext->PSSetShaderResources(0, 1, &s);
    m_d3dContext->PSSetSamplers(0, 1, m_bilinearSampler.GetAddressOf());
    m_d3dContext->Draw(3, 0);

    // 恢复视口
    D3D11_VIEWPORT fvp = { 0, 0, (float)m_width, (float)m_height, 0, 1 };
    m_d3dContext->RSSetViewports(1, &fvp);
    m_d3dContext->OMSetBlendState(nullptr, bf, 0xFFFFFFFF);
    ID3D11ShaderResourceView* nullSrv = nullptr;
    m_d3dContext->PSSetShaderResources(0, 1, &nullSrv);
}

// ========== IME Tracking ==========

bool D3D11Renderer::TrackIME() {
    GUITHREADINFO gui={sizeof(GUITHREADINFO)};
    if(!GetGUIThreadInfo(0,&gui)){ m_imeCooldown=0; return false; }
    if(gui.flags&GUI_CARETBLINKING){
        // 输入法活跃中
        POINT pt={gui.rcCaret.left,gui.rcCaret.top};
        ClientToScreen(gui.hwndFocus,&pt);
        m_cameraTarget.x=std::clamp((float)pt.x/m_width,0.0f,1.0f);
        m_cameraTarget.y=std::clamp((float)pt.y/m_height,0.0f,1.0f);
        m_imeCooldown=0.5f; // 停用后继续追踪0.5秒
        return true;
    } else if(m_imeCooldown>0){
        // 冷却中，继续用上次IME位置
        m_imeCooldown-=1.0f/60.0f;
        return true; // 保持相机不动
    }
    return false;
}

// ========== Preview ==========

void D3D11Renderer::RenderToPreview() {
    if(!m_previewSwapChain||!m_renderTarget) return;
    ComPtr<ID3D11Texture2D> bb;
    if(SUCCEEDED(m_previewSwapChain->GetBuffer(0,__uuidof(ID3D11Texture2D),&bb))){
        m_d3dContext->CopyResource(bb.Get(),m_renderTarget.Get());
        m_previewSwapChain->Present(1,0);
    }
}

// ========== Init ==========

bool D3D11Renderer::CreateRenderTargets() {
    D3D11_TEXTURE2D_DESC d={}; d.Width=m_width; d.Height=m_height;
    d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count=1; d.Usage=D3D11_USAGE_DEFAULT;
    d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    if(FAILED(m_d3dDevice->CreateTexture2D(&d,nullptr,&m_renderTarget))) return false;
    if(FAILED(m_d3dDevice->CreateRenderTargetView(m_renderTarget.Get(),nullptr,&m_rtv))) return false;
    return true;
}

bool D3D11Renderer::CreateSamplers() {
    D3D11_SAMPLER_DESC s={}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD=D3D11_FLOAT32_MAX;
    return SUCCEEDED(m_d3dDevice->CreateSamplerState(&s,&m_bilinearSampler));
}

bool D3D11Renderer::CompileShaders() {
    ID3DBlob *b=nullptr,*e=nullptr;
    D3DCompile(VS_HLSL,strlen(VS_HLSL),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&b,&e);
    if(e)e->Release();
    if(b){m_d3dDevice->CreateVertexShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&m_vs);b->Release();}
    if(!m_vs){std::cerr<<"[R] VS failed\n";return false;}
    e=nullptr;
    D3DCompile(MAIN_PS_HLSL,strlen(MAIN_PS_HLSL),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&b,&e);
    if(e){std::cerr<<"[R] PS: "<<(const char*)e->GetBufferPointer()<<"\n";e->Release();}
    if(!b){std::cerr<<"[R] PS failed\n";return false;}
    m_d3dDevice->CreatePixelShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&m_mainPS);
    b->Release();
    if(!m_mainPS) return false;
    D3D11_BUFFER_DESC cbd={}; cbd.ByteWidth=128; cbd.Usage=D3D11_USAGE_DEFAULT;
    cbd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    if(FAILED(m_d3dDevice->CreateBuffer(&cbd,nullptr,&m_cb))){std::cerr<<"[R] CB failed\n";return false;}
    std::cout<<"[R] Shaders ready\n";
    return true;
}

} // namespace mangke
