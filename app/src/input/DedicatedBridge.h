#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>

namespace audioguia {

// Owns only the bridge process launched by this application. Local preferences
// deliberately stay outside the portable audio configuration.
class DedicatedBridge final {
public:
    explicit DedicatedBridge(HWND window);
    ~DedicatedBridge();
    DedicatedBridge(const DedicatedBridge&) = delete;
    DedicatedBridge& operator=(const DedicatedBridge&) = delete;
    void tick();
    void configure();
    void diagnostics();
    bool accepts(ULONG_PTR session) const;
    bool active() const { return accepts(session_); }
    std::string status() const;
private:
    void start();
    void stop();
    HWND window_;
    HANDLE process_{nullptr};
    HANDLE job_{nullptr};
    DWORD started_{0};
    ULONG_PTR session_{0};
    int state_{0};
    std::string error_;
    std::filesystem::path settings_;
};

} // namespace audioguia
