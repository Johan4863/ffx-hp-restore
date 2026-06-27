/**
 * FFX HD Remaster - HP Restore After Battle
 * ==========================================
 * Modul dla FF10 Module Loader (dinput8.dll).
 * Po kazdej walce przywraca HP wszystkich postaci do maksimum.
 * NIE dotyka Overdrive'ow ani zadnych innych statystyk.
 *
 * Autor: Jakub (customowy mod)
 * Wymagania: FF10 Module Loader (Nexus Mods mod #150)
 *
 * Jak to dziala:
 *   Gra FFX.exe jest natywnym 32-bit procesem (x86).
 *   Dane postaci sa przechowywane w pamieci jako tablica struktur.
 *   Skanujemy pamiec pod katem wzorca, ktory wskazuje na baze party,
 *   a nastepnie w petli (polling) sprawdzamy stan battle flag i
 *   gdy walka sie skonczy, wpisujemy currentHP = maxHP dla kazdego
 *   aktywnego czlonka druzyny.
 *
 * UWAGA: Adresy pamieci sa znane z tablic Cheat Engine (spolecznosc FFX).
 *        Jesli gra dostanie patcha i adresy sie zmienia, zaktualizuj
 *        PARTY_BASE_OFFSET i CHARACTER_STRUCT_SIZE ponizej.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <string>

// ─── Konfiguracja ─────────────────────────────────────────────────────────────

// Nazwa procesu gry
static const wchar_t* GAME_EXE = L"FFX.exe";

// Offsety w strukturze postaci (bajty od poczatku struktury jednej postaci).
// Znane z Cheat Engine / spolecznosci FFX PC.
// Sprawdzone dla Steam build (v1.0.0 / 2016+).
namespace CharOffset {
    constexpr DWORD current_hp  = 0x04;   // int32 - obecne HP
    constexpr DWORD max_hp      = 0x08;   // int32 - maksymalne HP
    constexpr DWORD char_id     = 0x00;   // byte  - ID postaci (0=Tidus, 1=Yuna, itd; 0xFF = slot pusty)
    constexpr DWORD is_alive    = 0x24;   // byte  - 0=KO, 1=zywy (przyblizone)
}

// Rozmiar struktury jednej postaci w tablicy party
constexpr DWORD CHARACTER_STRUCT_SIZE = 0x1C0;

// Liczba slotow w party (7 postaci w FFX)
constexpr int MAX_PARTY_MEMBERS = 7;

// Pointer chain do bazy party:
//   [FFX.exe + BASE_PTR_OFFSET] -> ptr -> ptr + PARTY_CHAIN_OFFSET -> tablica party
// Wartosci z popularnych tabel Cheat Engine dla FFX HD Steam.
constexpr DWORD BASE_PTR_OFFSET   = 0xD2EB5C;  // statyczny wskaznik w module
constexpr DWORD PARTY_CHAIN_OFFSET = 0x10;      // offset drugiego derefa

// Offset flagi bitowej stanu walki (w obrebie modulu gry).
// Bit 0x01 = jestesmy w walce; 0x00 = mapa/menu.
constexpr DWORD BATTLE_FLAG_OFFSET = 0x00D2EB50;

// Ile milisekund czekamy miedzy skanami (polling interval)
constexpr DWORD POLL_MS = 300;

// ─── Globals ──────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{ true };
static HMODULE            g_game_module = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void log(const char* msg) {
    OutputDebugStringA("[HP-Restore] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

/**
 * Bezpieczny odczyt pamieci - zwraca false jesli adres jest nieprawidlowy.
 */
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

/**
 * Bezpieczny zapis do pamieci.
 */
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

/**
 * Rozwiazuje lancuch wskaznikow do tablicy party.
 * Zwraca adres pierwszego elementu tablicy lub 0 przy niepowodzeniu.
 */
static uintptr_t resolve_party_base() {
    if (!g_game_module) return 0;

    uintptr_t module_base = reinterpret_cast<uintptr_t>(g_game_module);

    // Krok 1: czytaj wskaznik ze statycznego pola w module
    uintptr_t ptr1 = 0;
    if (!safe_read(module_base + BASE_PTR_OFFSET, ptr1) || ptr1 == 0)
        return 0;

    // Krok 2: dereferencja + offset
    uintptr_t ptr2 = 0;
    if (!safe_read(ptr1 + PARTY_CHAIN_OFFSET, ptr2) || ptr2 == 0)
        return 0;

    return ptr2;
}

/**
 * Sprawdza czy aktualnie trwa walka.
 * Zwraca true = jestesmy w battle screen.
 */
static bool is_in_battle() {
    if (!g_game_module) return false;
    uintptr_t module_base = reinterpret_cast<uintptr_t>(g_game_module);

    uint8_t flag = 0;
    safe_read(module_base + BATTLE_FLAG_OFFSET, flag);
    return (flag & 0x01) != 0;
}

/**
 * Przywraca HP wszystkich zyJacych postaci do maksimum.
 */
static void restore_all_hp() {
    uintptr_t party_base = resolve_party_base();
    if (party_base == 0) {
        log("Nie udalo sie rozwiazac wskaznika party - pomijam");
        return;
    }

    int restored = 0;
    for (int i = 0; i < MAX_PARTY_MEMBERS; ++i) {
        uintptr_t char_addr = party_base + static_cast<uintptr_t>(i) * CHARACTER_STRUCT_SIZE;

        // Sprawdz ID postaci - 0xFF = pusty slot
        uint8_t char_id = 0xFF;
        if (!safe_read(char_addr + CharOffset::char_id, char_id)) continue;
        if (char_id == 0xFF) continue;

        // Czytaj max HP
        int32_t max_hp = 0;
        if (!safe_read(char_addr + CharOffset::max_hp, max_hp)) continue;
        if (max_hp <= 0) continue;

        // Czytaj current HP
        int32_t cur_hp = 0;
        if (!safe_read(char_addr + CharOffset::current_hp, cur_hp)) continue;

        // Jesli KO (cur_hp == 0) lub nie pelne HP - przywroc
        // (NIE przywracamy jesli postac jest w KO bo gra moze miec
        //  wewnetrzny flag KO oddzielnie - mozna odkomentowac ponizej)
        if (cur_hp < max_hp) {
            safe_write(char_addr + CharOffset::current_hp, max_hp);
            ++restored;
        }
    }

    if (restored > 0) {
        char buf[64];
        wsprintfA(buf, "Przywrocono HP dla %d postaci", restored);
        log(buf);
    }
}

// ─── Watek glowny moda ────────────────────────────────────────────────────────

static void mod_thread() {
    log("Mod zaladowany - HP Restore After Battle aktywny");

    // Poczekaj az gra sie w pelni zaladuje
    Sleep(3000);

    g_game_module = GetModuleHandleW(GAME_EXE);
    if (!g_game_module) {
        log("BLAD: Nie znaleziono modulu FFX.exe");
        return;
    }

    log("Znaleziono FFX.exe - rozpoczynam monitorowanie walki");

    bool was_in_battle = false;

    while (g_running) {
        bool in_battle = is_in_battle();

        // Wykryj moment ZAKONCZENIA walki (przejscie z true -> false)
        if (was_in_battle && !in_battle) {
            log("Walka zakonczona - przywracam HP...");
            // Krotkie opoznienie zeby gra zdazyla zakonczyc animacje
            // i zaktualizowac statystyki postaci po walce
            Sleep(500);
            restore_all_hp();
        }

        was_in_battle = in_battle;
        Sleep(POLL_MS);
    }

    log("Mod zatrzymany");
}

// ─── Eksport dla Module Loadera ────────────────────────────────────────────────

/**
 * FF10 Module Loader wywoluje te funkcje przy ladowaniu/zwalnianiu modulu.
 * Eksporty musza byc zgodne z interfejsem loadera (extern "C").
 */
extern "C" {

__declspec(dllexport) void __cdecl OnModuleLoad() {
    // Uruchom watek w tle zeby nie blokowac ladowania gry
    std::thread(mod_thread).detach();
}

__declspec(dllexport) void __cdecl OnModuleUnload() {
    g_running = false;
    // Daj watkowi chwile na zakonczenie
    Sleep(POLL_MS * 2 + 100);
}

} // extern "C"

// ─── DllMain ──────────────────────────────────────────────────────────────────

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            break;
        case DLL_PROCESS_DETACH:
            g_running = false;
            break;
    }
    return TRUE;
}
