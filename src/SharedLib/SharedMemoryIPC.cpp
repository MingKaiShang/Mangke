/**
 * @file SharedMemoryIPC.cpp
 * @brief Shared memory IPC implementation
 */

#include "SharedMemoryIPC.h"
#include <iostream>

namespace mangke {

static const wchar_t* SHARED_MEM_NAME = L"Local\\MangkeSharedMemory";
static const wchar_t* CMD_EVENT_NAME  = L"Local\\MangkeCommandEvent";

SharedMemoryIPC::SharedMemoryIPC() = default;

SharedMemoryIPC::~SharedMemoryIPC() {
    Close();
}

bool SharedMemoryIPC::CreateServer() {
    m_mapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(SharedMemoryData), SHARED_MEM_NAME
    );
    if (!m_mapFile) {
        std::cerr << "[IPC] Create shared memory failed: " << GetLastError() << "\n";
        return false;
    }

    m_data = static_cast<SharedMemoryData*>(
        MapViewOfFile(m_mapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryData))
    );
    if (!m_data) {
        std::cerr << "[IPC] Map shared memory failed\n";
        CloseHandle(m_mapFile);
        m_mapFile = nullptr;
        return false;
    }

    ZeroMemory(m_data, sizeof(SharedMemoryData));

    m_cmdEvent = CreateEventW(nullptr, FALSE, FALSE, CMD_EVENT_NAME);
    if (!m_cmdEvent) {
        std::cerr << "[IPC] Create command event failed\n";
        UnmapViewOfFile(m_data); m_data = nullptr;
        CloseHandle(m_mapFile); m_mapFile = nullptr;
        return false;
    }

    m_isServer = true;
    std::cout << "[IPC] Shared memory server created\n";
    return true;
}

bool SharedMemoryIPC::OpenClient() {
    m_mapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHARED_MEM_NAME);
    if (!m_mapFile) {
        std::cerr << "[IPC] Open shared memory failed: " << GetLastError() << "\n";
        return false;
    }

    m_data = static_cast<SharedMemoryData*>(
        MapViewOfFile(m_mapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemoryData))
    );
    if (!m_data) {
        std::cerr << "[IPC] Map shared memory failed\n";
        CloseHandle(m_mapFile); m_mapFile = nullptr;
        return false;
    }

    m_cmdEvent = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, CMD_EVENT_NAME);
    if (!m_cmdEvent) {
        std::cerr << "[IPC] Open command event failed\n";
        UnmapViewOfFile(m_data); m_data = nullptr;
        CloseHandle(m_mapFile); m_mapFile = nullptr;
        return false;
    }

    std::cout << "[IPC] Shared memory client connected\n";
    return true;
}

void SharedMemoryIPC::SendCommand(IPCCommand cmd) {
    if (!m_data || !m_cmdEvent) return;
    m_data->command = cmd;
    m_data->commandId++;
    SetEvent(m_cmdEvent);
}

bool SharedMemoryIPC::WaitForCommand(uint32_t timeoutMs) {
    if (!m_cmdEvent) return false;
    DWORD result = WaitForSingleObject(m_cmdEvent, timeoutMs);
    if (result == WAIT_OBJECT_0) {
        if (m_data && m_data->commandId != m_lastCommandId) {
            m_lastCommandId = m_data->commandId;
            return true;
        }
    }
    return false;
}

void SharedMemoryIPC::Close() {
    if (m_cmdEvent) { CloseHandle(m_cmdEvent); m_cmdEvent = nullptr; }
    if (m_data) { UnmapViewOfFile(m_data); m_data = nullptr; }
    if (m_mapFile) { CloseHandle(m_mapFile); m_mapFile = nullptr; }
}

} // namespace mangke
