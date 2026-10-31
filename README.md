# Hollow Halls Progression — Return Beacon travel for Enshrouded

The Hollow Halls are long. This mod remembers every Return Beacon you reach inside them, so you don't have to clear a hall in one sitting.

- Walk up to a Return Beacon in any of the six Hollow Halls: a notice tells you it's saved ("Return Beacon 2 of 5 reached").
- Later, open the world map and click that Hollow Halls. Below Send Ping and Set as Waypoint you get one button per beacon you reached.
- Pick one: a loading screen with a picture of that hall shows while the area loads, and you arrive next to the beacon.
- Server setting `ResetOnBossKill`: beacons stay unlocked for good (default), or a hall's beacons reset once a player finishes it (loots one of its end chests).
- Single player, self-hosted and dedicated servers (the server needs the mod too; it keeps everyone's progress).
- Notices and the loading screen in all 15 languages the game supports, following the game's language setting. The texts are in `src/strings.h`.

Download and install instructions: see the [Nexus Mods page](https://www.nexusmods.com/enshrouded/mods/272). The player-facing readme is [dist/README.txt](dist/README.txt).

## Settings

`greeneye\hollow-halls-progression\hollow-halls-progression.ini`, each setting documented in the file. The host (the dedicated server, or the game when you play alone or host) keeps each player's beacons in `progress.txt` next to it. Restart the game or server after changing the ini.

## Building

Needs Visual Studio 2022 Build Tools (x64) and Python 3 with Pillow for the tools.

```
build.bat                 -> bin\loader\hollow-halls-progression.dll, mod.json, ini
build-dev.bat             -> bin-dev\loader\... with HHP_DEV (testing aids; never ship it)
tools\make_halls.py       -> src\halls.h (beacons, end chests and hall boxes from the game's placement data)
tools\export_loading.py   -> loading\*.jpg (the loading screen pictures, from in-game shots)
tools\package.py          -> dist\Hollow-Halls-Progression-<version>.zip
```

The mod runs through the Greeneye Loader (API 2, `src/greeneye.h`). Every game function is found by byte signature; if a binding is missing the mod switches the affected part off instead of guessing. Every detour holds a `greeneye::Lease`, so the mod can be hot reloaded while idle.

How it fits together:

- `host.cpp` (the host only): watches every player's position in the player system, unlocks a beacon within 16 m, notices a finished hall when an end chest's inventory empties, and moves a player to a beacon with the game's own Teleport component (after the destination terrain has loaded).
- `main.cpp` (every game): the world map's marker menu (extra buttons on a Hollow Halls marker), the notices and the loading screen, and the hidden chat lines (type 255) it uses to ask the host for its beacons and to travel. Servers with text chat off work too.
- `progress.cpp`: the host's `progress.txt`, per player account.

## License

MIT, see [LICENSE](LICENSE). Made with the help of AI (Claude by Anthropic). The loading screen pictures are in-game screenshots of Enshrouded; Enshrouded and its assets belong to Keen Games.
