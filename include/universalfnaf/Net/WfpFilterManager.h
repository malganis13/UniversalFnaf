#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Net/WfpFilterManager.h
//
//  Windows Filtering Platform implementation of INetworkFilter.
//
//  Design notes (why this is non-invasive):
//    * The engine is opened with FWPM_SESSION_FLAG_DYNAMIC. Every filter and
//      the sub-layer live only as long as this process holds the engine
//      handle. If the utility is killed, crashes or is uninstalled, Windows
//      removes the objects automatically - nothing is left behind in the
//      persistent filter store.
//    * Blocking is expressed with a single, documented filter condition,
//      FWPM_CONDITION_ALE_APP_ID, at the ALE_AUTH_CONNECT (v4/v6) layers.
//      This is exactly the mechanism Windows Firewall itself uses for
//      "Program" rules. No kernel driver, no callout, no packet mangling.
//    * Only the current sub-layer (weight 0x7FFF) is touched; system rules and
//      third-party rules are never enumerated, modified or deleted.
// ---------------------------------------------------------------------------

#include "universalfnaf/Net/INetworkFilter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>   // GUID for the private helper signatures

#include <cstdint>
#include <string>
#include <vector>

namespace universalfnaf {

class ILogger;

class WfpFilterManager final : public INetworkFilter {
public:
    explicit WfpFilterManager(ILogger& logger);
    ~WfpFilterManager() override;

    WfpFilterManager(const WfpFilterManager&) = delete;
    WfpFilterManager& operator=(const WfpFilterManager&) = delete;

    bool Initialize(std::wstring& error) override;
    void Shutdown() override;

    bool IsReady() const override;
    std::wstring Status() const override;

    bool ApplyOutboundBlock(const BlockTarget& target, std::wstring& error) override;
    bool RemoveOutboundBlock(std::wstring& error) override;

    bool IsBlocking() const override;
    BlockTarget CurrentTarget() const override;
    std::vector<BlockRuleHandle> ActiveRules() const override;

private:
    // Creates one FWPM_FILTER0 (BLOCK on ALE_APP_ID) at the given layer.
    // Must be called inside a WFP transaction.
    bool AddAppIdBlockFilter(const GUID& layerKey,
                             const wchar_t* layerName,
                             const wchar_t* ruleSuffix,
                             void* appIdBlob,
                             std::uint64_t& outFilterId,
                             std::wstring& error);

    bool RemoveFiltersLocked(std::vector<std::uint64_t>& ids, std::wstring& error);
    bool EnsureSublayerOpen(std::wstring& error);

    ILogger& logger_;
    void* engine_ = nullptr;                 // HANDLE from FwpmEngineOpen0
    bool blocking_ = false;
    BlockTarget target_;
    std::vector<BlockRuleHandle> rules_;
    std::wstring status_ = L"WFP engine not opened";
};

} // namespace universalfnaf
