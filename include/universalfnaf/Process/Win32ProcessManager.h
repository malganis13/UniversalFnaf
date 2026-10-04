#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Process/Win32ProcessManager.h
//  Win32 implementation of IProcessMonitor (Toolhelp32 snapshot + SHELL32
//  icon extraction + GDI+ pixel conversion).
// ---------------------------------------------------------------------------

#include "universalfnaf/Process/IProcessMonitor.h"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace universalfnaf {

class ILogger;
class GdiPlusSession;

class Win32ProcessManager final : public IProcessMonitor {
public:
    struct Options {
        // Show svchost.exe, explorer.exe, Defender, DWM ... in the list.
        bool includeSystemProcesses = false;
        // Only processes that own a visible top-level window.
        bool requireVisibleWindow = false;
        // Only processes in the interactive session of this utility.
        bool onlyCurrentSession = true;
        // Drop entries whose image path cannot be read (they cannot be blocked).
        bool requireImagePath = true;
        // Extract SHELL32 icons (cached per image path).
        bool resolveIcons = true;
        // Hard cap so a runaway system cannot stall the UI thread.
        std::size_t maxProcesses = 512;
    };

    Win32ProcessManager(ILogger& logger,
                        const GdiPlusSession& gdiPlus,
                        Options options = {});

    std::vector<ProcessInfo> Snapshot(std::wstring& error) override;
    bool ResolveImagePath(std::uint32_t processId,
                          std::wstring& imagePath,
                          std::wstring& error) override;
    bool EnsureIcon(ProcessInfo& info) override;

    // True for the well-known OS/service components and for this utility.
    static bool IsSystemProcessName(const std::wstring& lowerCaseImageName);

    const Options& options() const noexcept { return options_; }
    Options& options() noexcept { return options_; }

private:
    struct IconData {
        std::vector<std::uint8_t> rgba;
        int width = 0;
        int height = 0;
        bool valid = false;
    };

    const IconData* LookupOrLoadIcon(const std::wstring& imagePath);
    static bool IsProcessElevatedByHandle(void* processHandle);

    ILogger& logger_;
    const GdiPlusSession& gdiPlus_;
    Options options_;
    std::unordered_map<std::wstring, IconData> iconCache_;
};

} // namespace universalfnaf
