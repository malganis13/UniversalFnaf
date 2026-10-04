#include "universalfnaf/Net/WfpFilterManager.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <rpc.h>     // RPC_C_AUTHN_WINNT
#include <fwpmu.h>

#include <algorithm>
#include <iterator>

namespace universalfnaf {
namespace {

// Our private sub-layer. The GUID is stable across runs so the objects are
// recognisable in `netsh wfp show state` output for auditing.
// {5A2E1B74-9C3F-4E8A-B1C7-2F7D4A9E6B31}
constexpr GUID kSubLayerKey = {
    0x5a2e1b74, 0x9c3f, 0x4e8a, {0xb1, 0xc7, 0x2f, 0x7d, 0x4a, 0x9e, 0x6b, 0x31}};

constexpr wchar_t kSessionName[] = L"UniversalFnaf";
constexpr wchar_t kSessionDescription[] = L"Dynamic, self-cleaning outbound filtering session";
constexpr wchar_t kSubLayerName[] = L"UniversalFnaf Outbound Filter";
constexpr wchar_t kSubLayerDescription[] =
    L"Blocks ALE_AUTH_CONNECT traffic for a single user-selected program";

// WFP orders filters by (sub-layer weight, filter weight). The Windows
// Firewall sub-layer sits far below this value, so our block wins over its
// default permits without ever touching the firewall's own rules.
constexpr UINT16 kSubLayerWeight = 0x7FFF;
constexpr UINT8 kFilterWeight = 0x0F;

struct LayerSpec {
    const GUID* layer;
    const wchar_t* name;
};

const LayerSpec kAleConnectLayers[] = {
    {&FWPM_LAYER_ALE_AUTH_CONNECT_V4, L"ALE_AUTH_CONNECT_V4"},
    {&FWPM_LAYER_ALE_AUTH_CONNECT_V6, L"ALE_AUTH_CONNECT_V6"},
};

std::wstring DescribeWfpError(const wchar_t* what, DWORD status)
{
    std::wstring text(what);
    text += L" (error ";
    text += std::to_wstring(status);
    text += L"): ";
    text += FormatWin32Message(status);
    return text;
}

} // namespace

WfpFilterManager::WfpFilterManager(ILogger& logger) : logger_(logger) {}

WfpFilterManager::~WfpFilterManager()
{
    Shutdown();
}

bool WfpFilterManager::EnsureSublayerOpen(std::wstring& error)
{
    if (engine_ == nullptr) {
        error = L"WFP engine is not open";
        return false;
    }

    DWORD status = ::FwpmTransactionBegin0(engine_, 0);
    if (status != ERROR_SUCCESS) {
        error = DescribeWfpError(L"FwpmTransactionBegin0", status);
        return false;
    }

    FWPM_SUBLAYER0 subLayer{};
    subLayer.subLayerKey = kSubLayerKey;
    subLayer.displayData.name = const_cast<wchar_t*>(kSubLayerName);
    subLayer.displayData.description = const_cast<wchar_t*>(kSubLayerDescription);
    subLayer.weight = kSubLayerWeight;

    status = ::FwpmSubLayerAdd0(engine_, &subLayer, nullptr);
    if (status == FWP_E_ALREADY_EXISTS) {
        // A persistent sub-layer with our GUID exists (for example it was
        // created by an older build that did not use a dynamic session).
        // Reuse it instead of failing; we still never delete foreign objects.
        LogWarn(logger_, "WFP", "sub-layer already exists, reusing it");
        status = ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS) {
        ::FwpmTransactionAbort0(engine_);
        error = DescribeWfpError(L"FwpmSubLayerAdd0", status);
        return false;
    }

    status = ::FwpmTransactionCommit0(engine_);
    if (status != ERROR_SUCCESS) {
        ::FwpmTransactionAbort0(engine_);
        error = DescribeWfpError(L"FwpmTransactionCommit0", status);
        return false;
    }

    LogInfo(logger_, "WFP", Sprintf("sub-layer ready (weight=0x%04X)", kSubLayerWeight));
    return true;
}

bool WfpFilterManager::Initialize(std::wstring& error)
{
    if (engine_ != nullptr) {
        return true;
    }

    FWPM_SESSION0 session{};
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;   // auto-cleanup, our key guarantee
    session.displayData.name = const_cast<wchar_t*>(kSessionName);
    session.displayData.description = const_cast<wchar_t*>(kSessionDescription);
    session.txnWaitTimeoutInMSec = 5000;
    session.kernelMode = FALSE;

    const DWORD status =
        ::FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine_);
    if (status != ERROR_SUCCESS) {
        engine_ = nullptr;
        error = DescribeWfpError(L"FwpmEngineOpen0", status);
        if (status == ERROR_ACCESS_DENIED) {
            error += L" - the process must run elevated (requireAdministrator)";
        }
        status_ = L"WFP engine open failed";
        LogError(logger_, "WFP", Utf8FromWide(error));
        return false;
    }

    LogInfo(logger_, "WFP", "dynamic filtering session opened");

    if (!EnsureSublayerOpen(error)) {
        ::FwpmEngineClose0(engine_);
        engine_ = nullptr;
        status_ = L"WFP sub-layer setup failed";
        LogError(logger_, "WFP", Utf8FromWide(error));
        return false;
    }

    status_ = L"WFP engine ready (dynamic session)";
    return true;
}

void WfpFilterManager::Shutdown()
{
    if (engine_ == nullptr) {
        return;
    }

    std::wstring ignored;
    if (blocking_) {
        RemoveOutboundBlock(ignored);
    }

    // Closing a dynamic session removes the session's filters and sub-layer.
    ::FwpmEngineClose0(engine_);
    engine_ = nullptr;
    blocking_ = false;
    rules_.clear();
    status_ = L"WFP engine closed";
    LogInfo(logger_, "WFP", "dynamic filtering session closed (all filters removed by the OS)");
}

bool WfpFilterManager::IsReady() const
{
    return engine_ != nullptr;
}

std::wstring WfpFilterManager::Status() const
{
    return status_;
}

bool WfpFilterManager::IsBlocking() const
{
    return blocking_;
}

BlockTarget WfpFilterManager::CurrentTarget() const
{
    return target_;
}

std::vector<BlockRuleHandle> WfpFilterManager::ActiveRules() const
{
    return rules_;
}

bool WfpFilterManager::AddAppIdBlockFilter(const GUID& layerKey,
                                           const wchar_t* layerName,
                                           const wchar_t* ruleSuffix,
                                           void* appIdBlob,
                                           std::uint64_t& outFilterId,
                                           std::wstring& error)
{
    if (appIdBlob == nullptr) {
        error = L"app-id blob is null";
        return false;
    }

    FWPM_FILTER_CONDITION0 condition{};
    condition.fieldKey = FWPM_CONDITION_ALE_APP_ID;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    condition.conditionValue.byteBlob = static_cast<FWP_BYTE_BLOB*>(appIdBlob);

    std::wstring displayName = L"UniversalFnaf block ";
    displayName += ruleSuffix;
    displayName += L" @ ";
    displayName += layerName;

    FWPM_FILTER0 filter{};
    filter.displayData.name = displayName.data();
    filter.displayData.description =
        const_cast<wchar_t*>(L"Outbound block for the user-selected program");
    filter.layerKey = layerKey;
    filter.action.type = FWP_ACTION_BLOCK;
    filter.subLayerKey = kSubLayerKey;
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = kFilterWeight;
    filter.numFilterConditions = 1;
    filter.filterCondition = &condition;
    filter.flags = 0;   // never FWPM_FILTER_FLAG_PERSISTENT: this rule is session-scoped

    UINT64 filterId = 0;
    const DWORD status = ::FwpmFilterAdd0(engine_, &filter, nullptr, &filterId);
    if (status != ERROR_SUCCESS) {
        error = DescribeWfpError(L"FwpmFilterAdd0", status);
        return false;
    }

    outFilterId = filterId;
    LogInfo(logger_, "WFP", Sprintf("filter added: id=%llu layer=%s",
                                    static_cast<unsigned long long>(filterId),
                                    Utf8FromWide(layerName).c_str()));
    return true;
}

bool WfpFilterManager::ApplyOutboundBlock(const BlockTarget& target, std::wstring& error)
{
    if (engine_ == nullptr) {
        error = L"WFP engine is not available (run elevated and check the log)";
        return false;
    }
    if (target.imagePath.empty()) {
        error = L"No target image path was resolved for the selected process";
        return false;
    }
    if (!FileExists(target.imagePath)) {
        error = L"Target image no longer exists: " + target.imagePath;
        return false;
    }

    // Replace semantics: drop the old rule set first, then add the new one.
    if (!rules_.empty()) {
        std::wstring removalError;
        if (!RemoveOutboundBlock(removalError)) {
            error = L"Could not remove the previous rule set: " + removalError;
            return false;
        }
    }

    FWP_BYTE_BLOB* appId = nullptr;
    DWORD status = ::FwpmGetAppIdFromFileName0(target.imagePath.c_str(), &appId);
    if (status != ERROR_SUCCESS || appId == nullptr) {
        error = DescribeWfpError(L"FwpmGetAppIdFromFileName0", status);
        error += L" [";
        error += target.imagePath;
        error += L"]";
        LogError(logger_, "WFP", Utf8FromWide(error));
        return false;
    }
    const auto appIdGuard = MakeScopeGuard([&appId]() noexcept {
        void* raw = appId;
        if (raw != nullptr) {
            ::FwpmFreeMemory0(&raw);
        }
        appId = nullptr;
    });

    status = ::FwpmTransactionBegin0(engine_, 0);
    if (status != ERROR_SUCCESS) {
        error = DescribeWfpError(L"FwpmTransactionBegin0", status);
        return false;
    }

    std::vector<BlockRuleHandle> created;
    created.reserve(std::size(kAleConnectLayers));

    for (const LayerSpec& spec : kAleConnectLayers) {
        std::uint64_t filterId = 0;
        if (!AddAppIdBlockFilter(*spec.layer, spec.name, target.imageName.c_str(),
                                 appId, filterId, error)) {
            ::FwpmTransactionAbort0(engine_);   // atomic: nothing is left half-applied
            LogError(logger_, "WFP", Utf8FromWide(error));
            return false;
        }
        created.push_back(BlockRuleHandle{filterId, spec.name});
    }

    status = ::FwpmTransactionCommit0(engine_);
    if (status != ERROR_SUCCESS) {
        ::FwpmTransactionAbort0(engine_);
        error = DescribeWfpError(L"FwpmTransactionCommit0", status);
        LogError(logger_, "WFP", Utf8FromWide(error));
        return false;
    }

    rules_ = std::move(created);
    blocking_ = true;
    target_ = target;
    status_ = L"BLOCKING outbound traffic for " + target.imageName;

    LogWarn(logger_, "WFP", Sprintf("outbound BLOCK engaged for pid=%lu image=%s filters=%zu",
                                    static_cast<unsigned long>(target.processId),
                                    Utf8FromWide(target.imagePath).c_str(),
                                    rules_.size()));
    return true;
}

bool WfpFilterManager::RemoveFiltersLocked(std::vector<std::uint64_t>& ids, std::wstring& error)
{
    if (ids.empty()) {
        return true;
    }

    DWORD status = ::FwpmTransactionBegin0(engine_, 0);
    if (status != ERROR_SUCCESS) {
        error = DescribeWfpError(L"FwpmTransactionBegin0", status);
        return false;
    }

    for (const std::uint64_t id : ids) {
        status = ::FwpmFilterDeleteById0(engine_, id);
        if (status != ERROR_SUCCESS && status != FWP_E_FILTER_NOT_FOUND) {
            ::FwpmTransactionAbort0(engine_);
            error = DescribeWfpError(L"FwpmFilterDeleteById0", status);
            return false;
        }
    }

    status = ::FwpmTransactionCommit0(engine_);
    if (status != ERROR_SUCCESS) {
        ::FwpmTransactionAbort0(engine_);
        error = DescribeWfpError(L"FwpmTransactionCommit0", status);
        return false;
    }

    ids.clear();
    return true;
}

bool WfpFilterManager::RemoveOutboundBlock(std::wstring& error)
{
    if (engine_ == nullptr) {
        blocking_ = false;
        rules_.clear();
        return true;
    }

    std::vector<std::uint64_t> ids;
    ids.reserve(rules_.size());
    for (const BlockRuleHandle& rule : rules_) {
        ids.push_back(rule.id);
    }

    if (!RemoveFiltersLocked(ids, error)) {
        LogError(logger_, "WFP", Utf8FromWide(error));
        return false;
    }

    rules_.clear();
    blocking_ = false;
    target_ = BlockTarget{};
    status_ = L"Outbound traffic is allowed";
    LogInfo(logger_, "WFP", "outbound block released; traffic is allowed again");
    return true;
}

} // namespace universalfnaf
