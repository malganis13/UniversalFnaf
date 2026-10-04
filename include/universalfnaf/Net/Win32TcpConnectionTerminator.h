#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Net/Win32TcpConnectionTerminator.h
//  Windows implementation of ITcpConnectionTerminator (IPHLPAPI, admin only).
// ---------------------------------------------------------------------------

#include "universalfnaf/Net/ITcpConnectionTerminator.h"

namespace universalfnaf {

class ILogger;

class Win32TcpConnectionTerminator final : public ITcpConnectionTerminator {
public:
    explicit Win32TcpConnectionTerminator(ILogger& logger);

    TcpTerminationResult TerminateOutboundForProcess(std::uint32_t processId) override;

private:
    ILogger& logger_;
};

} // namespace universalfnaf
