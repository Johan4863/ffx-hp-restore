/**
 * ffx_calibrate.cpp
 * =================
 * Narzedzie diagnostyczne do weryfikacji offsetow pamieci FFX.
 * Kompiluj jako ODDZIELNY EXEK (nie DLL), 32-bit x86.
 * Uruchamiaj gdy FFX jest juz odpalone (attach do procesu).
 *
 * Kompilacja:
 *   cl /EHsc /W3 ffx_calibrate.cpp /link kernel32.lib user32.lib /OUT:ffx_calibrate.exe
 *
 * Uzycie:
 *   1. Uruchom FFX
 *   2. Wejdz w walke
 *   3. Pozwol postaci stracic HP (nie zero - po prostu mniej niz max)
 *   4. Uruchom ffx_calibrate.exe
 *   5. Narzedzie wypisze kandydatow na adresy HP oraz sugerowan offsets
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <vector>
#include <cstdint>
#include <string>

#pragma comment(lib, "psapi.lib")

// ─── Znajdz PID procesu po nazwie ─────────────────────────────────────────────

static DWORD find_process(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

// ─── Skan wzorca w pamieci obcego procesu ─────────────────────────────────────

struct FoundHP {
    uintptr_t addr_current_hp;
    int32_t   current_val;
    int32_t   max_val;        // zakładamy ze max jest na +0x04
    uintptr_t module_offset;  // offset od bazy FFX.exe
};

static std::vector<FoundHP> scan_for_hp(HANDLE hProc, uintptr_t module_base, size_t module_size) {
    std::vector<FoundHP> results;

    // Czytaj caly modul do bufora lokalnego
    std::vector<uint8_t> buf(module_size);
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(module_base), buf.data(), module_size, &bytes_read))
        return results;

    printf("  Wczytano %zu bajtow z modulu FFX.exe\n", bytes_read);

    // Szukaj par (current_hp, max_hp) gdzie:
    //   0 < current_hp < max_hp <= 99999
    //   max_hp >= 100 (filtruj smieci)
    for (size_t i = 0; i + 8 <= bytes_read; i += 4) {
        int32_t cur = *reinterpret_cast<int32_t*>(&buf[i]);
        int32_t mx  = *reinterpret_cast<int32_t*>(&buf[i + 4]);

        if (cur > 0 && mx > 0 && cur < mx && mx >= 100 && mx <= 99999 && cur != mx) {
            FoundHP h;
            h.addr_current_hp = module_base + i;
            h.current_val     = cur;
            h.max_val         = mx;
            h.module_offset   = i;
            results.push_back(h);
        }
    }

    return results;
}

// ─── Main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("=== FFX HD Remaster - Kalibracja offsetow HP ===\n\n");

    // Znajdz FFX.exe
    DWORD pid = find_process(L"FFX.exe");
    if (pid == 0) {
        printf("BLAD: Nie znaleziono procesu FFX.exe!\n");
        printf("Upewnij sie ze gra jest uruchomiona.\n");
        system("pause");
        return 1;
    }

    printf("Znaleziono FFX.exe, PID = %lu\n", pid);

    // Otwieramy proces z prawem odczytu pamieci
    HANDLE hProc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) {
        printf("BLAD: Nie mozna otworzyc procesu (brak uprawnien?). Sprobuj jako Administrator.\n");
        system("pause");
        return 1;
    }

    // Znajdz modul FFX.exe w przestrzeni adresowej procesu
    HMODULE modules[256];
    DWORD   cb_needed = 0;
    uintptr_t ffx_base = 0;
    SIZE_T    ffx_size = 0;

    if (EnumProcessModules(hProc, modules, sizeof(modules), &cb_needed)) {
        DWORD mod_count = cb_needed / sizeof(HMODULE);
        for (DWORD i = 0; i < mod_count; ++i) {
            wchar_t mod_name[MAX_PATH];
            if (GetModuleFileNameExW(hProc, modules[i], mod_name, MAX_PATH)) {
                if (wcsstr(mod_name, L"FFX.exe") != nullptr) {
                    MODULEINFO mi{};
                    GetModuleInformation(hProc, modules[i], &mi, sizeof(mi));
                    ffx_base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                    ffx_size = mi.SizeOfImage;
                    printf("FFX.exe baza: 0x%08X, rozmiar: %zu bajtow\n", (unsigned)ffx_base, ffx_size);
                    break;
                }
            }
        }
    }

    if (ffx_base == 0) {
        printf("BLAD: Nie znaleziono modulu FFX.exe w procesie.\n");
        CloseHandle(hProc);
        system("pause");
        return 1;
    }

    printf("\nSkanuje pamiec... (to moze chwile zajac)\n");
    auto candidates = scan_for_hp(hProc, ffx_base, ffx_size);

    printf("\nZnaleziono %zu kandydatow na pary (currentHP, maxHP):\n\n", candidates.size());

    if (candidates.empty()) {
        printf("Brak kandydatow! Upewnij sie ze:\n");
        printf("  1. Jestes w walce\n");
        printf("  2. Jakis czlonek party ma HP < MAX HP\n");
        printf("  3. Nie uzyto buitl-in cheata 'Max HP'\n");
    } else {
        printf("  %-12s %-10s %-10s %-14s\n", "Adres", "CurHP", "MaxHP", "Offset od bazy");
        printf("  %-12s %-10s %-10s %-14s\n", "------------", "----------", "----------", "--------------");

        int show = 0;
        for (auto& h : candidates) {
            if (show++ > 30) {
                printf("  ... i %zu wiecej. Skup sie na wartosciach odpowiadajacych aktualnym HP postaci.\n",
                       candidates.size() - 30);
                break;
            }
            printf("  0x%08X  %-10d %-10d 0x%08X\n",
                (unsigned)h.addr_current_hp, h.current_val, h.max_val,
                (unsigned)h.module_offset);
        }

        printf("\n=== INSTRUKCJA KALIBRACJI ===\n");
        printf("1. Porownaj wartosci CurHP/MaxHP z aktualnymi HP postaci w grze.\n");
        printf("2. Gdy znajdziesz dopasowania - zapamietaj ich adresy.\n");
        printf("3. Offset od bazy = wartosc do wpisania w BASE_PTR_OFFSET w hp_restore.cpp.\n");
        printf("4. Jesli adresy sa w stalym miejscu (nie zmieniaja sie po restarcie gry),\n");
        printf("   mozna uzyc ich bezposrednio bez lancucha wskaznikow.\n");
        printf("5. Jesli sie zmieniaja - potrzebujesz pointer scan w Cheat Engine.\n");
    }

    CloseHandle(hProc);

    printf("\nGotowe. Nacisnij dowolny klawisz...\n");
    system("pause");
    return 0;
}
