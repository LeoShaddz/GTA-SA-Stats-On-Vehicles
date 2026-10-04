// VehicleStats.cpp - GTA San Andreas 1.0 US (inclui exe "compact"/Hoodlum)
//
// Tela de estatisticas rapidas tambem dentro de veiculos:
//  - Dentro do veiculo: abre com a tecla do .ini (padrao TAB) ou com o D-pad ESQUERDO
//    (le o CPad do proprio jogo, entao funciona com GInput). A pe: igual ao jogo original.
//  - Posicao: por padrao a tela fica no lugar original (a pe e no veiculo). Com
//    MoveToTop = 1 no .ini ela vai para o topo, centralizada, a pe E no veiculo.
//  - Dentro do veiculo, a linha "Resistencia" vira a habilidade do veiculo atual, com a
//    mesma barra/valor da estatistica do jogo:
//        carro/caminhao/quad -> habilidade de conducao  (stat 160, texto STAT160)
//        moto (subclasse 9)  -> habilidade de moto      (stat 229, texto STAT229)
//        bicicleta (BMX, 10) -> habilidade de ciclismo  (stat 230, texto STAT230)
//        aviao/helicoptero   -> habilidade de voo       (stat 223, texto STAT223)
//        barco/trem/trailer  -> continua "Resistencia"
//
// Tudo confirmado no gta_sa.exe 1.0 US:
//   CHud::Draw (trecho original):
//     58FC24  call CPad::GetDisplayVitalStats
//     58FC29  test ax,ax / je 58FC4C        <- estes 5 bytes viram um JMP para Stub1
//     58FC32  call FindPlayerVehicle        <- redirecionada para FindVehHook
//   CHud::DrawVitalStats = 0x589650..0x58A158. Dentro dela:
//     CMenuManager::DrawWindow 0x573EE0 (2 chamadas), CFont::PrintString 0x71A700 (8),
//     CSprite2d::DrawBarChart 0x728640 (6): recebem o deslocamento para centralizar.
//     Linha de estamina: texto em 0x589CD8 (CText::Get, chave STAT022) e valor em
//     0x589D30 (CStats::GetStatValue, id 0x16).
//   m_nVehicleSubClass = veiculo+0x594: 0 carro, 1 monster, 2 quad, 3 heli, 4 aviao, 5 barco,
//     6 trem, 7 heli falso, 8 aviao falso, 9 moto, 10 bicicleta (BMX), 11 trailer.
//
// Compilar como Win32 (x86), com MSVC. A saida deve se chamar VehicleStats.asi
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
static const uintptr_t KEY_STAT022    = 0x866BE4;  // "STAT022" (Resistencia)
static const uintptr_t SCREEN_W       = 0xC17044;  // RsGlobal.maximumWidth  (int)
static const uintptr_t SCREEN_H       = 0xC17048;  // RsGlobal.maximumHeight (int)
static const int       OFF_SUBCLASS   = 0x594;

static uintptr_t kCont = 0x58FC2E;   // continua: verifica se esta em veiculo
static uintptr_t kSkip = 0x58FC4C;   // pula: desenha o radar normalmente

// ---------------- Configuracao ----------------
static bool  g_enabled  = true;
static bool  g_useKey   = true;
static bool  g_usePad   = true;
static int   g_vk       = VK_TAB;
static int   g_padIdx   = 10;      // indice (em shorts) no CControllerState; 10 = D-pad esquerdo
static bool  g_center   = false;   // MoveToTop: tela no topo (a pe e no veiculo)
static float g_topMargin = 30.0f;  // distancia do topo (unidades 640x448)
static float g_offsetX   = 0.0f;   // deslocamento horizontal extra (unidades 640)
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

// ---------------- Estado do desenho atual ----------------
static bool        g_vehMode  = false;     // desenhando a tela DENTRO de um veiculo
static float       g_dx = 0.0f, g_dy = 0.0f;
static unsigned    g_skillId  = 0;         // 0 = manter Resistencia
static const char* g_skillKey = nullptr;

static const char kKey160[] = "STAT160";   // habilidade de conducao
static const char kKey223[] = "STAT223";   // habilidade de voo
static const char kKey229[] = "STAT229";   // habilidade de moto
static const char kKey230[] = "STAT230";   // habilidade de bicicleta

static void ChooseSkill(void* veh)
{
    g_skillId = 0;
    g_skillKey = nullptr;
    if (!g_skillRow || !veh) return;
    int sub = *(volatile int*)((uint8_t*)veh + OFF_SUBCLASS);
    switch (sub)
    {
        case 0: case 1: case 2:         g_skillId = 160; g_skillKey = kKey160; break; // carro, caminhao, quad
        case 3: case 4: case 7: case 8: g_skillId = 223; g_skillKey = kKey223; break; // heli/aviao
        case 9:                         g_skillId = 229; g_skillKey = kKey229; break; // moto
        case 10:                        g_skillId = 230; g_skillKey = kKey230; break; // bicicleta (BMX)
        default: break;                                                               // barco(5), trem(6), trailer(11)
    }
}

// ---------------- Gatilho ----------------
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
        short v = ((volatile short*)pad)[g_padIdx];   // CPad::NewState e o primeiro membro
        if (v > 100) return true;
    }
    return false;
}

static int  g_logForce = 0, g_logVeh = 0;
static bool g_prevForce = false;

// So vale DENTRO de um veiculo: a pe, a tela abre so pelo botao original do jogo.
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
            Log("Gatilho (veiculo) %s", t ? "PRESSIONADO" : "solto");
        }
    }
    return t;
}

// ---------------- Hooks do CHud::Draw ----------------
// Substitui "test ax,ax / je 58FC4C": se o jogo ja disse "sim", segue normal;
// se disse "nao" mas o nosso gatilho (em veiculo) esta ativo, segue como se fosse "sim".
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

// Chamada em 58FC32: diz ao jogo que NAO ha veiculo enquanto o gatilho estiver ativo,
// e prepara o modo "dentro do veiculo" (posicao e linha de habilidade).
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
            Log("Em veiculo + gatilho: tela de estatisticas (subclasse=%d, habilidade=%u).",
                *(volatile int*)((uint8_t*)v + OFF_SUBCLASS), g_skillId);
        }
        return nullptr;
    }
    return v;
}

// ---------------- Hooks dentro do CHud::DrawVitalStats ----------------
typedef void (__thiscall *DrawWindow_t)(void* self, float* rect, const char* key, unsigned char color,
                                        unsigned int backColor, unsigned char unused, unsigned char bg);
typedef void (__cdecl *Print_t)(float x, float y, unsigned short* text);
typedef void (__cdecl *Bar_t)(float x, float y, unsigned short w, unsigned char h, float progress,
                              signed char add, unsigned char pct, unsigned char border,
                              unsigned int fore, unsigned int back);
typedef const void* (__thiscall *TextGet_t)(void* self, const char* key);
typedef float (__cdecl *StatValue_t)(unsigned short id);

// DrawWindow desenha o fundo/titulo e recebe o retangulo: com MoveToTop = 1 calculamos aqui
// o deslocamento que centraliza a janela no topo e usamos o retangulo ja deslocado.
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

// Texto da linha de estamina -> nome da habilidade do veiculo
static const void* __fastcall HookTextGet(void* self, void* /*edx*/, const char* key)
{
    if (g_vehMode && g_skillKey && key == (const char*)KEY_STAT022) key = g_skillKey;
    return ((TextGet_t)FN_TEXTGET)(self, key);
}

// Valor da linha de estamina -> valor da habilidade do veiculo
static float __cdecl HookStat(unsigned short id)
{
    if (g_vehMode && g_skillId && id == 0x16) id = (unsigned short)g_skillId;
    return ((StatValue_t)FN_STATVALUE)(id);
}

// ---------------- Inicializacao ----------------
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

// Conta as chamadas "call <target>" dentro de DrawVitalStats
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
        Log("Versao do exe nao reconhecida (esperado 1.0 US). Mod desativado.");
        return TRUE;
    }

    LoadConfig();
    Log("Config: enabled=%d key=%d(vk=0x%02X) pad=%d(idx=%d) moverParaTopo=%d topo=%.1f habilidade=%d",
        g_enabled, g_useKey, g_vk, g_usePad, g_padIdx, g_center, g_topMargin, g_skillRow);

    // Confere os bytes originais antes de mexer em qualquer coisa
    static const uint8_t expectTest[5] = { 0x66, 0x85, 0xC0, 0x74, 0x1E };
    uint8_t* t = (uint8_t*)TEST_SITE;

    if (memcmp(t, expectTest, 5) != 0)
    {
        Log("Bytes em 0x%08X diferentes do esperado (%02X %02X %02X %02X %02X). "
            "Outro mod pode ter alterado essa area. Mod desativado.",
            (unsigned)TEST_SITE, t[0], t[1], t[2], t[3], t[4]);
        return TRUE;
    }
    if (CallTarget(VEH_CALL_SITE) != FIND_VEHICLE)
    {
        Log("Chamada em 0x%08X nao aponta para FindPlayerVehicle (alvo=0x%08X). Mod desativado.",
            (unsigned)VEH_CALL_SITE, (unsigned)CallTarget(VEH_CALL_SITE));
        return TRUE;
    }

    bool ok1 = WriteCall(VEH_CALL_SITE, (void*)&FindVehHook);
    bool ok2 = WriteJmp(TEST_SITE, (void*)&Stub1);
    Log("Patch: verificacao de veiculo=%s, teste do botao=%s",
        ok1 ? "OK" : "FALHOU", ok2 ? "OK" : "FALHOU");

    // ---- Layout (centralizar no topo) ----
    if (g_center)
    {
        int nPrint = CountCalls(FN_PRINT), nBar = CountCalls(FN_BAR), nWin = CountCalls(FN_WINDOW);
        if (nPrint == 8 && nBar == 6 && nWin == 2)
        {
            int a = PatchCalls(FN_WINDOW, (void*)&HookWindow);
            int b = PatchCalls(FN_PRINT,  (void*)&HookPrint);
            int c = PatchCalls(FN_BAR,    (void*)&HookBar);
            Log("Layout: janela=%d texto=%d barras=%d chamadas redirecionadas", a, b, c);
        }
        else
        {
            g_center = false;
            Log("Layout NAO aplicado: contagem inesperada (texto=%d barras=%d janela=%d; esperado 8/6/2).",
                nPrint, nBar, nWin);
        }
    }

    // ---- Linha de habilidade (substitui Resistencia) ----
    if (g_skillRow)
    {
        if (CallTarget(SITE_STAMINA_TXT) == FN_TEXTGET && CallTarget(SITE_STAMINA_STAT) == FN_STATVALUE)
        {
            bool a = WriteCall(SITE_STAMINA_TXT,  (void*)&HookTextGet);
            bool b = WriteCall(SITE_STAMINA_STAT, (void*)&HookStat);
            Log("Linha de habilidade: texto=%s valor=%s", a ? "OK" : "FALHOU", b ? "OK" : "FALHOU");
        }
        else
        {
            g_skillRow = false;
            Log("Linha de habilidade NAO aplicada: chamadas da estamina diferentes do esperado.");
        }
    }
    return TRUE;
}
