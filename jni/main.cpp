// GTA III (Android 1.8, ARM32, libR1.so) - Semua mod dalam satu .so
// Modul: kamera, saksi kejahatan, kriminal antar-NPC, lalu lintas tertib, jam lambat, ragdoll probe, alat (cheat, cuaca, waktu, teleport via file perintah).
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
#include <fcntl.h>
#include <link.h>
#include <mod/amlmod.h>
#include <mod/logger.h>

MYMOD(net.kamu.gta3all, GTA3 All In One, 1.0, Kamu)

// ================= DEBUG =================
// Menulis kejadian penting ke GTA3All_dbg.txt (folder files/ game). Kirim file ini kalau ada fitur yang tidak jalan.
static const bool ON_DEBUG = true;
#include <stdarg.h>
static char dbgPath[512]; static int dbgN = 0;
static void dbg(const char* fmt, ...) {
    if (!ON_DEBUG || dbgN > 700) return;
    if (!dbgPath[0]) { const char* dir = aml->GetAndroidDataPath(); snprintf(dbgPath, sizeof dbgPath, "%s/GTA3All_dbg.txt", dir ? dir : "."); }
    FILE* f = fopen(dbgPath, "a"); if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f); dbgN++;
}

// ================= SAKLAR MODUL =================
static const bool ON_CAMERA  = true;
static const bool ON_WITNESS = true;
static const bool ON_RAGDOLL_PROBE = false;  // pengintai debug; dimatikan (sumber crash 0x10F82). Aktifkan hanya untuk riset
static const bool ON_TOOLS   = true;   // cheat, kunci cuaca/jam, teleport lewat file GTA3All.txt
static const bool ON_SYMDUMP = false;  // dimatikan: di libR1.so .dynstr berada sebelum .dynsym, jadi pembacaan tabel simbolnya gagal (file kosong). Daftar simbol sudah dibaca langsung dari libR1.so

// ================= JAM GAME =================
// Bawaan: 1 menit game = 1000 ms nyata (1 hari game = 24 menit). 2000 ms -> 1 hari game = 48 menit nyata.
static const uint32_t GAME_MINUTE_MS = 2000;

// ================= LALU LINTAS TERTIB =================
static const bool  ON_TRAFFIC         = true;
static const float TRAFFIC_SPEED_MULT = 0.80f;   // pengali kecepatan maksimum mobil NPC (1.0 = bawaan). Lebih kecil = lebih pelan/hati-hati

// ================= KRIMINAL ANTAR-NPC =================
static const bool     ON_NPC_CRIME       = true;  // NPC memukul NPC lain dianggap kejahatan; saksi sekitar bereaksi (kabur)
static const uint32_t NPC_CRIME_COOLDOWN = 2500;  // ms, supaya pukulan beruntun tidak dihitung terus-menerus

// ================= JATUH SAAT KELUAR MOBIL MELAJU =================
static const bool     ON_CAR_FALL        = true;
static const float    CAR_FALL_MIN_SPEED = 8.0f;   // m/s (~29 km/j). Keluar mobil di atas kecepatan ini -> pemain jatuh
static const int      CAR_FALL_ANIM      = 43;     // AnimationId (urutan di tabel anim "man" libR1.so): 43 = FLOOR_hit_f. Alternatif: 25 = KO_skid_front, 154 = FALL_collapse
static const int      CAR_FALL_TIME_MS   = 1500;   // lama tergeletak sebelum bangun
static const bool     ALLOW_EXIT_MOVING  = true;   // true = boleh keluar saat mobil melaju (tanpa berhenti dulu) kalau kecepatan >= CAR_FALL_MIN_SPEED

// ================= KOTA HIDUP: numpang mobil + polisi menangkap =================
static const bool     ON_LIVE            = true;
static const bool     ON_RIDES           = true;      // NPC jalan kaki sesekali numpang mobil NPC lain (taxi diutamakan)
static const uint32_t RIDE_MIN_MS        = 25000;     // jeda acak antar tumpangan
static const uint32_t RIDE_MAX_MS        = 70000;
static const bool     ON_POLICE_EVENT    = true;      // NPC berantem -> mobil polisi datang, pelaku masuk kursi belakang
static const int      POLICE_CHANCE      = 70;        // % berantem yang berujung polisi
static const uint32_t POLICE_DELAY_MS    = 9000;      // berantem selama ini baru polisi dipanggil
static const uint32_t POLICE_COOLDOWN_MS = 60000;
static const uint32_t POLICE_REAR_DOOR   = 10;        // node pintu belakang kanan (RF=9, RR=10, LF=11, LR=12); ganti 12 untuk kiri

// ================= PEMAIN NUMPANG =================
// Tekan tombol masuk dekat mobil NPC yang berhenti: dekat pintu belakang -> duduk belakang, pintu depan kanan -> duduk depan,
// pintu sopir -> tetap aturan bawaan (mengemudi/merampas).
static const bool  ON_PASSENGER_ENTER     = true;
static const float PASSENGER_MAX_CAR_SPEED = 3.0f;   // m/s; mobil yang masih melaju lebih cepat dari ini tidak bisa dinaiki
static const float PASSENGER_REACH         = 3.2f;   // m dari pusat mobil

// ================= PENGINTAI MOBIL (sementara) =================
// Mencatat isi memori ~40 mobil pertama + ID model "taxi"/"cabbie" ke GTA3All_veh.txt (folder files/ game).
// Gunanya hanya untuk mencari cara mengenali taxi sebelum modul penumpang taxi ditulis. Matikan setelah filenya dikirim.
static const bool ON_VEH_PROBE = false;   // sudah selesai dipakai; pelacak mobil sekarang ada di modul KOTA HIDUP

// ================= KAMERA =================
static const float FOV_MULT      = 1.55f;  // 1.0 = normal; 1.55 ~ FOV GTA SA (45 -> ~70). Turunkan (mis. 1.3) kalau terasa terlalu lebar
static const bool  USE_MOUSE_CAM = false;  // eksperimen lama (tanpa input sentuhan); biarkan false, pakai TOUCH_LOOK di bawah
// Jarak kamera. Nilai ini dibaca dari fungsi SetZoomValue... milik libR1.so: preset jalan kaki 0.25 (dekat) / 1.7 (sedang) / 2.9 (jauh),
// preset di mobil 0.05 / 1.9 / 3.9. Boleh diisi angka lain di antara/di luar itu. Kalau terasa kejauhan, turunkan.
static const bool  CAM_ZOOM_ON   = true;
static const float CAM_PED_ZOOM  = 2.9f;   // kamera saat jalan kaki
static const float CAM_CAR_ZOOM  = 3.9f;   // kamera saat di mobil
// Kontrol kamera ala SA: geser layar (stik kanan) memutar kamera bebas, tidak otomatis kembali ke belakang pemain.
static const bool  TOUCH_LOOK      = true;
static const float CAM_TOUCH_SENS  = 12.0f;  // makin besar makin cepat berputar
static const bool  CAM_INVERT_X    = false;  // ganti true kalau arah putar kebalik (kiri-kanan)
static const bool  CAM_INVERT_Y    = false;  // ganti true kalau arah atas-bawah kebalik

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
    static uint8_t* theCam = nullptr;           // objek TheCamera
    static bool touchSeen = false;              // true setelah ada geseran terdeteksi; sebelum itu kamera tetap normal (tidak beku)
    // Saat pemain mati/ditangkap, semua pengaturan kamera kita dilepas supaya kamera bawaan (kamera mati/respawn) bekerja normal.
    static void* players = nullptr;                    // CWorld::Players (pemain 0)
    static bool (*isDeadFn)(void*) = nullptr;          // CPlayerInfo::IsRestartingAfterDeath
    static bool (*isBustedFn)(void*) = nullptr;        // CPlayerInfo::IsRestartingAfterArrest
    static bool lastWasted = false;
    static void ccHook(void* self) {
        bool wasted = false;
        if (players && isDeadFn   && isDeadFn(players))   wasted = true;
        if (players && isBustedFn && isBustedFn(players)) wasted = true;
        if (wasted) {
            if (!lastWasted) dbg("[cam] pemain mati/ditangkap -> pengaturan kamera dilepas");
            lastWasted = true;
            if (theCam) { theCam[0x6b] = 0; theCam[0x6c] = 0; }
            if (mouse3rd && !USE_MOUSE_CAM) *mouse3rd = 0;
            ccOrig(self);
            return;
        }
        if (lastWasted) { lastWasted = false; dbg("[cam] pemain hidup lagi -> pengaturan kamera dipasang lagi"); }
        if (mouse3rd && (USE_MOUSE_CAM || (TOUCH_LOOK && touchSeen))) *mouse3rd = 1;
        if (CAM_ZOOM_ON && theCam) {
            // offset dibaca dari kode SetZoomValueFollowPedScript / SetZoomValueCamStringScript:
            *(float*)(theCam + 0x144) = CAM_PED_ZOOM; theCam[0x6b] = 1;   // zoom jalan kaki + flag "pakai nilai script"
            *(float*)(theCam + 0x12c) = CAM_CAR_ZOOM; theCam[0x6c] = 1;   // zoom mobil + flag
        }
        ccOrig(self);
    }
    // Sentuhan -> kamera: nilai stik kanan dari Touchscreen::SetupJoystate dimasukkan ke status "mouse" milik game.
    static void (*sjOrig)(void*, void*, void*, void*) = nullptr;
    static void** gTouch = nullptr;             // pointer GTouchscreen
    static uint8_t* mouseState = nullptr;       // CPad::NewMouseControllerState (x di +8, y di +12)
    static int sjLog = 0;
    static void sjHook(void* a, void* b, void* c, void* d) {
        sjOrig(a, b, c, d);
        if (!TOUCH_LOOK || !mouseState) return;
        void* ts = gTouch ? *gTouch : nullptr;
        float* right = (float*)((ts && a == ts) ? d : c);   // fungsi bisa member (ada this) atau static
        if (!right) return;
        float rx = right[0], ry = right[1];
        if (fabsf(rx) > 0.05f || fabsf(ry) > 0.05f) {
            if (!touchSeen) { touchSeen = true; dbg("[cam] geseran pertama terdeteksi -> mode kamera bebas aktif"); }
            if (sjLog < 40) { sjLog++; dbg("[cam] stik kanan = %.3f, %.3f (member=%d)", rx, ry, (ts && a == ts) ? 1 : 0); }
        }
        float* m = (float*)(mouseState + 8);
        m[0] = rx * CAM_TOUCH_SENS * (CAM_INVERT_X ? -1.0f : 1.0f);
        m[1] = ry * CAM_TOUCH_SENS * (CAM_INVERT_Y ? -1.0f : 1.0f);
    }
    static void init() {
        void* fov = SYM("_ZN7CCamera10FindCamFOVEv");
        logger->Info("camera: FindCamFOV=%p", fov);
        if (fov) aml->Hook(fov, (void*)fovHook, (void**)&fovOrig);
        mouse3rd = (uint8_t*)SYM("_ZN7CCamera20m_bUseMouse3rdPersonE");
        theCam   = (uint8_t*)SYM("TheCamera");
        players    = SYM("_ZN6CWorld7PlayersE");
        isDeadFn   = (bool(*)(void*)) SYM("_ZN11CPlayerInfo22IsRestartingAfterDeathEv");
        isBustedFn = (bool(*)(void*)) SYM("_ZN11CPlayerInfo23IsRestartingAfterArrestEv");
        dbg("[init] camera players=%p isDead=%p isBusted=%p", players, (void*)isDeadFn, (void*)isBustedFn);
        void* cc = SYM("_ZN7CCamera10CamControlEv");
        logger->Info("camera: mouse3rd=%p TheCamera=%p CamControl=%p", mouse3rd, theCam, cc);
        dbg("[init] camera mouse3rd=%p TheCamera=%p CamControl=%p", mouse3rd, theCam, cc);
        if (cc && (USE_MOUSE_CAM || TOUCH_LOOK || CAM_ZOOM_ON)) aml->Hook(cc, (void*)ccHook, (void**)&ccOrig);
        if (TOUCH_LOOK) {
            mouseState = (uint8_t*)SYM("_ZN4CPad23NewMouseControllerStateE");
            gTouch     = (void**)  SYM("GTouchscreen");
            void* sj   = SYM("_ZN11Touchscreen13SetupJoystateERiR5RwV2dS2_");
            logger->Info("camera: mouseState=%p GTouchscreen=%p SetupJoystate=%p", mouseState, gTouch, sj);
            dbg("[init] camera mouseState=%p GTouchscreen=%p SetupJoystate=%p", mouseState, gTouch, sj);
            if (sj && mouseState) aml->Hook(sj, (void*)sjHook, (void**)&sjOrig);
        }
    }
}

// ---------------------------------------------------------------- SAKSI
namespace live { static void seenPed(void*); static void seenVeh(void*); static void onFight(void*, void*); static bool tryEnterPassenger(); }
namespace witness {
    static void (*getCentre)(void*, CVec*) = nullptr;
    static bool (*los)(const CVec&, const CVec&, bool,bool,bool,bool,bool,bool,bool) = nullptr;
    static void (*setFlee)(void*, const CVec2*, int) = nullptr;
    static void (*playerCoors)(CVec*) = nullptr;
    static uint32_t* timeMs = nullptr;

    static CVec crimePos; static uint32_t crimeTime = 0, crimeSerial = 0; static bool hasCrime = false;
    static void* crimeActor = nullptr;            // pelaku kejahatan NPC; dia sendiri tidak ikut kabur
    static void* (*findPed)() = nullptr;          // FindPlayerPed
    struct Reacted { void* ped; uint32_t serial; };
    static Reacted reacted[96]; static int reactedNext = 0;

    static void note(int type, const CVec* pos) {
        if (!pos || !timeMs) return;
        crimePos = *pos; crimeTime = *timeMs; crimeSerial++; hasCrime = true; crimeActor = nullptr;
        if (LOG_CRIME) logger->Info("[crime] type=%d pos=(%.1f,%.1f,%.1f) serial=%u",
                                    type, pos->x, pos->y, pos->z, crimeSerial);
    }
    static void noteBy(void* actor, int type, const CVec* pos) { note(type, pos); crimeActor = actor; }
    static bool already(void* p) { for (auto& r : reacted) if (r.ped == p && r.serial == crimeSerial) return true; return false; }
    static void mark(void* p) { reacted[reactedNext] = { p, crimeSerial }; reactedNext = (reactedNext + 1) % 96; }

    static void (*rcOrig)(void*, int, const CVec*, uint32_t, bool) = nullptr;
    static void rcHook(void* s, int t, const CVec* p, uint32_t id, bool dc) { note(t, p); rcOrig(s, t, p, id, dc); }
    static void (*rcnOrig)(void*, int, const CVec*, bool) = nullptr;
    static void rcnHook(void* s, int t, const CVec* p, bool dc) { note(t, p); rcnOrig(s, t, p, dc); }

    static void (*pcOrig)(void*) = nullptr;
    static void pcHook(void* ped) {
        pcOrig(ped);
        live::seenPed(ped);
        if (!hasCrime || !timeMs) return;
        if (*timeMs - crimeTime > CRIME_WINDOW) return;
        if (ped == crimeActor) { mark(ped); return; }
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
        findPed     = (void*(*)()) SYM("_Z13FindPlayerPedv");
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

// ---------------------------------------------------------------- KRIMINAL ANTAR-NPC
// NPC (bukan pemain) mulai menyerang NPC lain -> dicatat sebagai kejahatan di posisi penyerang.
// Pemain dikecualikan karena sudah ditangani sistem wanted bawaan + modul saksi di atas.
namespace npccrime {
    static void (*saOrig)(void*, void*) = nullptr;
    static uint32_t lastMs = 0;
    static void saHook(void* self, void* victim) {
        saOrig(self, victim);
        if (!self || !victim || !witness::findPed || !witness::timeMs || !witness::getCentre) return;
        void* pl = witness::findPed();
        if (self == pl || victim == pl) return;
        live::onFight(self, victim);
        uint32_t now = *witness::timeMs;
        if (now - lastMs < NPC_CRIME_COOLDOWN) return;
        lastMs = now;
        CVec p; witness::getCentre(self, &p);
        witness::noteBy(self, 100, &p);   // 100 = kode kejahatan buatan sendiri (NPC menyerang NPC), hanya untuk log
    }
    static void init() {
        void* sa = SYM("_ZN4CPed9SetAttackEP7CEntity");
        logger->Info("npccrime: SetAttack=%p", sa);
        if (sa) aml->Hook(sa, (void*)saHook, (void**)&saOrig);
    }
}

// ---------------------------------------------------------------- LALU LINTAS TERTIB
// Menurunkan kecepatan maksimum mobil NPC di lalu lintas supaya tidak gaspol.
namespace traffic {
    static float (*msOrig)(void*) = nullptr;
    static float msHook(void* veh) { return msOrig(veh) * TRAFFIC_SPEED_MULT; }
    static void init() {
        void* f = SYM("_ZN8CCarCtrl35FindMaximumSpeedForThisCarInTrafficEP8CVehicle");
        logger->Info("traffic: FindMaximumSpeedForThisCarInTraffic=%p", f);
        if (f) aml->Hook(f, (void*)msHook, (void**)&msOrig);
    }
}

// ---------------------------------------------------------------- JATUH SAAT KELUAR MOBIL MELAJU
// Kecepatan mobil diperkirakan dari perpindahan posisi pemain (tanpa baca struct mobil).
// Saat pemain menekan keluar (CPed::SetExitCar) dan kecepatan di atas batas, setelah pemain benar-benar
// sudah di luar mobil (FindPlayerVehicle() == null) dipanggil CPed::SetFall dengan animasi jatuh bawaan game.
namespace carfall {
    static void* (*findVeh)() = nullptr;
    static void (*setFall)(void*, int, int, unsigned char) = nullptr;
    static void (*exitOrig)(void*, void*, unsigned int) = nullptr;
    static bool tryForce(const char* via);
    static bool inForceWindow();
    static bool (*jdSym)(void*) = nullptr;           // alamat asli CPad::ExitVehicleJustDown (untuk polling cadangan)
    static bool (*canExitOrig)(void*) = nullptr;
    static void* (*getPad)(int) = nullptr;
    static bool (*jdOrig)(void*) = nullptr;          // CPad::ExitVehicleJustDown
    static bool (*geOrig)(void*) = nullptr;          // CPad::GetExitVehicle
    static void (*setExit)(void*, void*, unsigned int) = nullptr;
    static uint32_t forcedT = 0; static bool forcedOnce = false;
    static bool (*onSide)(void*) = nullptr;
    static CVec lastPos = {0,0,0}; static uint32_t lastT = 0; static float speed = 0.0f;
    static bool pending = false; static uint32_t pendT = 0;

    // Dari GTA3All_veh.txt: ID model mobil ada di int16 pada mobil+0x60 (taxi=110, cabbie=128, police=116).
    static inline int modelOf(void* veh) { return veh ? (int)*(const int16_t*)((const uint8_t*)veh + 0x60) : -1; }
    static inline bool isTaxi(void* veh) { int m = modelOf(veh); return m == 110 || m == 128; }

    // Pencari offset: saat pemain naik mobil, cari di memori mobil di mana pointer pemain (pengemudi) disimpan,
    // dan di memori pemain di mana pointer mobilnya disimpan. Hasilnya hanya dicatat ke GTA3All_dbg.txt.
    static void* lastVeh = nullptr;
    static void scanOffsets(void* veh) {
        void* pl = witness::findPed ? witness::findPed() : nullptr;
        if (!veh || !pl) return;
        const uint32_t* v = (const uint32_t*)veh; const uint32_t plv = (uint32_t)(uintptr_t)pl;
        const uint32_t* p = (const uint32_t*)pl;  const uint32_t cvv = (uint32_t)(uintptr_t)veh;
        for (int i = 0x40 / 4; i < 0x340 / 4; i++) if (v[i] == plv) dbg("[scan] mobil+0x%x = pointer pemain", i * 4);
        for (int i = 0x40 / 4; i < 0x480 / 4; i++) if (p[i] == cvv) dbg("[scan] pemain+0x%x = pointer mobil", i * 4);
    }

    static void track() {
        if (!witness::timeMs || !witness::playerCoors) return;
        if (ON_DEBUG && findVeh) {
            void* cv = findVeh();
            if (cv && cv != lastVeh) {
                dbg("[veh] pemain naik mobil %p model=%d%s", cv, modelOf(cv), isTaxi(cv) ? " (TAXI)" : (modelOf(cv) == 116 ? " (POLISI)" : ""));
                scanOffsets(cv);
            }
            lastVeh = cv;
        }
        uint32_t now = *witness::timeMs;
        if (lastT == 0 || now - lastT >= 100) {
            CVec p; witness::playerCoors(&p);
            if (lastT != 0 && now > lastT) {
                float dt = (now - lastT) / 1000.0f;
                float dx = p.x - lastPos.x, dy = p.y - lastPos.y;
                float v = sqrtf(dx*dx + dy*dy) / dt;
                if (v < 60.0f) speed = v;            // lonjakan (teleport) diabaikan
            }
            lastPos = p; lastT = now;
        }
        // cadangan: kalau hook tombol tidak terpanggil, cek tombol keluar sendiri tiap frame
        if (ALLOW_EXIT_MOVING && jdSym && getPad && findVeh && !inForceWindow() && findVeh()) {
            void* pad = getPad(0);
            if (pad && jdSym(pad) && !inForceWindow()) { dbg("[poll] tombol keluar ditekan"); tryForce("poll"); }
        }
        if (pending) {
            if (now - pendT > 4000) { pending = false; return; }
            if (now - pendT > 200 && findVeh && !findVeh()) {   // sudah di luar mobil
                void* pl = witness::findPed ? witness::findPed() : nullptr;
                if (pl && setFall) {
                    setFall(pl, CAR_FALL_TIME_MS, CAR_FALL_ANIM, 1);
                    logger->Info("[carfall] jatuh (kecepatan keluar %.1f m/s)", speed);
                    dbg("[fall] SetFall dipanggil (kecepatan %.1f)", speed);
                }
                pending = false;
            }
        }
    }
    // Aturan bawaan: pemain baru boleh keluar kalau mobil hampir berhenti (mobil direm dulu).
    // Di sini, kalau mobil pemain melaju kencang dan tidak terbalik, keluar tetap diizinkan.
    static bool canExitHook(void* veh) {
        bool ok = canExitOrig(veh);
        static int ceN = 0;
        if (ON_DEBUG && ceN < 12 && findVeh && veh == findVeh()) { ceN++; dbg("[canExit] bawaan=%d kecepatan=%.1f", ok ? 1 : 0, speed); }
        if (ok || !ALLOW_EXIT_MOVING || !findVeh || !veh) return ok;
        if (veh != findVeh()) return ok;                  // hanya mobil yang dipakai pemain
        if (speed < CAR_FALL_MIN_SPEED) return ok;        // pelan: aturan bawaan
        if (onSide && onSide(veh)) return ok;             // terbalik: aturan bawaan
        return true;
    }
    // Rem otomatis: saat tombol keluar ditekan, logika pemain (CPlayerPed) mengerem mobil dulu.
    // Cara menghindarinya: kalau mobil melaju kencang, kita ambil alih tombol keluar. Kita panggil
    // CPed::SetExitCar sendiri dan menyembunyikan tombol itu dari game selama 1,5 detik, jadi logika
    // "rem dulu baru keluar" tidak pernah jalan.
    static bool inForceWindow() { return forcedOnce && witness::timeMs && (*witness::timeMs - forcedT) < 1500; }
    static bool tryForce(const char* via) {
        if (!findVeh || !witness::findPed || !witness::timeMs || !setExit) return false;
        void* veh = findVeh(); void* pl = witness::findPed();
        if (!veh || !pl) return false;
        if (speed < CAR_FALL_MIN_SPEED) { dbg("[exit:%s] tombol keluar, tapi kecepatan %.1f < batas %.1f -> aturan bawaan", via, speed, CAR_FALL_MIN_SPEED); return false; }
        if (onSide && onSide(veh)) { dbg("[exit:%s] mobil terbalik -> aturan bawaan", via); return false; }
        forcedOnce = true; forcedT = *witness::timeMs;
        dbg("[exit:%s] PAKSA keluar, kecepatan %.1f m/s", via, speed);
        setExit(pl, veh, 0);
        return true;
    }
    static bool jdHook(void* pad) {
        bool r = jdOrig(pad);
        if (!getPad || pad != getPad(0)) return r;
        if (inForceWindow()) return false;
        if (!r) return r;
        dbg("[hook] ExitVehicleJustDown = true (%s)", (findVeh && findVeh()) ? "di mobil" : "jalan kaki");
        void* veh = findVeh ? findVeh() : nullptr;
        if (veh) return (ALLOW_EXIT_MOVING && tryForce("hook")) ? false : r;
        if (ON_PASSENGER_ENTER && witness::timeMs && live::tryEnterPassenger()) {   // jalan kaki: coba naik sebagai penumpang
            forcedOnce = true; forcedT = *witness::timeMs;
            return false;
        }
        return r;
    }
    static bool geHook(void* pad) {
        bool r = geOrig(pad);
        if (r && getPad && pad == getPad(0) && inForceWindow()) return false;
        return r;
    }
    static void exitHook(void* self, void* veh, unsigned int door) {
        exitOrig(self, veh, door);
        static int exN = 0;
        if (ON_DEBUG && exN < 20 && witness::findPed && self == witness::findPed()) { exN++; dbg("[SetExitCar] pemain, kecepatan=%.1f door=%u", speed, door); }
        if (!self || !witness::findPed || !witness::timeMs || self != witness::findPed()) return;
        if (speed >= CAR_FALL_MIN_SPEED) { pending = true; pendT = *witness::timeMs; }
    }
    static void init() {
        findVeh = (void*(*)())                       SYM("_Z17FindPlayerVehiclev");
        setFall = (void(*)(void*, int, int, unsigned char)) SYM("_ZN4CPed7SetFallEi11AnimationIdh");
        onSide  = (bool(*)(void*))                   SYM("_ZN8CVehicle11IsOnItsSideEv");
        void* ex = SYM("_ZN4CPed10SetExitCarEP8CVehiclej");
        void* ce = SYM("_ZN8CVehicle13CanPedExitCarEv");
        logger->Info("carfall: findVeh=%p setFall=%p exit=%p canExit=%p onSide=%p", findVeh, setFall, ex, ce, onSide);
        if (ex) aml->Hook(ex, (void*)exitHook, (void**)&exitOrig);
        if (ce && ALLOW_EXIT_MOVING) aml->Hook(ce, (void*)canExitHook, (void**)&canExitOrig);
        getPad  = (void*(*)(int)) SYM("_ZN4CPad6GetPadEi");
        setExit = (void(*)(void*, void*, unsigned int)) ex;
        void* jd = SYM("_ZN4CPad19ExitVehicleJustDownEv");
        jdSym = (bool(*)(void*)) jd;
        void* ge = SYM("_ZN4CPad14GetExitVehicleEv");
        logger->Info("carfall: getPad=%p exitJustDown=%p getExit=%p", getPad, jd, ge);
        dbg("[init] carfall getPad=%p exitJustDown=%p getExit=%p setExit=%p canExit=%p findVeh=%p setFall=%p", getPad, jd, ge, ex, ce, findVeh, setFall);
        if ((ALLOW_EXIT_MOVING || ON_PASSENGER_ENTER) && getPad && ex) {
            if (jd) aml->Hook(jd, (void*)jdHook, (void**)&jdOrig);
            if (ge) aml->Hook(ge, (void*)geHook, (void**)&geOrig);
        }
    }
}

// ---------------------------------------------------------------- PENGINTAI MOBIL
namespace vehprobe {
    static const int MAXV = 40;
    static void* seen[MAXV]; static int seenN = 0;
    static char path[512];
    static void* (*getMI)(const char*, int*) = nullptr;
    static void (*pcOrig)(void*) = nullptr;
    static bool header = false;

    static void writeHeader() {
        header = true;
        FILE* f = fopen(path, "a"); if (!f) return;
        const char* names[] = { "taxi", "cabbie", "idaho", "stinger", "police", nullptr };
        for (int i = 0; names[i]; i++) {
            int id = -1; void* mi = getMI ? getMI(names[i], &id) : nullptr;
            fprintf(f, "MODEL %s id=%d info=%p\n", names[i], id, mi);
        }
        fclose(f);
    }
    static void pcHook(void* veh) {
        pcOrig(veh);
        live::seenVeh(veh);
        if (seenN >= MAXV || !veh) return;
        for (int i = 0; i < seenN; i++) if (seen[i] == veh) return;
        seen[seenN++] = veh;
        if (!header) writeHeader();
        FILE* f = fopen(path, "a"); if (!f) return;
        fprintf(f, "VEH %p", veh);
        const uint8_t* b = (const uint8_t*)veh;
        for (int off = 0x40; off < 0x80; off += 2) fprintf(f, " %x:%d", off, (int)*(const int16_t*)(b + off));
        fputc('\n', f);
        fclose(f);
    }
    static void init() {
        getMI = (void*(*)(const char*, int*)) SYM("_ZN10CModelInfo12GetModelInfoEPKcPi");
        void* pc = SYM("_ZN11CAutomobile14ProcessControlEv");
        const char* dir = aml->GetAndroidDataPath();
        snprintf(path, sizeof path, "%s/GTA3All_veh.txt", dir ? dir : ".");
        logger->Info("vehprobe: GetModelInfo=%p AutomobilePC=%p file=%s", getMI, pc, path);
        if (pc) aml->Hook(pc, (void*)pcHook, (void**)&pcOrig);
    }
}

// ---------------------------------------------------------------- KOTA HIDUP
// 1) NPC jalan kaki sesekali numpang mobil NPC lain (taxi diutamakan): mobil dipanggil ke posisi NPC, NPC naik,
//    diantar ke tujuan, turun, lalu mobil kembali ke lalu lintas.
// 2) NPC berantem cukup lama -> mobil polisi dipanggil, polisi turun, pelaku masuk kursi belakang mobil polisi.
// Semua pointer NPC/mobil dicek "masih hidup" (terlihat dalam 0,5 detik terakhir) sebelum dipakai.
namespace live {
    struct TPed { void* p; CVec pos; uint32_t t, tPos; };
    struct TVeh { void* v; CVec pos; uint32_t t, tPos, movedT; float speed; };
    static TPed peds[160]; static TVeh vehs[120];

    static void (*seekCar)(void*, void*, unsigned int) = nullptr;     // CPed::SetSeekCar
    static void (*clearAll)(void*) = nullptr;                         // CPed::ClearAll
    static void (*setWander)(void*, signed char) = nullptr;           // CPed::SetWanderPath
    static void (*gotoCoors)(void*, CVec, bool) = nullptr;            // CCarCtrl::JoinCarWithRoadSystemGotoCoors
    static void (*joinRoad)(void*) = nullptr;                         // CCarCtrl::JoinCarWithRoadSystem
    static void (*tellLeave)(void*) = nullptr;                        // CCarAI::TellOccupantsToLeaveCar
    static void (*genEmerg)(unsigned int, CVec) = nullptr;            // CCarCtrl::GenerateOneEmergencyServicesCar
    static void (*setExit)(void*, void*, unsigned int) = nullptr;     // CPed::SetExitCar
    static void* (*getMI)(const char*, int*) = nullptr;
    static void (*vcOrig)(void*) = nullptr;
    static int policeModel = -1; static bool miInit = false;

    static inline uint32_t now() { return witness::timeMs ? *witness::timeMs : 0; }
    static float d2d(const CVec& a, const CVec& b) { float dx = a.x - b.x, dy = a.y - b.y; return sqrtf(dx*dx + dy*dy); }

    static void seenPed(void* p) {
        if (!ON_LIVE || !p || !witness::getCentre || !witness::timeMs) return;
        uint32_t t = *witness::timeMs;
        TPed* slot = nullptr; TPed* oldest = &peds[0];
        for (auto& e : peds) { if (e.p == p) { slot = &e; break; } if (e.t < oldest->t) oldest = &e; }
        if (!slot) { slot = oldest; slot->p = p; slot->tPos = 0; }
        if (slot->tPos == 0 || t - slot->tPos >= 200) { witness::getCentre(p, &slot->pos); slot->tPos = t; }
        slot->t = t;
    }
    static void seenVeh(void* v) {
        if (!ON_LIVE || !v || !witness::getCentre || !witness::timeMs) return;
        uint32_t t = *witness::timeMs;
        TVeh* slot = nullptr; TVeh* oldest = &vehs[0];
        for (auto& e : vehs) { if (e.v == v) { slot = &e; break; } if (e.t < oldest->t) oldest = &e; }
        if (!slot) { slot = oldest; slot->v = v; slot->tPos = 0; slot->speed = 0; slot->movedT = 0; }
        if (slot->tPos == 0) { witness::getCentre(v, &slot->pos); slot->tPos = t; slot->speed = 0; }
        else if (t - slot->tPos >= 300) {
            CVec np; witness::getCentre(v, &np);
            slot->speed = d2d(np, slot->pos) / ((t - slot->tPos) / 1000.0f);
            slot->pos = np; slot->tPos = t;
            if (slot->speed >= 1.5f) slot->movedT = t;      // pernah melaju = kemungkinan ada sopir NPC
        }
        slot->t = t;
    }
    static void vcHook(void* v) { vcOrig(v); seenVeh(v); }

    static TPed* pedE(void* p) { if (!p) return nullptr; for (auto& e : peds) if (e.p == p) return &e; return nullptr; }
    static TVeh* vehE(void* v) { if (!v) return nullptr; for (auto& e : vehs) if (e.v == v) return &e; return nullptr; }
    static bool pedAlive(void* p) { TPed* e = pedE(p); return e && now() - e->t <= 500; }
    static bool vehAlive(void* v) { TVeh* e = vehE(v); return e && now() - e->t <= 500; }
    static bool onFoot(const TPed& e) {
        uint32_t t = now();
        for (auto& v : vehs) { if (!v.v || t - v.t > 500) continue; if (d2d(e.pos, v.pos) < 2.4f) return false; }
        return true;
    }
    static bool pickFar(const CVec& from, CVec* out, float minD, float maxD, void* exclude) {
        TVeh* pick = nullptr; int c = 0; uint32_t t = now();
        for (auto& e : vehs) {
            if (!e.v || t - e.t > 1500 || e.v == exclude) continue;
            float d = d2d(e.pos, from); if (d < minD || d > maxD) continue;
            if (carfall::modelOf(e.v) == policeModel) continue;
            if (rand() % (++c) == 0) pick = &e;
        }
        if (!pick) return false;
        *out = pick->pos;
        return true;
    }

    // ---- pemain numpang
    static void (*setEnter)(void*, void*, unsigned int) = nullptr;   // CPed::SetEnterCar
    static int mPos = -1;                 // offset posisi di matriks mobil (sumbu kanan = -0x30, depan = -0x10), dideteksi saat jalan
    static void* lastPlayerVeh = nullptr;
    static uint32_t lastMatrixTry = 0;
    static bool detectMatrix(void* v, const CVec& c) {
        const uint8_t* b = (const uint8_t*)v;
        for (int off = 0x34; off <= 0x60; off += 4) {
            const float* f = (const float*)(b + off);
            if (fabsf(f[0] - c.x) > 2.0f || fabsf(f[1] - c.y) > 2.0f || fabsf(f[2] - c.z) > 2.0f) continue;
            const float* r = (const float*)(b + off - 0x30); const float* a = (const float*)(b + off - 0x10);
            float lr = r[0]*r[0] + r[1]*r[1] + r[2]*r[2], la = a[0]*a[0] + a[1]*a[1] + a[2]*a[2];
            float dt = r[0]*a[0] + r[1]*a[1] + r[2]*a[2];
            if (fabsf(lr - 1.0f) < 0.05f && fabsf(la - 1.0f) < 0.05f && fabsf(dt) < 0.05f) { mPos = off; return true; }
        }
        return false;
    }
    // node pintu (eCarNodes): RF=9, RR=10, LF=11 (sopir), LR=12. Mengembalikan 0 = sisi sopir (serahkan ke game).
    static int doorFor(const TVeh& e, const CVec& P) {
        const uint8_t* b = (const uint8_t*)e.v;
        const float* r = (const float*)(b + mPos - 0x30); const float* f = (const float*)(b + mPos - 0x10);
        float dx = P.x - e.pos.x, dy = P.y - e.pos.y;
        float lat = dx * r[0] + dy * r[1], lon = dx * f[0] + dy * f[1];
        if (lat > 0) return lon >= 0 ? 9 : 10;
        return lon >= 0 ? 0 : 12;
    }
    static bool tryEnterPassenger() {
        if (!ON_LIVE || !ON_PASSENGER_ENTER || !setEnter || mPos < 0 || !witness::playerCoors || !witness::timeMs || !witness::findPed) return false;
        uint32_t t = now(); CVec P; witness::playerCoors(&P);
        TVeh* best = nullptr; float bd = PASSENGER_REACH;
        for (auto& e : vehs) {
            if (!e.v || t - e.t > 500 || e.v == lastPlayerVeh) continue;
            float d = d2d(e.pos, P); if (d < bd) { bd = d; best = &e; }
        }
        if (!best) return false;
        if (best->speed > PASSENGER_MAX_CAR_SPEED) { dbg("[penumpang] mobil terlalu cepat (%.1f m/s)", best->speed); return false; }
        if (best->movedT == 0 || t - best->movedT > 25000) return false;      // tidak pernah melaju = tanpa sopir -> aturan bawaan
        int door = doorFor(*best, P);
        if (door == 0) { dbg("[penumpang] dekat pintu sopir -> aturan bawaan"); return false; }
        void* pl = witness::findPed();
        if (!pl) return false;
        dbg("[penumpang] naik: mobil=%p model=%d pintu=%d jarak=%.1f", best->v, carfall::modelOf(best->v), door, bd);
        setEnter(pl, best->v, (unsigned int)door);
        return true;
    }

    // ---- tumpangan
    enum { R_IDLE, R_PICK, R_RIDE, R_DROP };
    static int rs = R_IDLE; static void* rPed = nullptr; static void* rCar = nullptr;
    static uint32_t rT = 0, nextRide = 0, rNear = 0; static CVec rDest;

    // ---- polisi
    enum { P_IDLE, P_WAIT, P_COME, P_DRIVE, P_ARREST, P_BOARD };
    static int ps = P_IDLE; static void *pAtk = nullptr, *pVic = nullptr, *pCar = nullptr;
    static uint32_t pT = 0, nextPol = 0, pNear = 0; static CVec pPos;
    static void* preCars[16]; static int nPre = 0; static void* cops[4]; static int nCops = 0;

    // mobil yang harus dikembalikan ke lalu lintas setelah beberapa detik
    struct Pend { void* car; uint32_t at; }; static Pend pend[6];
    static void pendRoad(void* car, uint32_t at) { for (auto& p : pend) if (!p.car) { p.car = car; p.at = at; return; } }

    static bool busyPed(void* p) {
        if (p == rPed || p == pAtk || p == pVic) return true;
        for (int i = 0; i < nCops; i++) if (cops[i] == p) return true;
        return false;
    }
    static void releasePed(void* p) { if (pedAlive(p) && clearAll) { clearAll(p); if (setWander) setWander(p, (signed char)(rand() % 8)); } }

    static void rideAbort(const char* why) {
        dbg("[tumpangan] batal: %s", why);
        releasePed(rPed);
        if (vehAlive(rCar) && joinRoad) joinRoad(rCar);
        rs = R_IDLE; rPed = rCar = nullptr;
    }

    static void rideTick(uint32_t t) {
        if (!ON_RIDES || !seekCar || !gotoCoors || !joinRoad || !clearAll || !setExit) return;
        CVec P; witness::playerCoors(&P);
        void* pveh = carfall::findVeh ? carfall::findVeh() : nullptr;
        switch (rs) {
        case R_IDLE: {
            if (t < nextRide) return;
            nextRide = t + RIDE_MIN_MS + (uint32_t)(rand() % (int)(RIDE_MAX_MS - RIDE_MIN_MS));
            TVeh* best = nullptr; TVeh* taxi = nullptr; int cnt = 0;
            for (auto& e : vehs) {
                if (!e.v || t - e.t > 400 || e.v == pveh || e.v == pCar) continue;
                int m = carfall::modelOf(e.v);
                if (m == policeModel) continue;
                float d = d2d(e.pos, P);
                if (d < 12.0f || d > 60.0f || e.speed < 1.5f) continue;     // harus sedang dikemudikan NPC
                if (carfall::isTaxi(e.v)) taxi = &e;
                if (rand() % (++cnt) == 0) best = &e;
            }
            if (taxi && (rand() % 100) < 70) best = taxi;
            if (!best) return;
            TPed* bp = nullptr; float bd = 30.0f;
            for (auto& e : peds) {
                if (!e.p || t - e.t > 400 || busyPed(e.p)) continue;
                if (d2d(e.pos, P) < 3.0f || !onFoot(e)) continue;           // bukan pemain, bukan yang sedang di mobil
                float d = d2d(e.pos, best->pos);
                if (d < bd) { bd = d; bp = &e; }
            }
            if (!bp) return;
            rPed = bp->p; rCar = best->v; rT = t; rNear = 0; rs = R_PICK;
            gotoCoors(rCar, bp->pos, false);          // mobil menuju NPC
            seekCar(rPed, rCar, 0);                   // NPC menuju mobil (pintu dipilih game)
            dbg("[tumpangan] mulai: ped=%p mobil=%p model=%d jarak=%.1f", rPed, rCar, carfall::modelOf(rCar), bd);
            break; }
        case R_PICK: {
            TPed* pe = pedE(rPed); TVeh* ve = vehE(rCar);
            if (!pedAlive(rPed) || !vehAlive(rCar)) { rideAbort("ped/mobil hilang"); return; }
            if (t - rT > 35000) { rideAbort("kelamaan menjemput"); return; }
            if (d2d(pe->pos, ve->pos) < 1.15f) {
                if (!rNear) rNear = t;
                else if (t - rNear > 1000) {
                    CVec dest;
                    if (!pickFar(ve->pos, &dest, 70.0f, 180.0f, rCar)) { dbg("[tumpangan] naik, tapi tidak ada tujuan"); setExit(rPed, rCar, 0); rs = R_DROP; rT = t; return; }
                    rDest = dest; gotoCoors(rCar, rDest, false); rs = R_RIDE; rT = t;
                    dbg("[tumpangan] naik -> menuju (%.0f,%.0f)", dest.x, dest.y);
                }
            } else rNear = 0;
            break; }
        case R_RIDE: {
            TVeh* ve = vehE(rCar);
            if (!vehAlive(rCar)) { dbg("[tumpangan] mobil hilang (keluar jangkauan), selesai"); rs = R_IDLE; rPed = rCar = nullptr; return; }
            if (d2d(ve->pos, rDest) < 18.0f || t - rT > 100000) { setExit(rPed, rCar, 0); rs = R_DROP; rT = t; dbg("[tumpangan] sampai, turun"); }
            break; }
        case R_DROP:
            if (t - rT > 3500) { releasePed(rPed); if (vehAlive(rCar)) joinRoad(rCar); rs = R_IDLE; rPed = rCar = nullptr; }
            break;
        }
    }

    static void policeFinish(const char* why) {
        dbg("[polisi] selesai: %s", why);
        ps = P_IDLE; pAtk = pVic = pCar = nullptr; nCops = 0; nextPol = now() + POLICE_COOLDOWN_MS;
    }
    static void onFight(void* a, void* b) {
        if (!ON_LIVE || !ON_POLICE_EVENT || ps != P_IDLE || !genEmerg || !witness::timeMs) return;
        uint32_t t = now(); if (t < nextPol) return;
        if (rand() % 100 >= POLICE_CHANCE) { nextPol = t + 15000; return; }
        if (!pedAlive(a) || !pedAlive(b) || a == rPed || b == rPed) return;
        pAtk = a; pVic = b; pT = t; ps = P_WAIT; pPos = pedE(a)->pos;
        dbg("[polisi] berantem terdeteksi: pelaku=%p korban=%p -> polisi dipanggil dalam %u ms", a, b, (unsigned)POLICE_DELAY_MS);
    }
    static void policeTick(uint32_t t) {
        if (!ON_POLICE_EVENT || ps == P_IDLE) return;
        if (!seekCar || !gotoCoors || !joinRoad || !clearAll || !tellLeave || !genEmerg) { ps = P_IDLE; return; }
        switch (ps) {
        case P_WAIT:
            if (!pedAlive(pAtk)) { policeFinish("pelaku hilang"); return; }
            if (t - pT < POLICE_DELAY_MS) return;
            if (policeModel < 0) { policeFinish("ID model polisi tidak ada"); return; }
            pPos = pedE(pAtk)->pos; nPre = 0;
            for (auto& v : vehs) if (v.v && carfall::modelOf(v.v) == policeModel && nPre < 16) preCars[nPre++] = v.v;
            genEmerg((unsigned int)policeModel, pPos);
            ps = P_COME; pT = t; dbg("[polisi] mobil polisi dibuat di sekitar (%.0f,%.0f)", pPos.x, pPos.y);
            break;
        case P_COME:
            for (auto& v : vehs) {
                if (!v.v || t - v.t > 400 || carfall::modelOf(v.v) != policeModel) continue;
                bool old = false; for (int i = 0; i < nPre; i++) if (preCars[i] == v.v) old = true;
                if (old || d2d(v.pos, pPos) > 500.0f) continue;
                pCar = v.v; gotoCoors(pCar, pPos, false); ps = P_DRIVE; pT = t;
                dbg("[polisi] mobil polisi %p menuju TKP", pCar); return;
            }
            if (t - pT > 20000) policeFinish("mobil polisi tidak muncul");
            break;
        case P_DRIVE: {
            if (!vehAlive(pCar)) { policeFinish("mobil polisi hilang"); return; }
            TVeh* ve = vehE(pCar);
            if (d2d(ve->pos, pPos) < 14.0f || t - pT > 45000) {
                nCops = 0;                                    // yang duduk di mobil sekarang = polisi
                for (auto& e : peds) {
                    if (!e.p || t - e.t > 500 || e.p == pAtk || e.p == pVic) continue;
                    if (d2d(e.pos, ve->pos) < 2.8f && nCops < 4) cops[nCops++] = e.p;
                }
                tellLeave(pCar); ps = P_ARREST; pT = t; dbg("[polisi] tiba, %d polisi turun", nCops);
            }
            break; }
        case P_ARREST:
            if (t - pT < 2500) return;
            if (!pedAlive(pAtk)) { policeFinish("pelaku hilang"); return; }
            clearAll(pAtk); if (pedAlive(pVic)) { clearAll(pVic); if (setWander) setWander(pVic, (signed char)(rand() % 8)); }
            seekCar(pAtk, pCar, POLICE_REAR_DOOR);           // pelaku menyerah & masuk kursi belakang
            ps = P_BOARD; pT = t; pNear = 0; dbg("[polisi] pelaku diperintah masuk kursi belakang");
            break;
        case P_BOARD: {
            if (!pedAlive(pAtk) || !vehAlive(pCar)) { policeFinish("pelaku/mobil hilang"); return; }
            TPed* pe = pedE(pAtk); TVeh* ve = vehE(pCar);
            if (d2d(pe->pos, ve->pos) < 1.3f) {
                if (!pNear) pNear = t;
                else if (t - pNear > 1000) {
                    for (int i = 0; i < nCops; i++) if (pedAlive(cops[i])) seekCar(cops[i], pCar, 0);   // polisi naik lagi
                    CVec dest; bool far = pickFar(ve->pos, &dest, 70.0f, 180.0f, pCar);
                    dbg("[polisi] pelaku sudah di mobil, polisi naik lagi");
                    if (far) { gotoCoors(pCar, dest, false); pendRoad(pCar, t + 40000); } else pendRoad(pCar, t + 8000);
                    policeFinish("pelaku diangkut");
                }
            } else pNear = 0;
            if (t - pT > 25000) { releasePed(pAtk); policeFinish("pelaku tidak sempat masuk"); }
            break; }
        }
    }

    static void tick() {
        if (!ON_LIVE || !witness::timeMs || !witness::playerCoors || !witness::getCentre) return;
        uint32_t t = now();
        if (!miInit && getMI && t > 5000) { miInit = true; int id = -1; getMI("police", &id); policeModel = id; dbg("[live] model polisi = %d", policeModel); }
        if (carfall::findVeh) { void* pv = carfall::findVeh(); if (pv) lastPlayerVeh = pv; }
        if (ON_PASSENGER_ENTER && mPos < 0 && t - lastMatrixTry > 1000) {     // cari letak matriks sekali-sekali sampai ketemu
            lastMatrixTry = t;
            for (auto& e : vehs) if (e.v && t - e.t < 500 && detectMatrix(e.v, e.pos)) { dbg("[init] matriks mobil terdeteksi, offset posisi=0x%x", mPos); break; }
        }
        for (auto& p : pend) if (p.car && t >= p.at) { if (vehAlive(p.car) && joinRoad) joinRoad(p.car); p.car = nullptr; }
        rideTick(t);
        policeTick(t);
    }
    static void init() {
        seekCar   = (void(*)(void*, void*, unsigned int)) SYM("_ZN4CPed10SetSeekCarEP8CVehiclej");
        clearAll  = (void(*)(void*))                      SYM("_ZN4CPed8ClearAllEv");
        setWander = (void(*)(void*, signed char))         SYM("_ZN4CPed13SetWanderPathEa");
        gotoCoors = (void(*)(void*, CVec, bool))          SYM("_ZN8CCarCtrl30JoinCarWithRoadSystemGotoCoorsEP8CVehicle7CVectorb");
        joinRoad  = (void(*)(void*))                      SYM("_ZN8CCarCtrl21JoinCarWithRoadSystemEP8CVehicle");
        tellLeave = (void(*)(void*))                      SYM("_ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle");
        genEmerg  = (void(*)(unsigned int, CVec))         SYM("_ZN8CCarCtrl31GenerateOneEmergencyServicesCarEj7CVector");
        setExit   = (void(*)(void*, void*, unsigned int)) SYM("_ZN4CPed10SetExitCarEP8CVehiclej");
        getMI     = (void*(*)(const char*, int*))         SYM("_ZN10CModelInfo12GetModelInfoEPKcPi");
        setEnter  = (void(*)(void*, void*, unsigned int)) SYM("_ZN4CPed11SetEnterCarEP8CVehiclej");
        dbg("[init] live seekCar=%p clearAll=%p wander=%p goto=%p road=%p leave=%p gen=%p exit=%p mi=%p",
            (void*)seekCar, (void*)clearAll, (void*)setWander, (void*)gotoCoors, (void*)joinRoad, (void*)tellLeave, (void*)genEmerg, (void*)setExit, (void*)getMI);
        if (!ON_VEH_PROBE) {      // pelacak mobil memasang hook sendiri kalau pengintai lama mati
            void* pc = SYM("_ZN11CAutomobile14ProcessControlEv");
            dbg("[init] live hook CAutomobile::ProcessControl=%p", pc);
            if (pc) aml->Hook(pc, (void*)vcHook, (void**)&vcOrig);
        }
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

    // FIX: mincore() hanya cek "halaman ter-map", bukan "boleh dibaca". Halaman PROT_NONE
    // (mis. 0x80000000 = float -0.0 di struct ped) lolos mincore lalu crash SIGSEGV/SEGV_ACCERR.
    // write() dari memori itu ke pipe memakai hak baca sebenarnya dan mengembalikan EFAULT
    // (bukan crash) bila tidak terbaca, jadi aman dipakai sebagai uji baca.
    static bool readable(const void* p, size_t n) {
        uintptr_t a = (uintptr_t)p; if (a < 0x10000 || n == 0 || n > 4096) return false;
        static int fd[2] = { -1, -1 };
        if (fd[0] < 0 && pipe2(fd, O_NONBLOCK) != 0) { fd[0] = fd[1] = -1; return false; }
        ssize_t w = write(fd[1], p, n);
        char tmp[1024]; while (read(fd[0], tmp, sizeof tmp) > 0) {}   // kosongkan pipe
        return w == (ssize_t)n;
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
    static uint32_t* msPerMinute = nullptr;   // CClock::ms_nMillisecondsPerGameMinute
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
        // hanya ubah kalau masih nilai bawaan (1000); cheat fasttime/slowtime dibiarkan berfungsi
        if (msPerMinute && *msPerMinute == 1000) *msPerMinute = GAME_MINUTE_MS;
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
        if (ON_CAR_FALL) carfall::track();
        if (ON_LIVE) live::tick();
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
        msPerMinute   = (uint32_t*)           SYM("_ZN6CClock29ms_nMillisecondsPerGameMinuteE");
        void* gp      = SYM("_ZN5CGame7ProcessEv");
        const char* dir = aml->GetAndroidDataPath();
        snprintf(path, sizeof path, "%s/GTA3All.txt", dir ? dir : ".");
        logger->Info("tools: file=%s player=%p teleport=%p weather=%p clock=%p proc=%p",
                     path, findPlayerPed, pedTeleport, forceWeather, setClock, gp);
        if (gp) aml->Hook(gp, (void*)gameProcHook, (void**)&gameProcOrig);
    }
}

// ---------------------------------------------------------------- SYMDUMP (diagnostik)
// Menulis nama simbol libR1.so yang mengandung kata kunci (Anim, CCam, ...) ke teks biasa,
// supaya modul animasi/kamera berikutnya bisa ditulis dengan nama fungsi yang benar,
// tanpa perlu mengirim libR1.so. Hanya membaca tabel simbol; tidak mengubah game.
namespace symdump {
    struct Ctx { FILE* out; int count; int total; };
    static const char* NEEDLES[] = { "Anim", "CCam", "Fight", "Kick", "CPed", "CCarCtrl", "CClock", "CVehicle", "CWanted", "CPedType", "Door", "Task", nullptr };   // cocok dengan nama ter-mangle (_ZN4CCam..., _ZN7CCamera...)
    static bool match(const char* s) {
        for (int i = 0; NEEDLES[i]; i++) if (strstr(s, NEEDLES[i])) return true;
        return false;
    }
    static int cb(struct dl_phdr_info* info, size_t, void* data) {
        Ctx* c = (Ctx*)data;
        if (!info->dlpi_name || !strstr(info->dlpi_name, "libR1.so")) return 0;
        const ElfW(Dyn)* dyn = nullptr;
        for (int i = 0; i < info->dlpi_phnum; i++)
            if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
                dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        if (!dyn) return 1;
        uintptr_t symtab = 0, strtab = 0;
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_SYMTAB) symtab = dyn->d_un.d_ptr;
            else if (dyn->d_tag == DT_STRTAB) strtab = dyn->d_un.d_ptr;
        }
        if (!symtab || !strtab) return 1;
        if (symtab < info->dlpi_addr) symtab += info->dlpi_addr;   // sebagian linker memberi alamat relatif
        if (strtab < info->dlpi_addr) strtab += info->dlpi_addr;
        if (strtab <= symtab) return 1;
        size_t n = (strtab - symtab) / sizeof(ElfW(Sym));          // .dynsym biasanya tepat sebelum .dynstr
        if (n > 200000) n = 200000;
        const ElfW(Sym)* sy = (const ElfW(Sym)*)symtab;
        for (size_t i = 0; i < n; i++) {
            if (sy[i].st_shndx == 0 || sy[i].st_name == 0) continue;   // lewati simbol impor
            const char* nm = (const char*)(strtab + sy[i].st_name);
            c->total++;
            if (match(nm)) { fprintf(c->out, "%s\n", nm); c->count++; }
        }
        return 1;
    }
    static void run() {
        const char* dir = aml->GetAndroidDataPath();
        char path[512]; snprintf(path, sizeof path, "%s/GTA3All_syms.txt", dir ? dir : ".");
        if (FILE* t = fopen(path, "r")) { fclose(t); return; }       // sudah ada, jangan ulang
        FILE* f = fopen(path, "w"); if (!f) { logger->Error("symdump: gagal membuat %s", path); return; }
        Ctx c = { f, 0, 0 };
        dl_iterate_phdr(cb, &c);
        fclose(f);
        logger->Info("symdump: %d dari %d simbol ditulis ke %s", c.count, c.total, path);
    }
}

extern "C" void OnModLoad() {
    gLib = aml->GetLibHandle("libR1.so");
    if (!gLib) { logger->Error("libR1.so tidak ketemu"); return; }
    if (ON_SYMDUMP)        symdump::run();
    if (ON_CAMERA)         camera::init();
    dbg("== GTA3All mulai | build: kamera-zoom+geser, keluar-melaju(hook+poll) ==");
    if (ON_WITNESS)        witness::init();
    if (ON_NPC_CRIME)      npccrime::init();
    if (ON_TRAFFIC)        traffic::init();
    if (ON_VEH_PROBE)      vehprobe::init();
    if (ON_CAR_FALL)       carfall::init();
    if (ON_LIVE)           live::init();
    if (ON_RAGDOLL_PROBE)  ragdoll::init();
    if (ON_TOOLS)          tools::init();
    logger->Info("GTA3 All In One siap");
}
