#pragma once
// The link to the world map mod (Unlock Map Markers, another mod in the same game process): a small
// shared block, Local\HollowHallsProgression.<pid>. This side publishes which Return Beacons the host
// has on record for you; the map mod numbers each hall's beacons on its dungeon map and, when you click
// a reached one, leaves a journey request here, taken on the next frame like a click in the marker menu.
// Without the map mod nobody opens the block.
#include <windows.h>
#include <cstdint>
#include <cwchar>

namespace hhp::map_link {

struct Block {                 // 20 bytes; the map mod has the same layout
    uint32_t magic;            // 'HHL1'
    volatile LONG known;       // 1 while masks hold the host's record for you
    uint8_t masks[8];          // per hall (halls.h order): bit n = beacon n + 1 reached
    volatile LONG request;     // 0 none, else 0x80000000 | hall << 8 | beacon (from 0); taken by exchanging 0
};
static_assert(sizeof(Block) == 20, "map link layout");
inline constexpr uint32_t kMagic = 0x314C4848u; // 'HHL1'

inline Block* Get() {
    static Block* block = nullptr;
    static bool tried = false;
    if (block || tried) return block;
    tried = true;
    wchar_t name[64];
    swprintf_s(name, L"Local\\HollowHallsProgression.%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    // Kept open for the process (a hot reload opens the same block again)
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Block), name);
    if (!mapping) return nullptr;
    block = static_cast<Block*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block)));
    if (block) block->magic = kMagic;
    return block;
}

// The host's record for you (have false: not known yet, e.g. a new world). The map mod offers a journey only to a
// beacon this record has; the host checks its own record again before any journey.
inline void Publish(const uint8_t* masks, int halls, bool have) {
    Block* b = Get();
    if (!b) return;
    InterlockedExchange(&b->known, 0);
    if (!have) InterlockedExchange(&b->request, 0); // a request made against an older record
    for (int h = 0; h < 8; ++h) b->masks[h] = h < halls ? masks[h] : 0;
    if (have) InterlockedExchange(&b->known, 1);
}

// Paused or unloading: no record to offer, and a request left meanwhile is dropped (never a journey later)
inline void Withdraw() {
    Block* b = Get();
    if (!b) return;
    InterlockedExchange(&b->known, 0);
    InterlockedExchange(&b->request, 0);
}

// A journey the map mod asked for
inline bool TakeRequest(int& hall, int& beacon) {
    Block* b = Get();
    if (!b) return false;
    const LONG r = InterlockedExchange(&b->request, 0);
    if (!(r & 0x80000000)) return false;
    hall = (r >> 8) & 0xff;
    beacon = r & 0xff;
    return true;
}

}  // namespace hhp::map_link
