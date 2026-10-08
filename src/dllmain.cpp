// GTAIV KillCam - slow-motion scripted camera on headshot / one-shot on-foot kills.
//
// Standalone x86 .asi for GTA IV Complete Edition (1.2.0.x, tested target 1.2.0.59).
// No IV-SDK: IV-SDK only knows 1.0.7.0/1.0.8.0 addresses. Everything is found by pattern
// scanning (patterns taken from ThirteenAG's GTAIV.EFLC.FusionFix, Complete Edition variants)
// and every lookup fails silently (log line, mod idles) instead of asserting.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "version.lib")

namespace
{
	// ------------------------------------------------------------------ config
	struct Config
	{
		bool  enabled = true;
		float chance = 100.0f;         // % chance a qualifying kill triggers the killcam
		float cooldownSec = 15.0f;     // real seconds between killcams (counted from trigger)
		float durationSec = 2.0f;      // real seconds of slow motion
		float timeScale = 0.25f;
		bool  onHeadshot = true;
		bool  onOneShot = true;
		std::vector<int> headBones = { 0x4B5 }; // BONE_HEAD
		float bodyKillChance = 5.0f;   // % chance that an ordinary firearm kill (no headshot, not one shot) also gets a killcam
		float explosionKillChance = 4.0f; // % chance that an explosion kill gets a (wide, distant) killcam
		float expDistMin = 7.0f, expDistMax = 12.0f;
		float headshotRefill = 0.15f;  // Dead Eye energy (fraction of a full meter) restored per headshot kill
		float afterSlowSec = 0.5f;     // slow motion kept after the camera returns to the player (real seconds, 0 = off)
		float afterTimeScale = 0.0f;   // its time scale; 0 = the time scale of the killcam that just played
		float kcPlayerSpeed = 0.5f;    // player animation speed multiplier during the killcam and its slow tail (1 = off)
		bool  lockAim = true;          // keep your aim from changing while the killcam plays
		int   lockAimMethod = 1;       // bit 1 = SET_GAME_CAMERA_CONTROLS_ACTIVE, bit 2 = SET_PLAYER_CONTROL
		std::vector<int> sniperIds = { 16, 17 }; // SNIPERRIFLE, M40A1
		float sniperChance = 100.0f;   // % chance for kills made with a sniper weapon (replaces ChancePercent)
		float sniperCooldownSec = 7.0f; // cooldown used for sniper kills (replaces CooldownSec)
		float sniperMaxDist = 250.0f;  // max victim distance for sniper kills
		bool  vehicleKills = true;     // allow killcams for NPCs in cars/bikes (always from the front)
		float vehDistMin = 4.0f, vehDistMax = 6.5f;   // meters ahead of the vehicle
		float vehHeightMin = 0.8f, vehHeightMax = 2.0f; // camera height above the vehicle position
		// Shot variation (picked at random for every killcam)
		bool  varyTimeScale = true;
		float timeScaleMin = 0.10f, timeScaleMax = 0.40f;
		float radiusMin = 2.5f, radiusMax = 4.5f;
		float orbitSpeedMin = 6.0f, orbitSpeedMax = 22.0f; // deg/s, direction is random
		float dollyAmount = 0.30f;                          // radius change over the shot (fraction)
		float wStatic = 60, wOrbit = 30, wDolly = 10;       // movement weights
		float wEye = 45, wHigh = 30, wLow = 25;             // camera angle weights
		float highMin = 2.2f, highMax = 4.0f;               // height above victim for high angle
		float fov = 45.0f;
		float maxVictimDist = 60.0f;
		bool  logEnabled = true;
		// Dead Eye: hold a key to slow the world while the player stays (almost) at normal speed.
		bool  deadEye = true;
		int   deKey = 0xA4;            // VK_LMENU (left Alt, next to the Windows key)
		bool  deToggle = false;        // false = hold, true = press to toggle
		float deTimeScale = 0.25f;
		float dePlayerSpeed = 0.85f;   // player speed relative to normal time (1 = unaffected)
		float deMaxAnimSpeed = 4.0f;   // cap for the player's animation speed multiplier
		float deRampSec = 0.15f;       // real seconds to blend in/out
		float deMaxSec = 10.0f;        // seconds of Dead Eye on a full meter
		float deRechargeDelaySec = 1.0f; // after releasing, wait this long before recharging
		float deRechargeSec = 20.0f;   // seconds to refill an empty meter
		float deMinToStart = 0.20f;    // after running dry, the meter must refill to this fraction to start again
		bool  deHud = true;
		bool  deHudAlways = false;     // false = only show while using or recharging
		float deHudX = 0.058f, deHudY = 0.955f; // just below the radar, left edge aligned with it
		float deHudScale = 0.20f;
		int   deMethod = 1;            // 1 = SET_CHAR_ALL_ANIMS_SPEED, 2 = SET_CHAR_MOVE_ANIM_SPEED_MULTIPLIER, 0 = world only
		char  raycastPattern[256] = "";
		int   raycastOffset = 0;
		unsigned raycastFlags = 142;   // statics | buildings | vehicles | objects
	};

	Config cfg;
	char iniPath[MAX_PATH];
	char logPath[MAX_PATH];

	// --------------------------------------------------------------------- log
	void Log(const char* fmt, ...)
	{
		if (!cfg.logEnabled) return;
		FILE* f = fopen(logPath, "a");
		if (!f) return;
		SYSTEMTIME t;
		GetLocalTime(&t);
		fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		va_list va;
		va_start(va, fmt);
		vfprintf(f, fmt, va);
		va_end(va);
		fputc('\n', f);
		fclose(f);
	}

	// ------------------------------------------------------------------ ini
	float IniFloat(const char* key, float def)
	{
		char def_s[32], buf[64];
		snprintf(def_s, sizeof(def_s), "%g", def);
		GetPrivateProfileStringA("KillCam", key, def_s, buf, sizeof(buf), iniPath);
		return (float)atof(buf);
	}
	bool IniBool(const char* key, bool def)
	{
		return GetPrivateProfileIntA("KillCam", key, def ? 1 : 0, iniPath) != 0;
	}

	void LoadConfig()
	{
		cfg.enabled = IniBool("Enabled", cfg.enabled);
		cfg.chance = IniFloat("ChancePercent", cfg.chance);
		cfg.cooldownSec = IniFloat("CooldownSec", cfg.cooldownSec);
		cfg.durationSec = IniFloat("DurationSec", cfg.durationSec);
		cfg.timeScale = IniFloat("TimeScale", cfg.timeScale);
		cfg.onHeadshot = IniBool("TriggerOnHeadshot", cfg.onHeadshot);
		cfg.onOneShot = IniBool("TriggerOnOneShot", cfg.onOneShot);
		cfg.bodyKillChance = IniFloat("BodyKillChancePercent", cfg.bodyKillChance);
		cfg.vehicleKills = IniBool("VehicleKills", cfg.vehicleKills);
		cfg.afterSlowSec = IniFloat("AfterSlowSec", cfg.afterSlowSec);
		cfg.afterTimeScale = IniFloat("AfterTimeScale", cfg.afterTimeScale);
		cfg.kcPlayerSpeed = IniFloat("PlayerSpeedDuringKillcam", cfg.kcPlayerSpeed);
		if (cfg.kcPlayerSpeed < 0.05f) cfg.kcPlayerSpeed = 0.05f;
		if (cfg.kcPlayerSpeed > 1.0f) cfg.kcPlayerSpeed = 1.0f;
		if (cfg.afterSlowSec < 0.0f) cfg.afterSlowSec = 0.0f;
		if (cfg.afterTimeScale < 0.0f) cfg.afterTimeScale = 0.0f;
		if (cfg.afterTimeScale > 1.0f) cfg.afterTimeScale = 1.0f;
		cfg.lockAim = IniBool("LockAimDuringKillcam", cfg.lockAim);
		cfg.lockAimMethod = (int)GetPrivateProfileIntA("KillCam", "LockAimMethod", 1, iniPath);
		cfg.sniperChance = IniFloat("SniperChancePercent", cfg.sniperChance);
		cfg.sniperCooldownSec = IniFloat("SniperCooldownSec", cfg.sniperCooldownSec);
		if (cfg.sniperCooldownSec < 0.0f) cfg.sniperCooldownSec = 0.0f;
		cfg.sniperMaxDist = IniFloat("SniperMaxDistance", cfg.sniperMaxDist);
		{
			char sb[128];
			GetPrivateProfileStringA("KillCam", "SniperWeaponIds", "16,17", sb, sizeof(sb), iniPath);
			std::vector<int> ids;
			for (char* tok = strtok(sb, ", "); tok; tok = strtok(nullptr, ", ")) ids.push_back((int)strtol(tok, nullptr, 0));
			cfg.sniperIds = ids; // empty list = sniper rule off
		}
		cfg.explosionKillChance = IniFloat("ExplosionKillChancePercent", cfg.explosionKillChance);
		cfg.expDistMin = IniFloat("ExplosionDistMin", cfg.expDistMin);
		cfg.expDistMax = IniFloat("ExplosionDistMax", cfg.expDistMax);
		cfg.vehDistMin = IniFloat("VehicleDistMin", cfg.vehDistMin);
		cfg.vehDistMax = IniFloat("VehicleDistMax", cfg.vehDistMax);
		cfg.vehHeightMin = IniFloat("VehicleHeightMin", cfg.vehHeightMin);
		cfg.vehHeightMax = IniFloat("VehicleHeightMax", cfg.vehHeightMax);
		cfg.varyTimeScale = IniBool("VaryTimeScale", cfg.varyTimeScale);
		cfg.timeScaleMin = IniFloat("TimeScaleMin", cfg.timeScaleMin);
		cfg.timeScaleMax = IniFloat("TimeScaleMax", cfg.timeScaleMax);
		cfg.radiusMin = IniFloat("RadiusMin", cfg.radiusMin);
		cfg.radiusMax = IniFloat("RadiusMax", cfg.radiusMax);
		cfg.orbitSpeedMin = IniFloat("OrbitSpeedMin", cfg.orbitSpeedMin);
		cfg.orbitSpeedMax = IniFloat("OrbitSpeedMax", cfg.orbitSpeedMax);
		cfg.dollyAmount = IniFloat("DollyAmount", cfg.dollyAmount);
		cfg.wStatic = IniFloat("WeightStatic", cfg.wStatic);
		cfg.wOrbit = IniFloat("WeightOrbit", cfg.wOrbit);
		cfg.wDolly = IniFloat("WeightDolly", cfg.wDolly);
		cfg.wEye = IniFloat("WeightEyeLevel", cfg.wEye);
		cfg.wHigh = IniFloat("WeightHighAngle", cfg.wHigh);
		cfg.wLow = IniFloat("WeightLowAngle", cfg.wLow);
		cfg.highMin = IniFloat("HighAngleHeightMin", cfg.highMin);
		cfg.highMax = IniFloat("HighAngleHeightMax", cfg.highMax);
		cfg.fov = IniFloat("CamFov", cfg.fov);
		cfg.maxVictimDist = IniFloat("MaxVictimDistance", cfg.maxVictimDist);
		cfg.logEnabled = IniBool("Log", cfg.logEnabled);
		cfg.deadEye = GetPrivateProfileIntA("DeadEye", "Enabled", cfg.deadEye ? 1 : 0, iniPath) != 0;
		cfg.deKey = (int)GetPrivateProfileIntA("DeadEye", "Key", cfg.deKey, iniPath);
		cfg.deToggle = GetPrivateProfileIntA("DeadEye", "Toggle", cfg.deToggle ? 1 : 0, iniPath) != 0;
		cfg.deMethod = (int)GetPrivateProfileIntA("DeadEye", "PlayerSpeedMethod", cfg.deMethod, iniPath);
		{
			char b[64];
			auto f = [&](const char* k, float d) { char ds[32]; snprintf(ds, sizeof(ds), "%g", d); GetPrivateProfileStringA("DeadEye", k, ds, b, sizeof(b), iniPath); return (float)atof(b); };
			cfg.deTimeScale = f("TimeScale", cfg.deTimeScale);
			cfg.dePlayerSpeed = f("PlayerSpeed", cfg.dePlayerSpeed);
			cfg.deMaxAnimSpeed = f("MaxAnimSpeed", cfg.deMaxAnimSpeed);
			cfg.deRampSec = f("RampSec", cfg.deRampSec);
			cfg.deMaxSec = f("MaxSeconds", cfg.deMaxSec);
			cfg.deRechargeDelaySec = f("RechargeDelaySec", cfg.deRechargeDelaySec);
			cfg.deRechargeSec = f("RechargeSeconds", cfg.deRechargeSec);
			cfg.deMinToStart = f("MinToStart", cfg.deMinToStart);
			cfg.headshotRefill = f("HeadshotRefill", cfg.headshotRefill);
			cfg.deHudX = f("HudX", cfg.deHudX);
			cfg.deHudY = f("HudY", cfg.deHudY);
			cfg.deHudScale = f("HudScale", cfg.deHudScale);
		}
		cfg.deHud = GetPrivateProfileIntA("DeadEye", "Hud", cfg.deHud ? 1 : 0, iniPath) != 0;
		cfg.deHudAlways = GetPrivateProfileIntA("DeadEye", "HudAlways", cfg.deHudAlways ? 1 : 0, iniPath) != 0;
		{
		}
		if (cfg.deTimeScale < 0.05f) cfg.deTimeScale = 0.05f;
		if (cfg.deTimeScale > 1.0f) cfg.deTimeScale = 1.0f;
		if (cfg.dePlayerSpeed < 0.1f) cfg.dePlayerSpeed = 0.1f;
		if (cfg.deMaxAnimSpeed < 1.0f) cfg.deMaxAnimSpeed = 1.0f;
		if (cfg.deRampSec < 0.0f) cfg.deRampSec = 0.0f;
		if (cfg.deMaxSec < 0.5f) cfg.deMaxSec = 0.5f;
		if (cfg.deRechargeDelaySec < 0.0f) cfg.deRechargeDelaySec = 0.0f;
		if (cfg.deRechargeSec < 0.5f) cfg.deRechargeSec = 0.5f;
		if (cfg.deMinToStart < 0.0f) cfg.deMinToStart = 0.0f;
		if (cfg.deMinToStart > 1.0f) cfg.deMinToStart = 1.0f;

		char buf[128];
		GetPrivateProfileStringA("KillCam", "HeadBoneIds", "1205", buf, sizeof(buf), iniPath);
		std::vector<int> bones;
		for (char* tok = strtok(buf, ", "); tok; tok = strtok(nullptr, ", "))
			bones.push_back((int)strtol(tok, nullptr, 0));
		if (!bones.empty()) cfg.headBones = bones;

		GetPrivateProfileStringA("LineOfSight", "RaycastPattern", "", cfg.raycastPattern, sizeof(cfg.raycastPattern), iniPath);
		cfg.raycastOffset = GetPrivateProfileIntA("LineOfSight", "RaycastOffset", 0, iniPath);
		cfg.raycastFlags = (unsigned)GetPrivateProfileIntA("LineOfSight", "RaycastFlags", 142, iniPath);

		if (cfg.chance < 0.0f) cfg.chance = 0.0f;
		if (cfg.chance > 100.0f) cfg.chance = 100.0f;
		if (cfg.cooldownSec < 0.0f) cfg.cooldownSec = 0.0f;
		if (cfg.durationSec < 0.1f) cfg.durationSec = 0.1f;
		if (cfg.timeScale < 0.05f) cfg.timeScale = 0.05f;
		if (cfg.timeScale > 1.0f) cfg.timeScale = 1.0f;
		if (cfg.bodyKillChance < 0.0f) cfg.bodyKillChance = 0.0f;
		if (cfg.bodyKillChance > 100.0f) cfg.bodyKillChance = 100.0f;
		if (cfg.sniperChance < 0.0f) cfg.sniperChance = 0.0f;
		if (cfg.sniperChance > 100.0f) cfg.sniperChance = 100.0f;
		if (cfg.sniperMaxDist < cfg.maxVictimDist) cfg.sniperMaxDist = cfg.maxVictimDist;
		if (cfg.explosionKillChance < 0.0f) cfg.explosionKillChance = 0.0f;
		if (cfg.explosionKillChance > 100.0f) cfg.explosionKillChance = 100.0f;
		if (cfg.expDistMin < 3.0f) cfg.expDistMin = 3.0f;
		if (cfg.expDistMax < cfg.expDistMin) cfg.expDistMax = cfg.expDistMin;
		if (cfg.headshotRefill < 0.0f) cfg.headshotRefill = 0.0f;
		if (cfg.headshotRefill > 1.0f) cfg.headshotRefill = 1.0f;
		if (cfg.vehDistMin < 1.5f) cfg.vehDistMin = 1.5f;
		if (cfg.vehDistMax < cfg.vehDistMin) cfg.vehDistMax = cfg.vehDistMin;
		if (cfg.vehHeightMax < cfg.vehHeightMin) cfg.vehHeightMax = cfg.vehHeightMin;
		if (cfg.timeScaleMin < 0.05f) cfg.timeScaleMin = 0.05f;
		if (cfg.timeScaleMax < cfg.timeScaleMin) cfg.timeScaleMax = cfg.timeScaleMin;
		if (cfg.timeScaleMax > 1.0f) cfg.timeScaleMax = 1.0f;
		if (cfg.radiusMin < 0.8f) cfg.radiusMin = 0.8f;
		if (cfg.radiusMax < cfg.radiusMin) cfg.radiusMax = cfg.radiusMin;
		if (cfg.orbitSpeedMax < cfg.orbitSpeedMin) cfg.orbitSpeedMax = cfg.orbitSpeedMin;
		if (cfg.highMax < cfg.highMin) cfg.highMax = cfg.highMin;
	}

	// ------------------------------------------------------------ pattern scan
	struct Pattern
	{
		std::vector<uint8_t> bytes;
		std::vector<uint8_t> mask; // 1 = compare
		bool Parse(const char* s)
		{
			bytes.clear(); mask.clear();
			while (*s)
			{
				while (*s == ' ') s++;
				if (!*s) break;
				if (s[0] == '?')
				{
					bytes.push_back(0); mask.push_back(0);
					s += (s[1] == '?') ? 2 : 1;
				}
				else
				{
					char h[3] = { s[0], s[1], 0 };
					if (!isxdigit((unsigned char)h[0]) || !isxdigit((unsigned char)h[1])) return false;
					bytes.push_back((uint8_t)strtoul(h, nullptr, 16)); mask.push_back(1);
					s += 2;
				}
			}
			return !bytes.empty();
		}
	};

	// All matches of `pat` inside executable sections of the main module.
	std::vector<uint8_t*> ScanAll(const Pattern& pat)
	{
		std::vector<uint8_t*> out;
		auto base = (uint8_t*)GetModuleHandleA(nullptr);
		auto dos = (IMAGE_DOS_HEADER*)base;
		auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
		auto sec = IMAGE_FIRST_SECTION(nt);
		const size_t n = pat.bytes.size();
		for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++)
		{
			if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
			uint8_t* b = base + sec->VirtualAddress;
			size_t size = sec->Misc.VirtualSize;
			if (size < n) continue;
			for (size_t o = 0; o + n <= size; o++)
			{
				size_t k = 0;
				while (k < n && (!pat.mask[k] || b[o + k] == pat.bytes[k])) k++;
				if (k == n) out.push_back(b + o);
			}
		}
		return out;
	}

	bool quietFind = false; // no "not found" log lines while retrying

	// First match of the first pattern that matches at all; nullptr (and a log line) if none.
	uint8_t* Find(const char* name, std::initializer_list<const char*> patterns, size_t* countOut = nullptr)
	{
		for (const char* s : patterns)
		{
			Pattern p;
			if (!p.Parse(s)) continue;
			auto m = ScanAll(p);
			if (!m.empty())
			{
				if (countOut) *countOut = m.size();
				return m[0];
			}
		}
		if (!quietFind) Log("pattern not found: %s", name);
		return nullptr;
	}

	bool InImage(const void* ptr)
	{
		auto base = (uint8_t*)GetModuleHandleA(nullptr);
		auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
		return (const uint8_t*)ptr >= base && (const uint8_t*)ptr < base + nt->OptionalHeader.SizeOfImage;
	}

	// ------------------------------------------------------------ game access
	struct Game
	{
		void*   (__stdcall* getNative)(uint32_t) = nullptr; // preferred native lookup (as FusionFix on the Complete Edition)
		void*   (*findPlayerPed)(int) = nullptr;            // returns CPed*, fallback for the player handle
		uint32_t* nativeTableSize = nullptr;
		uint32_t** nativeTableVar = nullptr;   // address of the variable holding the table
		uint8_t*  userPause = nullptr;
		uint8_t*  scriptPause = nullptr;
		void***   pedPoolVar = nullptr;        // address of the variable holding the ped pool
		void**    currentThreadVar = nullptr;  // address of the variable holding the active scrThread*
		uint8_t*  frameCall = nullptr;         // E8 rel32 in the main game loop
	} game;

	struct fwPool
	{
		uint8_t* storage;
		uint8_t* flags;
		int32_t  size;
		int32_t  stride;
		int32_t  firstFree;
		int32_t  used;
	};

	int PedHandleFromPointer(void* ped)
	{
		fwPool* pool = game.pedPoolVar ? (fwPool*)*game.pedPoolVar : nullptr;
		if (!pool || !pool->storage || !pool->flags || pool->stride <= 0) return 0;
		ptrdiff_t off = (uint8_t*)ped - pool->storage;
		if (off < 0 || off % pool->stride) return 0;
		int i = (int)(off / pool->stride);
		if (i >= pool->size || (pool->flags[i] & 0x80)) return 0;
		return (int)pool->flags[i] + (i << 8);
	}

	// Same layout as scrNativeCallContext / NativeContext used by FusionFix.
	struct NativeCtx
	{
		void*    ret;
		uint32_t argc;
		void*    args;
		uint32_t dataCount;
		void*    orig[4];
		float    tmp[16];
		uint32_t stack[16];
		NativeCtx() { memset(this, 0, sizeof(*this)); ret = stack; args = stack; }
		template<typename T> void Push(T v)
		{
			static_assert(sizeof(T) <= 4, "native args are 4 bytes");
			uint32_t w = 0;
			memcpy(&w, &v, sizeof(T));
			if (argc < 16) stack[argc++] = w;
		}
	};
	typedef void(*NativeFn)(NativeCtx*);

	std::unordered_map<uint32_t, NativeFn> nativeCache;

	NativeFn LookupNative(uint32_t hash)
	{
		auto it = nativeCache.find(hash);
		if (it != nativeCache.end()) return it->second;

		NativeFn fn = nullptr;
		if (game.getNative && hash) fn = (NativeFn)game.getNative(hash);
		uint32_t* table = (!fn && game.nativeTableVar) ? *game.nativeTableVar : nullptr;
		uint32_t  size = game.nativeTableSize ? *game.nativeTableSize : 0;
		if (table && size && hash)
		{
			uint32_t idx = hash % size;
			uint32_t tmp = hash;
			uint32_t h = table[2 * idx];
			if (h != hash)
			{
				for (uint32_t guard = 0; h && guard < size; guard++)
				{
					tmp = (tmp >> 1) + 1;
					idx = (tmp + idx) % size;
					h = table[2 * idx];
					if (h == hash) break;
				}
			}
			if (h == hash) fn = (NativeFn)table[2 * idx + 1];
		}
		if (fn) nativeCache[hash] = fn; // only cache hits: the table fills while the game boots
		else
		{
			static std::vector<uint32_t> missLogged;
			bool seen = false;
			for (uint32_t h : missLogged) if (h == hash) { seen = true; break; }
			if (!seen) { missLogged.push_back(hash); Log("native 0x%08X NOT FOUND", hash); }
		}
		return fn;
	}

	template<typename... A>
	uint32_t Native(uint32_t hash, A... a)
	{
		NativeCtx c;
		(c.Push(a), ...);
		NativeFn fn = LookupNative(hash);
		if (fn) fn(&c);
		return c.stack[0];
	}

	// Native hashes (identical in IV-SDK and FusionFix tables).
	enum : uint32_t
	{
		N_GET_PLAYER_ID = 0x62E319C6,
		N_GET_PLAYER_CHAR = 0x511454A9,
		N_IS_CHAR_DEAD = 0x6A6B4F18,
		N_GET_CHAR_HEALTH = 0x4B6C2256,
		N_HAS_CHAR_BEEN_DAMAGED_BY_CHAR = 0x1DD624A0,
		N_IS_CHAR_IN_ANY_CAR = 0x71184DA3,
		N_IS_CHAR_ON_ANY_BIKE = 0x0FB44F54,
		N_IS_CHAR_IN_ANY_BOAT = 0x210A4F1D,
		N_IS_CHAR_IN_ANY_HELI = 0x0FC40275,
		N_GET_CHAR_LAST_DAMAGE_BONE = 0x767E5013,
		N_CLEAR_CHAR_LAST_DAMAGE_BONE = 0x1A013092,
		N_GET_CURRENT_CHAR_WEAPON = 0x5AB8289F,
		N_GET_CHAR_COORDINATES = 0x2B5C06E6,
		N_CREATE_CAM = 0x694A0DC1,
		N_SET_CAM_POS = 0x152F6314,
		N_POINT_CAM_AT_COORD = 0x4496175C,
		N_SET_CAM_FOV = 0x55D470C2,
		N_SET_CAM_ACTIVE = 0x43E42686,
		N_SET_CAM_PROPAGATE = 0x44414E60,
		N_ACTIVATE_SCRIPTED_CAMS = 0x3EBE11B9,
		N_DESTROY_CAM = 0x14334EEE,
		N_SET_TIME_SCALE = 0x24D467CC,
		N_IS_PAUSE_MENU_ACTIVE = 0x6C4568A7,
		N_GET_GROUND_Z_FOR_3D_COORD = 0x6D902EE3,
		N_SET_CHAR_ALL_ANIMS_SPEED = 0x5BDB7E2C,
		N_HAS_CHAR_BEEN_DAMAGED_BY_WEAPON = 0x6DB26E07,
		N_SET_GAME_CAMERA_CONTROLS_ACTIVE = 0x57952546,
		N_SET_PLAYER_CONTROL = 0x1A6203EA,
		N_IS_PLAYER_CONTROL_ON = 0x30CD2F1F,
		N_GET_CAR_CHAR_IS_USING = 0x1B067237,
		N_GET_CAR_COORDINATES = 0x2D432EAB,
		N_GET_CAR_FORWARD_X = 0x47A21100,
		N_GET_CAR_FORWARD_Y = 0x3BDB4496,
		N_DOES_VEHICLE_EXIST = 0x67A42263,
		N_DOES_CHAR_EXIST = 0x46531797,
		N_SET_TEXT_SCALE = 0x02C069E5,
		N_SET_TEXT_COLOUR = 0x19C967B5,
		N_SET_TEXT_FONT = 0x75363BB5,
		N_SET_TEXT_DROPSHADOW = 0x58F5023F,
		N_SET_TEXT_CENTRE = 0x204A6AA4,
		N_SET_TEXT_PROPORTIONAL = 0x15585A65,
		N_SET_TEXT_BACKGROUND = 0x768F5140,
		N_DISPLAY_TEXT_WITH_LITERAL_STRING = 0x661B239A,
		N_SET_CHAR_MOVE_ANIM_SPEED_MULTIPLIER = 0x5DC456DE,
	};

	bool  NBool(uint32_t r) { return (r & 0xFF) != 0; }
	int PedHandleFromPointer(void* ped);
	int   PlayerPed()
	{
		int ped = 0;
		int id = (int)Native(N_GET_PLAYER_ID);
		Native(N_GET_PLAYER_CHAR, id, &ped);
		static int logged = 0;
		if (!ped && game.findPlayerPed)
		{
			void* ptr = game.findPlayerPed(0);
			int viaPtr = ptr ? PedHandleFromPointer(ptr) : 0;
			if (logged < 3) { logged++; Log("player: native route gave 0 (playerId=%d), FindPlayerPed(0)=%p -> handle %d", id, ptr, viaPtr); }
			return viaPtr;
		}
		if (logged < 3) { logged++; Log("player: native route ok, playerId=%d handle=%d", id, ped); }
		return ped;
	}
	bool  IsDead(int ped) { return NBool(Native(N_IS_CHAR_DEAD, ped)); }
	void  Coords(int ped, float& x, float& y, float& z) { Native(N_GET_CHAR_COORDINATES, ped, &x, &y, &z); }

	// ------------------------------------------------------------------- timing
	// SET_TIME_SCALE also slows game time, so effect duration and cooldown use the
	// performance counter.
	double NowSec()
	{
		static LARGE_INTEGER freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		return (double)c.QuadPart / (double)freq.QuadPart;
	}

	std::mt19937 rng((unsigned)time(nullptr) ^ GetCurrentProcessId());
	float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

	// -------------------------------------------------------------- line of sight
	// Optional exact raycast: only used when [LineOfSight] RaycastPattern resolves to exactly
	// one address. Signature matches CWorld::ProcessLineOfSight as documented for 1.0.8.0
	// (cdecl, returns nonzero on hit); it is NOT verified for 1.2.0.59.
	struct Vec3 { float x, y, z; };
	typedef uint32_t(__cdecl* ProcessLineOfSightFn)(Vec3*, Vec3*, uint32_t*, void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
	ProcessLineOfSightFn processLos = nullptr;

	void ResolveRaycast()
	{
		if (!cfg.raycastPattern[0]) { Log("LOS: no RaycastPattern configured -> native fallback (terrain only, walls NOT detected)"); return; }
		Pattern p;
		if (!p.Parse(cfg.raycastPattern)) { Log("LOS: RaycastPattern is malformed -> native fallback"); return; }
		auto m = ScanAll(p);
		if (m.size() != 1) { Log("LOS: RaycastPattern matched %u times (need exactly 1) -> native fallback", (unsigned)m.size()); return; }
		processLos = (ProcessLineOfSightFn)(m[0] + cfg.raycastOffset);
		Log("LOS: raycast resolved at %p", (void*)processLos);
	}

	// true = path clear
	bool ClearLine(const Vec3& a, const Vec3& b)
	{
		if (processLos)
		{
			Vec3 from = a, to = b;
			uint32_t unk = 0;
			uint8_t results[0x60] = {};
			return processLos(&from, &to, &unk, results, cfg.raycastFlags, 1, 0, 2, 4) == 0;
		}
		// Fallback: sample the segment; a solid surface within 0.5 m above a sample point and
		// higher than the ray means terrain/floor/roof blocks it. Cannot see walls.
		const int steps = 8;
		for (int i = 1; i < steps; i++)
		{
			float t = (float)i / steps;
			float x = a.x + (b.x - a.x) * t, y = a.y + (b.y - a.y) * t, z = a.z + (b.z - a.z) * t;
			float gz = -1000.0f;
			Native(N_GET_GROUND_Z_FOR_3D_COORD, x, y, z + 0.5f, &gz);
			if (gz > z - 0.1f && gz < z + 0.6f) return false;
		}
		return true;
	}

	// Camera spot must see the target and not sit inside geometry.
	bool CameraSpotOk(const Vec3& cam, const Vec3& target)
	{
		if (!ClearLine(cam, target)) return false;
		if (processLos)
		{
			const float m = 0.35f;
			const float d[6][3] = { {m,0,0}, {-m,0,0}, {0,m,0}, {0,-m,0}, {0,0,m}, {0,0,-m} };
			for (auto& v : d)
				if (!ClearLine(cam, { cam.x + v[0], cam.y + v[1], cam.z + v[2] })) return false;
		}
		else
		{
			float gz = -1000.0f;
			Native(N_GET_GROUND_Z_FOR_3D_COORD, cam.x, cam.y, cam.z + 0.5f, &gz);
			if (cam.z - gz < 0.4f) return false; // camera on/below the ground
		}
		return true;
	}

	// ------------------------------------------------------------------ killcam
	struct DeadEye
	{
		bool   on = false;       // effect currently applied (scale != 1 or anim speed != 1)
		bool   toggled = false;
		bool   keyWasDown = false;
		float  cur = 1.0f;       // current world time scale
		double last = 0.0;
		float  animApplied = 1.0f;
		float  energy = 1.0f;    // 0..1
		bool   lockedOut = false; // ran dry; waits for energy >= deMinToStart
		double releasedAt = 0.0;  // when the effect last stopped (real time)
	} de;
	double tailUntil = 0.0; // slow-motion tail after a killcam ends (real time)
	float  tailScale = 0.25f; // time scale of that tail
	void SetPlayerAnimSpeed(int player, float v);

	struct Active
	{
		bool   on = false;
		int    cam = 0;
		double startTime = 0.0;
		double lastTick = 0.0;
		float  angle = 0.0f;
		float  radius = 0.0f;       // current radius
		float  baseRadius = 0.0f;
		float  height = 0.6f;       // camera height above the victim position
		float  targetH = 0.3f;      // look-at height above the victim position
		float  orbit = 0.0f;        // rad/s, signed; 0 = fixed
		float  dolly = 0.0f;        // fraction of baseRadius added over the shot (negative = push in)
		Vec3   victim = {};
		// Vehicle shot: camera stays in front of the vehicle and follows it.
		float  ts = 0.25f;          // time scale of this killcam
		bool   aimLocked = false;   // gameplay camera controls currently disabled by us
		bool   ctrlLocked = false;  // player control currently switched off by us
		bool   follow = false;
		int    car = 0, ped = 0;
		float  fwdX = 0, fwdY = 1;  // smoothed forward direction of the vehicle
		float  dist = 5.0f;
	} active;
	double lastTrigger = -1e9;

	Vec3 CamPos(float angle, float radius)
	{
		return { active.victim.x + cosf(angle) * radius, active.victim.y + sinf(angle) * radius,
		         active.victim.z + active.height };
	}

	void StopKillCam(const char* why)
	{
		if (!active.on) return;
		Native(N_ACTIVATE_SCRIPTED_CAMS, false, false);
		Native(N_SET_CAM_PROPAGATE, active.cam, false);
		Native(N_SET_CAM_ACTIVE, active.cam, false);
		Native(N_DESTROY_CAM, active.cam);
		if (cfg.afterSlowSec > 0.0f && !strcmp(why, "duration"))
		{
			// Camera is back on the player: stay in slow motion for a moment so there is time to aim.
			float ts = cfg.afterTimeScale > 0.0f ? cfg.afterTimeScale : active.ts;
			Native(N_SET_TIME_SCALE, ts);
			tailScale = ts;
			de.cur = ts; de.on = true; de.toggled = false;
			tailUntil = NowSec() + cfg.afterSlowSec;
		}
		else
		{
			Native(N_SET_TIME_SCALE, 1.0f);
			if (de.animApplied != 1.0f) SetPlayerAnimSpeed(PlayerPed(), 1.0f);
		}
		if (active.aimLocked) { Native(N_SET_GAME_CAMERA_CONTROLS_ACTIVE, true); active.aimLocked = false; }
		if (active.ctrlLocked)
		{
			int pid = (int)Native(N_GET_PLAYER_ID);
			Native(N_SET_PLAYER_CONTROL, pid, true);
			Log("aim lock released: player control on = %d", (int)NBool(Native(N_IS_PLAYER_CONTROL_ON, pid)));
			active.ctrlLocked = false;
		}
		active.on = false; active.follow = false;
		Log("killcam end (%s), %.2fs real", why, NowSec() - active.startTime);
	}

	float RandRange(float lo, float hi) { return lo + (hi - lo) * Rand01(); }

	// Weighted pick among n weights; falls back to index 0 if all are <= 0.
	int PickWeighted(const float* w, int n)
	{
		float total = 0;
		for (int i = 0; i < n; i++) if (w[i] > 0) total += w[i];
		if (total <= 0) return 0;
		float r = Rand01() * total;
		for (int i = 0; i < n; i++)
		{
			if (w[i] <= 0) continue;
			if (r < w[i]) return i;
			r -= w[i];
		}
		return n - 1;
	}

	enum { ANGLE_EYE, ANGLE_HIGH, ANGLE_LOW };
	enum { MOVE_STATIC, MOVE_ORBIT, MOVE_DOLLY };
	const char* angleName[] = { "eye", "high", "low" };
	const char* moveName[] = { "static", "orbit", "dolly" };

	void SetAngle(int angle)
	{
		switch (angle)
		{
		case ANGLE_HIGH: active.height = RandRange(cfg.highMin, cfg.highMax); active.targetH = 0.0f; break;
		case ANGLE_LOW:  active.height = -0.4f; active.targetH = 0.5f; break; // near the ground, looking slightly up
		default:         active.height = 0.6f;  active.targetH = 0.3f; break;
		}
	}

	bool FindSpot(float& outAngle, float& outRadius)
	{
		const Vec3 target = { active.victim.x, active.victim.y, active.victim.z + active.targetH };
		const float base = Rand01() * 6.2831853f;
		const float radii[3] = { active.baseRadius, active.baseRadius * 0.75f, active.baseRadius * 0.55f };
		for (float r : radii)
			for (int i = 0; i < 12; i++)
			{
				float a = base + i * (6.2831853f / 12.0f);
				if (CameraSpotOk(CamPos(a, r), target)) { outAngle = a; outRadius = r; return true; }
			}
		return false;
	}

	bool BeginCam(const Vec3& p, const Vec3& target, float ts)
	{
		int cam = 0;
		Native(N_CREATE_CAM, 14, &cam);
		if (!cam) { Log("skipped: CREATE_CAM returned 0"); return false; }
		active.cam = cam;
		Native(N_SET_CAM_POS, cam, p.x, p.y, p.z);
		Native(N_POINT_CAM_AT_COORD, cam, target.x, target.y, target.z);
		Native(N_SET_CAM_FOV, cam, cfg.fov);
		Native(N_SET_CAM_ACTIVE, cam, true);
		Native(N_SET_CAM_PROPAGATE, cam, true);
		Native(N_ACTIVATE_SCRIPTED_CAMS, true, true);
		Native(N_SET_TIME_SCALE, ts);
		active.ts = ts;
		if (cfg.lockAim)
		{
			int pid = (int)Native(N_GET_PLAYER_ID);
			bool before = NBool(Native(N_IS_PLAYER_CONTROL_ON, pid));
			if (cfg.lockAimMethod & 1) { Native(N_SET_GAME_CAMERA_CONTROLS_ACTIVE, false); active.aimLocked = true; }
			if ((cfg.lockAimMethod & 2) && before) { Native(N_SET_PLAYER_CONTROL, pid, false); active.ctrlLocked = true; }
			Log("aim lock (method %d): player control before=%d after=%d", cfg.lockAimMethod, (int)before, (int)NBool(Native(N_IS_PLAYER_CONTROL_ON, pid)));
		}
		active.on = true;
		active.startTime = active.lastTick = NowSec();
		return true;
	}

	bool VehicleForward(int car, float& fx, float& fy)
	{
		Native(N_GET_CAR_FORWARD_X, car, &fx);
		Native(N_GET_CAR_FORWARD_Y, car, &fy);
		float l = sqrtf(fx * fx + fy * fy);
		if (l < 0.1f) return false;
		fx /= l; fy /= l;
		return true;
	}

	// Camera in front of the vehicle, looking back at the victim. Never uses other angles.
	bool StartVehicleKillCam(int ped, int car, const Vec3& v)
	{
		Vec3 cp;
		Native(N_GET_CAR_COORDINATES, car, &cp.x, &cp.y, &cp.z);
		float fx = 0, fy = 0;
		if (!VehicleForward(car, fx, fy)) { Log("skipped: vehicle forward vector invalid"); return false; }

		active.victim = v;
		active.height = RandRange(cfg.vehHeightMin, cfg.vehHeightMax);
		active.targetH = 0.3f;
		const Vec3 target = { v.x, v.y, v.z + active.targetH };
		const float d0 = RandRange(cfg.vehDistMin, cfg.vehDistMax);
		float dist = 0;
		Vec3 p = {};
		for (float k : { 1.0f, 0.75f, 0.55f })
		{
			Vec3 c = { cp.x + fx * d0 * k, cp.y + fy * d0 * k, cp.z + active.height };
			if (CameraSpotOk(c, target)) { p = c; dist = d0 * k; break; }
		}
		if (dist == 0) { Log("skipped: no clear spot in front of the vehicle"); return false; }

		const float ts = cfg.varyTimeScale ? RandRange(cfg.timeScaleMin, cfg.timeScaleMax) : cfg.timeScale;
		if (!BeginCam(p, target, ts)) return false;
		active.follow = true; active.car = car; active.ped = ped;
		active.fwdX = fx; active.fwdY = fy; active.dist = dist;
		active.orbit = 0; active.dolly = 0;
		Log("killcam start: vehicle-front timescale %.2f height %.1f dist %.1f victim (%.1f %.1f %.1f)", ts, active.height, dist, v.x, v.y, v.z);
		return true;
	}

	void UpdateVehicleCam(float dt)
	{
		if (!NBool(Native(N_DOES_VEHICLE_EXIST, active.car))) return; // keep the last position
		Vec3 cp;
		Native(N_GET_CAR_COORDINATES, active.car, &cp.x, &cp.y, &cp.z);
		float fx = 0, fy = 0;
		if (VehicleForward(active.car, fx, fy))
		{
			float k = fminf(1.0f, dt * 6.0f); // smooth the heading so spins don't whip the camera
			active.fwdX += (fx - active.fwdX) * k;
			active.fwdY += (fy - active.fwdY) * k;
			float l = sqrtf(active.fwdX * active.fwdX + active.fwdY * active.fwdY);
			if (l > 0.05f) { active.fwdX /= l; active.fwdY /= l; }
		}
		Vec3 t = cp;
		if (NBool(Native(N_DOES_CHAR_EXIST, active.ped))) Native(N_GET_CHAR_COORDINATES, active.ped, &t.x, &t.y, &t.z);
		t.z += active.targetH;
		for (float k : { 1.0f, 0.7f, 0.5f })
		{
			Vec3 c = { cp.x + active.fwdX * active.dist * k, cp.y + active.fwdY * active.dist * k, cp.z + active.height };
			if (CameraSpotOk(c, t))
			{
				Native(N_SET_CAM_POS, active.cam, c.x, c.y, c.z);
				break;
			}
		}
		Native(N_POINT_CAM_AT_COORD, active.cam, t.x, t.y, t.z);
	}

	bool StartKillCam(const Vec3& v, bool wide = false)
	{
		active.victim = v;

		// Random shot: movement (mostly fixed) x camera angle.
		const float mw[3] = { cfg.wStatic, cfg.wOrbit, cfg.wDolly };
		const float aw[3] = { cfg.wEye, cfg.wHigh, wide ? 0.0f : cfg.wLow }; // no low angle for distant explosion shots
		int move = PickWeighted(mw, 3), angle = PickWeighted(aw, 3);

		active.baseRadius = wide ? RandRange(cfg.expDistMin, cfg.expDistMax) : RandRange(cfg.radiusMin, cfg.radiusMax);
		if (angle == ANGLE_HIGH && active.baseRadius < 3.0f) active.baseRadius = 3.0f;
		if (wide && angle == ANGLE_HIGH) active.baseRadius = fmaxf(active.baseRadius, 8.0f);
		active.orbit = 0; active.dolly = 0;
		if (move == MOVE_ORBIT)
			active.orbit = (Rand01() < 0.5f ? -1.0f : 1.0f) * RandRange(cfg.orbitSpeedMin, cfg.orbitSpeedMax) * 0.0174533f;
		else if (move == MOVE_DOLLY)
			active.dolly = (Rand01() < 0.5f ? -1.0f : 1.0f) * cfg.dollyAmount;

		SetAngle(angle);
		float fa = 0, fr = 0;
		if (!FindSpot(fa, fr) && angle != ANGLE_EYE)
		{
			Log("shot %s/%s blocked, retrying at eye level", moveName[move], angleName[angle]);
			angle = ANGLE_EYE;
			SetAngle(angle);
			if (!FindSpot(fa, fr))
			{
				Log("skipped: no camera position with clear line of sight around (%.1f %.1f %.1f)", v.x, v.y, v.z);
				return false;
			}
		}
		else if (fr == 0)
		{
			Log("skipped: no camera position with clear line of sight around (%.1f %.1f %.1f)", v.x, v.y, v.z);
			return false;
		}

		active.angle = fa;
		active.radius = fr;
		active.baseRadius = fr; // radius changes are relative to the spot that was accepted
		Vec3 p = CamPos(fa, fr);
		const Vec3 target = { v.x, v.y, v.z + active.targetH };
		const float ts = cfg.varyTimeScale ? RandRange(cfg.timeScaleMin, cfg.timeScaleMax) : cfg.timeScale;

		if (!BeginCam(p, target, ts)) return false;
		active.follow = false;

		Log("killcam start: %s/%s timescale %.2f height %.1f radius %.1f orbit %.0fdeg/s dolly %+.2f victim (%.1f %.1f %.1f)",
			moveName[move], angleName[angle], ts, active.height, fr, active.orbit * 57.29578f, active.dolly, v.x, v.y, v.z);
		return true;
	}

	void UpdateKillCam(int player)
	{
		const double now = NowSec();
		if (now - active.startTime >= cfg.durationSec) { StopKillCam("duration"); return; }
		if (IsDead(player)) { StopKillCam("player dead"); return; }
		if (NBool(Native(N_IS_PAUSE_MENU_ACTIVE))) { StopKillCam("pause menu"); return; }

		if (active.follow)
		{
			double fdt = now - active.lastTick;
			if (fdt > 0.1) fdt = 0.1;
			active.lastTick = now;
			UpdateVehicleCam((float)fdt);
		}
		else if (active.orbit != 0.0f || active.dolly != 0.0f)
		{
			double dt = now - active.lastTick;
			if (dt > 0.1) dt = 0.1;
			active.lastTick = now;
			float t = (float)((now - active.startTime) / cfg.durationSec);
			float na = active.angle + active.orbit * (float)dt;
			float nr = active.baseRadius * (1.0f + active.dolly * t);
			Vec3 p = CamPos(na, nr);
			Vec3 target = { active.victim.x, active.victim.y, active.victim.z + active.targetH };
			if (CameraSpotOk(p, target))
			{
				active.angle = na;
				active.radius = nr;
				Native(N_SET_CAM_POS, active.cam, p.x, p.y, p.z);
			}
		}
	}

	// Dead Eye state (logic further below); declared here because kills refill the meter.

	// ----------------------------------------------------------- kill detection
	struct PedState { int baseHealth; int lastHealth; bool counted; };
	std::unordered_map<int, PedState> peds;

	// eWeapon: 7 pistol, 8 unused, 9 deagle ... 17 M40A1.
	bool IsFirearm(int w) { return w >= 7 && w <= 17 && w != 8; }
	bool IsSniperWeapon(int w) { for (int x : cfg.sniperIds) if (x == w) return true; return false; }
	bool IsHeadBone(int b) { for (int x : cfg.headBones) if (x == b) return true; return false; }

	void EvaluateDeath(int ped, const PedState& st, int player)
	{
		if (!NBool(Native(N_HAS_CHAR_BEEN_DAMAGED_BY_CHAR, ped, player, false))) return;

		if (NBool(Native(N_IS_CHAR_IN_ANY_BOAT, ped)) || NBool(Native(N_IS_CHAR_IN_ANY_HELI, ped))) return;
		const bool inVehicle = NBool(Native(N_IS_CHAR_IN_ANY_CAR, ped)) || NBool(Native(N_IS_CHAR_ON_ANY_BIKE, ped));
		if (inVehicle && !cfg.vehicleKills) return;

		int bone = -1;
		bool haveBone = NBool(Native(N_GET_CHAR_LAST_DAMAGE_BONE, ped, &bone));
		bool headshot = haveBone && IsHeadBone(bone);

		int weapon = 0;
		Native(N_GET_CURRENT_CHAR_WEAPON, player, &weapon);
		bool oneShot = st.lastHealth >= st.baseHealth && IsFirearm(weapon);

		// Headshot kills refill the Dead Eye meter, whether or not a killcam follows.
		if (headshot && cfg.deadEye && cfg.headshotRefill > 0.0f)
		{
			de.energy = fminf(1.0f, de.energy + cfg.headshotRefill);
			Log("headshot: dead eye +%.0f%% -> %.0f%%", cfg.headshotRefill * 100.0f, de.energy * 100.0f);
		}

		// Explosion kill: damaged by an explosive weapon type.
		bool explosion = false;
		for (int w : { 4, 5, 6, 18, 51 }) // grenade, molotov, rocket, RPG, WEAPON_EXPLOSION
			if (NBool(Native(N_HAS_CHAR_BEEN_DAMAGED_BY_WEAPON, ped, w))) { explosion = true; break; }

		bool qualifies = (cfg.onHeadshot && headshot) || (cfg.onOneShot && oneShot);
		bool bodyRoll = false, explRoll = false;
		if (!qualifies && IsFirearm(weapon) && Rand01() * 100.0f < cfg.bodyKillChance) { qualifies = true; bodyRoll = true; }
		if (!qualifies && explosion && Rand01() * 100.0f < cfg.explosionKillChance) { qualifies = true; explRoll = true; }

		Log("player kill: ped %d bone %d(0x%X) weapon %d health %d/%d inVehicle=%d explosion=%d -> headshot=%d oneShot=%d bodyRoll=%d explRoll=%d",
			ped, bone, bone, weapon, st.lastHealth, st.baseHealth, (int)inVehicle, (int)explosion, (int)headshot, (int)oneShot, (int)bodyRoll, (int)explRoll);

		if (!qualifies) return;

		const double now = NowSec();
		if (active.on) { Log("skipped: killcam already active"); return; }
		const bool sniper = (headshot || oneShot) && IsSniperWeapon(weapon);
		const float cooldown = sniper ? cfg.sniperCooldownSec : cfg.cooldownSec;
		if (now - lastTrigger < cooldown) { Log("skipped: cooldown (%.1fs left%s)", cooldown - (now - lastTrigger), sniper ? ", sniper" : ""); return; }
		const float chance = sniper ? cfg.sniperChance : cfg.chance;
		if (Rand01() * 100.0f >= chance) { Log("skipped: chance roll (%.0f%%%s)", chance, sniper ? ", sniper" : ""); return; }

		Vec3 v, p;
		Coords(ped, v.x, v.y, v.z);
		Coords(player, p.x, p.y, p.z);
		float dx = v.x - p.x, dy = v.y - p.y, dz = v.z - p.z;
		if (sqrtf(dx * dx + dy * dy + dz * dz) > (sniper ? cfg.sniperMaxDist : cfg.maxVictimDist)) { Log("skipped: victim too far"); return; }

		bool started;
		if (inVehicle)
		{
			int car = 0;
			Native(N_GET_CAR_CHAR_IS_USING, ped, &car);
			if (!car) { Log("skipped: victim in vehicle but GET_CAR_CHAR_IS_USING gave 0"); return; }
			started = StartVehicleKillCam(ped, car, v);
		}
		else started = StartKillCam(v, explRoll);
		if (started) lastTrigger = now;
	}

	// ---------------------------------------------------------------- dead eye
	// Hold a key: SET_TIME_SCALE drops and the player's animations are sped up by
	// playerSpeed / timeScale, so the player moves/aims/reloads at ~playerSpeed of normal speed
	// while NPCs and physics run at timeScale.

	bool GameHasFocus()
	{
		DWORD pid = 0;
		HWND w = GetForegroundWindow();
		if (!w) return false;
		GetWindowThreadProcessId(w, &pid);
		return pid == GetCurrentProcessId();
	}

	void SetPlayerAnimSpeed(int player, float v)
	{
		if (cfg.deMethod == 1) Native(N_SET_CHAR_ALL_ANIMS_SPEED, player, v);
		else if (cfg.deMethod == 2) Native(N_SET_CHAR_MOVE_ANIM_SPEED_MULTIPLIER, player, v);
		de.animApplied = v;
	}

	void DeadEyeReset(int player, const char* why)
	{
		if (!de.on) return;
		if (de.animApplied != 1.0f) SetPlayerAnimSpeed(player, 1.0f);
		if (!active.on) Native(N_SET_TIME_SCALE, 1.0f); // the killcam owns the scale while it runs
		de.on = false; de.cur = 1.0f; de.toggled = false;
		de.releasedAt = NowSec();
		Log("dead eye off (%s), energy %.0f%%", why, de.energy * 100.0f);
	}

	// Text meter, e.g. "DEAD EYE [=========.........]". Must be drawn every frame.
	void DrawDeadEyeHud(bool usingNow)
	{
		if (!cfg.deHud) return;
		if (!usingNow && !cfg.deHudAlways && de.energy >= 0.999f) return;
		const int n = 12;
		int filled = (int)(de.energy * n + 0.5f);
		char bar[64];
		int k = 0;
		bar[k++] = '[';
		for (int i = 0; i < n; i++) bar[k++] = i < filled ? '=' : '.';
		bar[k++] = ']';
		bar[k] = 0;
		char line[96];
		snprintf(line, sizeof(line), "DEAD EYE %s", bar);

		unsigned r = 255, g = 255, b = 255; // ready
		if (usingNow) { r = 255; g = 200; b = 60; }
		else if (de.lockedOut) { r = 230; g = 60; b = 60; }

		Native(N_SET_TEXT_FONT, 0u);
		Native(N_SET_TEXT_SCALE, cfg.deHudScale, cfg.deHudScale * 1.3f);
		Native(N_SET_TEXT_COLOUR, r, g, b, 235u);
		Native(N_SET_TEXT_DROPSHADOW, true, 0u, 0u, 0u, 255u);
		Native(N_SET_TEXT_CENTRE, false); // left-aligned at HudX
		Native(N_SET_TEXT_PROPORTIONAL, false);
		Native(N_SET_TEXT_BACKGROUND, false);
		Native(N_DISPLAY_TEXT_WITH_LITERAL_STRING, cfg.deHudX, cfg.deHudY, "STRING", (const char*)line);
	}

	// suppressed = a killcam is running: the effect is off but the meter keeps recharging.
	void DeadEyeUpdate(int player, bool suppressed)
	{
		const double now = NowSec();
		const bool tail = !suppressed && now < tailUntil && cfg.afterSlowSec > 0.0f;
		if (!cfg.deadEye && !tail && !de.on) return;
		double dt = de.last > 0.0 ? now - de.last : 0.0;
		de.last = now;
		if (dt > 0.1) dt = 0.1;

		bool down = cfg.deadEye && !suppressed && GameHasFocus() && (GetAsyncKeyState(cfg.deKey) & 0x8000) != 0;
		bool want;
		if (cfg.deToggle)
		{
			if (down && !de.keyWasDown) de.toggled = !de.toggled;
			want = de.toggled;
		}
		else want = down;
		de.keyWasDown = down;

		const bool blocked = suppressed || IsDead(player) || NBool(Native(N_IS_PAUSE_MENU_ACTIVE));
		if (blocked) want = false;
		if (!cfg.deadEye) want = false;

		// Energy: drains while active, then recharges after a short delay.
		if (de.lockedOut && de.energy >= cfg.deMinToStart) { de.lockedOut = false; Log("dead eye ready again"); }
		if (de.lockedOut) want = false;
		if (want && de.energy <= 0.0f) want = false;

		if (want)
		{
			de.energy -= (float)(dt / cfg.deMaxSec);
			if (de.energy <= 0.0f)
			{
				de.energy = 0.0f; de.lockedOut = true; want = false;
				Log("dead eye ran dry");
			}
		}
		else if (now - de.releasedAt >= cfg.deRechargeDelaySec && de.energy < 1.0f)
		{
			de.energy += (float)(dt / cfg.deRechargeSec);
			if (de.energy > 1.0f) de.energy = 1.0f;
		}

		if (cfg.deadEye) DrawDeadEyeHud(want);

		const bool tailOnly = tail && !want && !blocked; // slow motion after a killcam, no key held, no energy used
		if (tailOnly) want = true;
		const float target = want ? (tailOnly ? tailScale : cfg.deTimeScale) : 1.0f;
		if (!want && !de.on) return;

		if (!de.on) { de.on = true; Log("dead eye on (timescale %.2f, player speed %.2f, method %d, energy %.0f%%)", cfg.deTimeScale, cfg.dePlayerSpeed, cfg.deMethod, de.energy * 100.0f); }

		// Blend the world scale toward the target (rate: full 1 -> deTimeScale span in deRampSec).
		float span = 1.0f - fminf(cfg.deTimeScale, tailOnly ? tailScale : cfg.deTimeScale);
		float step = cfg.deRampSec > 0.0f ? span * (float)(dt / cfg.deRampSec) : span;
		if (de.cur < target) de.cur = fminf(target, de.cur + step);
		else if (de.cur > target) de.cur = fmaxf(target, de.cur - step);

		if (!want && de.cur >= 0.999f) { DeadEyeReset(player, de.lockedOut ? "ran dry" : "released"); return; }
		if (!want) de.releasedAt = now; // keep the recharge delay counting from the end of the blend-out

		Native(N_SET_TIME_SCALE, de.cur);
		// Dead Eye boosts the player; the slow tail after a killcam keeps the player slow instead.
		float anim = cfg.deMethod ? (tailOnly ? cfg.kcPlayerSpeed : fminf(cfg.deMaxAnimSpeed, fmaxf(1.0f, cfg.dePlayerSpeed / de.cur))) : 1.0f;
		if (cfg.deMethod && fabsf(anim - de.animApplied) > 0.01f) SetPlayerAnimSpeed(player, anim);
	}

	// Diagnostics: every early exit is logged once per change of reason (not per frame).
	int      lastBail = 0;
	uint32_t statFrames = 0, statTicks = 0;
	int      statPeds = -1, statPlayer = -1;

	void Bail(int reason, const char* fmt, ...)
	{
		if (reason == lastBail) return;
		lastBail = reason;
		if (!cfg.logEnabled) return;
		char msg[256];
		va_list va;
		va_start(va, fmt);
		vsnprintf(msg, sizeof(msg), fmt, va);
		va_end(va);
		Log("bail: %s", msg);
	}
	void ClearBail() { if (lastBail) { lastBail = 0; Log("bail cleared: ticking normally"); } }

	void Tick()
	{
		if (!cfg.enabled) return;

		fwPool* pool = game.pedPoolVar ? (fwPool*)*game.pedPoolVar : nullptr;
		if (!pool) { Bail(10, "ped pool pointer is NULL (var=%p)", (void*)game.pedPoolVar); return; }
		if (!pool->storage || !pool->flags || pool->size <= 0 || pool->size > 4096 || pool->stride <= 0)
		{
			Bail(11, "ped pool invalid (storage=%p flags=%p count=%d itemSize=%d)", pool->storage, pool->flags, pool->size, pool->stride);
			return;
		}

		int player = PlayerPed();
		statPlayer = player;
		if (!player) { Bail(12, "player handle is 0"); peds.clear(); return; }
		ClearBail();
		statTicks++;

		if (active.on)
		{
			DeadEyeReset(player, "killcam started");
			if (cfg.deMethod && cfg.kcPlayerSpeed < 0.999f && fabsf(de.animApplied - cfg.kcPlayerSpeed) > 0.01f)
				SetPlayerAnimSpeed(player, cfg.kcPlayerSpeed); // player stays slow while the killcam camera plays
		}
		DeadEyeUpdate(player, active.on);

		if (active.on) UpdateKillCam(player);

		std::vector<int> seen;
		seen.reserve(64);
		for (int i = 0; i < pool->size; i++)
		{
			uint8_t fl = pool->flags[i];
			if (fl & 0x80) continue; // free slot
			int handle = (int)fl + (i << 8);
			if (handle == player) continue;
			seen.push_back(handle);

			auto it = peds.find(handle);
			if (!IsDead(handle))
			{
				int h = 0;
				Native(N_GET_CHAR_HEALTH, handle, &h);
				if (it == peds.end()) peds[handle] = { h, h, false };
				else it->second.lastHealth = h;
			}
			else if (it != peds.end() && !it->second.counted)
			{
				it->second.counted = true;
				EvaluateDeath(handle, it->second, player);
			}
		}

		statPeds = (int)seen.size();
		for (auto it = peds.begin(); it != peds.end();)
		{
			bool present = false;
			for (int h : seen) if (h == it->first) { present = true; break; }
			it = present ? std::next(it) : peds.erase(it);
		}
	}

	// ------------------------------------------------------------ hook / glue
	alignas(16) uint8_t dummyThread[1024];
	typedef void(*GameProcessFn)();
	GameProcessFn origGameProcess = nullptr;
	int faults = 0;

	void SafeTick()
	{
		__try { Tick(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { faults++; }
	}

	void SafeStop()
	{
		__try { StopKillCam("fault"); }
		__except (EXCEPTION_EXECUTE_HANDLER) { active.on = false; }
	}

	void Heartbeat()
	{
		static double next = 0.0;
		double now = NowSec();
		if (now < next) return;
		next = now + 5.0;
		Log("heartbeat: frames=%u ticks=%u peds=%d player=%d userPause=%d scriptPause=%d killcam=%d faults=%d",
			statFrames, statTicks, statPeds, statPlayer,
			game.userPause ? *game.userPause : -1, game.scriptPause ? *game.scriptPause : -1,
			(int)active.on, faults);
	}

	void GameProcessHook()
	{
		statFrames++;
		Heartbeat();
		if (!cfg.enabled || faults >= 3)
		{
			origGameProcess();
			return;
		}

		bool run = false;
		if (!game.userPause) Bail(1, "user pause flag pointer is NULL");
		else if (*game.userPause) Bail(2, "paused: userPause=%d scriptPause=%d", *game.userPause, game.scriptPause ? *game.scriptPause : -1);
		else if (game.scriptPause && *game.scriptPause) Bail(3, "paused: userPause=0 scriptPause=%d", *game.scriptPause);
		else run = true;

		if (run)
		{
			// Natives expect an active script thread; give them a zeroed dummy for this call.
			void* saved = game.currentThreadVar ? *game.currentThreadVar : nullptr;
			if (game.currentThreadVar) { memset(dummyThread, 0, sizeof(dummyThread)); *game.currentThreadVar = dummyThread; }
			int before = faults;
			SafeTick();
			if (game.currentThreadVar) *game.currentThreadVar = saved;
			if (faults != before)
			{
				Log("exception inside tick (%d/3)%s", faults, faults >= 3 ? " - mod disabled" : "");
				SafeStop(); // best effort: restore camera and time scale
			}
		}
		origGameProcess();
	}

	bool ResolveGame()
	{
		size_t n = 0;
		uint8_t* p;

		// Native lookup. FusionFix on the Complete Edition uses the 2nd match of this function prologue.
		{
			Pattern gp;
			gp.Parse("56 8B 35 ? ? ? ? 85 F6 75 06");
			auto m = ScanAll(gp);
			if (m.size() >= 2) game.getNative = (void*(__stdcall*)(uint32_t))m[1];
			else if (m.size() == 1) game.getNative = (void*(__stdcall*)(uint32_t))m[0];
			Log("native lookup fn: %u match(es), using %p", (unsigned)m.size(), (void*)game.getNative);
		}
		// Table walk, only a fallback. May point at the wrong table on this build.
		if ((p = Find("native table size", { "8B 35 ? ? ? ? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10" }, &n)))
			game.nativeTableSize = *(uint32_t**)(p + 2);
		if ((p = Find("native table", { "8B 1D ? ? ? ? 8B CF 8B 04 D3 3B C7 74 19 8D 64 24 00 85 C0" }, &n)))
			game.nativeTableVar = *(uint32_t***)(p + 2);
		if (!game.getNative && !(game.nativeTableSize && game.nativeTableVar)) return false;

		if ((p = Find("FindPlayerPed", { "8B 44 24 04 85 C0 75 18 A1" }, &n)))
			game.findPlayerPed = (void*(*)(int))p;

		if (!(p = Find("pause flags", { "0F B6 0D ? ? ? ? 0F B6 C0 0B C1" }, &n))) return false;
		{
			char dump[128] = {};
			for (int i = 0; i < 24; i++) snprintf(dump + i * 3, 4, "%02X ", p[i]);
			Log("pause flags match at %p (%u hit(s)); bytes: %s", (void*)p, (unsigned)n, dump);
		}
		game.userPause = *(uint8_t**)(p + 3);
		// Second flag is read at +15, OUTSIDE the 12 matched bytes (FusionFix offset). Only trust it
		// if it points inside the exe image; otherwise gate on the first flag alone.
		uint8_t* second = *(uint8_t**)(p + 15);
		if (!InImage(game.userPause)) { Log("userPause pointer %p outside image", (void*)game.userPause); return false; }
		if (InImage(second)) game.scriptPause = second;
		else { game.scriptPause = nullptr; Log("scriptPause candidate %p outside image -> ignored, gating on userPause only", (void*)second); }
		Log("pause flags: userPause=%p(%d) scriptPause=%p(%d)", (void*)game.userPause, *game.userPause,
			(void*)game.scriptPause, game.scriptPause ? *game.scriptPause : -1);

		if (!(p = Find("ped pool", { "8B 3D ? ? ? ? 8B F1 8B 47" }, &n))) return false;
		game.pedPoolVar = *(void****)(p + 2);

		if (!(p = Find("current script thread", { "8B 35 ? ? ? ? 8B 47 ? FF 77" }, &n))) return false;
		game.currentThreadVar = *(void***)(p + 2);

		if (!(p = Find("game process call", { "E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9" }, &n))) return false;
		if (n != 1) Log("note: game process pattern matched %u times, using the first", (unsigned)n);
		game.frameCall = p;
		return true;
	}

	// Redirect the E8 rel32 at `call` to GameProcessHook, keeping whatever it pointed to
	// (the original function, or another mod's hook installed earlier).
	bool InstallFrameHook()
	{
		uint8_t* c = game.frameCall;
		if (c[0] != 0xE8) return false;
		int32_t rel = *(int32_t*)(c + 1);
		origGameProcess = (GameProcessFn)(c + 5 + rel);
		DWORD old;
		if (!VirtualProtect(c, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
		*(int32_t*)(c + 1) = (int32_t)((uint8_t*)GameProcessHook - (c + 5));
		VirtualProtect(c, 5, old, &old);
		FlushInstructionCache(GetCurrentProcess(), c, 5);
		return true;
	}

	void LogExeVersion()
	{
		char exe[MAX_PATH];
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		DWORD h = 0, sz = GetFileVersionInfoSizeA(exe, &h);
		if (!sz) { Log("exe version: unknown"); return; }
		std::vector<char> buf(sz);
		VS_FIXEDFILEINFO* fi = nullptr;
		UINT len = 0;
		if (GetFileVersionInfoA(exe, 0, sz, buf.data()) && VerQueryValueA(buf.data(), "\\", (LPVOID*)&fi, &len) && fi)
			Log("exe version: %u.%u.%u.%u", HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS),
				HIWORD(fi->dwFileVersionLS), LOWORD(fi->dwFileVersionLS));
	}

	DWORD WINAPI InitThread(LPVOID)
	{
		LoadConfig();
		Log("---- GTAIV KillCam loaded ----");
		LogExeVersion();
		Log("config: chance=%.0f%% cooldown=%.1fs duration=%.1fs timescale=%.2f headshot=%d oneShot=%d",
			cfg.chance, cfg.cooldownSec, cfg.durationSec, cfg.timeScaleMax, (int)cfg.onHeadshot, (int)cfg.onOneShot);
		if (!cfg.enabled) return 0;

		// The exe may still be unpacking/initializing when the loader runs us: retry for a while.
		bool ok = false;
		for (int attempt = 0; attempt < 120 && !ok; attempt++)
		{
			if (attempt) Sleep(500);
			quietFind = attempt < 119;
			ok = ResolveGame();
			if (!ok && attempt == 0) Log("patterns incomplete, retrying for up to 60s");
		}
		if (!ok) { Log("gave up: game patterns not found (unsupported build or changed code) - mod idle"); return 0; }

		ResolveRaycast();
		if (!InstallFrameHook()) { Log("gave up: could not install frame hook"); return 0; }
		Log("ready: hooked game process at %p", (void*)game.frameCall);
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
		char path[MAX_PATH];
		GetModuleFileNameA(module, path, MAX_PATH);
		char* slash = strrchr(path, '\\');
		if (slash) *(slash + 1) = '\0';
		snprintf(iniPath, MAX_PATH, "%sGTAIV_KillCam.ini", path);
		snprintf(logPath, MAX_PATH, "%sGTAIV_KillCam.log", path);
		HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
		if (t) CloseHandle(t);
	}
	return TRUE;
}
