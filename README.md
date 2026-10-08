# GTAIV KillCam

`.asi` for **GTA IV Complete Edition 1.2.0.59** (x86, Ultimate ASI Loader, `plugins\` folder).
When the player kills an on-foot NPC with a headshot or a single shot, a scripted camera orbits
the victim with `SET_TIME_SCALE 0.25` for 2 s, then the camera and time scale are restored.

Standalone: it does not use IV-SDK, because IV-SDK (and IV-SDK .NET) only know 1.0.7.0/1.0.8.0
addresses. Instead it finds everything by pattern scan, using the Complete Edition patterns from
[GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix) (native table, pause
flags, ped pool, active script thread, main game-loop call).

## Build (Visual Studio 2019+, Windows)

Open `build/GTAIV_KillCam.sln`, Release | Win32, build. Output: `bin/GTAIV_KillCam.asi`.
No external dependencies (no DirectX SDK, no IV-SDK). Must be 32-bit.

## Install

Copy `GTAIV_KillCam.asi` and `GTAIV_KillCam.ini` to `...\Grand Theft Auto IV\GTAIV\plugins\`.
Log: `GTAIV_KillCam.log` next to the `.asi`.

## Safety against other mods (FusionFix, DLSS-IV)

- No D3D/Vulkan/DXVK hooks. The only code patch is one `E8 rel32` in the game loop; it keeps
  whatever that call pointed to (original or another mod's hook), so load order does not matter.
- Every pattern is checked; if one is missing the mod logs it and does nothing. No `assert`,
  no message boxes. Init runs in its own thread and retries for 60 s.
- Per-frame code runs inside an SEH guard; after 3 exceptions it disables itself.
- Only runs while the game is not user/script-paused (same condition FusionFix uses).

## Behaviour

- Kill = ped dead, `HAS_CHAR_BEEN_DAMAGED_BY_CHAR(ped, player)`, victim not in car/bike/boat/heli.
- Headshot = `GET_CHAR_LAST_DAMAGE_BONE` in `HeadBoneIds` (default 0x4B5 = 1205).
- One shot = victim's health never dropped before the lethal frame and the player held a firearm.
- Needs (headshot or one shot) + not on cooldown + chance roll + victim within range.
- Camera: 12 angles x 3 radii are tested for line of sight; none clear -> killcam skipped.
  While running it orbits and only moves to positions that pass the same check.
- Duration and cooldown use real time. Ends early if the player dies or the pause menu opens.

## Known limits (read this)

Not compiled or run: the authoring environment has no Windows/MSVC. `dllmain.cpp` was only
syntax-checked against stubs. Nothing was tested in-game.

1. **Line of sight is weak by default.** I have no verified 1.2.0.59 address/pattern for
   `CWorld::ProcessLineOfSight` (only 1.0.7/1.0.8 addresses exist publicly). Without it the
   check only probes ground height along the ray (`GET_GROUND_Z_FOR_3D_COORD`), so it detects
   terrain/floors but **not walls**. If you locate the function, put a unique byte pattern of its
   entry in `[LineOfSight] RaycastPattern`. Its signature is assumed identical to 1.0.8.0
   (cdecl, 9 args, nonzero = hit), which is unverified for 1.2.0.59.
2. Patterns come from FusionFix's source. If FusionFix updates the game code expectations or the
   exe differs from yours (MD5 `1a47b45f...`), a pattern may stop matching; the log says which.
3. Natives run from the game loop with a zeroed dummy script thread (same trick as IV-SDK /
   FusionFix). Camera natives were not checked for needing a real script owner.
4. `CREATE_CAM(14)` + `ACTIVATE_SCRIPTED_CAMS` without `BEGIN/END_CAM_COMMANDS`.
5. `HAS_CHAR_BEEN_DAMAGED_BY_CHAR` is assumed to stay true on the death frame.
6. Head bone id 0x4B5 comes from the IV-SDK .NET enum, not from a live check; read the
   `player kill: ... bone N` log line after a headshot.
