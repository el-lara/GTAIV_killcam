// GTAIV KillCam - slow-motion scripted camera on headshot / one-shot on-foot kills.
// Built on IV-SDK (Zolika1351). x86 only. See README.md for build and version notes.
#include "IVSDK.cpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <unordered_map>
#include <vector>

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
		std::vector<int> headBones = { 1301 }; // value returned by GET_CHAR_LAST_DAMAGE_BONE for the head
		float camRadius = 3.0f;
		float camHeight = 0.6f;        // camera height above the victim position
		float targetHeight = 0.3f;     // look-at height above the victim position
		float fov = 45.0f;
		float orbitDegPerSec = 25.0f;  // 0 = static camera
		float maxVictimDist = 60.0f;   // ignore kills farther than this from the player
		bool  logEnabled = true;
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

	// ------------------------------------------------------------------ config IO
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
		GetPrivateProfileStringA("KillCam", "HeadBoneIds", "1301", buf, sizeof(buf), iniPath);
		std::vector<int> bones;
		for (char* tok = strtok(buf, ", "); tok; tok = strtok(nullptr, ", "))
			bones.push_back(atoi(tok));
		if (!bones.empty()) cfg.headBones = bones;

		if (cfg.chance < 0.0f) cfg.chance = 0.0f;
		if (cfg.chance > 100.0f) cfg.chance = 100.0f;
		if (cfg.cooldownSec < 0.0f) cfg.cooldownSec = 0.0f;
		if (cfg.durationSec < 0.1f) cfg.durationSec = 0.1f;
		if (cfg.timeScale < 0.05f) cfg.timeScale = 0.05f;
		if (cfg.timeScale > 1.0f) cfg.timeScale = 1.0f;
		if (cfg.camRadius < 0.8f) cfg.camRadius = 0.8f;
	}

	// ------------------------------------------------------------------- timing
	// Real (unscaled) time: SET_TIME_SCALE also scales game time, so the effect
	// duration and cooldown are measured with the performance counter.
	double NowSec()
	{
		static LARGE_INTEGER freq = []() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
		LARGE_INTEGER c;
		QueryPerformanceCounter(&c);
		return (double)c.QuadPart / (double)freq.QuadPart;
	}

	std::mt19937 rng((unsigned)time(nullptr) ^ GetCurrentProcessId());
	float Rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

	// ------------------------------------------------------------------- state
	struct PedState
	{
		int  baseHealth;   // health when first seen alive
		int  lastHealth;   // health on the previous frame
		bool counted;      // death already evaluated
	};
	std::unordered_map<int, PedState> peds;

	struct Active
	{
		bool   on = false;
		int    cam = 0;
		double startTime = 0.0;
		float  angle = 0.0f;       // current orbit angle (rad)
		double lastTick = 0.0;
		float  radius = 0.0f;
		float  vx = 0, vy = 0, vz = 0; // victim position
	} active;

	double lastTrigger = -1e9;

	// -------------------------------------------------------------- LOS helpers
	// true = clear. Peds are not in the flags, so the victim never blocks the ray.
	bool ClearLine(float ax, float ay, float az, float bx, float by, float bz)
	{
		CVector from = { ax, ay, az };
		CVector to = { bx, by, bz };
		tLineOfSightResults res;
		uint32_t unk = 0;
		const uint32_t flags = STATIC_COLLISION | BUILDINGS | VEHICLES | OBJECTS; // 142
		bool hit = CWorld::ProcessLineOfSight(&from, &to, &unk, &res, flags, 1, 0, 2, 4) != 0;
		return !hit;
	}

	// Camera spot must see the target and not sit inside geometry: check the ray
	// victim->camera plus short rays from the camera along the axes.
	bool CameraSpotOk(float cx, float cy, float cz, float tx, float ty, float tz)
	{
		if (!ClearLine(cx, cy, cz, tx, ty, tz)) return false;
		const float m = 0.35f;
		const float dirs[6][3] = { {m,0,0}, {-m,0,0}, {0,m,0}, {0,-m,0}, {0,0,m}, {0,0,-m} };
		for (auto& d : dirs)
			if (!ClearLine(cx, cy, cz, cx + d[0], cy + d[1], cz + d[2])) return false;
		return true;
	}

	// ------------------------------------------------------------------ camera
	void PlaceCam(float angle, float radius)
	{
		Scripting::SET_CAM_POS(active.cam,
			active.vx + cosf(angle) * radius,
			active.vy + sinf(angle) * radius,
			active.vz + cfg.camHeight);
	}

	void StopKillCam(const char* why)
	{
		if (!active.on) return;
		Scripting::ACTIVATE_SCRIPTED_CAMS(false, false);
		Scripting::SET_CAM_PROPAGATE(active.cam, false);
		Scripting::SET_CAM_ACTIVE(active.cam, false);
		Scripting::DESTROY_CAM(active.cam);
		Scripting::SET_TIME_SCALE(1.0f);
		active.on = false;
		Log("killcam end (%s), %.2fs real", why, NowSec() - active.startTime);
	}

	bool StartKillCam(float vx, float vy, float vz)
	{
		const float tz = vz + cfg.targetHeight;
		const float base = Rand01() * 6.2831853f;
		const float radii[3] = { cfg.camRadius, cfg.camRadius * 0.7f, cfg.camRadius * 0.5f };

		float foundAngle = 0, foundRadius = 0;
		bool found = false;
		for (float r : radii)
		{
			for (int i = 0; i < 12 && !found; i++)
			{
				float a = base + i * (6.2831853f / 12.0f);
				float cx = vx + cosf(a) * r, cy = vy + sinf(a) * r, cz = vz + cfg.camHeight;
				if (CameraSpotOk(cx, cy, cz, vx, vy, tz)) { found = true; foundAngle = a; foundRadius = r; }
			}
			if (found) break;
		}
		if (!found)
		{
			Log("skipped: no camera position with clear line of sight around (%.1f %.1f %.1f)", vx, vy, vz);
			return false;
		}

		active.vx = vx; active.vy = vy; active.vz = vz;
		active.angle = foundAngle;
		active.radius = foundRadius;

		Scripting::CREATE_CAM(14, &active.cam);
		PlaceCam(active.angle, active.radius);
		Scripting::POINT_CAM_AT_COORD(active.cam, vx, vy, tz);
		Scripting::SET_CAM_FOV(active.cam, cfg.fov);
		Scripting::SET_CAM_ACTIVE(active.cam, true);
		Scripting::SET_CAM_PROPAGATE(active.cam, true);
		Scripting::ACTIVATE_SCRIPTED_CAMS(true, true);
		Scripting::SET_TIME_SCALE(cfg.timeScale);

		active.on = true;
		active.startTime = active.lastTick = NowSec();
		Log("killcam start: victim (%.1f %.1f %.1f) angle %.0fdeg radius %.1f", vx, vy, vz,
			foundAngle * 57.29578f, foundRadius);
		return true;
	}

	// Orbit slowly while the effect runs; stop rotating if the next spot is blocked.
	void UpdateKillCam(Scripting::Ped player)
	{
		const double now = NowSec();
		if (now - active.startTime >= cfg.durationSec) { StopKillCam("duration"); return; }
		if (Scripting::IS_CHAR_DEAD(player)) { StopKillCam("player dead"); return; }
		if (Scripting::IS_PAUSE_MENU_ACTIVE()) { StopKillCam("pause menu"); return; }

		if (cfg.orbitDegPerSec != 0.0f)
		{
			double dt = now - active.lastTick;
			if (dt > 0.1) dt = 0.1;
			active.lastTick = now;
			float na = active.angle + cfg.orbitDegPerSec * 0.0174533f * (float)dt;
			float cx = active.vx + cosf(na) * active.radius;
			float cy = active.vy + sinf(na) * active.radius;
			float cz = active.vz + cfg.camHeight;
			if (CameraSpotOk(cx, cy, cz, active.vx, active.vy, active.vz + cfg.targetHeight))
			{
				active.angle = na;
				PlaceCam(active.angle, active.radius);
			}
		}
	}

	// ----------------------------------------------------------- kill detection
	bool IsFirearm(int w) { return w >= Scripting::WEAPON_PISTOL && w <= Scripting::WEAPON_M40A1 && w != Scripting::WEAPON_UNUSED0; }

	bool IsHeadBone(int bone)
	{
		for (int b : cfg.headBones) if (b == bone) return true;
		return false;
	}

	// Called once, on the frame a ped is first seen dead.
	void EvaluateDeath(int handle, const PedState& st, Scripting::Ped player)
	{
		// Only deaths the player caused.
		if (!Scripting::HAS_CHAR_BEEN_DAMAGED_BY_CHAR(handle, player, false)) return;

		// Victim on foot.
		if (Scripting::IS_CHAR_IN_ANY_CAR(handle) || Scripting::IS_CHAR_ON_ANY_BIKE(handle) ||
			Scripting::IS_CHAR_IN_ANY_BOAT(handle) || Scripting::IS_CHAR_IN_ANY_HELI(handle)) return;

		int bone = -1;
		bool haveBone = NativeInvoke::Invoke<bool>(NATIVE_GET_CHAR_LAST_DAMAGE_BONE, handle, &bone);
		bool headshot = haveBone && IsHeadBone(bone);

		unsigned int weapon = 0;
		Scripting::GET_CURRENT_CHAR_WEAPON(player, &weapon);
		// One shot: health never dropped before the lethal frame, and a firearm was used.
		bool oneShot = st.lastHealth >= st.baseHealth && IsFirearm((int)weapon);

		Log("player kill: ped %d bone %d weapon %u health %d/%d -> headshot=%d oneShot=%d",
			handle, bone, weapon, st.lastHealth, st.baseHealth, (int)headshot, (int)oneShot);

		bool qualifies = (cfg.onHeadshot && headshot) || (cfg.onOneShot && oneShot);
		if (!qualifies) return;

		const double now = NowSec();
		if (active.on) { Log("skipped: killcam already active"); return; }
		if (now - lastTrigger < cfg.cooldownSec) { Log("skipped: cooldown (%.1fs left)", cfg.cooldownSec - (now - lastTrigger)); return; }
		if (Rand01() * 100.0f >= cfg.chance) { Log("skipped: chance roll"); return; }

		float x, y, z;
		Scripting::GET_CHAR_COORDINATES(handle, &x, &y, &z);
		float px, py, pz;
		Scripting::GET_CHAR_COORDINATES(player, &px, &py, &pz);
		float dx = x - px, dy = y - py, dz = z - pz;
		if (sqrtf(dx * dx + dy * dy + dz * dz) > cfg.maxVictimDist) { Log("skipped: victim too far"); return; }

		if (StartKillCam(x, y, z)) lastTrigger = now;
	}

	// ---------------------------------------------------------------- main loop
	void Tick()
	{
		if (!cfg.enabled) return;

		CPed* playerPed = FindPlayerPed();
		if (!playerPed || !CPools::ms_pPedPool) return;
		Scripting::Ped player = CPools::ms_pPedPool->GetIndex(playerPed);

		if (active.on) UpdateKillCam(player);

		// Scan the ped pool: remember health while alive, react on the death frame.
		std::vector<int> seen;
		for (CPed* ped : CPools::ms_pPedPool)
		{
			if (!ped || ped == playerPed) continue;
			int handle = CPools::ms_pPedPool->GetIndex(ped);
			seen.push_back(handle);

			auto it = peds.find(handle);
			if (!Scripting::IS_CHAR_DEAD(handle))
			{
				unsigned int h = 0;
				Scripting::GET_CHAR_HEALTH(handle, &h);
				if (it == peds.end()) peds[handle] = { (int)h, (int)h, false };
				else it->second.lastHealth = (int)h;
			}
			else if (it != peds.end() && !it->second.counted)
			{
				it->second.counted = true;
				EvaluateDeath(handle, it->second, player);
			}
		}

		// Drop entries for peds no longer in the pool.
		for (auto it = peds.begin(); it != peds.end();)
		{
			bool present = false;
			for (int h : seen) if (h == it->first) { present = true; break; }
			it = present ? std::next(it) : peds.erase(it);
		}
	}
}

// Ran by the SDK after it initializes (only on supported game versions).
void plugin::gameStartupEvent()
{
	char dllPath[MAX_PATH];
	GetModuleFileNameA(plugin::GetCurrentModule(), dllPath, MAX_PATH);
	char* slash = strrchr(dllPath, '\\');
	if (slash) *(slash + 1) = '\0';
	snprintf(iniPath, MAX_PATH, "%sGTAIV_KillCam.ini", dllPath);
	snprintf(logPath, MAX_PATH, "%sGTAIV_KillCam.log", dllPath);

	LoadConfig();
	Log("---- GTAIV KillCam loaded (game version id %d) ----", (int)plugin::gameVer);
	Log("config: chance=%.0f%% cooldown=%.1fs duration=%.1fs timescale=%.2f headshot=%d oneShot=%d bones=%d",
		cfg.chance, cfg.cooldownSec, cfg.durationSec, cfg.timeScale, (int)cfg.onHeadshot, (int)cfg.onOneShot,
		(int)cfg.headBones.size());

	plugin::processScriptsEvent::Add(Tick);
}
