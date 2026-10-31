#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "progress.h"
#include "greeneye.h"
#include <windows.h>
#include <cstdio>
#include <cwctype>
#include <map>
#include <mutex>
#include <string>

namespace hhp::progress {
namespace {

std::mutex g_lock;
std::wstring g_file;
Settings g_settings;
std::map<uint64_t, protocol::Masks> g_players;
bool g_dirty = false;

void Log(const char* s) {
    if (auto* h = greeneye::Host(); h && h->log) h->log(s);
}

bool Switch(const wchar_t* section, const wchar_t* key, bool fallback, const std::wstring& ini) {
    wchar_t value[32]{};
    GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false", value, 32, ini.c_str());
    std::wstring v(value);
    for (auto& c : v) c = wchar_t(towlower(c));
    while (!v.empty() && iswspace(v.back())) v.pop_back();
    while (!v.empty() && iswspace(v.front())) v.erase(v.begin());
    if (v == L"true" || v == L"on" || v == L"yes" || v == L"1") return true;
    if (v == L"false" || v == L"off" || v == L"no" || v == L"0") return false;
    return fallback;
}

// One player a line: "<account in hex>=<mask hall 0>,...,<mask hall 5>".
void Load() {
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_file.c_str(), L"rb") || !f) return;
    char line[256];
    int players = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long account = 0;
        unsigned m[halls::kHallCount]{};
        static_assert(halls::kHallCount == 6, "progress.txt lines carry six halls");
        if (line[0] == '#' || sscanf_s(line, "%llx=%u,%u,%u,%u,%u,%u", &account, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 7 || !account)
            continue;
        auto& p = g_players[account];
        for (int h = 0; h < halls::kHallCount; ++h) p.hall[h] = uint8_t(m[h] & ((1u << halls::kHalls[h].count) - 1));
        ++players;
    }
    fclose(f);
    char text[160];
    sprintf_s(text, "Hollow Halls Progression: loaded the Return Beacons of %d player(s)", players);
    Log(text);
}

}  // namespace

void Init(const wchar_t* dir) {
    std::lock_guard guard(g_lock);
    std::wstring base(dir ? dir : L"");
    auto ini = base + L"hollow-halls-progression.ini";
    g_settings.resetOnBoss = Switch(L"Progress", L"ResetOnBossKill", false, ini);
    g_settings.unlockAll = Switch(L"Testing", L"UnlockAll", false, ini);
    if (g_settings.unlockAll) Log("Hollow Halls Progression: testing, every Return Beacon counts as reached ([Testing] UnlockAll=true)");
    g_file = base + L"progress.txt";
    g_players.clear();
    Load();
    Log(g_settings.resetOnBoss ? "Hollow Halls Progression: a hall's Return Beacons reset when its boss fight is over (ResetOnBossKill=true)"
                               : "Hollow Halls Progression: Return Beacons stay unlocked for good (ResetOnBossKill=false)");
}

Settings Current() {
    std::lock_guard guard(g_lock);
    return g_settings;
}

protocol::Masks Get(uint64_t account) {
    std::lock_guard guard(g_lock);
    protocol::Masks m{};
    if (auto it = g_players.find(account); it != g_players.end()) m = it->second;
    if (g_settings.unlockAll)
        for (int h = 0; h < halls::kHallCount; ++h) m.hall[h] = uint8_t((1u << halls::kHalls[h].count) - 1);
    m.resetOnBoss = g_settings.resetOnBoss;
    return m;
}

bool Has(uint64_t account, int hall, int beacon) {
    std::lock_guard guard(g_lock);
    if (g_settings.unlockAll && hall >= 0 && hall < halls::kHallCount && beacon >= 0 && beacon < halls::kHalls[hall].count) return true;
    auto it = g_players.find(account);
    return it != g_players.end() && hall >= 0 && hall < halls::kHallCount && (it->second.hall[hall] >> beacon & 1);
}

bool Unlock(uint64_t account, int hall, int beacon) {
    std::lock_guard guard(g_lock);
    if (!account || hall < 0 || hall >= halls::kHallCount || beacon < 0 || beacon >= halls::kHalls[hall].count) return false;
    auto& mask = g_players[account].hall[hall];
    if (mask >> beacon & 1) return false;
    mask = uint8_t(mask | (1u << beacon));
    g_dirty = true;
    return true;
}

bool ResetHall(uint64_t account, int hall) {
    std::lock_guard guard(g_lock);
    auto it = g_players.find(account);
    if (it == g_players.end() || hall < 0 || hall >= halls::kHallCount || !it->second.hall[hall]) return false;
    it->second.hall[hall] = 0;
    g_dirty = true;
    return true;
}

void Save() {
    std::string text = "# Hollow Halls Progression: the Return Beacons each player has reached.\n"
                       "# <account>=<one mask per hall: Springlands, Revelwood, Nomad Highlands, Veilwater Basin, Albaneve Summits, Kindlewastes>\n"
                       "# Bit 1 is the hall's first beacon. Delete a line to reset that player; edit only while the game or server is closed.\n";
    std::wstring file, temp;
    {
        std::lock_guard guard(g_lock);
        if (!g_dirty || g_file.empty()) return;
        for (const auto& [account, m] : g_players) {
            bool any = false;
            for (auto v : m.hall) any = any || v;
            if (!any) continue;
            char line[128];
            sprintf_s(line, "%llx=%u,%u,%u,%u,%u,%u\n", static_cast<unsigned long long>(account), m.hall[0], m.hall[1], m.hall[2],
                      m.hall[3], m.hall[4], m.hall[5]);
            text += line;
        }
        g_dirty = false;
        file = g_file;
    }
    temp = file + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, temp.c_str(), L"wb") || !f) {
        Log("Hollow Halls Progression: could not write progress.txt");
        std::lock_guard guard(g_lock);
        g_dirty = true;
        return;
    }
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (!ok || !MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        Log("Hollow Halls Progression: could not replace progress.txt");
        std::lock_guard guard(g_lock);
        g_dirty = true;
    }
}

}  // namespace hhp::progress
