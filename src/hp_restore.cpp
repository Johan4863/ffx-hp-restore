/**
 * FFX HD Remaster - HP Restore After Battle
 * Modul dla FF10 Module Loader (dinput8.dll).
 * Logika uruchamiana z DllMain - gwarantowane wywolanie przez loader.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <thread>
#include <atomic>

// ─── Konfiguracja ─────────────────────────────────────────────────────────────

// Offsety struktury postaci - znane z Cheat Engine / spolecznosci FFX PC Steam
namespace CharOffset {
    constexpr DWORD current_hp = 0x04;
    constexpr DWORD max_hp     = 0x08;
    constexpr DWORD char_id    = 0x00;  // 0xFF = pusty slot
}

constexpr DWORD CHARACTER_STRUCT_SIZE = 0x1C0;
constexpr int   MAX_PARTY_MEMBERS     = 7;

// Statyczny wskaznik do tablicy party w FFX.exe
// Pointer chain: [FFX.exe + BASE_PTR_OFFSET] -> ptr -> ptr+CHAIN_OFFSET -> party[]
constexpr DWORD BASE_PTR_OFFSET  = 0xD2EB5C;
constexpr DWORD CHAIN_OFFSET     = 0x10;

// Flaga stanu walki - bit 0x01 = jestesmy w battle
constexpr DWORD BATTLE_FLAG_OFFSET = 0xD2EB50;

// Polling interval (ms)
constexpr DWORD POLL_MS = 300;

// ─── Globals ──────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{ false };
static HMODULE g_self = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void log(const char* msg) {
    OutputDebugStringA("[HP-Restore] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

template<typename T>
static bool safe_read(uintptr_t addr, T& out) {
    __try {
        out = *reinterpret_cast<T*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template<typename T>
static bool safe_write(uintptr_t addr, T val) {
    __try {
        *reinterpret_cast<T*>(addr) = val;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uintptr_t resolve_party_base(uintptr_t module_base) {
    uintptr_t ptr1 = 0;
    if (!safe_read(module_base + BASE_PTR_OFFSET, ptr1) || ptr1 == 0) return 0;
    uintptr_t ptr2 = 0;
    if (!safe_read(ptr1 + CHAIN_OFFSET, ptr2) || ptr2 == 0) return 0;
    return ptr2;
}

static bool is_in_battle(uintptr_t module_base) {
    uint8_t flag = 0;
    safe_read(module_base + BATTLE_FLAG_OFFSET, flag);
    return (flag & 0x01) != 0;
}

static void restore_all_hp(uintptr_t module_base) {
    uintptr_t party_base = resolve_party_base(module_base);
    if (party_base == 0) {
        log("Nie udalo sie rozwiazac wskaznika party");
        return;
    }

    int restored = 0;
    for (int i = 0; i < MAX_PARTY_MEMBERS; ++i) {
        uintptr_t addr = party_base + (uintptr_t)i * CHARACTER_STRUCT_SIZE;

        uint8_t char_id = 0xFF;
        if (!safe_read(addr + CharOffset::char_id, char_id)) continue;
        if (char_id == 0xFF) continue;

        int32_t max_hp = 0;
        if (!safe_read(addr + CharOffset::max_hp, max_hp) || max_hp <= 0) continue;

        int32_t cur_hp = 0;
        if (!safe_read(addr + CharOffset::current_hp, cur_hp)) continue;

        if (cur_hp < max_hp) {
            safe_write(addr + CharOffset::current_hp, max_hp);
            ++restored;
        }
    }

    if (restored > 0) {
        char buf[64];
        wsprintfA(buf, "Przywrocono HP dla %d postaci", restored);
        log(buf);
    } else {
        log("Brak postaci do przywrocenia HP (juz pelne lub blad wskaznika)");
    }
}

// ─── Watek glowny ─────────────────────────────────────────────────────────────

static DWORD WINAPI mod_thread(LPVOID) {
    log("Watek HP Restore uruchomiony");

    // Czekaj az gra sie zaladuje
    Sleep(4000);

    // Znajdz modul FFX.exe
    HMODULE ffx = GetModuleHandleA("FFX.exe");
    if (!ffx) {
        log("BLAD: Nie znaleziono FFX.exe - mod nieaktywny");
        return 0;
    }

    uintptr_t module_base = reinterpret_cast<uintptr_t>(ffx);
    char buf[128];
    wsprintfA(buf, "FFX.exe zaladowany pod adresem: 0x%08X", (unsigned)module_base);
    log(buf);
    log("Monitorowanie walki aktywne");

    bool was_in_battle = false;

    while (g_running) {
        bool in_battle = is_in_battle(module_base);

        // Edge: przejscie walka -> mapa
        if (was_in_battle && !in_battle) {
            log("Walka zakonczona - przywracam HP...");
            Sleep(600);  // czekaj az gra zaaktualizuje statystyki
            restore_all_hp(module_base);
        }

        was_in_battle = in_battle;
        Sleep(POLL_MS);
    }

    log("Watek HP Restore zatrzymany");
    return 0;
}

// ─── Eksporty dla Module Loadera ───────────────────────────────────────────────
// Loader wywoluje GetModuleInfo zeby wyswietlic nazwe w hook.log.
// Struktura musi byc zgodna z tym czego loader oczekuje.

struct ModuleInfo {
    const char* name;
    const char* author;
    int version_major;
    int version_minor;
    int version_patch;
};

static ModuleInfo s_info = {
    "FFX HP Restore After Battle",
    "Jakub",
    1, 0, 0
};

extern "C" {

// Loader wywoluje to zeby pobrac nazwe/wersje modulu
__declspec(dllexport) ModuleInfo* __cdecl GetModuleInfo() {
    return &s_info;
}

// Niektorzy loaderzy uzywaja tez tych nazw - eksportujemy na wszelki wypadek
__declspec(dllexport) void __cdecl OnModuleLoad() {
    log("OnModuleLoad wywolany");
}

__declspec(dllexport) void __cdecl OnModuleUnload() {
    g_running = false;
}

__declspec(dllexport) void __cdecl Initialize() {
    log("Initialize wywolany");
}

} // extern "C"

// ─── DllMain ──────────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_self = hModule;
        g_running = true;
        // Uruchom watek - DllMain jest wywolywane przez loader przy LoadLibrary
        CreateThread(nullptr, 0, mod_thread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        g_running = false;
        break;
    }
    return TRUE;
}
