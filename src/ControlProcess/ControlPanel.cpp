/**
 * @file ControlPanel.cpp
 * @brief 暗色主题中文控制面板（完整设置版）
 */

#include "ControlPanel.h"
#include <dwmapi.h>
#include <CommCtrl.h>
#include <CaptureLib/MonitorManager.h>
#include <iostream>
#include <algorithm>
#include <shellapi.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

namespace mangke {

#define ID_BTN_START       1001
#define ID_BTN_PREVIEW     1014
#define ID_BTN_STOP        1002
#define ID_BTN_PAUSE       1003
#define ID_BTN_ZOOM        1004
#define ID_BTN_CURSOR      1005
#define ID_BTN_SHARP       1006
#define ID_BTN_EASING      1007
#define ID_BTN_BITRATE     1008
#define ID_CHK_HIGHLIGHT   1009
#define ID_CHK_RIPPLE      1010
#define ID_CHK_AUDIO       1011
#define ID_COMBO_EXPORT    1012
#define ID_EDIT_OUTPUT     1013
#define ID_COMBO_MONITOR   1016
#define ID_TIMER_UPDATE    2001
#define WM_TRAYICON        (WM_APP + 1)

static ControlPanel* g_instance = nullptr;

ControlPanel::ControlPanel() { g_instance = this; }
ControlPanel::~ControlPanel() {
    m_running.store(false);
    if (m_renderProcess) {
        // 先发关闭指令，等待最多 3 秒让编码器完成
        if (m_connected.load()) m_ipc.SendCommand(IPCCommand::Shutdown);
        WaitForSingleObject(m_renderProcess, 3000);
        TerminateProcess(m_renderProcess, 0);
        CloseHandle(m_renderProcess);
    }
    if (m_fontNormal) DeleteObject(m_fontNormal);
    if (m_fontBold) DeleteObject(m_fontBold);
    if (m_fontMono) DeleteObject(m_fontMono);
    if (m_brushBg) DeleteObject(m_brushBg);
    g_instance = nullptr;
}

bool ControlPanel::Create(HINSTANCE hInst, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    // 初始化 GDI+（用于加载 JPG 收款码）
    GdiplusStartupInput gdiStart;
    ULONG_PTR gdiToken;
    GdiplusStartup(&gdiToken, &gdiStart, nullptr);
    m_fontNormal=CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"\x5FAE\x8F6F\x96C5\x9ED1");
    m_fontBold=CreateFontW(-15,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"\x5FAE\x8F6F\x96C5\x9ED1");
    m_fontMono=CreateFontW(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Cascadia Code");
    m_brushBg=CreateSolidBrush(RGB(28,28,30));
    MonitorManager::EnumerateMonitors(); // 仅用于日志输出
    if(!RegisterWindowClass(hInst)) return false;
    if(!CreateMainWindow(hInst,nCmdShow)) return false;
    return true;
}

int ControlPanel::Run(){MSG m;while(GetMessage(&m,0,0,0)){TranslateMessage(&m);DispatchMessage(&m);}return m.wParam;}

bool ControlPanel::ConnectToRenderProcess(){
    if(m_connected.load()) return true;
    if(m_ipc.OpenClient()){
        m_connected.store(true); m_running.store(true);
        // 确保旧线程已释放再创建新线程
        if(m_updateThread.joinable()) m_updateThread.detach();
        m_updateThread=std::thread([this](){while(m_running.load()){if(m_connected.load()&&m_hwnd)InvalidateRect(m_hwnd,nullptr,FALSE);Sleep(200);}});
        return true;
    }
    // 一次性启动 RenderProcess
    if (!m_renderProcess) LaunchRenderProcess();
    return false;
}

bool ControlPanel::RegisterWindowClass(HINSTANCE h){
    WNDCLASSEXW w={sizeof(w)}; w.style=CS_HREDRAW|CS_VREDRAW; w.lpfnWndProc=WndProc;
    w.hInstance=h; w.hCursor=LoadCursor(nullptr,IDC_ARROW); w.hbrBackground=m_brushBg;
    w.lpszClassName=L"MangkeControlPanel";
    return RegisterClassExW(&w)!=0;
}

bool ControlPanel::CreateMainWindow(HINSTANCE h,int n){
    const int W=520,H=860;
    int sx=(GetSystemMetrics(SM_CXSCREEN)-W)/2,sy=(GetSystemMetrics(SM_CYSCREEN)-H)/2;
    m_hwnd=CreateWindowExW(0,L"MangkeControlPanel",L"Mangke 屏幕录制",
        (WS_OVERLAPPEDWINDOW&~WS_THICKFRAME&~WS_MAXIMIZEBOX),sx,sy,W,H,0,0,h,0);
    if(!m_hwnd) return false;
    BOOL dark=TRUE; DwmSetWindowAttribute(m_hwnd,20,&dark,sizeof(dark));
    CreateControls(m_hwnd);
    SetTimer(m_hwnd,ID_TIMER_UPDATE,500,nullptr);
    // 全局快捷键：Win+Shift+R 开始/停止录制
    RegisterHotKey(m_hwnd,2,MOD_WIN|MOD_SHIFT,0x52); // 'R'
    ShowWindow(m_hwnd,n); UpdateWindow(m_hwnd);

    // 系统托盘
    m_trayData.cbSize=sizeof(NOTIFYICONDATA);
    m_trayData.hWnd=m_hwnd; m_trayData.uID=1;
    m_trayData.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE;
    m_trayData.uCallbackMessage=WM_TRAYICON;
    m_trayData.hIcon=LoadIcon(nullptr,IDI_APPLICATION);
    wcscpy_s(m_trayData.szTip,L"Mangke 屏幕录制");
    Shell_NotifyIconW(NIM_ADD,&m_trayData);

    m_trayMenu=CreatePopupMenu();
    AppendMenuW(m_trayMenu,MF_STRING,ID_BTN_START,L"开始录制");
    AppendMenuW(m_trayMenu,MF_STRING,ID_BTN_STOP,L"停止录制");
    AppendMenuW(m_trayMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(m_trayMenu,MF_STRING,WM_DESTROY,L"退出");

    ConnectToRenderProcess();
    return true;
}

static HWND Label(HWND p,const wchar_t* t,int x,int y,int w,int h,HFONT f){
    HWND hw=CreateWindowExW(0,L"STATIC",t,WS_CHILD|WS_VISIBLE,x,y,w,h,p,0,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    if(f)SendMessage(hw,WM_SETFONT,(WPARAM)f,TRUE); return hw;
}
static HWND Button(HWND p,const wchar_t* t,int id,int x,int y,int w,int h,HFONT f){
    HWND hw=CreateWindowExW(0,L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,x,y,w,h,p,(HMENU)id,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    if(f)SendMessage(hw,WM_SETFONT,(WPARAM)f,TRUE); return hw;
}
static HWND Check(HWND p,const wchar_t* t,int id,int x,int y,int w,int h,HFONT f){
    HWND hw=CreateWindowExW(0,L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,w,h,p,(HMENU)id,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    if(f)SendMessage(hw,WM_SETFONT,(WPARAM)f,TRUE); return hw;
}
static HWND Slider(HWND p,int id,int x,int y,int w,HFONT f){
    HWND hw=CreateWindowExW(0,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_AUTOTICKS,x,y,w,24,p,(HMENU)id,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    if(f)SendMessage(hw,WM_SETFONT,(WPARAM)f,TRUE); return hw;
}

void ControlPanel::CreateControls(HWND p){
    int x=20,y=16,w=440,h=18,s=6;

    Label(p,L"Mangke 屏幕录制",x,y,w,28,m_fontBold); y+=36;

    // === 录制控制 ===
    m_hwndStartBtn=Button(p,L"开始录制",ID_BTN_START,x,y,130,40,m_fontNormal);
    m_hwndPauseBtn=Button(p,L"暂停",ID_BTN_PAUSE,x+142,y,130,40,m_fontNormal); EnableWindow(m_hwndPauseBtn,FALSE);
    m_hwndStopBtn=Button(p,L"停止",ID_BTN_STOP,x+284,y,130,40,m_fontNormal); EnableWindow(m_hwndStopBtn,FALSE);
    y+=50;
    y+=8;

    // === 状态 ===
    m_hwndStatusText=CreateWindowExW(WS_EX_CLIENTEDGE,L"STATIC",
        L"就绪\nFPS:-- | 帧数:-- | 大小:-- MB\n鼠标:(0,0) | 变焦:1.0x",
        WS_CHILD|WS_VISIBLE|SS_LEFT,x,y,440,68,p,0,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    SendMessage(m_hwndStatusText,WM_SETFONT,(WPARAM)m_fontMono,TRUE);
    y+=78;

    // === 变焦设置 ===
    Label(p,L"变焦设置",x,y,w,20,m_fontBold); y+=24;
    Label(p,L"倍率",x,y,50,h,m_fontNormal);
    m_zoomTrack=Slider(p,ID_BTN_ZOOM,x+50,y,330,m_fontNormal);
    SendMessage(m_zoomTrack,TBM_SETRANGE,TRUE,MAKELPARAM(10,50)); SendMessage(m_zoomTrack,TBM_SETPOS,TRUE,15);
    m_zoomLabel=Label(p,L"1.5x",x+380,y,60,h,m_fontNormal);
    y+=28;
    Label(p,L"缓动",x,y,50,h,m_fontNormal);
    m_easingTrack=Slider(p,ID_BTN_EASING,x+50,y,330,m_fontNormal);
    SendMessage(m_easingTrack,TBM_SETRANGE,TRUE,MAKELPARAM(1,30)); SendMessage(m_easingTrack,TBM_SETPOS,TRUE,8);
    m_easingLabel=Label(p,L"0.08",x+380,y,60,h,m_fontNormal);
    y+=34;

    // === 光标设置 ===
    Label(p,L"光标设置",x,y,w,20,m_fontBold); y+=24;
    Label(p,L"大小",x,y,50,h,m_fontNormal);
    m_cursorTrack=Slider(p,ID_BTN_CURSOR,x+50,y,330,m_fontNormal);
    SendMessage(m_cursorTrack,TBM_SETRANGE,TRUE,MAKELPARAM(5,50)); SendMessage(m_cursorTrack,TBM_SETPOS,TRUE,20);
    m_cursorLabel=Label(p,L"2.0x",x+380,y,60,h,m_fontNormal);
    y+=28;
    Label(p,L"锐度",x,y,50,h,m_fontNormal);
    m_sharpTrack=Slider(p,ID_BTN_SHARP,x+50,y,330,m_fontNormal);
    SendMessage(m_sharpTrack,TBM_SETRANGE,TRUE,MAKELPARAM(0,15)); SendMessage(m_sharpTrack,TBM_SETPOS,TRUE,5);
    m_sharpLabel=Label(p,L"0.5",x+380,y,60,h,m_fontNormal);
    y+=34;

    // === 显示选项 ===
    Label(p,L"显示选项",x,y,w,20,m_fontBold); y+=24;
    Check(p,L"鼠标高亮",ID_CHK_HIGHLIGHT,x,y,130,h,m_fontNormal);
    SendMessage(m_hwndHighlightCheck=GetDlgItem(p,ID_CHK_HIGHLIGHT),BM_SETCHECK,BST_CHECKED,0);
    Check(p,L"点击涟漪",ID_CHK_RIPPLE,x+140,y,130,h,m_fontNormal);
    SendMessage(m_hwndRippleCheck=GetDlgItem(p,ID_CHK_RIPPLE),BM_SETCHECK,BST_CHECKED,0);
    y+=28;
    y+=20;

    // === 录制设置 ===
    Label(p,L"录制设置",x,y,w,20,m_fontBold); y+=24;
    Label(p,L"码率",x,y,60,h,m_fontNormal);
    m_bitrateTrack=Slider(p,ID_BTN_BITRATE,x+60,y,360,m_fontNormal);
    SendMessage(m_bitrateTrack,TBM_SETRANGE,TRUE,MAKELPARAM(10,100)); SendMessage(m_bitrateTrack,TBM_SETPOS,TRUE,15); // 1.5~15 Mbps
    m_bitrateLabel=Label(p,L"15 Mbps",x+380,y,90,h,m_fontNormal);
    y+=28;
    y+=24;
    Label(p,L"导出格式",x,y,80,h,m_fontNormal);
    m_exportCombo=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,x+80,y-2,180,200,p,(HMENU)ID_COMBO_EXPORT,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    SendMessage(m_exportCombo,WM_SETFONT,(WPARAM)m_fontNormal,TRUE);
    SendMessage(m_exportCombo,CB_ADDSTRING,0,(LPARAM)L"H.264 MP4");
    SendMessage(m_exportCombo,CB_ADDSTRING,0,(LPARAM)L"H.265 MP4");
    SendMessage(m_exportCombo,CB_SETCURSEL,0,0);
    y+=28;
    Label(p,L"路径",x,y,60,h,m_fontNormal);
    m_outputEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"output/recording.mp4",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,x+60,y,380,24,p,(HMENU)ID_EDIT_OUTPUT,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    SendMessage(m_outputEdit,WM_SETFONT,(WPARAM)m_fontNormal,TRUE);
    y+=34;

    // === 赞助支持 ===
    y+=10;
    // 分隔线
    RECT line={x,y,x+420,y}; FillRect(GetDC(p),&line,(HBRUSH)GetStockObject(DC_BRUSH));
    y+=16;
    Label(p,L"❤️ 赞助支持",x,y,w,20,m_fontBold); y+=22;
    Label(p,L"如果 Mangke 对你有帮助，欢迎扫码赞助",x,y,w,h,m_fontNormal); y+=4;
    Label(p,L"你的支持是持续更新的动力 ✨",x,y,w,h,m_fontNormal); y+=20;
    // 收款码（180×180，点击可放大）
    HWND qrWx = CreateWindowExW(0,L"BUTTON",nullptr,WS_CHILD|WS_VISIBLE|BS_BITMAP, x,y,180,180,p,(HMENU)100,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    HWND qrAli = CreateWindowExW(0,L"BUTTON",nullptr,WS_CHILD|WS_VISIBLE|BS_BITMAP, x+200,y,180,180,p,(HMENU)101,(HINSTANCE)GetWindowLongPtr(p,GWLP_HINSTANCE),0);
    // 用 GDI+ 加载 JPG 收款码（缩放到 180x180 并居中，保持比例）
    auto LoadQR = [](const wchar_t* path, int size) -> HBITMAP {
        Bitmap* src = Bitmap::FromFile(path);
        if(!src||src->GetLastStatus()!=Ok){delete src; return nullptr;}
        int sw=src->GetWidth(), sh=src->GetHeight();
        // 计算保持比例后的目标尺寸
        float scale = min((float)size/sw, (float)size/sh);
        int dw=(int)(sw*scale), dh=(int)(sh*scale);
        Bitmap* dst = new Bitmap(size, size, PixelFormat32bppARGB);
        Graphics g(dst);
        g.Clear(Color(0,0,0)); // 黑色背景
        g.DrawImage(src, (size-dw)/2, (size-dh)/2, dw, dh);
        HBITMAP hb=nullptr; dst->GetHBITMAP(Color(0,0,0),&hb);
        delete dst; delete src;
        return hb;
    };
    HBITMAP hbWx=LoadQR(L"收款码\\微信.jpg",180);
    HBITMAP hbAli=LoadQR(L"收款码\\支付宝.jpg",180);
    if(hbWx){SendMessage(qrWx,BM_SETIMAGE,IMAGE_BITMAP,(LPARAM)hbWx);}else{SetWindowTextW(qrWx,L"微信");}
    if(hbAli){SendMessage(qrAli,BM_SETIMAGE,IMAGE_BITMAP,(LPARAM)hbAli);}else{SetWindowTextW(qrAli,L"支付宝");}
    y+=190;
    Label(p,L"扫码赞助，你的支持是持续更新的动力 ✨",x,y,w,h,m_fontNormal);

    EnumChildWindows(p,[](HWND c,LPARAM)->BOOL{SetWindowTheme(c,L"",L"");return TRUE;},0);
    LoadConfig();
}

void ControlPanel::OnTimer(){
    if(!m_connected.load()||!m_ipc.IsConnected()){ConnectToRenderProcess();return;}
    auto* d=m_ipc.GetData(); if(!d) return;
    m_isRecording=d->isRecording; m_isPaused=d->isPaused;
    wchar_t s[512];
    swprintf_s(s,L"%s\nFPS:%.1f | 帧数:%llu | 大小:%.1f MB\n鼠标:(%.0f,%.0f) | 变焦:%.1fx",
        m_isRecording?(m_isPaused?L"已暂停":L"录制中..."):L"就绪",
        d->currentFPS,d->recordedFrames,(double)d->outputFileSize/(1024*1024),d->mouseX,d->mouseY,d->currentZoom);
    SetWindowTextW(m_hwndStatusText,s);
    EnableWindow(m_hwndStartBtn,!m_isRecording); EnableWindow(m_hwndStopBtn,m_isRecording);
    EnableWindow(m_hwndPauseBtn,m_isRecording);
    SetWindowTextW(m_hwndPauseBtn,m_isPaused?L"恢复":L"暂停");

}

void ControlPanel::SyncAllSettings(){
    if(!m_connected.load()||!m_ipc.GetData()) return;
    auto* d=m_ipc.GetData();
    // 变焦
    d->zoomLevel=(float)SendMessage(m_zoomTrack,TBM_GETPOS,0,0)/10.0f;
    d->zoomSmoothing=(float)SendMessage(m_easingTrack,TBM_GETPOS,0,0)/100.0f;
    // 光标
    d->cursorScale=(float)SendMessage(m_cursorTrack,TBM_GETPOS,0,0)/10.0f;
    // 显示
    d->clickRippleEnabled=SendMessage(GetDlgItem(m_hwnd,ID_CHK_RIPPLE),BM_GETCHECK,0,0)==BST_CHECKED;
    d->highlightEnabled=SendMessage(GetDlgItem(m_hwnd,ID_CHK_HIGHLIGHT),BM_GETCHECK,0,0)==BST_CHECKED;
    d->encoderBitrate=(uint32_t)SendMessage(m_bitrateTrack,TBM_GETPOS,0,0)*1000;
    m_ipc.SendCommand(IPCCommand::UpdateCursorConfig);
    m_ipc.SendCommand(IPCCommand::UpdateZoomConfig);

    // 保存配置
    SaveConfig();

    // 更新标签
    wchar_t buf[32];
    swprintf_s(buf,L"%.1fx",d->zoomLevel); SetWindowTextW(m_zoomLabel,buf);
    swprintf_s(buf,L"%.2f",d->zoomSmoothing); SetWindowTextW(m_easingLabel,buf);
    swprintf_s(buf,L"%.1fx",d->cursorScale); SetWindowTextW(m_cursorLabel,buf);
    swprintf_s(buf,L"%d Mbps",d->encoderBitrate/1000); SetWindowTextW(m_bitrateLabel,buf);
}

void ControlPanel::SaveConfig(){
    wchar_t p2[MAX_PATH]; GetModuleFileNameW(nullptr,p2,MAX_PATH);
    wchar_t *q2=wcsrchr(p2,L'\\'); if(q2)*q2=0;
    wcscat_s(p2,L"\\config.ini");
    FILE* f2=nullptr; _wfopen_s(&f2,p2,L"w");
    if(!f2) return;
    fprintf(f2,"zoom=%d\n",(int)SendMessage(m_zoomTrack,TBM_GETPOS,0,0));
    fprintf(f2,"cursor=%d\n",(int)SendMessage(m_cursorTrack,TBM_GETPOS,0,0));
    fprintf(f2,"easing=%d\n",(int)SendMessage(m_easingTrack,TBM_GETPOS,0,0));
    fprintf(f2,"sharp=%d\n",(int)SendMessage(m_sharpTrack,TBM_GETPOS,0,0));
    fprintf(f2,"bitrate=%d\n",(int)SendMessage(m_bitrateTrack,TBM_GETPOS,0,0));
    fprintf(f2,"highlight=%d\n",SendMessage(GetDlgItem(m_hwnd,ID_CHK_HIGHLIGHT),BM_GETCHECK,0,0)==BST_CHECKED?1:0);
    fprintf(f2,"ripple=%d\n",SendMessage(GetDlgItem(m_hwnd,ID_CHK_RIPPLE),BM_GETCHECK,0,0)==BST_CHECKED?1:0);
    fprintf(f2,"audio=%d\n",SendMessage(GetDlgItem(m_hwnd,ID_CHK_AUDIO),BM_GETCHECK,0,0)==BST_CHECKED?1:0);
    wchar_t op[MAX_PATH]; GetWindowTextW(m_outputEdit,op,MAX_PATH);
    fwprintf(f2,L"output=%s\n",op);
    RECT rc; GetWindowRect(m_hwnd,&rc);
    fprintf(f2,"win_x=%d\nwin_y=%d\n",rc.left,rc.top);
    fclose(f2);
}
void ControlPanel::LoadConfig(){
    wchar_t p2[MAX_PATH]; GetModuleFileNameW(nullptr,p2,MAX_PATH);
    wchar_t *q2=wcsrchr(p2,L'\\'); if(q2)*q2=0;
    wcscat_s(p2,L"\\config.ini");
    FILE* f2=nullptr; _wfopen_s(&f2,p2,L"r");
    if(!f2) return;
    int v; wchar_t op[MAX_PATH]={};
    if(fwscanf_s(f2,L"zoom=%d\n",&v)==1) SendMessage(m_zoomTrack,TBM_SETPOS,TRUE,v);
    if(fwscanf_s(f2,L"cursor=%d\n",&v)==1) SendMessage(m_cursorTrack,TBM_SETPOS,TRUE,v);
    if(fwscanf_s(f2,L"easing=%d\n",&v)==1) SendMessage(m_easingTrack,TBM_SETPOS,TRUE,v);
    if(fwscanf_s(f2,L"sharp=%d\n",&v)==1) SendMessage(m_sharpTrack,TBM_SETPOS,TRUE,v);
    if(fwscanf_s(f2,L"bitrate=%d\n",&v)==1) SendMessage(m_bitrateTrack,TBM_SETPOS,TRUE,v);
    if(fwscanf_s(f2,L"highlight=%d\n",&v)==1) SendMessage(GetDlgItem(m_hwnd,ID_CHK_HIGHLIGHT),BM_SETCHECK,v?BST_CHECKED:BST_UNCHECKED,0);
    if(fwscanf_s(f2,L"ripple=%d\n",&v)==1) SendMessage(GetDlgItem(m_hwnd,ID_CHK_RIPPLE),BM_SETCHECK,v?BST_CHECKED:BST_UNCHECKED,0);
    if(fwscanf_s(f2,L"audio=%d\n",&v)==1) SendMessage(GetDlgItem(m_hwnd,ID_CHK_AUDIO),BM_SETCHECK,v?BST_CHECKED:BST_UNCHECKED,0);
    if(fwscanf_s(f2,L"output=%ls\n",op,(unsigned)_countof(op))==1) SetWindowTextW(m_outputEdit,op);
    int wx=0,wy=0;
    if(fwscanf_s(f2,L"win_x=%d\nwin_y=%d\n",&wx,&wy)==2) SetWindowPos(m_hwnd,nullptr,wx,wy,0,0,SWP_NOSIZE|SWP_NOZORDER);
    fclose(f2);
}

void ControlPanel::OnCommand(WORD cmd){
    switch(cmd){
        case ID_BTN_START:{
            // 先断开旧连接、杀旧进程，确保全新启动
            m_ipc.Close();
            m_connected.store(false);
            if (m_renderProcess) {
                TerminateProcess(m_renderProcess,0);
                CloseHandle(m_renderProcess);
                m_renderProcess=nullptr;
                Sleep(500);
            }
            if(!LaunchRenderProcess()){MessageBoxW(m_hwnd,L"无法启动录制进程",L"错误",MB_ICONERROR);break;}
            SetWindowTextW(m_hwndStatusText,L"正在连接...");
            for(int r=0;r<16;r++){Sleep(500);if(ConnectToRenderProcess())break;}
            if(!m_connected.load()){MessageBoxW(m_hwnd,L"无法连接到录制引擎",L"连接失败",MB_ICONWARNING);break;}
            SyncAllSettings();
            if(m_connected.load()){
                wchar_t p[MAX_PATH]; GetWindowTextW(m_outputEdit,p,MAX_PATH);
                wcscpy_s(m_ipc.GetData()->outputPath,p);
                m_ipc.SendCommand(IPCCommand::StartRecording);
            }
            break;
        }
        case ID_BTN_STOP:
            if(m_connected.load()){
                m_ipc.SendCommand(IPCCommand::StopRecording);
                SetWindowTextW(m_hwndStatusText,L"正在停止...");
                EnableWindow(m_hwndStartBtn,FALSE);
                EnableWindow(m_hwndStopBtn,FALSE);
                EnableWindow(m_hwndPauseBtn,FALSE);
            }
            break;
        case ID_BTN_PREVIEW:{
            wchar_t path[MAX_PATH]; GetWindowTextW(m_outputEdit,path,MAX_PATH);
            ShellExecuteW(m_hwnd,L"open",path,nullptr,nullptr,SW_SHOW);
            break;
        }
        case ID_BTN_PAUSE: if(m_connected.load()){auto*d=m_ipc.GetData();if(d&&d->isPaused)m_ipc.SendCommand(IPCCommand::ResumeRecording);else m_ipc.SendCommand(IPCCommand::PauseRecording);} break;
        case 100: ShellExecuteW(m_hwnd,L"open",L"收款码\\微信.jpg",nullptr,nullptr,SW_SHOW); break;
        case 101: ShellExecuteW(m_hwnd,L"open",L"收款码\\支付宝.jpg",nullptr,nullptr,SW_SHOW); break;
        default: SyncAllSettings(); break; // 复选框、滑块等所有其他控件
    }
}

LRESULT CALLBACK ControlPanel::WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
        case WM_COMMAND:
            if (g_instance && HIWORD(w) == 0) // BN_CLICKED
                g_instance->OnCommand(LOWORD(w));
            return 0;
        case WM_HSCROLL: if(g_instance)g_instance->SyncAllSettings(); return 0;
        case WM_HOTKEY:
            if(w==2&&g_instance){ // Win+Shift+R 切换录制
                if(g_instance->m_isRecording) g_instance->OnCommand(ID_BTN_STOP);
                else g_instance->OnCommand(ID_BTN_START);
            }
            return 0;
        case WM_TRAYICON:
            if(l==WM_LBUTTONDBLCLK){ShowWindow(g_instance->m_hwnd,SW_SHOW);SetForegroundWindow(g_instance->m_hwnd);}
            if(l==WM_RBUTTONDOWN){
                SetForegroundWindow(g_instance->m_hwnd);
                TrackPopupMenu(g_instance->m_trayMenu,TPM_RIGHTBUTTON,0,0,0,g_instance->m_hwnd,nullptr);
            }
            return 0;
        case WM_TIMER: if(w==ID_TIMER_UPDATE&&g_instance)g_instance->OnTimer(); return 0;
        case WM_CTLCOLORSTATIC:{
            SetTextColor((HDC)w,RGB(245,245,247)); SetBkColor((HDC)w,RGB(28,28,30));
            static HBRUSH hb=CreateSolidBrush(RGB(28,28,30)); return (LRESULT)hb;
        }
        case WM_CTLCOLORBTN:{
            SetBkColor((HDC)w,RGB(44,44,46));
            static HBRUSH hb2=CreateSolidBrush(RGB(44,44,46)); return (LRESULT)hb2;
        }
        case WM_ERASEBKGND:{RECT rc;GetClientRect(h,&rc);FillRect((HDC)w,&rc,g_instance?g_instance->m_brushBg:(HBRUSH)GetStockObject(BLACK_BRUSH));return 1;}
        case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0; // 最小化到托盘
        case WM_DESTROY:
            if(g_instance){if(g_instance->m_connected.load())g_instance->m_ipc.SendCommand(IPCCommand::Shutdown);g_instance->m_running.store(false);}
            Shell_NotifyIconW(NIM_DELETE,&g_instance->m_trayData);
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

bool ControlPanel::LaunchRenderProcess(){
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr,path,MAX_PATH);
    std::wstring s(path); auto p=s.find_last_of(L"\\");
    if(p!=std::wstring::npos) s=s.substr(0,p+1)+L"RenderProcess.exe";
    STARTUPINFOW si={sizeof(si)}; PROCESS_INFORMATION pi={};
    if(CreateProcessW(s.c_str(),nullptr,nullptr,nullptr,FALSE,DETACHED_PROCESS,nullptr,nullptr,&si,&pi)){
        m_renderProcess=pi.hProcess; CloseHandle(pi.hThread); return true;
    }
    return false;
}

} // namespace mangke
