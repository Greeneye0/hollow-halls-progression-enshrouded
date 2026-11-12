#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "host.h"
#include "halls.h"
#include "progress.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <windows.h>

// The player and teleport bindings below come from Town Portal's direct travel (same author),
// verified on game build 1273043; they move a player without the native cast.
namespace hhp::host {
namespace {

template <class T>
T& at(void* p, size_t offset) { return *reinterpret_cast<T*>(static_cast<uint8_t*>(p) + offset); }
template <class T>
T field(const void* p, size_t offset) {
    T value{};
    std::memcpy(&value, static_cast<const uint8_t*>(p) + offset, sizeof(T));
    return value;
}

constexpr double kFixed = 4294967296.0;

// The player system and the teleport-apply system, and the entity iterator both call per entity.
constexpr char kPlayerPattern[] =
    "48 89 4C 24 08 55 53 48 8D AC 24 E8 F5 FF FF 48 81 EC 18 0B 00 00 41 B8 B0 05 00 00 "
    "48 8D 55 F0 48 8B D9 E8 ?? ?? ?? ?? 41 B8 B0 05 00 00 48 8D 55 F0 48 8B CB E8 ?? ?? ?? ?? "
    "84 C0 0F 84 ?? ?? ?? ??";
constexpr char kApplyPattern[] =
    "40 55 56 48 8D AC 24 A8 FD FF FF 48 81 EC 58 03 00 00 41 B8 B0 01 00 00 48 8D 55 90 "
    "48 8B F1 E8 ?? ?? ?? ?? 41 B8 B0 01 00 00 48 8D 55 90 48 8B CE E8 ?? ?? ?? ?? 84 C0 "
    "0F 84 ?? ?? ?? ?? 48 89 9C 24 70 03 00 00";
constexpr char kNextPattern[] =
    "40 57 48 83 EC 20 4C 8B 01 48 8B FA 8B 41 08 4C 8B C9 49 3B 80 ?? 08 00 00 75 ?? 32 C0 "
    "48 83 C4 20 5F C3 41 0F B6 80 B0 04 00 00 49 8D 88 70 04 00 00 48 89 5C 24 38 45 33 DB "
    "41 0F B7 98 BA 04 00 00";
constexpr char kEntityPattern[] =
    "8B 41 08 4C 8B C2 85 C0 75 ?? 89 02 48 8B C2 C3 8D 50 FF 48 8B 01 48 03 D2 48 8B 88 "
    "?? 08 00 00 8B 04 D1 41 89 00 49 8B C0 C3 CC CC CC CC CC CC 8B 05 ?? ?? ?? ?? 48 85 C9 "
    "74 ?? 8B 41 10 C3 CC";
constexpr char kRayPattern[] =
    "48 8B C4 48 89 50 10 55 53 57 41 54 48 8D A8 58 FF FF FF 48 81 EC 88 01 00 00 41 0F 28 08 "
    "48 8B F9 F2 0F 10 05 ?? ?? ?? ?? 49 8B D9 0F 29 70 A8 4D 8B E0 41 0F 28 31 0F 29 78 98 "
    "0F 28 FE 44 0F 29 40 88";
constexpr char kDirtyPattern[] =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56 41 57 48 83 EC 30 "
    "48 8B B1 90 02 00 00 49 8B D9 0F B7 6C 24 70 4D 8B F8 4C 8B D2 4C 8B F1 44 8B A6 DC 04 00 00 "
    "48 85 D2";
constexpr char kQueuePattern[] =
    "48 89 74 24 10 57 48 83 EC 20 44 8B 52 10 48 8B FA 41 8B C2 83 E0 0F 74 ?? 44 2B D0 41 83 C2 10 "
    "43 8D 34 0A 8B C6 48 3B 42 08 76 ?? 33 C0 48 8B 74 24 38 48 83 C4 20 5F C3 41 8B D2 48 89 5C 24 30";
constexpr char kNotifyPattern[] =
    "40 53 48 83 EC 30 8B C2 41 B9 10 00 00 00 41 8B D8 89 44 24 20 45 0F B6 C1 48 8D 51 10 E8 ?? ?? ?? ?? "
    "48 85 C0 75 ?? 48 83 C4 30 5B C3 89 58 0C B0 01 48 83 C4 30 5B C3 CC CC CC CC CC CC CC CC";
constexpr char kComponentPattern[] =
    "40 53 48 83 EC 20 48 8B D9 E8 ?? ?? ?? ?? 48 8B C3 48 83 C4 20 5B C3 CC CC CC CC CC CC CC CC CC 4C 8B D1";
constexpr char kReceivePattern[] = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D8 F6 FF FF 48 81 EC 28 0A 00 00 4C 8B E9";
constexpr char kReceivePattern2[] = "40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 98 F9 FF FF 48 81 EC 68 07 00 00 4C 8B F9";
constexpr char kChatPattern[] = "40 55 56 41 54 41 55 41 57 48 83 EC 30 49 8B 41 08 33 ED";

using NextFn = bool (*)(void*, void*, size_t);
using EntityFn = uint32_t* (*)(void*, uint32_t*);
using DirtyFn = void (*)(void*, uint64_t, uint64_t, uint64_t, uint16_t, uint8_t);
using RayFn = void* (*)(void*, void*, const float*, const float*, const uint8_t*);
using ReceiveFn = void (*)(void*);
using QueueFn = void* (*)(void*, void*, uint8_t, uint32_t, uint32_t);
using NotifyFn = bool (*)(void*, uint32_t, uint32_t);
using ComponentFn = void** (*)(void*, void*, void*, uint32_t);
using AppendFn = uint8_t* (*)(uint8_t*, void*, uint8_t, uint64_t*, uint32_t, uint32_t, uint32_t, uint64_t, uint64_t*);

NextFn g_playerFirst{}, g_playerMore{}, g_applyFirst{}, g_applyMore{};
EntityFn g_entityOf{};
DirtyFn g_dirty{};
RayFn g_ray{};
ReceiveFn g_receive{};
QueueFn g_queue{};
NotifyFn g_notify{};
ComponentFn g_component{};
AppendFn g_append{};

std::atomic<void*> g_client{};
std::atomic<void*> g_server{};
std::atomic<bool> g_paused{false}, g_faulted{false};
std::atomic<AnswerHandler> g_answers{};
std::mutex g_lock;

struct World {
    void* storage{};
    void* terrain{};
    uint64_t seen{};
    uint8_t realm{};
} g_world;

// The local player as the client sees it, to find the host's entity for it (single player, hosting).
struct Local {
    bool valid{};
    float x{}, y{}, z{};
    uint64_t seen{};
    uint32_t entity{};
    uint64_t account{}; // the server's account for it: beacons saved while the local player was unknown
} g_local;

struct Player {
    float x{}, y{}, z{};
    uint64_t seen{}, checked{}, travelled{};
    int hall = -1;        // the hall the player was in at the last check
    bool finished{};      // this visit's hall was finished (no unlocks until outside)
};
Player g_players[17];
uint64_t g_saveAfter = 0;

enum class Stage { none, resolving, armed, applying };
struct Request {
    Stage stage{};
    uint32_t entity{};
    uint64_t nonce{}, expires{}, terrainSince{}, logAfter{};
    void* storage{};
    int64_t target[3]{};
    int64_t groundY{};
    bool groundRead{};
};
Request g_requests[16];
// The Teleport component is held until the destination's terrain is resident; then the beacon's
// own height is used.
constexpr uint64_t kTerrainWaitMs = 12000;

void Log(const char* s) {
    if (auto* h = greeneye::Host(); h && h->log) h->log(s);
}

void* Storage(void* ctx) { return at<void*>(at<void*>(ctx, 0), 0x30); }
bool Authority(void* ctx) { return at<uint8_t>(at<void*>(ctx, 0), 0xa1) == 0; }
uint32_t Id(void* ctx) {
    uint32_t result = 0;
    return *g_entityOf(ctx, &result);
}

void MarkDirty(void* wrapper) {
    if (auto* tracker = at<void*>(wrapper, 0x28))
        g_dirty(tracker, at<uint64_t>(wrapper, 8), at<uint64_t>(wrapper, 16), at<uint64_t>(wrapper, 24), at<uint16_t>(wrapper, 32),
                at<uint8_t>(wrapper, 34));
}

// The server's player slots: +0 in use, +0x18 entity, +0x48 account (the hash chat lines carry).
uint32_t SenderEntity(uint64_t account) {
    auto* s = static_cast<uint8_t*>(g_server.load());
    for (int i = 0; s && account && i < 16; ++i) {
        auto* slot = s + 0x1e0 + i * 0x148;
        if (at<uint32_t>(slot, 0) && at<uint64_t>(slot, 0x48) == account) return at<uint32_t>(slot, 0x18);
    }
    return 0;
}

uint64_t SlotAccount(uint32_t id) {
    auto* s = static_cast<uint8_t*>(g_server.load());
    for (int i = 0; s && id && i < 16; ++i) {
        auto* slot = s + 0x1e0 + i * 0x148;
        if (at<uint32_t>(slot, 0) && at<uint32_t>(slot, 0x18) == id) return at<uint64_t>(slot, 0x48);
    }
    return 0;
}

// Caller holds g_lock. The local player is kLocalPlayer, whatever the slots say, so a host's own
// progress never depends on how its session numbered it.
uint64_t AccountOf(uint32_t id) {
    if (id && id == g_local.entity && GetTickCount64() - g_local.seen < 3000) return progress::kLocalPlayer;
    auto* s = static_cast<uint8_t*>(g_server.load());
    for (int i = 0; s && i < 16; ++i) {
        auto* slot = s + 0x1e0 + i * 0x148;
        if (at<uint32_t>(slot, 0) && at<uint32_t>(slot, 0x18) == id) return at<uint64_t>(slot, 0x48);
    }
    return 0;
}

int HallAt(float x, float y, float z) {
    for (const auto& b : halls::kBoxes)
        if (x >= b.minX && x < b.maxX && y >= b.minY && y < b.maxY && z >= b.minZ && z < b.maxZ) return b.hall;
    return -1;
}

float Distance(float x, float y, float z, const halls::Point& p) {
    return std::sqrt((x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) + (z - p.z) * (z - p.z));
}

// A Return Beacon takes a player within 16 m (its CheckPlayerInRange distance).
constexpr float kBeaconRange = 16.0f;

// Caller holds g_lock. At most four times a second per player.
void Check(uint32_t id, Player& p, uint64_t now) {
    if (now - p.checked < 250) return;
    p.checked = now;
    const int hall = HallAt(p.x, p.y, p.z);
    const uint64_t account = AccountOf(id);
    if (hall != p.hall) {
        p.hall = hall;
        p.finished = false;
    }
    if (hall < 0 || !account || p.finished) return;
    const auto& h = halls::kHalls[hall];
    for (int b = 0; b < h.count; ++b) {
        if (Distance(p.x, p.y, p.z, halls::kBeacons[h.first + b]) > kBeaconRange || !progress::Unlock(account, hall, b)) continue;
        char line[160];
        std::snprintf(line, sizeof(line), "Hollow Halls Progression: player %u reached Return Beacon %d of %d in the %s Hollow Halls", id,
                      halls::ShownNumber(hall, b), int(h.count), h.region);
        Log(line);
        g_saveAfter = now;
    }
}

// --- A hall is finished when one of its end chests is looted ---------------------------------------
// The mage, ranger and warrior sarcophagi at the end of a hall hold its reward. Looting one of them is
// the end, whichever way the players leave afterwards. The host's entity registry (the player system's
// iterator +0x30; layout as in the map mod's built_water.inl: entity map at +0x148, records at +0x188,
// a record's archetype +0x18 with a u16 offset per type at +0x84 and a u16 size at +0xa84, chunk data
// +0x20, row +0x30) holds the chests while a player is near. Component type numbers change between
// sessions, so the chest's state is recognised by its data: InventoryEmptyState {bool toggleVFX,
// bool isEmpty} reads 01 00 while a chest has loot and 01 01 once it is emptied. Every 2-byte
// component of an entity at an end chest's place that reads 01 00 is watched; one that turns 01 01
// finishes the hall for every player in it. A chest first seen empty (looted on an earlier visit)
// does not count, nor does a change between two reads far apart: the scan pauses while nobody is in
// the hall, and a player arriving later (by beacon travel) must not finish it on the old reading.
// A chest being unloaded (its player gone) can read 01 01 for a moment too, so a change only counts
// while a player stands at that chest and has not just travelled away.
constexpr int kTypeBits = 640;
const uint8_t* Component(uint8_t* rec, int type, uint16_t* size) {
    uint8_t* arch = *reinterpret_cast<uint8_t**>(rec + 0x18);
    if (!arch || !(reinterpret_cast<const uint64_t*>(arch)[type >> 6] >> (type & 63) & 1)) return nullptr;
    const uint16_t off = *reinterpret_cast<const uint16_t*>(arch + 0x84 + type * 2);
    *size = *reinterpret_cast<const uint16_t*>(arch + 0xa84 + type * 2);
    uint8_t* data = *reinterpret_cast<uint8_t**>(rec + 0x20);
    return data ? data + off + size_t(*reinterpret_cast<const uint32_t*>(rec + 0x30)) * *size : nullptr;
}
template <class F>
void ForEachEntity(uint8_t* reg, F&& f) {
    uint8_t* map = reg + 0x148;
    const uint64_t capacity = *reinterpret_cast<uint64_t*>(map + 0x10);
    auto* used = *reinterpret_cast<uint64_t**>(map + 0x08);
    auto* ids = *reinterpret_cast<uint32_t**>(map + 0x28);
    auto* records = *reinterpret_cast<uint8_t***>(reg + 0x188);
    if (!used || !ids || !records || capacity > (1u << 22)) return;
    for (uint64_t i = 0; i < capacity; ++i) {
        if (!(used[i >> 6] >> (i & 63) & 1) || ids[i] < 0x400 || !records[i]) continue; // players are 1..0x3ff
        f(ids[i], records[i]);
    }
}
bool Position(uint8_t* rec, int type, float out[3]) {
    uint16_t size = 0;
    const uint8_t* p = Component(rec, type, &size);
    if (!p || size != 0x38) return false;
    for (int i = 0; i < 3; ++i) out[i] = float(double(reinterpret_cast<const int64_t*>(p)[i]) / kFixed);
    return true;
}

struct Chests {
    void* registry{};
    int positionType{};
    uint64_t scanAt{};
    struct Read {
        uint8_t empty;
        uint64_t at;
    };
    std::unordered_map<uint64_t, Read> watched; // (entity << 16 | type) -> isEmpty last read, and when
} g_chests;

// The position type: the 0x38-byte type most entities have with a world position in it.
int FindPositionType(uint8_t* reg) {
    static int counts[kTypeBits];
    std::memset(counts, 0, sizeof(counts));
    int sampled = 0;
    ForEachEntity(reg, [&](uint32_t, uint8_t* rec) {
        if (sampled >= 3000) return;
        ++sampled;
        const auto* bits = *reinterpret_cast<const uint64_t**>(rec + 0x18);
        for (int t = 0; t < kTypeBits; ++t) {
            float p[3];
            if ((bits[t >> 6] >> (t & 63) & 1) && Position(rec, t, p) && p[0] > 1 && p[0] < 12000 && p[2] > 1 && p[2] < 12000) ++counts[t];
        }
    });
    int best = 0;
    for (int t = 1; t < kTypeBits; ++t)
        if (counts[t] > counts[best]) best = t;
    return counts[best] >= 100 ? best : 0;
}

// A player of the hall within looting reach of a chest at p, who did not just travel. Caller holds g_lock.
bool PlayerAtChest(int hall, const float p[3], uint64_t now) {
    const halls::Point at{p[0], p[1], p[2]};
    for (uint32_t id = 1; id <= 16; ++id) {
        const auto& q = g_players[id];
        if (q.hall == hall && now - q.seen < 1500 && now - q.travelled > 5000 && Distance(q.x, q.y, q.z, at) <= 6.0f) return true;
    }
    return false;
}

// The halls finished since the last scan (bit per hall). Caller holds g_lock.
uint32_t ScanChests(uint8_t* reg, uint32_t occupied, uint64_t now) {
    if (reg != g_chests.registry) {
        g_chests.registry = reg;
        g_chests.positionType = 0;
        g_chests.watched.clear();
    }
    if (!g_chests.positionType && !(g_chests.positionType = FindPositionType(reg))) return 0;
    uint32_t finished = 0;
    ForEachEntity(reg, [&](uint32_t id, uint8_t* rec) {
        float p[3];
        if (!Position(rec, g_chests.positionType, p)) return;
        int hall = -1;
        for (int h = 0; h < halls::kHallCount && hall < 0; ++h) {
            if (!(occupied >> h & 1)) continue;
            const auto& info = halls::kHalls[h];
            for (int c = 0; c < info.chestCount; ++c)
                if (Distance(p[0], p[1], p[2], halls::kEndChests[info.firstChest + c]) <= 2.5f) hall = h;
        }
        if (hall < 0) return;
        const auto* bits = *reinterpret_cast<const uint64_t**>(rec + 0x18);
        for (int t = 0; t < kTypeBits; ++t) {
            if (!(bits[t >> 6] >> (t & 63) & 1)) continue;
            uint16_t size = 0;
            const uint8_t* c = Component(rec, t, &size);
            if (!c || size != 2 || c[0] != 1 || c[1] > 1) continue;
            const uint64_t key = uint64_t(id) << 16 | uint64_t(t);
            auto it = g_chests.watched.find(key);
            if (it == g_chests.watched.end()) {
                g_chests.watched.emplace(key, Chests::Read{c[1], now});
                continue;
            }
            if (it->second.empty == 0 && c[1] == 1 && now - it->second.at < 2500 && PlayerAtChest(hall, p, now)) finished |= 1u << hall;
            it->second = {c[1], now};
        }
    });
    static size_t logged = 0;
    if (g_chests.watched.size() != logged) {
        logged = g_chests.watched.size();
        char line[160];
        std::snprintf(line, sizeof(line), "Hollow Halls Progression: watching %zu end chest state(s) (position type %d)", logged,
                      g_chests.positionType);
        Log(line);
    }
    if (g_chests.watched.size() > 4096) g_chests.watched.clear();
    return finished;
}

// Once a second while a player is in a hall, from the player system (the host's registry at hand).
void WatchChests(void* ctx) {
    const uint64_t now = GetTickCount64();
    std::lock_guard guard(g_lock);
    if (now < g_chests.scanAt) return;
    g_chests.scanAt = now + 1000;
    uint32_t occupied = 0;
    for (const auto& p : g_players)
        if (p.hall >= 0 && now - p.seen < 3000) occupied |= 1u << p.hall;
    if (!occupied) return;
    const uint32_t finished = ScanChests(static_cast<uint8_t*>(Storage(ctx)), occupied, now);
    for (int h = 0; finished && h < halls::kHallCount; ++h) {
        if (!(finished >> h & 1)) continue;
        const bool resetOn = progress::Current().resetOnBoss;
        for (uint32_t id = 1; id <= 16; ++id) {
            auto& p = g_players[id];
            if (p.hall != h || p.finished || now - p.seen >= 3000) continue;
            p.finished = resetOn; // without the reset, beacons of a finished hall still unlock
            const uint64_t account = AccountOf(id);
            bool reset = resetOn && account && progress::ResetHall(account, h);
            // The host's own beacons may sit under its account as well (see LocalMasks)
            if (resetOn && account == progress::kLocalPlayer && g_local.account) reset = progress::ResetHall(g_local.account, h) || reset;
            char line[192];
            std::snprintf(line, sizeof(line), "Hollow Halls Progression: player %u finished the %s Hollow Halls (an end chest was looted)%s", id,
                          halls::kHalls[h].region, reset ? "; its Return Beacons are reset (ResetOnBossKill)" : "");
            Log(line);
            if (reset) g_saveAfter = now;
        }
    }
}

void SafeWatchChests(void* ctx) {
    __try {
        WatchChests(ctx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("Hollow Halls Progression: end chest watch failed; finished halls are not noticed");
        g_chests.scanAt = ~uint64_t(0);
    }
}

// A player's position (32.32 fixed x, y, z: the start of its transform), then the checks above.
void Observe(uint32_t id, const void* position) {
    if (!id || id > 16 || !position) return;
    const float x = float(double(field<int64_t>(position, 0)) / kFixed);
    const float y = float(double(field<int64_t>(position, 8)) / kFixed);
    const float z = float(double(field<int64_t>(position, 16)) / kFixed);
    if (!(x > 1.0f && x < 12000.0f && z > 1.0f && z < 12000.0f && std::isfinite(y))) return;
    const uint64_t now = GetTickCount64();
    bool save = false;
    {
        std::lock_guard guard(g_lock);
        auto& p = g_players[id];
        p.x = x, p.y = y, p.z = z, p.seen = now;
        if (g_local.valid && now - g_local.seen < 1000 && std::fabs(g_local.x - x) < 1.5f && std::fabs(g_local.z - z) < 1.5f &&
            std::fabs(g_local.y - y) < 3.0f) {
            g_local.entity = id;
            if (const uint64_t account = SlotAccount(id)) g_local.account = account;
        }
        Check(id, p, now);
        save = g_saveAfter && now >= g_saveAfter;
        if (save) g_saveAfter = 0;
    }
    if (save) progress::Save();
}

void ObserveWorld(void* ctx) {
    if (!Authority(ctx)) return;
    auto* storage = Storage(ctx);
    const auto realm = at<uint8_t>(at<void*>(ctx, 0), 0xbb);
    void* terrain = storage ? at<void*>(storage, 0x478) : nullptr;
    if (terrain && (!at<void*>(terrain, 0x1bf8) || at<uint64_t>(terrain, 0x1c00) > 256 || realm >= at<uint64_t>(terrain, 0x1c00)))
        terrain = nullptr;
    std::lock_guard guard(g_lock);
    if (g_world.storage && g_world.storage != storage) {
        // Another world: its players and requests are not this one's.
        for (auto& r : g_requests) r = {};
        for (auto& p : g_players) p = {};
        g_local.entity = 0;
    }
    g_world = {storage, terrain ? terrain : (g_world.storage == storage ? g_world.terrain : nullptr), GetTickCount64(), realm};
}

enum class Ground { ready, loading, unknown };

// A short vertical ray around the destination ([y+2.8 m .. y-3.5 m]) that stops at the first cell not
// streamed in yet (options byte 2); the native query queues that cell for loading. Hit +0x12 says every
// crossed cell was available, +0x13 that a surface was hit.
Ground Probe(float x, float y, float z, float& outY) {
    void* terrain = g_world.terrain;
    const uint8_t realm = g_world.realm;
    if (!terrain || !g_ray) return Ground::loading;
    auto count = field<uint64_t>(terrain, 0x1c00);
    auto* realms = field<uint8_t*>(terrain, 0x1bf8);
    if (!realms || !count || count > 256 || realm >= count) return Ground::unknown;
    auto* grid = field<uint8_t*>(realms + size_t(realm) * 0x3870, 0);
    if (!grid) return Ground::unknown;
    const uint32_t shift = field<uint32_t>(grid, 0x10), nx = field<uint32_t>(grid, 0x14), ny = field<uint32_t>(grid, 0x18);
    const float ox = field<float>(grid, 0x70), oy = field<float>(grid, 0x74), oz = field<float>(grid, 0x78);
    if (shift > 12 || !nx || !ny || nx > 65536 || ny > 4096 || !std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz))
        return Ground::unknown;
    const double cell = double(uint64_t(1) << shift), width = nx * cell, height = ny * cell;
    if (x < ox || z < oz || x >= ox + width || z >= oz + width) return Ground::unknown;
    const float top = std::min(float(oy + height + 0.25), y + 2.8f), bottom = std::max(oy, y - 3.5f);
    if (top <= bottom) return Ground::unknown;
    alignas(16) float from[4] = {x, top, z, 0.0f};
    alignas(16) float to[4] = {x, bottom, z, 0.0f};
    alignas(16) uint8_t hit[64]{};
    const uint8_t options[4] = {realm, 0, 1, 1};
    g_ray(hit, terrain, from, to, options);
    if (!hit[0x12]) return Ground::loading;
    if (hit[0x13]) {
        const float found = field<float>(hit, 4);
        if (std::isfinite(found) && found >= bottom - 0.5f && found <= top + 0.5f) outY = found;
    }
    return Ground::ready;
}

// Caller holds g_lock. Queues the player's move to x, y, z.
protocol::Answer QueueAt(uint32_t entity, uint64_t nonce, float x, float y, float z) {
    if (g_paused || g_faulted || !entity || entity > 16) return protocol::Answer::unavailable;
    const uint64_t now = GetTickCount64();
    for (auto& r : g_requests)
        if (r.stage != Stage::none && r.entity == entity) {
            if (now < r.expires) return protocol::Answer::busy;
            r = {};
        }
    for (auto& r : g_requests) {
        if (r.stage != Stage::none) continue;
        r = {};
        r.stage = Stage::resolving;
        r.entity = entity;
        r.nonce = nonce;
        r.expires = now + 20000;
        r.target[0] = int64_t(double(x) * kFixed);
        r.target[1] = int64_t(double(y) * kFixed);
        r.target[2] = int64_t(double(z) * kFixed);
        return protocol::Answer::queued;
    }
    return protocol::Answer::busy;
}

// Caller holds g_lock. Queues the player's move to the beacon: two metres out from it, where the game
// spawns players (its five landing pads lie on that circle).
protocol::Answer Queue(uint32_t entity, uint64_t nonce, int hall, int beacon) {
    const auto& b = halls::kBeacons[halls::kHalls[hall].first + beacon];
    const auto result = QueueAt(entity, nonce, b.x, b.y + 0.5f, b.z + 2.0f);
    if (result == protocol::Answer::queued) {
        char line[160];
        std::snprintf(line, sizeof(line), "Hollow Halls Progression: player %u travels to Return Beacon %d of the %s Hollow Halls", entity,
                      halls::ShownNumber(hall, beacon), halls::kHalls[hall].region);
        Log(line);
    }
    return result;
}

#ifdef HHP_DEV
// Development builds: greeneye\hollow-halls-progression\goto.txt holding "x y z [player]" moves that
// player (or the only one around) there once, then the file is deleted.
std::wstring g_gotoFile;
uint64_t g_gotoAt = 0;

void DevGoto() {
    const uint64_t now = GetTickCount64();
    if (g_gotoFile.empty() || now < g_gotoAt) return;
    g_gotoAt = now + 1000;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_gotoFile.c_str(), L"rb") || !f) return;
    float x = 0, y = 0, z = 0;
    unsigned player = 0;
    const int read = fscanf_s(f, "%f %f %f %u", &x, &y, &z, &player);
    fclose(f);
    DeleteFileW(g_gotoFile.c_str());
    std::lock_guard guard(g_lock);
    if (read == 3)
        for (uint32_t id = 1; id <= 16; ++id)
            if (now - g_players[id].seen < 3000) {
                if (player) { player = 0; break; }
                player = id;
            }
    char line[160];
    if (read < 3 || !player || player > 16) {
        Log("Hollow Halls Progression [dev]: goto.txt needs \"x y z [player]\" (player required when several are on)");
        return;
    }
    const auto result = QueueAt(player, now, x, y, z);
    std::snprintf(line, sizeof(line), "Hollow Halls Progression [dev]: moving player %u to %.1f, %.1f, %.1f (%s)", player, x, y, z,
                  result == protocol::Answer::queued ? "queued" : "refused");
    Log(line);
}

// Development builds: greeneye\hollow-halls-progression\players.txt (any content) logs every player seen
// in the last 3 s: position, hall, account, that hall's beacons and the finished flag; then it is deleted.
std::wstring g_playersFile;
uint64_t g_playersAt = 0;

void DevPlayers(void* ctx) {
    const uint64_t now = GetTickCount64();
    if (g_playersFile.empty() || now < g_playersAt) return;
    g_playersAt = now + 1000;
    if (GetFileAttributesW(g_playersFile.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    DeleteFileW(g_playersFile.c_str());
    char state[160];
    std::snprintf(state, sizeof(state), "Hollow Halls Progression [dev]: hooks %s, %s, authority %d", g_faulted ? "faulted" : "fine",
                  g_paused ? "paused" : "running", ctx ? int(Authority(ctx)) : -1);
    Log(state);
    std::lock_guard guard(g_lock);
    int shown = 0;
    for (uint32_t id = 1; id <= 16; ++id) {
        const auto& p = g_players[id];
        if (!p.seen || now - p.seen >= 3000) continue;
        const uint64_t account = AccountOf(id);
        const int hall = HallAt(p.x, p.y, p.z);
        char line[256];
        std::snprintf(line, sizeof(line),
                      "Hollow Halls Progression [dev]: player %u at %.1f, %.1f, %.1f, hall %d (kept %d), account %llx, mask %u, finished %d, checked %llu ms ago",
                      id, p.x, p.y, p.z, hall, p.hall, static_cast<unsigned long long>(account),
                      hall >= 0 && account ? unsigned(progress::Get(account).hall[hall]) : 0u, int(p.finished),
                      static_cast<unsigned long long>(now - p.checked));
        Log(line);
        ++shown;
    }
    if (!shown) Log("Hollow Halls Progression [dev]: no players seen in the last 3 s");
}
#else
void DevGoto() {}
void DevPlayers(void*) {}
#endif

// The player system, per player: a waiting request queues the game's Teleport component, the same one
// its own TeleportToTarget uses, at this ECS safe point. ApplyTick then supplies the destination.
void PlayerTick(void* ctx) {
    if (!Authority(ctx)) return;
    const uint32_t id = Id(ctx);
    void* storage = Storage(ctx);
    // Every player passes the player system every tick: its transform gives its position.
    // (The teleport-apply system only passes players with a teleport under way.)
    uint16_t transformType = 133;
    void* transformRef[2]{};
    if (id && id <= 16) g_component(transformRef, storage, &transformType, id);
    if (transformRef[0]) Observe(id, transformRef[0]);
    const uint64_t now = GetTickCount64();
    std::lock_guard guard(g_lock);
    for (auto& r : g_requests) {
        if (r.stage == Stage::none || r.entity != id) continue;
        if (r.storage && r.storage != storage) {
            r = {};
            continue;
        }
        if (now >= r.expires && r.stage == Stage::resolving) {
            Log("Hollow Halls Progression: travel request expired before it started");
            r = {};
            continue;
        }
        if (r.stage != Stage::resolving) continue;
        void* queue = at<void*>(at<void*>(ctx, 0), 8);
        if (!queue || !transformRef[0]) continue;
        const auto* transform = static_cast<const uint8_t*>(transformRef[0]);
        float norm = 0;
        for (int i = 0; i < 4; ++i) {
            const float q = field<float>(transform, 0x18 + i * 4);
            norm += q * q;
        }
        if (!std::isfinite(norm) || std::fabs(norm - 1) > 0.05f) continue;
        if (!g_notify(queue, id, id)) continue;
        auto* command = static_cast<uint8_t*>(g_queue(queue, static_cast<uint8_t*>(queue) + 0x10, 3, 0xbc, id));
        if (!command) continue;
        at<uint32_t>(command, 0xc) = 0x23384d40;
        at<uint16_t>(command, 0x4c) = 0x50;
        std::memset(command + 0x6c, 0, 0x50);
        std::memcpy(command + 0x6c, transform, 0x38);
        std::memcpy(command + 0x6c, r.target, sizeof(r.target));
        r.storage = storage;
        r.stage = Stage::armed;
    }
}

struct Applied {
    uint32_t entity{};
    uint64_t nonce{};
};
thread_local Applied t_applied;

// The teleport-apply system, per player. False defers this entity (as the game's own spawn queries do)
// while the destination's terrain streams in.
bool ApplyTick(void* ctx, void* descriptors) {
    const uint32_t id = Id(ctx);
    if (!Authority(ctx)) return true;
    void* storage = Storage(ctx);
    std::lock_guard guard(g_lock);
    for (auto& r : g_requests) {
        if (r.stage != Stage::armed || r.entity != id || r.storage != storage) continue;
        void* wrapper = static_cast<uint8_t*>(descriptors) + 0x100;
        void* component = at<void*>(wrapper, 0);
        if (!component || at<uint8_t>(component, 0x48)) return true;
        const uint64_t now = GetTickCount64();
        if (now >= r.expires) {
            MarkDirty(wrapper);
            at<uint8_t>(component, 0x48) = 1;
            r = {};
            Log("Hollow Halls Progression: travel request expired; movement cancelled");
            return true;
        }
        const float x = float(double(r.target[0]) / kFixed), y = float(double(r.target[1]) / kFixed), z = float(double(r.target[2]) / kFixed);
        if (!r.terrainSince) r.terrainSince = now;
        float groundY = y;
        const auto ground = Probe(x, y, z, groundY);
        const bool waitedOut = now - r.terrainSince >= kTerrainWaitMs;
        if (ground == Ground::loading && !waitedOut) {
            if (now >= r.logAfter) {
                Log("Hollow Halls Progression: waiting for the beacon's terrain to load");
                r.logAfter = now + 3000;
            }
            return false;
        }
        if (ground == Ground::ready) {
            // Read the same ground on two ticks in a row: a value read the moment cells finish loading
            // is never used on its own.
            const int64_t found = int64_t(double(groundY) * kFixed);
            if (!waitedOut && (!r.groundRead || std::llabs(r.groundY - found) > int64_t(0.05 * kFixed))) {
                r.groundRead = true;
                r.groundY = found;
                return false;
            }
            r.target[1] = found;
        }
        MarkDirty(wrapper);
        std::memcpy(component, r.target, sizeof(r.target));
        at<uint32_t>(component, 0x38) = 0;
        at<uint8_t>(component, 0x48) = 0;
        at<uint8_t>(component, 0x49) = 0;
        t_applied = {id, r.nonce};
        r.stage = Stage::applying;
        if (id < 17) g_players[id].travelled = now;
        char line[160];
        std::snprintf(line, sizeof(line), "Hollow Halls Progression: applying %.1f, %.1f, %.1f (terrain waited %llu ms)", x,
                      double(r.target[1]) / kFixed, z, static_cast<unsigned long long>(now - r.terrainSince));
        Log(line);
        return true;
    }
    return true;
}

void FinishApplication() {
    auto done = t_applied;
    t_applied = {};
    if (!done.entity) return;
    std::lock_guard guard(g_lock);
    for (auto& r : g_requests)
        if (r.stage == Stage::applying && r.entity == done.entity && r.nonce == done.nonce) r = {};
}

// 0: not ours, 1: replaced with answer (host), 2: consumed.
int Chat(void* store, uint8_t type, uint64_t* text, uint32_t senderHandle, uint64_t account, char* answer, size_t capacity) {
    if (type != protocol::kChatType || !text || !text[0] || text[1] > 240) return 0;
    std::string_view line(reinterpret_cast<const char*>(text[0]), size_t(text[1]));
    if (!line.starts_with("/hh")) return 0;
    auto* client = g_client.load();
    if (client && store == at<void*>(client, 0xf8)) {
        // This player's own chat: answers to its own questions go to the client side; every
        // protocol line is kept out of the chat window.
        const auto own = at<uint32_t>(client, 0x466b44);
        if (own && senderHandle == own && !line.starts_with("/hhh1") && !line.starts_with("/hhq1") && !line.starts_with("/hht1"))
            if (auto handler = g_answers.load()) handler(line);
        return 2;
    }
    // The host's store: questions from a player, answered in place.
    if (line == protocol::kHello) {
        std::snprintf(answer, capacity, "%.*s", int(protocol::kHelloReply.size()), protocol::kHelloReply.data());
        return 1;
    }
    if (line == protocol::kQuery) {
        // The host's own game asks this way while it is paused: answer with all of its beacons, as
        // the local path does, or its list would shrink for a moment.
        const uint64_t local = LocalAccount();
        auto masks = local && account == local ? LocalMasks() : progress::Get(account);
        return protocol::FormatMasks(answer, capacity, masks) > 0 ? 1 : 2;
    }
    protocol::Travel travel{};
    if (protocol::ParseTravel(line, travel)) {
        protocol::Answer result = protocol::Answer::unavailable;
        const uint64_t local = LocalAccount();
        const bool has = local && account == local ? (LocalMasks().hall[travel.hall] >> travel.beacon & 1) != 0
                                                   : progress::Has(account, travel.hall, travel.beacon);
        if (!has) {
            result = protocol::Answer::locked;
        } else if (uint32_t entity = SenderEntity(account)) {
            std::lock_guard guard(g_lock);
            result = Queue(entity, travel.nonce, travel.hall, travel.beacon);
        }
        return protocol::FormatAnswer(answer, capacity, travel.nonce, result) > 0 ? 1 : 2;
    }
    return 2;
}

int SafeChat(void* s, uint8_t t, uint64_t* text, uint32_t handle, uint64_t a, char* answer, size_t capacity) {
    __try {
        return Chat(s, t, text, handle, a, answer, capacity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uint8_t* AppendHook(uint8_t* result, void* store, uint8_t type, uint64_t* text, uint32_t a, uint32_t b, uint32_t c, uint64_t account,
                    uint64_t* time) {
    greeneye::Lease lease;
    char answer[160]{};
    const int action = lease ? SafeChat(store, type, text, c, account, answer, sizeof(answer)) : 0;
    if (action == 2) {
        std::memset(result, 0, 8);
        return result;
    }
    if (action == 1) {
        uint64_t replacement[] = {reinterpret_cast<uint64_t>(answer), std::strlen(answer)};
        return g_append(result, store, type, replacement, a, b, c, account, time);
    }
    return g_append(result, store, type, text, a, b, c, account, time);
}
// A fault in the game's data turns the host part off for this session; said once, with where.
void Faulted(const char* where) {
    if (!g_faulted.exchange(true)) {
        char line[160];
        std::snprintf(line, sizeof(line), "Hollow Halls Progression: a fault in %s; beacons and travel are off until the next start", where);
        Log(line);
    }
}

void SafeObserve(void* c) {
    __try { ObserveWorld(c); } __except (EXCEPTION_EXECUTE_HANDLER) { Faulted("watching the world"); }
}
void SafePlayer(void* c) {
    __try { PlayerTick(c); } __except (EXCEPTION_EXECUTE_HANDLER) { Faulted("the player tick"); }
}
bool SafeApply(void* c, void* d) {
    __try { return ApplyTick(c, d); } __except (EXCEPTION_EXECUTE_HANDLER) { Faulted("applying a journey"); return true; }
}
void SafeFinish() {
    __try { FinishApplication(); } __except (EXCEPTION_EXECUTE_HANDLER) { Faulted("finishing a journey"); }
}

bool PlayerFirst(void* c, void* d, size_t n) {
    greeneye::Lease lease;
    if (lease) DevPlayers(c);
    if (lease && !g_faulted && !g_paused) SafeObserve(c);
    const bool ok = g_playerFirst(c, d, n);
    if (lease && ok && !g_faulted && !g_paused) {
        SafePlayer(c);
        if (Authority(c)) {
            SafeWatchChests(c);
            DevGoto();
        }
    }
    return ok;
}
bool PlayerMore(void* c, void* d, size_t n) {
    greeneye::Lease lease;
    const bool ok = g_playerMore(c, d, n);
    if (lease && ok && !g_faulted && !g_paused) SafePlayer(c);
    return ok;
}
bool ApplyFirst(void* c, void* d, size_t n) {
    greeneye::Lease lease;
    bool ok = g_applyFirst(c, d, n);
    while (lease && ok && !g_faulted && !g_paused && !SafeApply(c, d)) ok = g_applyFirst(c, d, n);
    return ok;
}
bool ApplyMore(void* c, void* d, size_t n) {
    greeneye::Lease lease;
    if (lease) SafeFinish();
    bool ok = g_applyMore(c, d, n);
    while (lease && ok && !g_faulted && !g_paused && !SafeApply(c, d)) ok = g_applyMore(c, d, n);
    return ok;
}
void ReceiveHook(void* s) {
    greeneye::Lease lease;
    if (lease) g_server = s;
    g_receive(s);
}

bool CallMatches(const GreeneyeHost* host, uint8_t* site, uint8_t* expected) {
    uint8_t bytes[5]{};
    host->read(site, bytes, 5);
    int32_t rel = 0;
    std::memcpy(&rel, bytes + 1, 4);
    return bytes[0] == 0xe8 && site + 5 + rel == expected;
}

}  // namespace

bool Init(const GreeneyeHost* host) {
    auto* player = host->find(kPlayerPattern, nullptr);
    auto* apply = host->find(kApplyPattern, nullptr);
    auto* next = host->find(kNextPattern, nullptr);
    g_entityOf = reinterpret_cast<EntityFn>(host->find(kEntityPattern, nullptr));
    g_dirty = reinterpret_cast<DirtyFn>(host->find(kDirtyPattern, nullptr));
    g_ray = reinterpret_cast<RayFn>(host->find(kRayPattern, nullptr));
    g_queue = reinterpret_cast<QueueFn>(host->find(kQueuePattern, nullptr));
    g_notify = reinterpret_cast<NotifyFn>(host->find(kNotifyPattern, nullptr));
    g_component = reinterpret_cast<ComponentFn>(host->find(kComponentPattern, nullptr));
    auto* receive = host->find(kReceivePattern, nullptr);
    if (!receive) receive = host->find(kReceivePattern2, nullptr);
    auto* chat = host->find(kChatPattern, nullptr);
    if (!player || !apply || !next || !g_entityOf || !g_dirty || !g_ray || !g_queue || !g_notify || !g_component || !receive || !chat) {
        Log("Hollow Halls Progression: host signatures not found; disabled");
        return false;
    }
    if (!CallMatches(host, player + 0x35, next) || !CallMatches(host, player + 0x11c6, next) || !CallMatches(host, apply + 0x31, next) ||
        !CallMatches(host, apply + 0x7be, next)) {
        Log("Hollow Halls Progression: player system call sites did not match; disabled");
        return false;
    }
    g_playerFirst = reinterpret_cast<NextFn>(host->redirectCall(player + 0x35, reinterpret_cast<void*>(PlayerFirst)));
    g_playerMore = reinterpret_cast<NextFn>(host->redirectCall(player + 0x11c6, reinterpret_cast<void*>(PlayerMore)));
    g_applyFirst = reinterpret_cast<NextFn>(host->redirectCall(apply + 0x31, reinterpret_cast<void*>(ApplyFirst)));
    g_applyMore = reinterpret_cast<NextFn>(host->redirectCall(apply + 0x7be, reinterpret_cast<void*>(ApplyMore)));
    g_receive = reinterpret_cast<ReceiveFn>(host->hook(receive, reinterpret_cast<void*>(ReceiveHook), 13, nullptr, 0));
    g_append = reinterpret_cast<AppendFn>(host->hook(chat, reinterpret_cast<void*>(AppendHook), 13, nullptr, 0));
#ifdef HHP_DEV
    g_gotoFile = std::wstring(host->modDir) + L"goto.txt";
    g_playersFile = std::wstring(host->modDir) + L"players.txt";
#endif
    const bool ok = g_playerFirst && g_playerMore && g_applyFirst && g_applyMore && g_receive && g_append;
    if (!ok) Log("Hollow Halls Progression: host hooks could not be installed");
    g_paused = !ok;
    return ok;
}

void SetClient(void* gameClient, bool valid, float x, float y, float z, uint32_t entity) {
    g_client = gameClient;
    std::lock_guard guard(g_lock);
    g_local.valid = gameClient && valid;
    if (g_local.valid) {
        g_local.x = x, g_local.y = y, g_local.z = z;
        g_local.seen = GetTickCount64();
    }
    // The client's own player handle names its entity outright; the position match in Observe
    // covers a moment where it is not set yet.
    if (gameClient && entity && entity <= 16) {
        g_local.entity = entity;
        g_local.seen = GetTickCount64();
    }
}

bool HostsWorld() {
    std::lock_guard guard(g_lock);
    return g_world.storage && GetTickCount64() - g_world.seen <= 2000;
}

void OnAnswer(AnswerHandler handler) { g_answers = handler; }

uint64_t LocalAccount() {
    std::lock_guard guard(g_lock);
    return g_local.account;
}

// The host's own beacons: the local player's record, and the one under its account (saved while the
// local player was not recognised, or answered by the chat path).
protocol::Masks LocalMasks() {
    auto masks = progress::Get(progress::kLocalPlayer);
    if (const uint64_t account = LocalAccount()) {
        const auto more = progress::Get(account);
        for (int h = 0; h < halls::kHallCount; ++h) masks.hall[h] |= more.hall[h];
    }
    return masks;
}

protocol::Answer TravelLocal(int hall, int beacon) {
    if (hall < 0 || hall >= halls::kHallCount || beacon < 0 || beacon >= halls::kHalls[hall].count) return protocol::Answer::unavailable;
    if (!(LocalMasks().hall[hall] >> beacon & 1)) return protocol::Answer::locked;
    std::lock_guard guard(g_lock);
    if (!g_local.entity || GetTickCount64() - g_local.seen > 3000) return protocol::Answer::unavailable;
    return Queue(g_local.entity, GetTickCount64(), hall, beacon);
}

bool Prepare() {
    g_paused = true;
    std::lock_guard guard(g_lock);
    for (const auto& r : g_requests)
        if (r.stage == Stage::armed || r.stage == Stage::applying) {
            g_paused = false;
            return false;
        }
    for (auto& r : g_requests) r = {};
    return true;
}

void Resume() { g_paused = false; }

}  // namespace hhp::host
