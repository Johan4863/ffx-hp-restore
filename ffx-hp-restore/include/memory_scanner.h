/**
 * memory_scanner.h
 * ================
 * Pomocnicze narzedzia do skanowania pamieci procesow.
 * Uzywane do weryfikacji/aktualizacji offsetow jesli gra dostanie patch.
 *
 * Uzycie (w oddzielnym procesie diagnostycznym):
 *   auto results = PatternScanner::scan_process(pid, pattern_bytes, mask);
 */

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>

namespace PatternScanner {

struct ScanResult {
    uintptr_t address;
    std::string region_info;
};

/**
 * Skanuje pamiec procesu pod katem wzorca bajtow.
 * @param base  Adres startowy (np. adres modulu)
 * @param size  Rozmiar obszaru do skanowania
 * @param pattern  Wzorzec bajtow, np. {0x8B, 0x00, 0x00, 0x89}
 * @param mask     Maska: '?' = dowolny bajt, 'x' = dokladne dopasowanie
 *                 np. "xx??x"
 */
static std::vector<uintptr_t> scan_region(
    uintptr_t base,
    size_t    size,
    const std::vector<uint8_t>& pattern,
    const std::string&          mask)
{
    std::vector<uintptr_t> results;
    if (pattern.size() != mask.size()) return results;

    const uint8_t* mem = reinterpret_cast<const uint8_t*>(base);

    for (size_t i = 0; i + pattern.size() <= size; ++i) {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); ++j) {
            if (mask[j] == 'x' && mem[i + j] != pattern[j]) {
                found = false;
                break;
            }
        }
        if (found)
            results.push_back(base + i);
    }

    return results;
}

/**
 * Skanuje wszystkie regiony pamieci biezacego procesu.
 */
static std::vector<uintptr_t> scan_all(
    const std::vector<uint8_t>& pattern,
    const std::string&          mask,
    uintptr_t start_addr = 0x00400000,
    uintptr_t end_addr   = 0x7FFFFFFF)
{
    std::vector<uintptr_t> all_results;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = start_addr;

    while (addr < end_addr) {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0)
            break;

        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & PAGE_GUARD) == 0 &&
            (mbi.Protect & PAGE_NOACCESS) == 0 &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0)
        {
            auto found = scan_region(
                reinterpret_cast<uintptr_t>(mbi.BaseAddress),
                mbi.RegionSize,
                pattern,
                mask
            );
            all_results.insert(all_results.end(), found.begin(), found.end());
        }

        addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }

    return all_results;
}

} // namespace PatternScanner
