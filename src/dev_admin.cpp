#include "dev_admin.h"
#ifdef HHP_DEV
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdio>

namespace hhp::dev_admin {
namespace {

using IsAdminFn = uint32_t (*)(int, void*);
IsAdminFn g_receiveNext{}, g_processNext{};
std::wstring g_flag;
std::atomic<uint64_t> g_checkedAt{0};
std::atomic<bool> g_enabled{false};
std::atomic<uint64_t> g_receiveCalls{0}, g_processCalls{0}, g_reportAt{0};

void Log(const char* s) {
    if (auto* h = greeneye::Host(); h && h->log) h->log(s);
}

// The flag file, looked at once a second.
bool Enabled() {
    const uint64_t now = GetTickCount64();
    if (now - g_checkedAt.load() >= 1000) {
        g_checkedAt = now;
        const bool on = GetFileAttributesW(g_flag.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (on != g_enabled.exchange(on))
            Log(on ? "Hollow Halls Progression [dev]: admin world-settings commands accepted (admin.enable)"
                   : "Hollow Halls Progression [dev]: admin world-settings commands refused again");
    }
    return g_enabled.load();
}

// Once every 10 s while enabled: how often each check ran and what the game itself answered.
void Report(int player, uint32_t receiveOriginal) {
    const uint64_t now = GetTickCount64();
    if (now < g_reportAt.load()) return;
    g_reportAt = now + 10000;
    char line[200];
    sprintf_s(line, "Hollow Halls Progression [dev]: admin checks: receive %llu, processing %llu (player %d, game's own answer %u)",
              static_cast<unsigned long long>(g_receiveCalls.load()), static_cast<unsigned long long>(g_processCalls.load()), player,
              receiveOriginal);
    Log(line);
}

uint32_t ReceiveIsAdmin(int player, void* table) {
    greeneye::Lease lease;
    if (lease) {
        ++g_receiveCalls;
        if (Enabled()) Report(player, g_receiveNext(player, table));
    }
    if (lease && Enabled()) return 1;
    return g_receiveNext(player, table);
}

uint32_t ProcessIsAdmin(int player, void* table) {
    greeneye::Lease lease;
    if (lease) ++g_processCalls;
    if (lease && Enabled()) return 1;
    return g_processNext(player, table);
}

}  // namespace

bool Init(const GreeneyeHost* host) {
    if (host->side != kGreeneyeServer) return true;
    g_flag = std::wstring(host->modDir) + L"admin.enable";
    // The call to the "is admin" check in the receive (after storing a double at [rbp+0xb4c]) and
    // in the processing (player index from [rax], table in rdx); each followed by test al / jz.
    auto* receive = host->find("F2 0F 11 85 4C 0B 00 00 E8 ?? ?? ?? ?? 84 C0 0F 84 DD 02 00 00", nullptr);
    auto* process = host->find("48 8B D3 8B 08 E8 ?? ?? ?? ?? 84 C0 0F 84 3F 1B 00 00", nullptr);
    if (!receive || !process) {
        Log("Hollow Halls Progression [dev]: admin check call sites not found");
        return false;
    }
    g_receiveNext = reinterpret_cast<IsAdminFn>(host->redirectCall(receive + 8, reinterpret_cast<void*>(ReceiveIsAdmin)));
    g_processNext = reinterpret_cast<IsAdminFn>(host->redirectCall(process + 5, reinterpret_cast<void*>(ProcessIsAdmin)));
    Log(g_receiveNext && g_processNext ? "Hollow Halls Progression [dev]: admin checks redirected; create admin.enable to accept world settings"
                                       : "Hollow Halls Progression [dev]: admin check redirects failed");
    return g_receiveNext && g_processNext;
}

}  // namespace hhp::dev_admin
#else
namespace hhp::dev_admin {
bool Init(const GreeneyeHost*) { return true; }
}  // namespace hhp::dev_admin
#endif
