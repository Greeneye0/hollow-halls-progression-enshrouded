#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include "dev_admin.h"
#include "greeneye.h"
#include "halls.h"
#include "host.h"
#include "map_link.h"
#include "notice.h"
#include "progress.h"
#include "protocol.h"

namespace {
using namespace hhp;
template <class T>
T& at(void* p, size_t n) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + n); }

constexpr const char* kVersion = "1.0.5";
// The game's "Return Beacon" (its name in the game's text tables).
constexpr uint32_t kReturnBeaconLoca = 2316234444u;

using LoopFn = void (*)(void*);
using DispatchFn = void (*)(void*, void*);
using BuildFn = void (*)(void*, void*, void*);
using ButtonFn = uint64_t (*)(uint8_t*, void*);
using EntryFn = uint8_t* (*)(uint8_t*, uint8_t, uint8_t);
using AddFn = void (*)(void*, uint8_t*);
LoopFn nextLoop{}, nextPlay{};
DispatchFn dispatch{};
BuildFn nextBuild{};
ButtonFn nextButton{};
EntryFn makeEntry{};
AddFn addEntry{};
void** root = nullptr;
// Client flag: the server allows text chat (read from the chat command's own check; 0 = unknown).
int32_t textChatAllowed = 0;
std::atomic<bool> paused{false}, broken{false};

void Log(const char* s) { greeneye::Host()->log(s); }

// UI snapshots and the live game client are distinct objects. The command loop's live client
// holds the world (+0x47d4e0), the player handle and the chat command dispatch.
void* GameClient(void* app) {
    auto ui = app ? at<void*>(app, 0x352a0) : nullptr;
    return ui ? at<void*>(ui, 0x1b8) : nullptr;
}
// The local player: the client world's player position (+0x2c18, 32.32 fixed point) and its handle
// (client + 0x466b44; entity id = (handle & 0x3f) + 1), as in Quick Deposit.
bool LocalPosition(uint8_t* world, float& x, float& y, float& z) {
    x = float(double(at<int64_t>(world, 0x2c18)) / 4294967296.0);
    y = float(double(at<int64_t>(world, 0x2c20)) / 4294967296.0);
    z = float(double(at<int64_t>(world, 0x2c28)) / 4294967296.0);
    return x > 1.0f && x < 12000.0f && z > 1.0f && z < 12000.0f && std::isfinite(y);
}
uint32_t LocalEntity(void* client) {
    const uint32_t handle = at<uint32_t>(client, 0x466b44);
    return handle ? (handle & 0x3f) + 1 : 0;
}

void SendLine(void* client, std::string_view text) {
    alignas(8) uint8_t command[0x410]{};
    command[0] = 0x74;
    command[1] = protocol::kChatType;
    std::memcpy(command + 2, text.data(), text.size());
    at<uint16_t>(command, 0x402) = uint16_t(text.size());
    // The game sends chat only while the server allows text chat, or system lines (type 4).
    // Hidden protocol lines must also reach servers with text chat off, so allow this one send.
    if (!textChatAllowed) {
        dispatch(client, command);
        return;
    }
    auto& allowed = at<uint8_t>(client, size_t(textChatAllowed));
    auto saved = allowed;
    allowed = 1;
    dispatch(client, command);
    allowed = saved;
}

// --- What this player has unlocked, from the host -----------------------------------------------
// A remote host is asked (twice at most) per world whether it runs the mod. Until it answers, the
// client sends nothing else: on a host without the mod the lines would show up as player chat.
enum class HostMod { present, checking, absent };
struct Link {
    void* client{};
    void* world{};
    uint64_t helloAt{}, deadline{};
    int asked{};
    bool absent{};
} link;
std::atomic<bool> hostAnswered{false};
enum class Source { none, local, remote };
struct Known {
    protocol::Masks masks{};
    bool have{};
    Source source{};
    uint64_t askAt{};
} known;
uint64_t hostedAt = 0;

// This game hosts the world (single player, hosting), or did moments ago: the host's player system
// stops while the game pauses (menus, loading), and the chat path must not take over meanwhile. It
// answers from the host's record of this player's account, which can differ from the local player's.
bool Hosting(uint64_t now) {
    if (host::HostsWorld()) {
        hostedAt = now;
        return true;
    }
    return hostedAt && now - hostedAt < 30000;
}
struct Pending {
    uint64_t nonce{}, until{};
    int hall{}, beacon{};
} pending;
// Answers arrive on the chat path (another thread than the command loop); handed over here.
std::atomic<bool> answerReady{false}, travelAnswered{false};
protocol::Masks answeredMasks{};
uint64_t answeredNonce{};
protocol::Answer answeredCode{};
SRWLOCK answerLock = SRWLOCK_INIT;

void OnHostAnswer(std::string_view line) {
    protocol::Masks m{};
    uint64_t nonce = 0;
    protocol::Answer code{};
    AcquireSRWLockExclusive(&answerLock);
    if (line == protocol::kHelloReply) hostAnswered = true;
    else if (protocol::ParseMasks(line, m)) answeredMasks = m, answerReady = true, hostAnswered = true;
    else if (protocol::ParseAnswer(line, nonce, code)) answeredNonce = nonce, answeredCode = code, travelAnswered = true;
    ReleaseSRWLockExclusive(&answerLock);
}

HostMod CheckHost(void* client, void* world) {
    if (Hosting(GetTickCount64()) || hostAnswered) return HostMod::present;
    const auto now = GetTickCount64();
    if (link.client != client || link.world != world || !link.deadline) link = {client, world, now + 3000, now + 15000};
    // Ask twice (3 s and 9 s after joining), in case the first hello is lost while the world loads.
    if (link.asked < 2 && now >= link.helloAt) {
        ++link.asked;
        link.helloAt = now + 6000;
        SendLine(client, protocol::kHello);
        Log(link.asked == 1 ? "Hollow Halls Progression: asking the host whether it runs the mod" : "Hollow Halls Progression: asking the host again");
    }
    if (now < link.deadline) return HostMod::checking;
    if (!link.absent) {
        link.absent = true;
        Log("Hollow Halls Progression: host did not answer; beacon travel is off in this world");
    }
    return HostMod::absent;
}

int HallAt(float x, float y, float z) {
    for (const auto& b : halls::kBoxes)
        if (x >= b.minX && x < b.maxX && y >= b.minY && y < b.maxY && z >= b.minZ && z < b.maxZ) return b.hall;
    return -1;
}

int Count(uint8_t mask) {
    int n = 0;
    for (; mask; mask &= uint8_t(mask - 1)) ++n;
    return n;
}

// New beacons and resets since the last answer, as notices. The first answer in a world, or the first
// from another source, only sets what is known.
void Accept(const protocol::Masks& now, Source source) {
    if (known.have && known.source == source) {
        for (int h = 0; h < halls::kHallCount; ++h) {
            const uint8_t was = known.masks.hall[h], is = now.hall[h];
            if (was && !is && now.resetOnBoss) {
                notice::Show(notice::Say(text::Title), notice::Say(text::Reset), 7000);
                continue;
            }
            for (int b = 0; b < halls::kHalls[h].count; ++b) {
                if (!(is >> b & 1) || (was >> b & 1)) continue;
                char message[512];
                sprintf_s(message, notice::Say(text::Reached), unsigned(halls::ShownNumber(h, b)), unsigned(halls::kHalls[h].count));
                notice::Show(notice::Say(text::Title), message, 7000);
                char line[160];
                sprintf_s(line, "Hollow Halls Progression: notice: Return Beacon %d of %d reached in hall %d", halls::ShownNumber(h, b), int(halls::kHalls[h].count), h);
                Log(line);
            }
        }
    } else if (known.have) {
        char line[200];
        sprintf_s(line, "Hollow Halls Progression: beacons now %s; no notices for this reading (Springlands %u)",
                  source == Source::local ? "read from this game's own record" : "asked of the host over chat", unsigned(now.hall[0]));
        Log(line);
    }
    known.masks = now;
    known.have = true;
    known.source = source;
    map_link::Publish(now.hall, halls::kHallCount, true); // the world map mod's beacon numbers
}

std::atomic<int> clickedHall{-1}, clickedBeacon{-1};

// --- The loading screen during a journey (as Town Portal's arrival) ----------------------------
// It covers the screen from the click until the area round the beacon has loaded here. The game's
// own signal is the client's "ready for gameplay" byte: it drops to 0 when the teleport reaches this
// client and returns to 1 once the area has loaded. Its offset is read from the instruction that
// stores it. A short hop may never drop it; then smooth frames after arriving decide.
constexpr uint64_t kArrivalTimeoutMs = 15000;
constexpr double kHitchMs = 45.0;
int32_t areaReady = 0;  // game client offset of the ready-for-gameplay byte
struct Arrival {
    uint64_t since{}, arrivedAt{}, nativeReadyAt{}, smoothSince{};
    bool sawLoading{};
    float x{}, y{}, z{};
} arrival;
double typicalFrameMs = 0.0;

int ReadAreaReady(void* client) {
    if (!areaReady || !client) return -1;
    __try {
        return at<uint8_t>(client, size_t(areaReady)) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void BeginArrival(int hall, int beacon) {
    const auto& b = halls::kBeacons[halls::kHalls[hall].first + beacon];
    arrival = {GetTickCount64(), 0, 0, 0, false, b.x, b.y + 0.5f, b.z + 2.0f};
    char caption[256];
    sprintf_s(caption, notice::Say(text::Travelling), unsigned(halls::ShownNumber(hall, beacon)));
    notice::ShowLoading(hall, caption);
    notice::SetLoadingProgress(0.08f);
}

void EndArrival(const char* line) {
    notice::HideLoading();
    arrival = {};
    if (line) Log(line);
}

// The estimate shown as "Loading… N%": each stage creeps toward the next while it waits.
void Progress(float from, float to, uint64_t since, uint64_t now) {
    notice::SetLoadingProgress(from + (to - from) * float(1.0 - std::exp(-double(now - since) / 4000.0)));
}

void CheckArrival(void* client, bool positioned, float px, float pz) {
    if (!arrival.since) return;
    const uint64_t now = GetTickCount64();
    const int ready = ReadAreaReady(client);
    if (ready == 0) arrival.sawLoading = true;
    if (now - arrival.since > kArrivalTimeoutMs) {
        char line[192];
        sprintf_s(line, "Hollow Halls Progression: loading screen timed out (area flag %d, saw loading %d, %.1f m from the beacon)", ready,
                  int(arrival.sawLoading), positioned ? std::sqrt((px - arrival.x) * (px - arrival.x) + (pz - arrival.z) * (pz - arrival.z)) : -1.0f);
        EndArrival(line);
        return;
    }
    const float dx = px - arrival.x, dz = pz - arrival.z;
    if (!positioned || dx * dx + dz * dz > 8.0f * 8.0f) {  // not moved yet
        Progress(0.2f, 0.55f, arrival.since, now);
        return;
    }
    if (!arrival.arrivedAt) arrival.arrivedAt = now;
    // Streaming shows up as long frames: a frame over 45 ms and over twice the usual time.
    static LARGE_INTEGER frequency{}, last{};
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    const double frameMs = last.QuadPart ? double(counter.QuadPart - last.QuadPart) * 1000.0 / double(frequency.QuadPart) : 0.0;
    last = counter;
    const bool hitch = frameMs > kHitchMs && frameMs > 2.0 * typicalFrameMs;
    if (frameMs > 0.0 && frameMs < 1000.0 && (!hitch || typicalFrameMs == 0.0))
        typicalFrameMs = typicalFrameMs == 0.0 ? frameMs : typicalFrameMs * 0.9 + frameMs * 0.1;
    if (hitch || !arrival.smoothSince) arrival.smoothSince = now;
    Progress(0.6f, 0.95f, arrival.arrivedAt, now);
    char line[128];
    if (arrival.sawLoading) {
        // The game's signal dropped and is back: a short pause lets the first loaded frames reach
        // the screen before the cover fades.
        if (ready != 1) {
            arrival.nativeReadyAt = 0;
            return;
        }
        if (!arrival.nativeReadyAt) arrival.nativeReadyAt = now;
        if (now - arrival.nativeReadyAt < 400) return;
        sprintf_s(line, "Hollow Halls Progression: arrived; the game reports the area loaded after %llu ms",
                  static_cast<unsigned long long>(now - arrival.since));
        EndArrival(line);
        return;
    }
    // The signal never dropped (not found, or a hop too short to reset it): two seconds after
    // arriving, then a second of smooth frames.
    if (now - arrival.arrivedAt < 2000 || now - arrival.smoothSince < 1000) return;
    sprintf_s(line, "Hollow Halls Progression: arrived; loading screen held %llu ms", static_cast<unsigned long long>(now - arrival.since));
    EndArrival(line);
}

void Travel(void* client, int hall, int beacon) {
    const auto now = GetTickCount64();
    if (pending.nonce && now < pending.until) {
        notice::Show(notice::Say(text::Title), notice::Say(text::Busy), 3000);
        return;
    }
    if (Hosting(now)) {
        const auto answer = host::TravelLocal(hall, beacon);
        if (answer != protocol::Answer::queued) {
            notice::Show(notice::Say(text::Title),
                         notice::Say(answer == protocol::Answer::locked ? text::Locked : answer == protocol::Answer::busy ? text::Busy : text::Failed), 4000);
            return;
        }
        BeginArrival(hall, beacon);
        notice::SetLoadingProgress(0.2f);
    } else {
        const auto mod = CheckHost(client, at<void*>(client, 0x47d4e0));
        if (mod != HostMod::present) {
            notice::Show(notice::Say(text::Title), notice::Say(mod == HostMod::checking ? text::Checking : text::Missing), 6000);
            return;
        }
        static uint64_t lastNonce = 0;
        lastNonce = now > lastNonce ? now : lastNonce + 1;
        pending = {lastNonce, now + 8000, hall, beacon};
        char line[64];
        int n = protocol::FormatTravel(line, sizeof(line), {pending.nonce, hall, beacon});
        if (n <= 0) return;
        SendLine(client, {line, size_t(n)});
        BeginArrival(hall, beacon);
    }
    // Back to play, as a finished map hold in Embervale Teleport does.
    alignas(8) uint8_t openPlay[0x410]{};
    openPlay[0] = 0x01;
    openPlay[1] = 1;
    dispatch(client, openPlay);
}

// Every frame from the command loop.
void ClientFrame(void* app) {
    auto* client = GameClient(app);
    auto* world = client ? at<void*>(client, 0x47d4e0) : nullptr;
    float x = 0, y = 0, z = 0;
    const bool positioned = world && LocalPosition(static_cast<uint8_t*>(world), x, y, z);
    host::SetClient(world ? client : nullptr, positioned, x, y, z, world ? LocalEntity(client) : 0);
    static void* lastWorld = nullptr;
    if (!world || world != lastWorld) {
        // A new session can reuse the old client and world addresses, so forget the old host here.
        link = {};
        hostAnswered = false;
        known = {};
        hostedAt = 0;
        map_link::Publish(known.masks.hall, halls::kHallCount, false);
        pending = {};
        clickedHall = -1;
        lastWorld = world;
        if (arrival.since) EndArrival(nullptr);
        if (!world) {
            notice::Clear();
            return;
        }
    }
    CheckArrival(client, positioned, x, z);
#ifdef HHP_DEV
    // Development builds: greeneye\hollow-halls-progression\loadtest.txt holding a hall number (0-5)
    // shows its loading screen for eight seconds without travelling, then the file is deleted.
    static uint64_t testAt = 0, testUntil = 0;
    if (const auto t = GetTickCount64(); t >= testAt) {
        testAt = t + 1000;
        if (testUntil && t >= testUntil) {
            testUntil = 0;
            notice::HideLoading();
        }
        const auto file = std::wstring(greeneye::Host()->modDir) + L"loadtest.txt";
        FILE* f = nullptr;
        if (!_wfopen_s(&f, file.c_str(), L"rb") && f) {
            int testHall = -1;
            const int read = fscanf_s(f, "%d", &testHall);
            fclose(f);
            DeleteFileW(file.c_str());
            if (read == 1 && testHall >= 0 && testHall < halls::kHallCount) {
                char caption[256];
                sprintf_s(caption, notice::Say(text::Travelling), 1u);
                notice::ShowLoading(testHall, caption);
                notice::SetLoadingProgress(0.6f);
                testUntil = t + 8000;
            }
        }
    }
#endif
    const auto now = GetTickCount64();
    const int hall = positioned ? HallAt(x, y, z) : -1;
    if (Hosting(now)) {
        // Hosting (or single player): the record is right here. Paused meanwhile: nothing changes.
        if (host::HostsWorld() && now >= known.askAt) {
            known.askAt = now + 500;
            Accept(host::LocalMasks(), Source::local);
        }
    } else if (CheckHost(client, world) == HostMod::present) {
        if (answerReady.exchange(false)) {
            AcquireSRWLockShared(&answerLock);
            auto m = answeredMasks;
            ReleaseSRWLockShared(&answerLock);
            Accept(m, Source::remote);
        }
        if (travelAnswered.exchange(false)) {
            AcquireSRWLockShared(&answerLock);
            const auto nonce = answeredNonce;
            const auto code = answeredCode;
            ReleaseSRWLockShared(&answerLock);
            if (nonce == pending.nonce && code == protocol::Answer::queued) notice::SetLoadingProgress(0.2f);
            if (nonce == pending.nonce && code != protocol::Answer::queued) {
                EndArrival("Hollow Halls Progression: the host refused the journey");
                notice::Show(notice::Say(text::Title),
                             notice::Say(code == protocol::Answer::locked ? text::Locked : code == protocol::Answer::busy ? text::Busy : text::Failed), 4000);
            }
            if (nonce == pending.nonce) pending = {};
        }
        // Asked every two seconds inside a hall (new beacons show soon), every 20 s elsewhere.
        if (now >= known.askAt) {
            known.askAt = now + (hall >= 0 || !known.have ? 2000 : 20000);
            SendLine(client, protocol::kQuery);
        }
    }
    const int h = clickedHall.exchange(-1);
    if (h >= 0) Travel(client, h, clickedBeacon.load());
    // A beacon clicked on the world map mod's dungeon map (only one it showed as reached)
    int mapHall = -1, mapBeacon = -1;
    if (map_link::TakeRequest(mapHall, mapBeacon) && mapHall < halls::kHallCount && mapBeacon < halls::kHalls[mapHall].count)
        Travel(client, mapHall, mapBeacon);
}

void SafeClient(void* app) {
    __try {
        ClientFrame(app);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        broken = true;
        Log("Hollow Halls Progression: client disabled after invalid game state");
    }
}

void LoopHook(void* app) {
    greeneye::Lease lease;
    nextLoop(app);
    if (lease && !paused && !broken) SafeClient(app);
}

// A fault while drawing leaves the game's UI layers unbalanced (the layer it was in is never
// ended), and the game's next new layer then stops the game. So the first fault ends this mod's
// drawing for the session, and where it happened is logged for a bug report.
std::atomic<bool> drawingOff{false};
int DrawFault(EXCEPTION_POINTERS* info) {
    char line[200];
    auto* module = static_cast<uint8_t*>(static_cast<void*>(GetModuleHandleW(nullptr)));
    const auto* address = static_cast<uint8_t*>(info->ExceptionRecord->ExceptionAddress);
    HMODULE owner{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(address), &owner);
    sprintf_s(line, "Hollow Halls Progression: drawing failed (exception %08lx at %s+0x%llx); notices and the loading screen are off until restart",
              info->ExceptionRecord->ExceptionCode, owner == reinterpret_cast<HMODULE>(module) ? "game" : "module",
              static_cast<unsigned long long>(address - static_cast<const uint8_t*>(static_cast<void*>(owner ? owner : reinterpret_cast<HMODULE>(module)))));
    Log(line);
    return EXCEPTION_EXECUTE_HANDLER;
}
void DrawNotice() {
    __try {
        notice::Draw();
    } __except (DrawFault(GetExceptionInformation())) {
        drawingOff = true;
        notice::Clear();
    }
}
void PlayHook(void* self) {
    greeneye::Lease lease;
    if (lease && !paused && !drawingOff) DrawNotice();
    nextPlay(self);
}

// --- The map marker menu ------------------------------------------------------------------------
// The world map builds a clicked marker's menu (Fast Travel / Send Ping / Set as Waypoint) as up to
// 16 entries of 0x48 bytes (count at +0x480), drawn by the game's ContextMenuWindow as its stone
// buttons. For a Hollow Halls marker one entry per unlocked beacon is added: action 0x5b (no input
// binding, ignored by the map's own handlers), enabled, with its own label at +0x20 (text, length,
// FNV-1a hash). A click is caught where the window draws each entry (it returns 1 when clicked).
struct MenuEntry {
    uint8_t* entry;
    int hall, beacon;
};
MenuEntry menuEntries[16];
int menuCount = 0;
char labels[16][128];
std::string beaconWord;
int beaconWordLang = -1;

uint64_t Fnv64(const char* s, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(s[i])) * 0x100000001b3ull;
    return h;
}

// A Hollow Halls entrance marker: within 40 m of one (marker +0x10 / +0x20: 32.32 fixed x / z).
int HallOfMarker(void* marker) {
    const double mx = double(at<int64_t>(marker, 0x10)) / 4294967296.0, mz = double(at<int64_t>(marker, 0x20)) / 4294967296.0;
    for (int h = 0; h < halls::kHallCount; ++h) {
        const double dx = mx - halls::kHalls[h].entranceX, dz = mz - halls::kHalls[h].entranceZ;
        if (dx * dx + dz * dz <= 40.0 * 40.0) return h;
    }
    return -1;
}

void AddBeacons(uint8_t* entries, void* marker) {
    menuCount = 0;
    if (!entries || !marker || !makeEntry || !addEntry) return;
    const int hall = HallOfMarker(marker);
    if (hall < 0) return;
    const uint8_t mask = Hosting(GetTickCount64()) ? host::LocalMasks().hall[hall] : known.masks.hall[hall];
    if (!mask) return;
    notice::PrepareLoading(hall);  // the picture is ready by the time a beacon is chosen
    // The game's word in the current language, else the mod's own.
    const int lang = int(notice::Language());
    if (lang != beaconWordLang) {
        beaconWord = notice::GameText(kReturnBeaconLoca);
        if (beaconWord.empty() || beaconWord.size() > 96) beaconWord = notice::Say(text::ReturnBeacon);
        beaconWordLang = lang;
    }
    // In the order shown (the order you reach them), not the stored one
    for (int shown = 1; shown <= halls::kHalls[hall].count; ++shown) {
        const int b = halls::BeaconShownAs(hall, shown);
        if (!(mask >> b & 1) || at<uint8_t>(entries, 0x480) >= 16 || menuCount >= 16) continue;
        auto& label = labels[menuCount];
        const int n = sprintf_s(label, "%s %d", beaconWord.c_str(), shown);
        if (n <= 0) continue;
        alignas(16) uint8_t entry[0x48]{};
        makeEntry(entry, 0x5b, 1);
        at<const char*>(entry, 0x20) = label;
        at<uint64_t>(entry, 0x28) = uint64_t(n);
        at<uint64_t>(entry, 0x30) = Fnv64(label, size_t(n));
        at<uint8_t>(entry, 0x1b) = 1;
        const uint8_t index = at<uint8_t>(entries, 0x480);
        addEntry(entries, entry);
        menuEntries[menuCount++] = {entries + size_t(index) * 0x48, hall, b};
    }
}

void SafeAddBeacons(uint8_t* entries, void* marker) {
    __try {
        AddBeacons(entries, marker);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        menuCount = 0;
        broken = true;
        Log("Hollow Halls Progression: map menu disabled after invalid game state");
    }
}

void BuildHook(void* entries, void* mapState, void* marker) {
    greeneye::Lease lease;
    nextBuild(entries, mapState, marker);
    if (lease && !paused && !broken) SafeAddBeacons(static_cast<uint8_t*>(entries), marker);
}

uint64_t ButtonHook(uint8_t* entry, void* style) {
    greeneye::Lease lease;
    const uint64_t result = nextButton(entry, style);
    if (lease && (result & 0xff)) {
        for (int i = 0; i < menuCount; ++i) {
            if (menuEntries[i].entry != entry || at<const char*>(entry, 0x20) != labels[i]) continue;
            clickedBeacon = menuEntries[i].beacon;
            clickedHall = menuEntries[i].hall;
            menuCount = 0;
            break;
        }
    }
    return result;
}

bool CallTo(const GreeneyeHost* host, uint8_t* site, uint8_t* target) {
    uint8_t bytes[5]{};
    host->read(site, bytes, 5);
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, 4);
    return bytes[0] == 0xe8 && site + 5 + rel == target;
}

// addEntry (push onto a 16-entry list) has a byte-identical twin elsewhere in the game since
// build 26320373, so it is taken from the call the map's own menu builder makes (+0x10c).
uint8_t* FindAddEntry(const GreeneyeHost* host, uint8_t* build) {
    static const uint8_t kHead[] = {0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0x81, 0x80, 0x04, 0x00, 0x00, 0x4C, 0x8B, 0xC1};
    auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    uint8_t* end = image + nt->OptionalHeader.SizeOfImage;
    for (size_t i = 0; build && i < 0x400; ++i) {
        uint8_t bytes[5]{};
        host->read(build + i, bytes, 5);
        if (bytes[0] != 0xe8) continue;
        int32_t rel = 0;
        std::memcpy(&rel, bytes + 1, 4);
        uint8_t* target = build + i + 5 + rel;
        if (target < image || target + sizeof(kHead) > end) continue;
        uint8_t head[sizeof(kHead)]{};
        host->read(target, head, sizeof(head));
        if (!std::memcmp(head, kHead, sizeof(kHead))) return target;
    }
    return nullptr;
}

bool Prepare() {
    paused = true;
    if (!host::Prepare()) {
        paused = false;
        return false;
    }
    progress::Save();
    notice::Clear();
    notice::StopLoading();
    map_link::Withdraw(); // the map mod offers no journeys while this generation is gone
    return true;
}
bool Ready() { return true; }
void Resume() {
    paused = false;
    host::Resume();
    map_link::Publish(known.masks.hall, halls::kHallCount, known.have);
}
}  // namespace

GREENEYE_MOD_START {
    if (!greeneye::HostHasHooks(host) || !host->find || !host->read || !host->redirectCall) return false;
    greeneye::UseHost(host);
    if (!greeneye::ClaimProcess(L"HollowHallsProgression", host)) return false;
    const bool serverSide = host->side == kGreeneyeServer;
    progress::Init(host->modDir);
    if (!host::Init(host)) return false;
    host::OnAnswer(OnHostAnswer);
    char line[160];
    if (serverSide) {
        dev_admin::Init(host);
        sprintf_s(line, "Hollow Halls Progression %s: server companion loaded", kVersion);
        Log(line);
        return true;
    }
    auto mapTail = host->find("48 8D AC 24 A0 F8 FF FF 48 81 EC 60 08 00 00 48 8B 05", nullptr);
    auto loop = host->find("40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 98 00 00 00 48 8D 81 78 4F 03 00", nullptr);
    auto send = host->find("4C 8B DC 55 56 57 49 8D AB 18 FF FF FF", nullptr);
    auto play = host->find("4C 8B DC 55 49 8D AB E8 FD FF FF 48 81 EC 10 03 00 00", nullptr);
    auto build = host->find("48 89 5C 24 10 48 89 6C 24 20 56 57 41 57", nullptr);
    auto button = host->find("40 55 53 57 48 8D AC 24 C0 FD FF FF", nullptr);
    auto buttons = host->find("48 8B C4 57 48 81 EC A0 00 00 00 48 8B 91 B0 00 00 00", nullptr);
    makeEntry = reinterpret_cast<EntryFn>(host->find("40 53 48 83 EC 40 45 33 D2 66 C7 01 5B 00", nullptr));
    addEntry = reinterpret_cast<AddFn>(FindAddEntry(host, build));
    if (!mapTail || !loop || !send || !play || !build || !button || !buttons || !makeEntry || !addEntry) {
        Log("Hollow Halls Progression: unsupported game signatures; no map menu");
        return false;
    }
    // The map's call that builds a marker's menu, and the menu window's call that draws each entry.
    uint8_t* buildCall = mapTail + 0x10f;
    uint8_t* buttonCall = buttons + 0xae;
    if (!CallTo(host, buildCall, build) || !CallTo(host, buttonCall, button)) {
        Log("Hollow Halls Progression: map menu call sites did not match; no map menu");
        return false;
    }
    int32_t displacement = 0;
    host->read(mapTail + 0x12, &displacement, 4);
    root = reinterpret_cast<void**>(mapTail + 0x16 + displacement);
    if (!notice::Init(host, root)) {
        Log("Hollow Halls Progression: notice bindings unavailable");
        return false;
    }
    // cmp byte [rdi+allowed],0 / jne / cmp byte [rdx+1],4 in the chat command (0x74).
    if (auto check = host->find("80 BF ?? ?? ?? ?? 00 75 ?? 80 7A 01 04", nullptr)) host->read(check + 2, &textChatAllowed, 4);
    if (textChatAllowed <= 0 || textChatAllowed > 0x800000) {
        textChatAllowed = 0;
        Log("Hollow Halls Progression: chat permission check not found; beacon travel needs text chat on the server");
    }
    // movzx ecx, byte [rsi+disp32] ... mov [rsi+disp32], al: the ready-for-gameplay latch.
    if (auto ready = host->find("0F B6 8E ?? ?? ?? ?? 84 DB 74 12 45 84 F6 74 0D 45 84 FF 75 08 84 C0 75 04 B0 01 EB 02 32 C0 0A C1 88 86",
                                nullptr))
        host->read(ready + 3, &areaReady, 4);
    if (areaReady <= 0 || areaReady > 0x800000) {
        areaReady = 0;
        Log("Hollow Halls Progression: area-ready signal not found; the loading screen waits on smooth frames instead");
    }
    dispatch = reinterpret_cast<DispatchFn>(send);
    nextBuild = reinterpret_cast<BuildFn>(host->redirectCall(buildCall, reinterpret_cast<void*>(BuildHook)));
    nextButton = reinterpret_cast<ButtonFn>(host->redirectCall(buttonCall, reinterpret_cast<void*>(ButtonHook)));
    nextLoop = reinterpret_cast<LoopFn>(host->hook(loop, reinterpret_cast<void*>(LoopHook), 13, nullptr, 0));
    nextPlay = reinterpret_cast<LoopFn>(host->hook(play, reinterpret_cast<void*>(PlayHook), 18, nullptr, 0));
    if (!nextBuild || !nextButton || !nextLoop || !nextPlay) {
        paused = true;
        Log("Hollow Halls Progression: hook installation failed");
        return false;
    }
    sprintf_s(line, "Hollow Halls Progression %s: click a Hollow Halls on the world map to travel to its Return Beacons", kVersion);
    Log(line);
    return true;
}
GREENEYE_MOD_HOT {
    static const GreeneyeHot api{sizeof(GreeneyeHot), Prepare, Ready, Resume};
    return &api;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}
