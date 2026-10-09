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
- `BodyKillChancePercent` (default 5): an ordinary firearm kill can also trigger.
- Explosion kills (`ExplosionKillChancePercent`, default 4): filmed from 7-12 m, eye level or high angle.
  Detected with `HAS_CHAR_BEEN_DAMAGED_BY_WEAPON` for weapon ids 4, 5, 6, 18, 51; unverified in-game, see the
  `explosion=` field of the `player kill:` log line.
- Sniper weapons (`SniperWeaponIds`, default 16/17): headshot or one-shot kills use `SniperChancePercent` (100) and
  `SniperCooldownSec` (7) instead of `ChancePercent` and `CooldownSec` and may be up to `SniperMaxDistance` away. Far kills may be filmed where the
  world is not fully streamed in; unverified.
- `LockAimDuringKillcam`/`LockAimMethod`: while the killcam runs, the gameplay camera controls
  (`SET_GAME_CAMERA_CONTROLS_ACTIVE`, method 1) and/or the player control (`SET_PLAYER_CONTROL`, method 2) are
  switched off and restored when it ends. Method 1 alone did not work for the author; unverified otherwise. The
  log line `aim lock (method N): player control before=.. after=..` shows whether the native took effect.
- When a killcam ends and the camera returns to the player, slow motion continues for `AfterSlowSec` (0.5 s) at
  the time scale of the killcam that just played, then blends back to normal. During the killcam and that tail the
  player's animations run at `PlayerSpeedDuringKillcam` (0.5) so you barely move/turn. Unverified in-game.
- The aim lock (`SET_GAME_CAMERA_CONTROLS_ACTIVE`) now also covers the slow tail. The log line `aim lock released`
  prints the gameplay camera rotation before and after: if they differ, the lock did not hold. `HoldPlayerHeading` (default on) re-applies the player's heading every frame during the killcam and its tail so the
  character cannot turn, and `RestoreAimHeading` (default 0 = off) can turn the gameplay camera when the lock ends (1 = saved heading,
  2 = heading 0). With 1 the aim spun at the end in testing, so it is off.
- Dead Eye costs `ActivationCost` (5% of the meter) every time it is switched on.
- Cooldown: `CooldownSec` 30, but after `ShortCooldownChancePercent` (45) of killcams it is `ShortCooldownSec` (15);
  chosen when each killcam starts and logged as `next cooldown`. Sniper kills keep `SniperCooldownSec`.
- Not every qualifying kill triggers: `ArmNextKillChancePercent` (25) makes a kill only arm the next one, and the
  next qualifying kill within `ArmNextKillWindowSec` (8) triggers for sure.
- Cinematic killcams (`CinematicChancePercent` 15, 50 when armed): time scale 0.03-0.07 for 3.2 s, the victim for
  55% of it, then a cut to the shooter in a random style: 3/4 front, gun pointed at the camera, low hero, over the shoulder,
  profile, high angle or head close-up (weights in the .ini), sometimes with a slow push-in. Unverified in-game.
- Kills refill the Dead Eye meter by `KillRefill` (default 7.5%); headshot kills refill `HeadshotRefill` (20%) instead. While Dead Eye is active: `KillRefillWhileActive` 4%,
  `HeadshotRefillWhileActive` 15%. Killcam chances are multiplied by `KillcamChanceMultiplier` (0.7) while Dead Eye is active. Kills while Dead Eye is active also restore health
  (`HealthOnKillWhileActive` 8 points, `HealthOnHeadshotWhileActive` 25; scale unverified, see the log).
- NPCs in cars/bikes qualify too (`VehicleKills`): camera mostly in front of the vehicle (`VehicleFrontPercent` 70), otherwise from an angle of 25-70 degrees,
  sometimes orbiting slowly (`VehicleOrbitChancePercent` 25); it follows the vehicle (heading is smoothed). A blocked angle
  falls back to straight ahead; if that is blocked too, skipped.
  Not tested in-game: whether `GET_CAR_CHAR_IS_USING` works for a dead ped is unverified (the log says if not).
- Needs (headshot or one shot) + not on cooldown + chance roll + victim within range.
- Each killcam picks a random shot: movement (fixed 60 / orbit 30 / dolly 10, random orbit
  direction and speed) x angle (eye level / high angle / low angle looking up) and a random
  slow-motion scale. All weights and ranges are in the .ini. The log line `killcam start:` shows what was picked.
- Camera: 12 angles x 3 radii are tested for line of sight; a blocked high/low shot falls back to
  eye level; none clear -> killcam skipped. Moving shots only move to positions that pass the same check.
- Duration and cooldown use real time. Ends early if the player dies or the pause menu opens.

## Dead Eye

Hold the key (default Left Alt, `[DeadEye] Key`) to drop `SET_TIME_SCALE` to 0.25 with a short blend.
The player's animations are sped up by `PlayerSpeed / TimeScale` (default 0.85 / 0.25 = x3.4,
capped at `MaxAnimSpeed`) using `SET_CHAR_ALL_ANIMS_SPEED`, so the player moves, aims and reloads
at about 85% of normal speed while NPCs and physics run at 25%. It is suspended while a killcam
plays and released if the player dies or the pause menu opens. It has an energy meter (10 s by default,
1 s delay, then refills over 20 s; after running dry it needs 20% to start again), shown as a text bar
`DEAD EYE [=====.....]` drawn with the game's text natives. Not tested in-game: whether the
anim-speed native multiplies with the time scale as assumed, and whether camera/aim turning is
slowed, are unknown. `PlayerSpeedMethod=2` tries the move-speed multiplier instead.

## Diagnosing with the log

- `heartbeat:` every 5 s: frames seen by the hook, ticks that passed all guards, ped count,
  player handle, pause flags. `frames` rising with `ticks` stuck = a guard is bailing.
- `bail: ...` is logged once per reason change (paused, ped pool invalid, player handle 0).
- At startup it dumps the bytes around the pause-flag match and logs both flag pointers.
- Do not validate patterns against `GTAIV.exe` on disk: the Complete Edition exe is protected
  (`.tbm`/`.rkstr`, parts of `.text` encrypted). Only in-memory scanning works.

## Known limits (read this)

Not compiled or run: the authoring environment has no Windows/MSVC. `dllmain.cpp` was only
syntax-checked against stubs. Nothing was tested in-game.

1. **Line of sight is approximate by default.** Without a raycast the camera spot is checked with natives: a
   top-surface probe along the line (`ProbeWalls`: walls/buildings/roofs) and `GET_CLOSEST_CAR` (`ProbeCars`:
   vehicles). This is crude: it also rejects spots inside or under buildings. The text below is the older description.
   **Line of sight was weak by default.** I have no verified 1.2.0.59 address/pattern for
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
