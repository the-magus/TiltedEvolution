
#include "launcher.h"
#include <FunctionHook.hpp>
#include <mutex>
#include <TiltedCore/Initializer.hpp>
#include <intrin.h> // _ReturnAddress
#include <cstdint>  // uintptr_t
#include <cstdio>   // sprintf_s

static std::once_flag s_initGuard;
static uint16_t(WINAPI* Real_crtGetShowWindowMode)() = nullptr;
static int(WINAPI* Real_ismbbled)(uint32_t) = nullptr;

void TP_GetStartupInfoW(LPSTARTUPINFOW apInfo) noexcept
{
    std::call_once(s_initGuard, []() { launcher::InitClient(); });
    GetStartupInfoW(apInfo);
}

int TP_ismbblead(uint32_t c)
{
    std::call_once(s_initGuard, []() { launcher::InitClient(); });
    return Real_ismbbled(c);
}

// this is more of a workaround, till we add SEH table support.
void WINAPI TP_RaiseException(DWORD dwExceptionCode, DWORD dwExceptionFlags, DWORD nNumberOfArguments, const ULONG_PTR* lpArguments)
{
    if (dwExceptionCode == 0x406D1388 && !IsDebuggerPresent())
        return; // thread naming

    RaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);
}

// --- Early-boot exit/exception forensics (personal Wine build) -------------
// Pure observation: the VEH only logs then continues the search; the exit hooks
// log the caller, then forward to the real API. game RVA = addr - module base,
// valid because ExeLoader maps SkyrimSE.exe over GetModuleHandleW(nullptr).
static LONG CALLBACK TP_ForensicVeh(PEXCEPTION_POINTERS apInfo)
{
    const DWORD code = apInfo->ExceptionRecord->ExceptionCode;
    if ((code & 0xF0000000u) == 0xC0000000u) // STATUS_* hard faults; skips 0x406D1388 and C++ EH 0xE06D7363
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto addr = reinterpret_cast<uintptr_t>(apInfo->ExceptionRecord->ExceptionAddress);
        char buf[160];
        sprintf_s(buf, "EXC code=%08X addr=%p game+0x%llX", code, reinterpret_cast<void*>(addr), static_cast<unsigned long long>(addr - base));
        launcher::Trace(buf);
    }
    return EXCEPTION_CONTINUE_SEARCH; // never alter dispatch
}

static void WINAPI TP_ExitProcess(UINT uExitCode)
{
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    char buf[160];
    sprintf_s(buf, "EXIT ExitProcess code=%u caller=%p game+0x%llX", uExitCode, reinterpret_cast<void*>(caller), static_cast<unsigned long long>(caller - base));
    launcher::Trace(buf);
    ExitProcess(uExitCode);
}

static BOOL WINAPI TP_TerminateProcess(HANDLE hProcess, UINT uExitCode)
{
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (hProcess == GetCurrentProcess() || hProcess == (HANDLE)-1)
    {
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        char buf[160];
        sprintf_s(buf, "EXIT TerminateProcess code=%u caller=%p game+0x%llX", uExitCode, reinterpret_cast<void*>(caller), static_cast<unsigned long long>(caller - base));
        launcher::Trace(buf);
    }
    return TerminateProcess(hProcess, uExitCode);
}

void InstallStartHook()
{
    AddVectoredExceptionHandler(1, TP_ForensicVeh);
    TP_HOOK_IAT2("Kernel32.dll", "GetStartupInfoW", TP_GetStartupInfoW);
    TP_HOOK_IAT2("Kernel32.dll", "RaiseException", TP_RaiseException);
    TP_HOOK_IAT2("Kernel32.dll", "ExitProcess", TP_ExitProcess);
    TP_HOOK_IAT2("Kernel32.dll", "TerminateProcess", TP_TerminateProcess);
};
