Hollow Halls Progression 1.0.5 for Enshrouded
==============================================

The Hollow Halls are long. This mod remembers every Return Beacon you reach inside them, so you
can leave and come back later without clearing the whole hall again.

How to use it
-------------
1. Walk up to a Return Beacon in any Hollow Halls. A notice says it's saved
   ("Return Beacon 2 of 5 reached").
2. Later, open the world map and click that Hollow Halls. Under Send Ping and Set as Waypoint
   there is a button for each Return Beacon you reached.
3. Click one. A loading screen shows a picture of that hall while the area loads, and you
   arrive next to the beacon.

Install (your game)
-------------------
1. Close Enshrouded.
2. Open the game folder (Steam: right-click Enshrouded > Manage > Browse local files).
3. Copy everything from this zip into it, keeping the folders: dbgcore.dll next to
   enshrouded.exe, and the greeneye\hollow-halls-progression folder.
4. Start the game.

Single player and hosting your own game: that's all. Your game is the host.

Dedicated server
----------------
The mod has to be on the server AND on the game of every player who wants beacon travel.
1. Stop the server.
2. Copy everything from this zip into the server folder (the one with enshrouded_server.exe),
   the same way: dbgcore.dll next to enshrouded_server.exe, and the
   greeneye\hollow-halls-progression folder.
3. Start the server. greeneye\loader.log in the server folder should contain
   "Hollow Halls Progression 1.0.5: server companion loaded".
Players without the mod can still join; they just don't get beacon travel.

Settings
--------
greeneye\hollow-halls-progression\hollow-halls-progression.ini, each setting explained in the
file. The host's ini decides for everyone:
  ResetOnBossKill=false  Return Beacons stay unlocked for good (default).
  ResetOnBossKill=true   A player's beacons in a hall reset once they finish it (when one of the
                         hall's end chests is looted while they are inside).
Each player's own ini decides whether, and in which language, notices are shown.
The host keeps everyone's beacons in progress.txt in the same folder.

Uninstall
---------
Delete greeneye\hollow-halls-progression. If no other Greeneye mod is left, also delete
dbgcore.dll and the greeneye folder.

Troubleshooting
---------------
Please attach this file to a bug report:
  greeneye\loader.log in the game folder (the mod's lines start with "Hollow Halls Progression:").
On a dedicated server, also greeneye\loader.log from the server folder, and progress.txt from
greeneye\hollow-halls-progression if beacons go missing.

Made by GreeneyeDS with the help of AI. MIT license (see LICENSE).
Source: https://github.com/Greeneye0/hollow-halls-progression-enshrouded
