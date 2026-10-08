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
		}
		if (cfg.deTimeScale < 0.05f) cfg.deTimeScale = 0.05f;
		if (cfg.deTimeScale > 1.0f) cfg.deTimeScale = 1.0f;
		if (cfg.dePlayerSpeed < 0.1f) cfg.dePlayerSpeed = 0.1f;
		if (cfg.deMaxAnimSpeed < 1.0f) cfg.deMaxAnimSpeed = 1.0f;
		if (cfg.deRampSec < 0.0f) cfg.deRampSec = 0.0f;

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
		Native(N_SET_TIME_SCALE, 1.0f);
		active.on = false;
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

	bool StartKillCam(const Vec3& v)
	{
		active.victim = v;

		// Random shot: movement (mostly fixed) x camera angle.
		const float mw[3] = { cfg.wStatic, cfg.wOrbit, cfg.wDolly };
		const float aw[3] = { cfg.wEye, cfg.wHigh, cfg.wLow };
		int move = PickWeighted(mw, 3), angle = PickWeighted(aw, 3);

		active.baseRadius = RandRange(cfg.radiusMin, cfg.radiusMax);
		if (angle == ANGLE_HIGH && active.baseRadius < 3.0f) active.baseRadius = 3.0f;
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

		active.on = true;
		active.startTime = active.lastTick = NowSec();
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

		if (active.orbit != 0.0f || active.dolly != 0.0f)
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

	// ----------------------------------------------------------- kill detection
	struct PedState { int baseHealth; int lastHealth; bool counted; };
	std::unordered_map<int, PedState> peds;

	// eWeapon: 7 pistol, 8 unused, 9 deagle ... 17 M40A1.
	bool IsFirearm(int w) { return w >= 7 && w <= 17 && w != 8; }
	bool IsHeadBone(int b) { for (int x : cfg.headBones) if (x == b) return true; return false; }

	void EvaluateDeath(int ped, const PedState& st, int player)
	{
		if (!NBool(Native(N_HAS_CHAR_BEEN_DAMAGED_BY_CHAR, ped, player, false))) return;

		if (NBool(Native(N_IS_CHAR_IN_ANY_CAR, ped)) || NBool(Native(N_IS_CHAR_ON_ANY_BIKE, ped)) ||
			NBool(Native(N_IS_CHAR_IN_ANY_BOAT, ped)) || NBool(Native(N_IS_CHAR_IN_ANY_HELI, ped))) return;

		int bone = -1;
		bool haveBone = NBool(Native(N_GET_CHAR_LAST_DAMAGE_BONE, ped, &bone));
		bool headshot = haveBone && IsHeadBone(bone);

		int weapon = 0;
		Native(N_GET_CURRENT_CHAR_WEAPON, player, &weapon);
		bool oneShot = st.lastHealth >= st.baseHealth && IsFirearm(weapon);

		Log("player kill: ped %d bone %d(0x%X) weapon %d health %d/%d -> headshot=%d oneShot=%d",
			ped, bone, bone, weapon, st.lastHealth, st.baseHealth, (int)headshot, (int)oneShot);

		if (!((cfg.onHeadshot && headshot) || (cfg.onOneShot && oneShot))) return;

		const double now = NowSec();
		if (active.on) { Log("skipped: killcam already active"); return; }
		if (now - lastTrigger < cfg.cooldownSec) { Log("skipped: cooldown (%.1fs left)", cfg.cooldownSec - (now - lastTrigger)); return; }
		if (Rand01() * 100.0f >= cfg.chance) { Log("skipped: chance roll"); return; }

		Vec3 v, p;
		Coords(ped, v.x, v.y, v.z);
		Coords(player, p.x, p.y, p.z);
		float dx = v.x - p.x, dy = v.y - p.y, dz = v.z - p.z;
		if (sqrtf(dx * dx + dy * dy + dz * dz) > cfg.maxVictimDist) { Log("skipped: victim too far"); return; }

		if (StartKillCam(v)) lastTrigger = now;
	}

	// ---------------------------------------------------------------- dead eye
	// Hold a key: SET_TIME_SCALE drops and the player's animations are sped up by
	// playerSpeed / timeScale, so the player moves/aims/reloads at ~playerSpeed of normal speed
	// while NPCs and physics run at timeScale.
	struct DeadEye
	{
		bool   on = false;       // effect currently applied (scale != 1 or anim speed != 1)
		bool   toggled = false;
		bool   keyWasDown = false;
		float  cur = 1.0f;       // current world time scale
		double last = 0.0;
		float  animApplied = 1.0f;
	} de;

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
		Log("dead eye off (%s)", why);
	}

	void DeadEyeUpdate(int player)
	{
		if (!cfg.deadEye) return;
		const double now = NowSec();
		double dt = de.last > 0.0 ? now - de.last : 0.0;
		de.last = now;
		if (dt > 0.1) dt = 0.1;

		bool down = GameHasFocus() && (GetAsyncKeyState(cfg.deKey) & 0x8000) != 0;
		bool want;
		if (cfg.deToggle)
		{
			if (down && !de.keyWasDown) de.toggled = !de.toggled;
			want = de.toggled;
		}
		else want = down;
		de.keyWasDown = down;

		if (IsDead(player) || NBool(Native(N_IS_PAUSE_MENU_ACTIVE))) want = false;

		const float target = want ? cfg.deTimeScale : 1.0f;
		if (!want && !de.on) return;

		if (!de.on) { de.on = true; Log("dead eye on (timescale %.2f, player speed %.2f, method %d)", cfg.deTimeScale, cfg.dePlayerSpeed, cfg.deMethod); }

		// Blend the world scale toward the target (rate: full 1 -> deTimeScale span in deRampSec).
		float span = 1.0f - cfg.deTimeScale;
		float step = cfg.deRampSec > 0.0f ? span * (float)(dt / cfg.deRampSec) : span;
		if (de.cur < target) de.cur = fminf(target, de.cur + step);
		else if (de.cur > target) de.cur = fmaxf(target, de.cur - step);

		if (!want && de.cur >= 0.999f) { DeadEyeReset(player, "released"); return; }

		Native(N_SET_TIME_SCALE, de.cur);
		float anim = cfg.deMethod ? fminf(cfg.deMaxAnimSpeed, fmaxf(1.0f, cfg.dePlayerSpeed / de.cur)) : 1.0f;
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

		if (active.on) DeadEyeReset(player, "killcam started"); else DeadEyeUpdate(player);

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
