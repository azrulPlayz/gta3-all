// GTA III (Android 1.8, ARM32, libR1.so) - Semua mod dalam satu .so
// Modul: kamera, saksi kejahatan, ragdoll probe, alat (cheat, cuaca, waktu, teleport via file perintah).
// Kepadatan dan jarak pandang sengaja TIDAK ada di sini (mod terpisah).
// Semua simbol diverifikasi dari libR1.so milik user. Tiap modul bisa dimatikan di bawah ini.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mod/amlmod.h>
#include <mod/logger.h>

MYMOD(net.kamu.gta3all, GTA3 All In One, 1.0, Kamu)

// ================= SAKLAR MODUL =================
static const bool ON_CAMERA  = true;
static const bool ON_WITNESS = true;
static const bool ON_RAGDOLL_PROBE = true;   // pengintai saja, tidak mengubah game
static const bool ON_TOOLS   = true;   // cheat, kunci cuaca/jam, teleport lewat file GTA3All.txt

// ================= KAMERA =================
static const float FOV_MULT      = 1.15f;  // 1.0 = normal, lebih besar = lebih lebar
static const bool  USE_MOUSE_CAM = false;  // eksperimen, belum tentu merespon sentuhan

// ================= SAKSI =================
static const float    WITNESS_RADIUS = 22.0f;
static const int      REACT_CHANCE   = 70;     // %
static const int      FLEE_MS        = 7000;
static const uint32_t CRIME_WINDOW   = 400;    // ms
static const bool     LOG_CRIME      = true;
// =================================================

static void* gLib = nullptr;
#define SYM(name) ((void*)aml->GetSym(gLib, name))

struct CVec  { float x, y, z; };
struct CVec2 { float x, y; };

// ---------------------------------------------------------------- KAMERA
namespace camera {
    static uint8_t* mouse3rd = nullptr;
    static float (*fovOrig)(void*) = nullptr;
    static float fovHook(void* self) {
        float fov = fovOrig(self);
        if (fov >= 35.0f && fov <= 90.0f) fov *= FOV_MULT;   // FOV bawaan sekitar 45
        return fov;
    }
    static void (*ccOrig)(void*) = nullptr;
    static void ccHook(void* self) { if (mouse3rd) *mouse3rd = 1; ccOrig(self); }
    static void init() {
        void* fov = SYM("_ZN7CCamera10FindCamFOVEv");
        logger->Info("camera: FindCamFOV=%p", fov);
        if (fov) aml->Hook(fov, (void*)fovHook, (void**)&fovOrig);
        if (USE_MOUSE_CAM) {
            mouse3rd = (uint8_t*)SYM("_ZN7CCamera20m_bUseMouse3rdPersonE");
            void* cc = SYM("_ZN7CCamera10CamControlEv");
            logger->Info("camera: mouse3rd=%p CamControl=%p", mouse3rd, cc);
            if (cc && mouse3rd) aml->Hook(cc, (void*)ccHook, (void**)&ccOrig);
        }
    }
}

// ---------------------------------------------------------------- SAKSI
namespace witness {
    static void (*getCentre)(void*, CVec*) = nullptr;
    static bool (*los)(const CVec&, const CVec&, bool,bool,bool,bool,bool,bool,bool) = nullptr;
    static void (*setFlee)(void*, const CVec2*, int) = nullptr;
    static void (*playerCoors)(CVec*) = nullptr;
    static uint32_t* timeMs = nullptr;

    static CVec crimePos; static uint32_t crimeTime = 0, crimeSerial = 0; static bool hasCrime = false;
    struct Reacted { void* ped; uint32_t serial; };
    static Reacted reacted[96]; static int reactedNext = 0;

    static void note(int type, const CVec* pos) {
        if (!pos || !timeMs) return;
        crimePos = *pos; crimeTime = *timeMs; crimeSerial++; hasCrime = true;
        if (LOG_CRIME) logger->Info("[crime] type=%d pos=(%.1f,%.1f,%.1f) serial=%u",
                                    type, pos->x, pos->y, pos->z, crimeSerial);
    }
    static bool already(void* p) { for (auto& r : reacted) if (r.ped == p && r.serial == crimeSerial) return true; return false; }
    static void mark(void* p) { reacted[reactedNext] = { p, crimeSerial }; reactedNext = (reactedNext + 1) % 96; }

    static void (*rcOrig)(void*, int, const CVec*, uint32_t, bool) = nullptr;
    static void rcHook(void* s, int t, const CVec* p, uint32_t id, bool dc) { note(t, p); rcOrig(s, t, p, id, dc); }
    static void (*rcnOrig)(void*, int, const CVec*, bool) = nullptr;
    static void rcnHook(void* s, int t, const CVec* p, bool dc) { note(t, p); rcnOrig(s, t, p, dc); }

    static void (*pcOrig)(void*) = nullptr;
    static void pcHook(void* ped) {
        pcOrig(ped);
        if (!hasCrime || !timeMs) return;
        if (*timeMs - crimeTime > CRIME_WINDOW) return;
        if (already(ped)) return;
        if (!getCentre || !los || !setFlee) return;

        CVec p; getCentre(ped, &p);
        float dx = p.x - crimePos.x, dy = p.y - crimePos.y, dz = p.z - crimePos.z;
        float d2 = dx*dx + dy*dy + dz*dz;
        if (d2 > WITNESS_RADIUS * WITNESS_RADIUS) return;

        if (playerCoors) {   // jangan buat pemain sendiri kabur
            CVec pl; playerCoors(&pl);
            float px = p.x - pl.x, py = p.y - pl.y;
            if (px*px + py*py < 0.25f) { mark(ped); return; }
        }
        mark(ped);
        if ((rand() % 100) >= REACT_CHANCE) return;

        CVec a = { p.x, p.y, p.z + 0.6f }, b = { crimePos.x, crimePos.y, crimePos.z + 0.6f };
        if (!los(a, b, true, false, false, false, false, true, false)) return;

        CVec2 from = { crimePos.x, crimePos.y };
        setFlee(ped, &from, FLEE_MS);
        if (LOG_CRIME) logger->Info("[witness] ped=%p dist=%.1f -> kabur", ped, sqrtf(d2));
    }
    static void init() {
        getCentre   = (void(*)(void*, CVec*)) SYM("_ZN7CEntity14GetBoundCentreER7CVector");
        los         = (bool(*)(const CVec&, const CVec&, bool,bool,bool,bool,bool,bool,bool))
                      SYM("_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb");
        setFlee     = (void(*)(void*, const CVec2*, int)) SYM("_ZN4CPed7SetFleeERK9CVector2Di");
        playerCoors = (void(*)(CVec*)) SYM("_Z15FindPlayerCoorsv");
        timeMs      = (uint32_t*) SYM("_ZN6CTimer22m_snTimeInMillisecondsE");
        void* rc  = SYM("_ZN7CWanted13RegisterCrimeE10eCrimeTypeRK7CVectorjb");
        void* rcn = SYM("_ZN7CWanted14ReportCrimeNowE10eCrimeTypeRK7CVectorb");
        void* pc  = SYM("_ZN4CPed14ProcessControlEv");
        logger->Info("witness: centre=%p los=%p flee=%p coors=%p time=%p rc=%p rcn=%p pc=%p",
                     getCentre, los, setFlee, playerCoors, timeMs, rc, rcn, pc);
        if (rc)  aml->Hook(rc,  (void*)rcHook,  (void**)&rcOrig);
        if (rcn) aml->Hook(rcn, (void*)rcnHook, (void**)&rcnOrig);
        if (pc)  aml->Hook(pc,  (void*)pcHook,  (void**)&pcOrig);
    }
}

// ---------------------------------------------------------------- RAGDOLL PROBE
namespace ragdoll {
    static const int MAX_PROBES = 4, SCAN_BYTES = 0x140;
    struct NodeInfo { int nodeID; int nodeIndex; int flags; void* pFrame; };
    struct Hier { int flags; int numNodes; void* mat; void* matUnaligned; NodeInfo* info; void* parentFrame; };
    static void* (*firstAtomic)(void*) = nullptr;
    static void* (*skinHier)(const void*) = nullptr;
    static void* seen[MAX_PROBES]; static int seenN = 0;

    static bool readable(const void* p, size_t n) {
        uintptr_t a = (uintptr_t)p; if (a < 0x10000) return false;
        long pg = sysconf(_SC_PAGESIZE);
        uintptr_t st = a & ~(uintptr_t)(pg - 1); size_t len = (a + n) - st;
        unsigned char vec[4]; if (len > (size_t)pg * 4) return false;
        return mincore((void*)st, len, vec) == 0;
    }
    static void probe(void* ped) {
        for (int i = 0; i < seenN; i++) if (seen[i] == ped) return;
        if (seenN >= MAX_PROBES) return;
        seen[seenN++] = ped;
        logger->Info("[probe] ped=%p", ped);
        for (int off = 0; off < SCAN_BYTES; off += 4) {
            void* p = *(void**)((uint8_t*)ped + off);
            if (!readable(p, 16)) continue;
            if (*(uint8_t*)p != 2 || *((uint8_t*)p + 1) != 0) continue;   // rpCLUMP
            void* at = firstAtomic ? firstAtomic(p) : nullptr; if (!at) continue;
            Hier* h = skinHier ? (Hier*)skinHier(at) : nullptr;
            if (!h || !readable(h, sizeof(*h))) continue;
            logger->Info("[probe]  clump offset=0x%x clump=%p hier=%p numNodes=%d", off, p, h, h->numNodes);
            if (h->numNodes > 0 && h->numNodes < 64 && readable(h->info, h->numNodes * sizeof(NodeInfo)))
                for (int n = 0; n < h->numNodes; n++)
                    logger->Info("[probe]   node %d: id=%d index=%d flags=%d",
                                 n, h->info[n].nodeID, h->info[n].nodeIndex, h->info[n].flags);
        }
    }
    static void (*prOrig)(void*) = nullptr;
    static void prHook(void* ped) { prOrig(ped); if (seenN < MAX_PROBES) probe(ped); }
    static void init() {
        firstAtomic = (void*(*)(void*))       SYM("_Z14GetFirstAtomicP7RpClump");
        skinHier    = (void*(*)(const void*)) SYM("_Z29RpSkinAtomicGetHAnimHierarchyPK8RpAtomic");
        void* pr    = SYM("_ZN4CPed9PreRenderEv");
        logger->Info("ragdoll probe: atomic=%p hier=%p preRender=%p", firstAtomic, skinHier, pr);
        if (pr) aml->Hook(pr, (void*)prHook, (void**)&prOrig);
    }
}

// ---------------------------------------------------------------- ALAT (pengganti menu CLEO)
// Tidak ada menu. Kamu mengontrolnya lewat file teks GTA3All.txt di folder files/ game.
// Buka file itu pakai editor teks di HP (game tetap jalan di latar), simpan, dan perintah dijalankan
// maksimal 1 detik kemudian. Baris "do=" dijalankan sekali lalu dihapus otomatis.
namespace tools {
    struct Cheat { const char* name; const char* sym; void (*fn)(); };
    static Cheat cheats[] = {
        {"weapon","_Z11WeaponCheatv",0},{"money","_Z10MoneyCheatv",0},{"health","_Z11HealthCheatv",0},
        {"armour","_Z11ArmourCheatv",0},{"wantedup","_Z18WantedLevelUpCheatv",0},
        {"wanteddown","_Z20WantedLevelDownCheatv",0},{"tank","_Z9TankCheatv",0},
        {"changeplayer","_Z17ChangePlayerCheatv",0},{"mayhem","_Z11MayhemCheatv",0},
        {"attackall","_Z27EverybodyAttacksPlayerCheatv",0},{"weapons4all","_Z18WeaponsForAllCheatv",0},
        {"fasttime","_Z13FastTimeCheatv",0},{"slowtime","_Z13SlowTimeCheatv",0},
        {"sunny","_Z17SunnyWeatherCheatv",0},{"cloudy","_Z18CloudyWeatherCheatv",0},
        {"rainy","_Z17RainyWeatherCheatv",0},{"foggy","_Z17FoggyWeatherCheatv",0},
        {"fastweather","_Z16FastWeatherCheatv",0},{"wheels","_Z21OnlyRenderWheelsCheatv",0},
        {"bangbang","_Z25ChittyChittyBangBangCheatv",0},{"grip","_Z15StrongGripCheatv",0},
        {"limbs","_Z15NastyLimbsCheatv",0},
    };
    static void* (*findPlayerPed)() = nullptr;
    static void  (*findCoors)(CVec*) = nullptr;
    static void  (*pedTeleport)(void*, float, float, float) = nullptr;   // CPed::Teleport(CVector) -> 3 float di r1..r3
    static void  (*forceWeather)(short) = nullptr;
    static void  (*setClock)(uint8_t, uint8_t) = nullptr;
    static uint8_t* clockHours = nullptr;
    static int   lockTime = -1, lockWeather = -1, appliedWeather = -2;
    static bool  haveSaved = false; static CVec saved;
    static char  path[512];
    static struct timespec lastPoll;

    static const char* DEFAULT_FILE =
        "# GTA3All - file perintah. Ubah, simpan, dan game membacanya dalam 1 detik.\n"
        "# lock_time: 0-23 mengunci jam, -1 mati\n"
        "lock_time=-1\n"
        "# lock_weather: 0 cerah, 1 mendung, 2 hujan, 3 kabut, -1 mati\n"
        "lock_weather=-1\n"
        "# Perintah sekali jalan (baris dihapus otomatis setelah dijalankan). Contoh:\n"
        "# do=money\n"
        "# Nama cheat: weapon money health armour wantedup wanteddown tank changeplayer mayhem\n"
        "#   attackall weapons4all fasttime slowtime sunny cloudy rainy foggy fastweather\n"
        "#   wheels bangbang grip limbs\n"
        "# do=savepos   (simpan posisi sekarang)\n"
        "# do=loadpos   (teleport ke posisi tersimpan)\n"
        "# do=tp=x,y,z  (teleport ke koordinat, contoh: do=tp=1000,-300,15)\n";

    static void runCheat(const char* n) {
        for (auto& c : cheats) if (!strcmp(c.name, n)) {
            if (c.fn) { c.fn(); logger->Info("[tools] cheat %s", n); }
            else logger->Error("[tools] cheat %s: simbol tidak ketemu", n);
            return;
        }
        logger->Error("[tools] perintah tidak dikenal: %s", n);
    }
    static void teleportTo(float x, float y, float z) {
        void* p = findPlayerPed ? findPlayerPed() : nullptr;
        if (!p || !pedTeleport) { logger->Error("[tools] teleport gagal (pemain/fungsi tidak ada)"); return; }
        pedTeleport(p, x, y, z);
        logger->Info("[tools] teleport -> %.1f %.1f %.1f", x, y, z);
    }
    static bool runDo(const char* cmd) {
        if (!findPlayerPed || !findPlayerPed()) return false;   // tunggu sampai pemain ada
        if (!strcmp(cmd, "savepos")) {
            if (findCoors) { findCoors(&saved); haveSaved = true; logger->Info("[tools] posisi disimpan %.1f %.1f %.1f", saved.x, saved.y, saved.z); }
        } else if (!strcmp(cmd, "loadpos")) {
            if (haveSaved) teleportTo(saved.x, saved.y, saved.z + 0.5f); else logger->Error("[tools] belum ada posisi tersimpan");
        } else if (!strncmp(cmd, "tp=", 3)) {
            float x, y, z; if (sscanf(cmd + 3, "%f,%f,%f", &x, &y, &z) == 3) teleportTo(x, y, z);
            else logger->Error("[tools] format tp salah, pakai: do=tp=x,y,z");
        } else runCheat(cmd);
        return true;
    }
    static void poll() {
        FILE* f = fopen(path, "r");
        if (!f) { f = fopen(path, "w"); if (f) { fputs(DEFAULT_FILE, f); fclose(f); } return; }
        char line[256]; static char keep[8192]; keep[0] = 0; bool consumed = false;
        while (fgets(line, sizeof line, f)) {
            char t[256]; strncpy(t, line, sizeof t - 1); t[sizeof t - 1] = 0;
            for (char* e = t + strlen(t); e > t && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
            if (!strncmp(t, "lock_time=", 10))         lockTime = atoi(t + 10);
            else if (!strncmp(t, "lock_weather=", 13)) lockWeather = atoi(t + 13);
            if (!strncmp(t, "do=", 3)) {
                if (runDo(t + 3)) { consumed = true; continue; }   // sukses: hapus baris
            }
            strncat(keep, line, sizeof keep - strlen(keep) - 1);
        }
        fclose(f);
        if (consumed) { f = fopen(path, "w"); if (f) { fputs(keep, f); fclose(f); } }
    }
    static void applyLocks() {
        if (lockTime >= 0 && lockTime <= 23 && clockHours && setClock && *clockHours != (uint8_t)lockTime)
            setClock((uint8_t)lockTime, 0);
        if (lockWeather != appliedWeather) {
            if (forceWeather) forceWeather((short)lockWeather);   // -1 = kembali normal
            appliedWeather = lockWeather;
        }
    }
    static void (*gameProcOrig)() = nullptr;
    static void gameProcHook() {
        gameProcOrig();
        applyLocks();
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - lastPoll.tv_sec) * 1000 + (now.tv_nsec - lastPoll.tv_nsec) / 1000000 >= 1000) {
            lastPoll = now; poll();
        }
    }
    static void init() {
        for (auto& c : cheats) c.fn = (void(*)())SYM(c.sym);
        findPlayerPed = (void*(*)())          SYM("_Z13FindPlayerPedv");
        findCoors     = (void(*)(CVec*))      SYM("_Z15FindPlayerCoorsv");
        pedTeleport   = (void(*)(void*,float,float,float)) SYM("_ZN4CPed8TeleportE7CVector");
        forceWeather  = (void(*)(short))      SYM("_ZN8CWeather12ForceWeatherEs");
        setClock      = (void(*)(uint8_t,uint8_t)) SYM("_ZN6CClock12SetGameClockEhh");
        clockHours    = (uint8_t*)            SYM("_ZN6CClock18ms_nGameClockHoursE");
        void* gp      = SYM("_ZN5CGame7ProcessEv");
        const char* dir = aml->GetAndroidDataPath();
        snprintf(path, sizeof path, "%s/GTA3All.txt", dir ? dir : ".");
        logger->Info("tools: file=%s player=%p teleport=%p weather=%p clock=%p proc=%p",
                     path, findPlayerPed, pedTeleport, forceWeather, setClock, gp);
        if (gp) aml->Hook(gp, (void*)gameProcHook, (void**)&gameProcOrig);
    }
}

extern "C" void OnModLoad() {
    gLib = aml->GetLibHandle("libR1.so");
    if (!gLib) { logger->Error("libR1.so tidak ketemu"); return; }
    if (ON_CAMERA)         camera::init();
    if (ON_WITNESS)        witness::init();
    if (ON_RAGDOLL_PROBE)  ragdoll::init();
    if (ON_TOOLS)          tools::init();
    logger->Info("GTA3 All In One siap");
}
