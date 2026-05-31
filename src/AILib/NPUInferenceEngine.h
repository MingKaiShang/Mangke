#pragma once
#include "AICaptureCommon.h"
#include <vector>

namespace mangke {

class NPUInferenceEngine {
public:
    NPUInferenceEngine()=default;
    ~NPUInferenceEngine()=default;
    static std::vector<InferenceDeviceInfo> DetectDevices();
    static InferenceDevice GetBestDevice();
    bool Initialize(void*) { m_initialized=true; return true; }
    bool IsInitialized() const { return m_initialized; }
    void Release() { m_initialized=false; }
private:
    bool m_initialized=false;
};

} // namespace mangke
