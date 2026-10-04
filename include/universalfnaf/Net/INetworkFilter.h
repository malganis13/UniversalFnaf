#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Net/INetworkFilter.h
//  Abstraction over "block outbound traffic for one target program".
//
//  Implementations MUST use only officially supported OS mechanisms
//  (Windows Filtering Platform or the Windows Firewall COM API). No NDIS
//  filter driver, no WFP callout driver, no LSP, no global hooking.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace universalfnaf {

// A single outbound-block target. The image path is the authoritative key:
// it is what the WFP ALE_APP_ID condition is built from and what survives a
// process restart (a PID does not).
struct BlockTarget {
    std::uint32_t processId = 0;
    std::wstring  imagePath;        // full path to the .exe
    std::wstring  imageName;        // "game.exe" (display only)
};

// Identifies one kernel filter object created by the implementation.
struct BlockRuleHandle {
    std::uint64_t id = 0;
    std::wstring  layerName;
};

class INetworkFilter {
public:
    virtual ~INetworkFilter() = default;

    // Opens the filtering engine. Safe to call more than once.
    virtual bool Initialize(std::wstring& error) = 0;

    // Removes every rule this instance created and closes the engine.
    virtual void Shutdown() = 0;

    virtual bool IsReady() const = 0;
    virtual std::wstring Status() const = 0;

    // Idempotent: replaces any previously applied rule set.
    virtual bool ApplyOutboundBlock(const BlockTarget& target, std::wstring& error) = 0;
    virtual bool RemoveOutboundBlock(std::wstring& error) = 0;

    virtual bool IsBlocking() const = 0;
    virtual BlockTarget CurrentTarget() const = 0;
    virtual std::vector<BlockRuleHandle> ActiveRules() const = 0;
};

} // namespace universalfnaf
