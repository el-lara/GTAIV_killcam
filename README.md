# GTAIV KillCam

`.asi` mod for GTA IV (IV-SDK by Zolika1351, x86). When the player kills an on-foot NPC with a
headshot or with a single shot, a scripted camera orbits the victim with `SET_TIME_SCALE 0.25`
for 2 s, then the camera and time scale are restored.

## IMPORTANT: game version

IV-SDK supports **only 1.0.7.0 and 1.0.8.0 (EFIGS)**. On any other exe (including 1.2.0.59)
`AddressSetter::DetermineVersion` leaves `gameVer = VERSION_NONE` and the SDK installs no hooks:
the mod loads and does nothing (no log either). The SDK's hook/pool/LOS addresses for 1.2.0.59
are not published, so none were invented here. Options: run it on 1.0.8.0, or port the address
table (`Addresses.h`, `Hooks.h`, `CWorld.h`, `CPool.h`, `CPools.h`, `CPlayerInfo.h`,
`CTheScripts.h`) to 1.2.0.59 yourself.

## Build (Visual Studio 2019+, Windows)

1. `git clone https://github.com/Zolika1351/iv-sdk third_party/iv-sdk`  (or pass `/p:IVSDK_DIR=<path>`)
2. Install the DirectX June 2010 SDK (the SDK includes `d3dx9.h`; `DXSDK_DIR` must be set).
3. Open `build/GTAIV_KillCam.sln`, Release | Win32, build. Output: `bin/GTAIV_KillCam.asi`.

## Install

Copy `GTAIV_KillCam.asi` and `GTAIV_KillCam.ini` to the game folder (ASI loader required).
Log: `GTAIV_KillCam.log` next to the `.asi`.

## Behaviour

- Kill = ped dead, `HAS_CHAR_BEEN_DAMAGED_BY_CHAR(ped, player)`, victim not in a vehicle.
- Headshot = `GET_CHAR_LAST_DAMAGE_BONE` is in `HeadBoneIds`.
- One shot = victim's health never dropped before the lethal frame and the player held a firearm.
- Triggers if (headshot or one shot), not on cooldown, chance roll passes, victim within range.
- Camera: 12 angles x 3 radii are tested with `CWorld::ProcessLineOfSight` (statics, buildings,
  vehicles, objects) from camera to victim, plus short rays around the camera so it is not
  inside geometry. If none is clear, the killcam is skipped. While running, the camera orbits
  and only moves to positions that pass the same check.
- Duration and cooldown use real time (the time scale does not affect them).
- Ends early if the player dies or the pause menu opens.

## Not verified

Nothing here was compiled against the real SDK or run in-game (no Windows/MSVC in the authoring
environment; only a syntax check of `dllmain.cpp` against stubs). Check in-game:

- `HeadBoneIds=1301` is from memory, not confirmed. Shoot a head and read the `bone N` log line.
- `CREATE_CAM(14)` + `ACTIVATE_SCRIPTED_CAMS` without `BEGIN/END_CAM_COMMANDS`; add them if the
  camera does not take over.
- `HAS_CHAR_BEEN_DAMAGED_BY_CHAR` is assumed to stay true on the death frame.
