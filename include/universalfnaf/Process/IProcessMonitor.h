#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Process/IProcessMonitor.h
//  Enumerates user-facing processes so the operator can pick a block target.
//
//  Access policy (non-invasive by construction):
//    * Enumeration uses CreateToolhelp32Snapshot - no handle to any process.
//    * The image path is read with OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)
//      followed by QueryFullProcessImageNameW. That right grants access to
//      *metadata* only: it cannot read or write a single byte of another
//      process's address space.
//    * PROCESS_VM_READ, PROCESS_VM_WRITE, PROCESS_VM_OPERATION,
//      PROCESS_CREATE_THREAD and PROCESS_SUSPEND_RESUME are never requested.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace universalfnaf {

struct ProcessInfo {
    std::uint32_t processId = 0;
    std::uint32_t parentProcessId = 0;
    std::uint32_t sessionId = 0;
    bool isElevated = false;
    bool hasVisibleWindow = false;
    bool isCurrentProcess = false;

    std::wstring imageName;          // "game.exe"
    std::wstring imagePath;          // "C:\Games\game.exe" ("" if unavailable)

    std::vector<std::uint8_t> iconRgba;   // RGBA8, top-down, straight alpha
    int iconWidth = 0;
    int iconHeight = 0;
};

class IProcessMonitor {
public:
    virtual ~IProcessMonitor() = default;

    // Full refresh. Returns a stable, alphabetically ordered list.
    virtual std::vector<ProcessInfo> Snapshot(std::wstring& error) = 0;

    // PID -> full image path. Used when a rule has to be (re)built from a PID.
    virtual bool ResolveImagePath(std::uint32_t processId,
                                  std::wstring& imagePath,
                                  std::wstring& error) = 0;

    // Fills iconRgba/iconWidth/iconHeight lazily (icons are the expensive part).
    virtual bool EnsureIcon(ProcessInfo& info) = 0;
};

} // namespace universalfnaf
