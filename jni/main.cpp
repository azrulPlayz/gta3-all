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

// ================= PENGINTAI MOBIL (sementara) =================
// Mencatat isi memori ~40 mobil pertama + ID model "taxi"/"cabbie" ke GTA3All_veh.txt (folder files/ game).
// Gunanya hanya untuk mencari cara mengenali taxi sebelum modul penumpang taxi ditulis. Matikan setelah filenya dikirim.
static const bool ON_VEH_PROBE = true;

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
    static void ccHook(void* self) {
        if (mouse3rd && (USE_MOUSE_CAM || TOUCH_LOOK)) *mouse3rd = 1;
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
        if ((rx != 0.0f || ry != 0.0f) && sjLog < 20) { sjLog++; logger->Info("[cam] stik kanan = %.3f, %.3f", rx, ry); }
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
        void* cc = SYM("_ZN7CCamera10CamControlEv");
        logger->Info("camera: mouse3rd=%p TheCamera=%p CamControl=%p", mouse3rd, theCam, cc);
        if (cc && (USE_MOUSE_CAM || TOUCH_LOOK || CAM_ZOOM_ON)) aml->Hook(cc, (void*)ccHook, (void**)&ccOrig);
        if (TOUCH_LOOK) {
            mouseState = (uint8_t*)SYM("_ZN4CPad23NewMouseControllerStateE");
            gTouch     = (void**)  SYM("GTouchscreen");
            void* sj   = SYM("_ZN11Touchscreen13SetupJoystateERiR5RwV2dS2_");
            logger->Info("camera: mouseState=%p GTouchscreen=%p SetupJoystate=%p", mouseState, gTouch, sj);
            if (sj && mouseState) aml->Hook(sj, (void*)sjHook, (void**)&sjOrig);
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
    static bool (*canExitOrig)(void*) = nullptr;
    static void* (*getPad)(int) = nullptr;
    static bool (*jdOrig)(void*) = nullptr;          // CPad::ExitVehicleJustDown
    static bool (*geOrig)(void*) = nullptr;          // CPad::GetExitVehicle
    static void (*setExit)(void*, void*, unsigned int) = nullptr;
    static uint32_t forcedT = 0; static bool forcedOnce = false;
    static bool (*onSide)(void*) = nullptr;
    static CVec lastPos = {0,0,0}; static uint32_t lastT = 0; static float speed = 0.0f;
    static bool pending = false; static uint32_t pendT = 0;

    static void track() {
        if (!witness::timeMs || !witness::playerCoors) return;
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
        if (pending) {
            if (now - pendT > 4000) { pending = false; return; }
            if (now - pendT > 200 && findVeh && !findVeh()) {   // sudah di luar mobil
                void* pl = witness::findPed ? witness::findPed() : nullptr;
                if (pl && setFall) {
                    setFall(pl, CAR_FALL_TIME_MS, CAR_FALL_ANIM, 1);
                    logger->Info("[carfall] jatuh (kecepatan keluar %.1f m/s)", speed);
                }
                pending = false;
            }
        }
    }
    // Aturan bawaan: pemain baru boleh keluar kalau mobil hampir berhenti (mobil direm dulu).
    // Di sini, kalau mobil pemain melaju kencang dan tidak terbalik, keluar tetap diizinkan.
    static bool canExitHook(void* veh) {
        bool ok = canExitOrig(veh);
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
    static bool jdHook(void* pad) {
        bool r = jdOrig(pad);
        if (!ALLOW_EXIT_MOVING || !getPad || pad != getPad(0)) return r;
        if (inForceWindow()) return false;
        if (!r || !findVeh || !witness::findPed || !witness::timeMs || !setExit) return r;
        void* veh = findVeh(); void* pl = witness::findPed();
        if (!veh || !pl || speed < CAR_FALL_MIN_SPEED) return r;
        if (onSide && onSide(veh)) return r;
        forcedOnce = true; forcedT = *witness::timeMs;
        logger->Info("[carfall] paksa keluar saat melaju (%.1f m/s)", speed);
        setExit(pl, veh, 0);
        return false;
    }
    static bool geHook(void* pad) {
        bool r = geOrig(pad);
        if (r && getPad && pad == getPad(0) && inForceWindow()) return false;
        return r;
    }
    static void exitHook(void* self, void* veh, unsigned int door) {
        exitOrig(self, veh, door);
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
        void* ge = SYM("_ZN4CPad14GetExitVehicleEv");
        logger->Info("carfall: getPad=%p exitJustDown=%p getExit=%p", getPad, jd, ge);
        if (ALLOW_EXIT_MOVING && getPad && ex) {
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
    if (ON_WITNESS)        witness::init();
    if (ON_NPC_CRIME)      npccrime::init();
    if (ON_TRAFFIC)        traffic::init();
    if (ON_VEH_PROBE)      vehprobe::init();
    if (ON_CAR_FALL)       carfall::init();
    if (ON_RAGDOLL_PROBE)  ragdoll::init();
    if (ON_TOOLS)          tools::init();
    logger->Info("GTA3 All In One siap");
}
