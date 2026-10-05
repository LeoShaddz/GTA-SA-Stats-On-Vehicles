// VehicleStats.cpp - GTA San Andreas 1.0 US (includes "compact"/Hoodlum exe)
//
// Quick statistics screen also inside vehicles:
//  - Inside a vehicle: opens with the .ini key (default TAB) or with LEFT D-pad
//    (reads the game's own CPad, so it works with GInput). On foot: same as the original game.
//  - Position: by default the screen stays in its original position (on foot and in a vehicle). With
//    MoveToTop = 1 in the .ini it moves to the top, centered, both on foot AND in a vehicle.
//  - Inside a vehicle, the "Stamina" row becomes the skill of the current vehicle, with the
//    same bar/value as the game's statistic:
//        car/truck/quad -> driving skill       (stat 160, text STAT160)
//        motorcycle (subclass 9) -> motorcycle skill (stat 229, text STAT229)
//        bicycle (BMX, 10) -> cycling skill     (stat 230, text STAT230)
//        airplane/helicopter -> flying skill    (stat 223, text STAT223)
//        boat/train/trailer -> remains "Stamina"
//
// Everything confirmed in gta_sa.exe 1.0 US:
//   CHud::Draw (original section):
//     58FC24  call CPad::GetDisplayVitalStats
//     58FC29  test ax,ax / je 58FC4C        <- these 5 bytes are replaced with a JMP to Stub1
//     58FC32  call FindPlayerVehicle         <- redirected to FindVehHook
//   CHud::DrawVitalStats = 0x589650..0x58A158. Inside it:
//     CMenuManager::DrawWindow 0x573EE0 (2 calls), CFont::PrintString 0x71A700 (8),
//     CSprite2d::DrawBarChart 0x728640 (6): receive the offset used for centering.
//     Stamina line: text at 0x589CD8 (CText::Get, key STAT022) and value at
//     0x589D30 (CStats::GetStatValue, id 0x16).
//   m_nVehicleSubClass = vehicle+0x594: 0 car, 1 monster, 2 quad, 3 heli, 4 airplane, 5 boat,
//     6 train, 7 fake heli, 8 fake airplane, 9 motorcycle, 10 bicycle (BMX), 11 trailer.
//
// Compile as Win32 (x86), with MSVC. The output should be named VehicleStats.asi
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

static const uintptr_t VERSION_ADDR   = 0x82457C;
static const uint32_t  VERSION_OK     = 0x94BF;

static const uintptr_t TEST_SITE      = 0x58FC29;  // test ax,ax / je 58FC4C
static const uintptr_t VEH_CALL_SITE  = 0x58FC32;  // call FindPlayerVehicle
static const uintptr_t FIND_VEHICLE   = 0x56E0D0;  // CVehicle* FindPlayerVehicle(int, bool)
static const uintptr_t GET_PAD        = 0x53FB70;  // CPad* CPad::GetPad(int)

static const uintptr_t VS_LO          = 0x589650;  // CHud::DrawVitalStats
static const uintptr_t VS_HI          = 0x58A158;
static const uintptr_t FN_PRINT       = 0x71A700;  // CFont::PrintString(float,float,ushort*)
static const uintptr_t FN_BAR         = 0x728640;  // CSprite2d::DrawBarChart(...)
static const uintptr_t FN_WINDOW      = 0x573EE0;  // CMenuManager::DrawWindow(...)
static const uintptr_t FN_TEXTGET     = 0x6A0050;  // CText::Get(char*)
static const uintptr_t FN_STATVALUE   = 0x558E40;  // CStats::GetStatValue(ushort)
static const uintptr_t SITE_STAMINA_TXT  = 0x589CD8;
static const uintptr_t SITE_STAMINA_STAT = 0x589D30;
static const uintptr_t KEY_STAT022    = 0x866BE4;  // "STAT022" (Stamina)
static const uintptr_t SCREEN_W       = 0xC17044;  // RsGlobal.maximumWidth  (int)
static const uintptr_t SCREEN_H       = 0xC17048;  // RsGlobal.maximumHeight (int)
static const int       OFF_SUBCLASS   = 0x594;

static uintptr_t kCont = 0x58FC2E;   // continue: check whether the player is in a vehicle
static uintptr_t kSkip = 0x58FC4C;   // skip: draw the radar normally

// ---------------- Configuration ----------------
static bool  g_enabled  = true;
static bool  g_useKey   = true;
static bool  g_usePad   = true;
static int   g_vk       = VK_TAB;
static int   g_padIdx   = 10;      // index (in shorts) in CControllerState; 10 = D-pad left
static bool  g_center   = false;   // MoveToTop: screen at the top (on foot and in a vehicle)
static float g_topMargin = 30.0f;  // distance from the top (640x448 units)
static float g_offsetX   = 0.0f;   // extra horizontal offset (640 units)
static bool  g_skillRow = true;

struct PadBtn { const char* name; int idx; };
static const PadBtn kPad[] = {
    {"L1",4},{"L2",5},{"R1",6},{"R2",7},{"UP",8},{"DOWN",9},{"LEFT",10},{"RIGHT",11},
    {"START",12},{"SELECT",13},{"SQUARE",14},{"TRIANGLE",15},{"CROSS",16},{"CIRCLE",17},
    {"L3",18},{"R3",19}
};

static char g_iniPath[MAX_PATH];

static int ParseKey(const char* s)
{
    if (!s[0]) return VK_TAB;
    if (!_stricmp(s, "TAB"))      return VK_TAB;
    if (!_stricmp(s, "CAPSLOCK")) return VK_CAPITAL;
    if (!_stricmp(s, "ENTER"))    return VK_RETURN;
    if (!_stricmp(s, "SPACE"))    return VK_SPACE;
    if (!_stricmp(s, "LSHIFT"))   return VK_LSHIFT;
    if (!_stricmp(s, "LCTRL"))    return VK_LCONTROL;
    if (!_stricmp(s, "LALT"))     return VK_LMENU;
    if ((s[0] == 'F' || s[0] == 'f') && s[1])
    {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (strlen(s) == 1)
    {
        char c = s[0];
        if (c >= 'a' && c <= 'z') c -= 32;
        return (unsigned char)c;
    }
    return (int)strtol(s, nullptr, 0);
}

static float ReadFloat(const char* sec, const char* key, float def)
{
    char buf[32], d[32];
    sprintf(d, "%g", def);
    GetPrivateProfileStringA(sec, key, d, buf, sizeof(buf), g_iniPath);
    return (float)atof(buf);
}

static void LoadConfig()
{
    char buf[64];
    g_enabled = GetPrivateProfileIntA("VehicleStats", "Enabled", 1, g_iniPath) != 0;
    g_useKey  = GetPrivateProfileIntA("VehicleStats", "UseKeyboard", 1, g_iniPath) != 0;
    g_usePad  = GetPrivateProfileIntA("VehicleStats", "UsePad", 1, g_iniPath) != 0;

    GetPrivateProfileStringA("VehicleStats", "KeyboardKey", "TAB", buf, sizeof(buf), g_iniPath);
    g_vk = ParseKey(buf);

    GetPrivateProfileStringA("VehicleStats", "PadButton", "LEFT", buf, sizeof(buf), g_iniPath);
    g_padIdx = 10;
    for (const PadBtn& b : kPad)
        if (!_stricmp(buf, b.name)) { g_padIdx = b.idx; break; }

    g_center    = GetPrivateProfileIntA("Layout", "MoveToTop", 0, g_iniPath) != 0;
    g_topMargin = ReadFloat("Layout", "TopMargin", 30.0f);
    g_offsetX   = ReadFloat("Layout", "OffsetX", 0.0f);
    g_skillRow  = GetPrivateProfileIntA("Layout", "SkillRow", 1, g_iniPath) != 0;
}

// ---------------- Log ----------------
static void Log(const char* fmt, ...)
{
    FILE* f = fopen("VehicleStats.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

// ---------------- Current drawing state ----------------
static bool        g_vehMode  = false;     // drawing the screen INSIDE a vehicle
static float       g_dx = 0.0f, g_dy = 0.0f;
static unsigned    g_skillId  = 0;         // 0 = keep Stamina
static const char* g_skillKey = nullptr;

static const char kKey160[] = "STAT160";   // driving skill
static const char kKey223[] = "STAT223";   // flying skill
static const char kKey229[] = "STAT229";   // motorcycle skill
static const char kKey230[] = "STAT230";   // cycling skill

static void ChooseSkill(void* veh)
{
    g_skillId = 0;
    g_skillKey = nullptr;
    if (!g_skillRow || !veh) return;
    int sub = *(volatile int*)((uint8_t*)veh + OFF_SUBCLASS);
    switch (sub)
    {
        case 0: case 1: case 2:         g_skillId = 160; g_skillKey = kKey160; break; // car, truck, quad
        case 3: case 4: case 7: case 8: g_skillId = 223; g_skillKey = kKey223; break; // heli/airplane
        case 9:                         g_skillId = 229; g_skillKey = kKey229; break; // motorcycle
        case 10:                        g_skillId = 230; g_skillKey = kKey230; break; // bicycle (BMX)
        default: break;                                                               // boat(5), train(6), trailer(11)
    }
}

// ---------------- Trigger ----------------
static bool GameFocused()
{
    HWND h = GetForegroundWindow();
    DWORD pid = 0;
    if (h) GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

static void* PlayerVehicle()
{
    return ((void* (__cdecl*)(int, bool))FIND_VEHICLE)(-1, false);
}

static bool Triggered(void* pad)
{
    if (g_useKey && GameFocused() && (GetAsyncKeyState(g_vk) & 0x8000)) return true;
    if (g_usePad && pad)
    {
        short v = ((volatile short*)pad)[g_padIdx];   // CPad::NewState is the first member
        if (v > 100) return true;
    }
    return false;
}

static int  g_logForce = 0, g_logVeh = 0;
static bool g_prevForce = false;

// Only applies INSIDE a vehicle: on foot, the screen opens only through the game's original button.
static bool __cdecl ForceNow()
{
    if (!g_enabled) return false;
    if (!PlayerVehicle()) return false;
    void* pad = ((void* (__cdecl*)(int))GET_PAD)(0);
    bool t = Triggered(pad);
    if (t != g_prevForce)
    {
        g_prevForce = t;
        if (g_logForce < 20)
        {
            ++g_logForce;
            Log("Trigger (vehicle) %s", t ? "PRESSED" : "released");
        }
    }
    return t;
}

// ---------------- CHud::Draw hooks ----------------
// Replaces "test ax,ax / je 58FC4C": if the game already said "yes", continue normally;
// if it said "no" but our trigger (in a vehicle) is active, continue as if it said "yes".
__declspec(naked) static void Stub1()
{
    __asm {
        test ax, ax
        jnz  cont
        call ForceNow
        test al, al
        jz   skip
    cont:
        jmp  dword ptr [kCont]
    skip:
        jmp  dword ptr [kSkip]
    }
}

// Call at 58FC32: tells the game that there is NO vehicle while the trigger is active,
// and prepares "inside vehicle" mode (position and skill row).
static void* __cdecl FindVehHook(int player, bool remote)
{
    void* v = ((void* (__cdecl*)(int, bool))FIND_VEHICLE)(player, remote);
    g_vehMode = false;
    g_dx = g_dy = 0.0f;
    g_skillId = 0;
    g_skillKey = nullptr;
    if (v && ForceNow())
    {
        g_vehMode = true;
        ChooseSkill(v);
        if (g_logVeh < 5)
        {
            ++g_logVeh;
            Log("In vehicle + trigger: statistics screen (subclass=%d, skill=%u).",
                *(volatile int*)((uint8_t*)v + OFF_SUBCLASS), g_skillId);
        }
        return nullptr;
    }
    return v;
}

// ---------------- Hooks inside CHud::DrawVitalStats ----------------
typedef void (__thiscall *DrawWindow_t)(void* self, float* rect, const char* key, unsigned char color,
                                        unsigned int backColor, unsigned char unused, unsigned char bg);
typedef void (__cdecl *Print_t)(float x, float y, unsigned short* text);
typedef void (__cdecl *Bar_t)(float x, float y, unsigned short w, unsigned char h, float progress,
                              signed char add, unsigned char pct, unsigned char border,
                              unsigned int fore, unsigned int back);
typedef const void* (__thiscall *TextGet_t)(void* self, const char* key);
typedef float (__cdecl *StatValue_t)(unsigned short id);

// DrawWindow draws the background/title and receives the rectangle: with MoveToTop = 1 we calculate here
// the offset that centers the window at the top and use the already-offset rectangle.
static void __fastcall HookWindow(void* self, void* /*edx*/, float* rect, const char* key, unsigned char color,
                                  unsigned int backColor, unsigned char unused, unsigned char bg)
{
    float r[4] = { rect[0], rect[1], rect[2], rect[3] };
    if (g_center)
    {
        float sw = (float)*(volatile int*)SCREEN_W;
        float sh = (float)*(volatile int*)SCREEN_H;
        float cx   = (rect[0] + rect[2]) * 0.5f;                 // left, bottom, right, top
        float topY = rect[1] < rect[3] ? rect[1] : rect[3];
        g_dx = sw * 0.5f - cx + g_offsetX * sw / 640.0f;
        g_dy = g_topMargin * sh / 448.0f - topY;
        r[0] += g_dx; r[2] += g_dx;
        r[1] += g_dy; r[3] += g_dy;
    }
    ((DrawWindow_t)FN_WINDOW)(self, r, key, color, backColor, unused, bg);
}

static void __cdecl HookPrint(float x, float y, unsigned short* text)
{
    if (g_center) { x += g_dx; y += g_dy; }
    ((Print_t)FN_PRINT)(x, y, text);
}

static void __cdecl HookBar(float x, float y, unsigned short w, unsigned char h, float progress,
                            signed char add, unsigned char pct, unsigned char border,
                            unsigned int fore, unsigned int back)
{
    if (g_center) { x += g_dx; y += g_dy; }
    ((Bar_t)FN_BAR)(x, y, w, h, progress, add, pct, border, fore, back);
}

// Stamina row text -> vehicle skill name
static const void* __fastcall HookTextGet(void* self, void* /*edx*/, const char* key)
{
    if (g_vehMode && g_skillKey && key == (const char*)KEY_STAT022) key = g_skillKey;
    return ((TextGet_t)FN_TEXTGET)(self, key);
}

// Stamina row value -> vehicle skill value
static float __cdecl HookStat(unsigned short id)
{
    if (g_vehMode && g_skillId && id == 0x16) id = (unsigned short)g_skillId;
    return ((StatValue_t)FN_STATVALUE)(id);
}

// ---------------- Initialization ----------------
static bool WriteJmp(uintptr_t at, void* target)
{
    DWORD old;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)at = 0xE9;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

static bool WriteCall(uintptr_t at, void* target)
{
    DWORD old;
    if (!VirtualProtect((void*)at, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(uint8_t*)at = 0xE8;
    *(int32_t*)(at + 1) = (int32_t)((uintptr_t)target - (at + 5));
    VirtualProtect((void*)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, 5);
    return true;
}

static uintptr_t CallTarget(uintptr_t site)
{
    uint8_t* c = (uint8_t*)site;
    if (c[0] != 0xE8) return 0;
    return site + 5 + (uintptr_t)(intptr_t)(*(int32_t*)(c + 1));
}

// Count "call <target>" instructions inside DrawVitalStats
static int CountCalls(uintptr_t target)
{
    int n = 0;
    for (uintptr_t a = VS_LO; a < VS_HI - 4; ++a)
        if (*(uint8_t*)a == 0xE8 && CallTarget(a) == target) ++n;
    return n;
}

static int PatchCalls(uintptr_t target, void* hook)
{
    int n = 0;
    for (uintptr_t a = VS_LO; a < VS_HI - 4; ++a)
        if (*(uint8_t*)a == 0xE8 && CallTarget(a) == target)
            if (WriteCall(a, hook)) ++n;
    return n;
}

BOOL APIENTRY DllMain(HMODULE hm, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hm);

    FILE* f = fopen("VehicleStats.log", "w");
    if (f) fclose(f);

    GetModuleFileNameA(hm, g_iniPath, MAX_PATH);
    char* dot = strrchr(g_iniPath, '.');
    if (dot) strcpy(dot, ".ini");

    if (*(uint32_t*)VERSION_ADDR != VERSION_OK)
    {
        Log("Unrecognized exe version (expected 1.0 US). Mod disabled.");
        return TRUE;
    }

    LoadConfig();
    Log("Config: enabled=%d key=%d(vk=0x%02X) pad=%d(idx=%d) moveToTop=%d top=%.1f skill=%d",
        g_enabled, g_useKey, g_vk, g_usePad, g_padIdx, g_center, g_topMargin, g_skillRow);

    // Check the original bytes before modifying anything
    static const uint8_t expectTest[5] = { 0x66, 0x85, 0xC0, 0x74, 0x1E };
    uint8_t* t = (uint8_t*)TEST_SITE;

    if (memcmp(t, expectTest, 5) != 0)
    {
        Log("Bytes at 0x%08X differ from expected (%02X %02X %02X %02X %02X). "
            "Another mod may have modified this area. Mod disabled.",
            (unsigned)TEST_SITE, t[0], t[1], t[2], t[3], t[4]);
        return TRUE;
    }
    if (CallTarget(VEH_CALL_SITE) != FIND_VEHICLE)
    {
        Log("Call at 0x%08X does not point to FindPlayerVehicle (target=0x%08X). Mod disabled.",
            (unsigned)VEH_CALL_SITE, (unsigned)CallTarget(VEH_CALL_SITE));
        return TRUE;
    }

    bool ok1 = WriteCall(VEH_CALL_SITE, (void*)&FindVehHook);
    bool ok2 = WriteJmp(TEST_SITE, (void*)&Stub1);
    Log("Patch: vehicle check=%s, button check=%s",
        ok1 ? "OK" : "FAILED", ok2 ? "OK" : "FAILED");

    // ---- Layout (center at the top) ----
    if (g_center)
    {
        int nPrint = CountCalls(FN_PRINT), nBar = CountCalls(FN_BAR), nWin = CountCalls(FN_WINDOW);
        if (nPrint == 8 && nBar == 6 && nWin == 2)
        {
            int a = PatchCalls(FN_WINDOW, (void*)&HookWindow);
            int b = PatchCalls(FN_PRINT,  (void*)&HookPrint);
            int c = PatchCalls(FN_BAR,    (void*)&HookBar);
            Log("Layout: window=%d text=%d bars=%d calls redirected", a, b, c);
        }
        else
        {
            g_center = false;
            Log("Layout NOT applied: unexpected count (text=%d bars=%d window=%d; expected 8/6/2).",
                nPrint, nBar, nWin);
        }
    }

    // ---- Skill row (replaces Stamina) ----
    if (g_skillRow)
    {
        if (CallTarget(SITE_STAMINA_TXT) == FN_TEXTGET && CallTarget(SITE_STAMINA_STAT) == FN_STATVALUE)
        {
            bool a = WriteCall(SITE_STAMINA_TXT,  (void*)&HookTextGet);
            bool b = WriteCall(SITE_STAMINA_STAT, (void*)&HookStat);
            Log("Skill row: text=%s value=%s", a ? "OK" : "FAILED", b ? "OK" : "FAILED");
        }
        else
        {
            g_skillRow = false;
            Log("Skill row NOT applied: stamina calls differ from expected.");
        }
    }
    return TRUE;
}
