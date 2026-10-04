#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Net/ITcpConnectionTerminator.h
//
//  A WFP filter at ALE_AUTH_CONNECT stops *new* connections. Connections that
//  were already established before the block was engaged keep flowing until one
//  side closes them, which is visible to the user as "upload still goes out".
//
//  This interface closes that gap using documented IPHLPAPI only:
//    GetExtendedTcpTable(TCP_TABLE_OWNER_PID_ALL) + SetTcpEntry(DELETE_TCB)
//  No driver, no hooking, no packet mangling; only entries owned by the target
//  PID are touched.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace universalfnaf {

struct TcpTerminationResult {
    std::size_t terminated = 0;       // TCP sessions deleted (IPv4)
    std::size_t inboundPreserved = 0; // accepted on the process's listening ports
    std::size_t ipv6NotSupported = 0; // Windows has no SetTcpEntry equivalent for IPv6
    std::size_t failed = 0;           // SetTcpEntry refused
};

class ITcpConnectionTerminator {
public:
    virtual ~ITcpConnectionTerminator() = default;

    // Deletes outbound-initiated TCP sessions owned by `processId`.
    // Sessions whose local port is a listening port of the same process are
    // treated as inbound and left alone, so the "traffic to me still works"
    // property is preserved.
    virtual TcpTerminationResult TerminateOutboundForProcess(std::uint32_t processId) = 0;
};

} // namespace universalfnaf
