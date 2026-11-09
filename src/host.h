#pragma once
// The authoritative side: runs wherever the world runs (dedicated server, the player hosting, or
// single player). It watches every player's position, unlocks the Return Beacons they reach,
// notices a finished hall, moves players to a beacon on request and answers remote players.
#include "greeneye.h"
#include "protocol.h"
#include <cstdint>
#include <string_view>

namespace hhp::host {

bool Init(const GreeneyeHost* host);

// Client side, every frame: the live game client (null outside a world) and the local player's
// position (valid false when unknown). Lets the host tell which of its players is this machine's.
void SetClient(void* gameClient, bool valid, float x, float y, float z, uint32_t entity);
// True while this process runs the world the local player is in.
bool HostsWorld();

// Hidden lines the host sent back to this player (the answers to its own questions).
using AnswerHandler = void (*)(std::string_view line);
void OnAnswer(AnswerHandler handler);

// The local player's beacons and travel while this process hosts the world.
uint64_t LocalAccount(); // the server account of this game's own player, 0 until known
protocol::Masks LocalMasks();
protocol::Answer TravelLocal(int hall, int beacon);

bool Prepare();
void Resume();

}  // namespace hhp::host
