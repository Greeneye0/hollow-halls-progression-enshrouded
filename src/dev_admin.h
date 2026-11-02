#pragma once
// Development builds only (build-dev.bat, HHP_DEV): lets the dedicated server accept the game's own
// admin command for world settings, so testers can switch on the Quick Deposit dev bridge's combat
// test mode (no enemy damage, weak bosses) while playing on a server. Retail gives nobody admin.
//
// The server's receive clears a player's admin commands and its processing ignores them unless the
// "is admin" check (the same function for both) says yes. While greeneye\hollow-halls-progression\
// admin.enable exists on the server, these two calls answer yes; the third caller (spawning and the
// other admin commands) is left alone. Same evidence as QuickDeposit dev/admin_ops.cpp, whose
// client-side sites are exe+0x4a11aa and exe+0x2d0158 (build 26320373); on the server build
// 0x6ae46022 (26319703) they are +0x23967a and +0xbc1d8.
#include "greeneye.h"
#include <cstdint>
#include <string>

namespace hhp::dev_admin {

bool Init(const GreeneyeHost* host);

}  // namespace hhp::dev_admin
