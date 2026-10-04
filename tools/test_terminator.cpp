// Temporary verification harness for Win32TcpConnectionTerminator.
// Build (from the project root, in an x64 developer prompt):
//   cl /nologo /std:c++20 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
//      /D_WIN32_WINNT=0x0A00 /Iinclude ^
//      tools\test_terminator.cpp src\Net\Win32TcpConnectionTerminator.cpp ^
//      src\Common\Logger.cpp src\Common\Utils.cpp src\Common\WinError.cpp ^
//      /Fe:build-x64\test_terminator.exe /link iphlpapi.lib ws2_32.lib shell32.lib ole32.lib
//
// Usage: test_terminator.exe <pid>
#include "universalfnaf/Net/Win32TcpConnectionTerminator.h"

#include "universalfnaf/Common/Logger.h"

#include <cstdio>
#include <cstdlib>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        std::printf("usage: test_terminator.exe <pid>\n");
        return 1;
    }

    universalfnaf::FileLogger logger;
    logger.Open(L"test_terminator.log", universalfnaf::LogLevel::Debug);

    universalfnaf::Win32TcpConnectionTerminator terminator(logger);
    const auto processId = static_cast<std::uint32_t>(std::wcstoul(argv[1], nullptr, 10));
    const universalfnaf::TcpTerminationResult result = terminator.TerminateOutboundForProcess(processId);

    std::printf("pid=%u terminated=%zu inboundKept=%zu ipv6Untouched=%zu failed=%zu\n",
                processId, result.terminated, result.inboundPreserved,
                result.ipv6NotSupported, result.failed);
    return result.terminated > 0 ? 0 : 2;
}
