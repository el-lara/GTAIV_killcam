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
		float camRadius = 3.0f;
		float camHeight = 0.6f;        // camera height above the victim position
		float targetHeight = 0.3f;     // look-at height above the victim position
		float fov = 45.0f;
		float orbitDegPerSec = 25.0f;  // 0 = static camera
		float maxVictimDist = 60.0f;
		bool  logEnabled = true;
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
		cfg.camRadius = IniFloat("CamRadius", cfg.camRadius);
		cfg.camHeight = IniFloat("CamHeight", cfg.camHeight);
		cfg.targetHeight = IniFloat("TargetHeight", cfg.targetHeight);
		cfg.fov = IniFloat("CamFov", cfg.fov);
		cfg.orbitDegPerSec = IniFloat("OrbitDegPerSec", cfg.orbitDegPerSec);
		cfg.maxVictimDist = IniFloat("MaxVictimDistance", cfg.maxVictimDist);
		cfg.logEnabled = IniBool("Log", cfg.logEnabled);

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
		if (cfg.camRadius < 0.8f) cfg.camRadius = 0.8f;
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

	// ------------------------------------------------------------ game access
	struct Game
	{
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
		uint32_t* table = game.nativeTableVar ? *game.nativeTableVar : nullptr;
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
	};

	bool  NBool(uint32_t r) { return (r & 0xFF) != 0; }
	int   PlayerPed()
	{
		int ped = 0;
		Native(N_GET_PLAYER_CHAR, (int)Native(N_GET_PLAYER_ID), &ped);
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
		float  radius = 0.0f;
		Vec3   victim = {};
	} active;
	double lastTrigger = -1e9;

	Vec3 CamPos(float angle, float radius)
	{
		return { active.victim.x + cosf(angle) * radius, active.victim.y + sinf(angle) * radius,
		         active.victim.z + cfg.camHeight };
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

	bool StartKillCam(const Vec3& v)
	{
		const Vec3 target = { v.x, v.y, v.z + cfg.targetHeight };
		active.victim = v;
		const float base = Rand01() * 6.2831853f;
		const float radii[3] = { cfg.camRadius, cfg.camRadius * 0.7f, cfg.camRadius * 0.5f };

		bool found = false;
		float fa = 0, fr = 0;
		for (float r : radii)
		{
			for (int i = 0; i < 12 && !found; i++)
			{
				float a = base + i * (6.2831853f / 12.0f);
				if (CameraSpotOk(CamPos(a, r), target)) { found = true; fa = a; fr = r; }
			}
			if (found) break;
		}
		if (!found)
		{
			Log("skipped: no camera position with clear line of sight around (%.1f %.1f %.1f)", v.x, v.y, v.z);
			return false;
		}

		active.angle = fa;
		active.radius = fr;
		Vec3 p = CamPos(fa, fr);

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
		Native(N_SET_TIME_SCALE, cfg.timeScale);

		active.on = true;
		active.startTime = active.lastTick = NowSec();
		Log("killcam start: victim (%.1f %.1f %.1f) angle %.0fdeg radius %.1f", v.x, v.y, v.z,
			fa * 57.29578f, fr);
		return true;
	}

	void UpdateKillCam(int player)
	{
		const double now = NowSec();
		if (now - active.startTime >= cfg.durationSec) { StopKillCam("duration"); return; }
		if (IsDead(player)) { StopKillCam("player dead"); return; }
		if (NBool(Native(N_IS_PAUSE_MENU_ACTIVE))) { StopKillCam("pause menu"); return; }

		if (cfg.orbitDegPerSec != 0.0f)
		{
			double dt = now - active.lastTick;
			if (dt > 0.1) dt = 0.1;
			active.lastTick = now;
			float na = active.angle + cfg.orbitDegPerSec * 0.0174533f * (float)dt;
			Vec3 p = CamPos(na, active.radius);
			Vec3 target = { active.victim.x, active.victim.y, active.victim.z + cfg.targetHeight };
			if (CameraSpotOk(p, target))
			{
				active.angle = na;
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

	void Tick()
	{
		if (!cfg.enabled) return;

		fwPool* pool = game.pedPoolVar ? (fwPool*)*game.pedPoolVar : nullptr;
		if (!pool || !pool->storage || !pool->flags || pool->size <= 0 || pool->size > 4096 || pool->stride <= 0) return;

		int player = PlayerPed();
		if (!player) { peds.clear(); return; }

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

	void GameProcessHook()
	{
		if (cfg.enabled && faults < 3 && game.userPause && game.scriptPause && !*game.userPause && !*game.scriptPause)
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

		if (!(p = Find("native table size", { "8B 35 ? ? ? ? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10" }, &n))) return false;
		game.nativeTableSize = *(uint32_t**)(p + 2);

		if (!(p = Find("native table", { "8B 1D ? ? ? ? 8B CF 8B 04 D3 3B C7 74 19 8D 64 24 00 85 C0" }, &n))) return false;
		game.nativeTableVar = *(uint32_t***)(p + 2);

		if (!(p = Find("pause flags", { "0F B6 0D ? ? ? ? 0F B6 C0 0B C1" }, &n))) return false;
		game.userPause = *(uint8_t**)(p + 3);
		game.scriptPause = *(uint8_t**)(p + 15);

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
			cfg.chance, cfg.cooldownSec, cfg.durationSec, cfg.timeScale, (int)cfg.onHeadshot, (int)cfg.onOneShot);
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
