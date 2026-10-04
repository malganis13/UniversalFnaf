#include "universalfnaf/Net/Win32TcpConnectionTerminator.h"

// winsock2.h must be included before windows.h; the project headers pull in
// windows.h, so they come after the network headers on purpose.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2ipdef.h>   // _WS2IPDEF_ gates the IPv6 MIB structures in tcpmib.h
#include <iphlpapi.h>
#include <tcpmib.h>

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/WinError.h"

#include <unordered_set>
#include <vector>

namespace universalfnaf {
namespace {

// MIB_TCPROW::dwLocalPort holds the port in network byte order in its low word.
std::uint16_t HostPort(std::uint32_t networkPort)
{
    return static_cast<std::uint16_t>(((networkPort & 0x00FFu) << 8) |
                                      ((networkPort >> 8) & 0x00FFu));
}

std::vector<std::uint8_t> QueryTcpTable(ULONG addressFamily, TCP_TABLE_CLASS tableClass,
                                        DWORD& status)
{
    DWORD size = 0;
    status = ::GetExtendedTcpTable(nullptr, &size, FALSE, addressFamily, tableClass, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return {};
    }

    std::vector<std::uint8_t> buffer(size);
    status = ::GetExtendedTcpTable(buffer.data(), &size, FALSE, addressFamily, tableClass, 0);
    if (status != ERROR_SUCCESS) {
        buffer.clear();
    }
    return buffer;
}

std::size_t CountIpv6Sessions(std::uint32_t processId)
{
    DWORD status = ERROR_SUCCESS;
    const std::vector<std::uint8_t> buffer = QueryTcpTable(AF_INET6, TCP_TABLE_OWNER_PID_ALL, status);
    if (buffer.empty()) {
        return 0;
    }

    const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.data());
    std::size_t count = 0;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_TCP6ROW_OWNER_PID& row = table->table[index];
        if (row.dwOwningPid == processId && row.dwState != MIB_TCP_STATE_LISTEN) {
            ++count;
        }
    }
    return count;
}

} // namespace

Win32TcpConnectionTerminator::Win32TcpConnectionTerminator(ILogger& logger) : logger_(logger) {}

TcpTerminationResult Win32TcpConnectionTerminator::TerminateOutboundForProcess(
    std::uint32_t processId)
{
    TcpTerminationResult result;
    if (processId == 0) {
        return result;
    }

    DWORD status = ERROR_SUCCESS;
    const std::vector<std::uint8_t> buffer = QueryTcpTable(AF_INET, TCP_TABLE_OWNER_PID_ALL, status);
    if (buffer.empty()) {
        if (status != ERROR_SUCCESS) {
            LogWarn(logger_, "TCP", Sprintf("GetExtendedTcpTable failed: %s",
                                            Utf8FromWide(FormatWin32Message(status)).c_str()));
        }
        return result;
    }

    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());

    // Pass 1: ports this process listens on. A session whose local port is one
    // of them was accepted from the outside (inbound) and must survive.
    std::unordered_set<std::uint16_t> listeningPorts;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_TCPROW_OWNER_PID& row = table->table[index];
        if (row.dwOwningPid == processId && row.dwState == MIB_TCP_STATE_LISTEN) {
            listeningPorts.insert(HostPort(row.dwLocalPort));
        }
    }

    // Pass 2: outbound-initiated sessions of the target process.
    std::vector<MIB_TCPROW> doomed;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
        const MIB_TCPROW_OWNER_PID& row = table->table[index];
        if (row.dwOwningPid != processId) {
            continue;
        }
        if (row.dwState == MIB_TCP_STATE_LISTEN || row.dwState == MIB_TCP_STATE_DELETE_TCB) {
            continue;
        }
        if (listeningPorts.count(HostPort(row.dwLocalPort)) != 0) {
            ++result.inboundPreserved;
            continue;
        }

        MIB_TCPROW entry{};
        entry.dwState = MIB_TCP_STATE_DELETE_TCB;
        entry.dwLocalAddr = row.dwLocalAddr;
        entry.dwLocalPort = row.dwLocalPort;
        entry.dwRemoteAddr = row.dwRemoteAddr;
        entry.dwRemotePort = row.dwRemotePort;
        doomed.push_back(entry);
    }

    for (MIB_TCPROW& entry : doomed) {
        const DWORD setStatus = ::SetTcpEntry(&entry);
        if (setStatus == NO_ERROR) {
            ++result.terminated;
        } else {
            ++result.failed;
            LogWarn(logger_, "TCP", Sprintf("SetTcpEntry failed: %s",
                                            Utf8FromWide(FormatWin32Message(setStatus)).c_str()));
        }
    }

    result.ipv6NotSupported = CountIpv6Sessions(processId);

    LogWarn(logger_, "TCP",
            Sprintf("terminate pid=%lu: deleted=%zu inboundKept=%zu ipv6Untouched=%zu failed=%zu",
                    static_cast<unsigned long>(processId), result.terminated,
                    result.inboundPreserved, result.ipv6NotSupported, result.failed));
    return result;
}

} // namespace universalfnaf
