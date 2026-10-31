#pragma once
// The host's record of which Return Beacons each player has reached (progress.txt in the mod's
// folder), and the host's [Progress] settings. Players are told apart by their account (the hash
// the game's chat carries for a remote player; kLocalPlayer for the person playing on the host).
#include "protocol.h"
#include <cstdint>

namespace hhp::progress {

inline constexpr uint64_t kLocalPlayer = 1;

struct Settings {
    bool resetOnBoss = false; // a hall's beacons are forgotten when its boss fight is over
    bool unlockAll = false;   // testing: every beacon counts as reached (progress.txt is unchanged)
};

// Reads the settings and the saved progress; dir ends with a backslash.
void Init(const wchar_t* dir);
Settings Current();
protocol::Masks Get(uint64_t account);
bool Has(uint64_t account, int hall, int beacon);
// True when it was not unlocked before.
bool Unlock(uint64_t account, int hall, int beacon);
// True when the player had any beacon of the hall.
bool ResetHall(uint64_t account, int hall);
// Writes progress.txt when something changed since the last save.
void Save();

}  // namespace hhp::progress
