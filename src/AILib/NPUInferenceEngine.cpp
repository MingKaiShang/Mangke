#include "NPUInferenceEngine.h"
#include <dxgi1_6.h>
#include <iostream>

namespace mangke {

std::vector<InferenceDeviceInfo> NPUInferenceEngine::DetectDevices() {
    std::vector<InferenceDeviceInfo> devices;
    IDXGIFactory6* f=nullptr;
    if(SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory6),(void**)&f))){
        IDXGIAdapter1* a=nullptr;
        for(UINT i=0;f->EnumAdapters1(i,&a)!=DXGI_ERROR_NOT_FOUND;i++){
            DXGI_ADAPTER_DESC1 d; a->GetDesc1(&d); a->Release();
            if(d.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            InferenceDeviceInfo info;
            info.name=d.Description; info.dedicatedMemoryMB=d.DedicatedVideoMemory/(1024*1024); info.available=true;
            std::wstring n=d.Description;
            info.type=n.find(L"NPU")!=std::wstring::npos||n.find(L"Intel AI Boost")!=std::wstring::npos?InferenceDevice::NPU:InferenceDevice::GPU;
            info.description=info.type==InferenceDevice::NPU?"NPU":"GPU";
            devices.push_back(info);
        }
        f->Release();
    }
    InferenceDeviceInfo cpu; cpu.type=InferenceDevice::CPU; cpu.name=L"CPU"; cpu.available=true; cpu.description="CPU";
    devices.push_back(cpu);
    return devices;
}

InferenceDevice NPUInferenceEngine::GetBestDevice() {
    for(auto& d:DetectDevices()) if(d.type==InferenceDevice::NPU&&d.available) return InferenceDevice::NPU;
    for(auto& d:DetectDevices()) if(d.type==InferenceDevice::GPU&&d.available) return InferenceDevice::GPU;
    return InferenceDevice::CPU;
}

} // namespace mangke
