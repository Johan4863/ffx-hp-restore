# FFX HD Remaster – HP Restore After Battle

Mod przywraca HP wszystkich postaci do maksimum po **każdej zakończonej walce**.  
**Nie dotyka Overdrive'ów, SB, limitów ani żadnych innych statystyk.**

---

## Wymagania

1. **Final Fantasy X/X-2 HD Remaster** (Steam, wersja x86 — `FFX.exe`)
2. **FF10 Module Loader** – [Nexus Mods #150](https://www.nexusmods.com/finalfantasyxx2hdremaster/mods/150)
   - Hijackuje `dinput8.dll` – kompatybilny z UnX (który używa `dxgi.dll`)
3. **Microsoft Visual C++ 2015-2022 Redistributable (x86)**
4. Do kompilacji: **Visual Studio 2019/2022** z toolchainem MSVC x86

---

## Instalacja (gotowy .dll)

```
[Katalog gry]\
├── FFX.exe
├── FFX-2.exe
├── dinput8.dll          ← Module Loader (już zainstalowany)
├── hook.ini             ← konfiguracja Module Loadera
└── modules\
    └── ffx_hp_restore.dll   ← ← ← TU wrzuć skompilowany .dll
```

Module Loader automatycznie ładuje wszystkie `.dll` z folderu `modules\`.

---

## Kompilacja (Visual Studio)

```bat
:: Otwórz "x86 Native Tools Command Prompt" z Visual Studio
cd ścieżka\do\ffx-hp-restore
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A Win32 ..
cmake --build . --config Release
```

Plik `ffx_hp_restore.dll` pojawi się w `build\Release\`.

---

## Kompilacja (MinGW / MSYS2)

```bash
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake
cd ffx-hp-restore
mkdir build && cd build
cmake -G "MinGW Makefiles" -DCMAKE_C_FLAGS="-m32" -DCMAKE_CXX_FLAGS="-m32" ..
make
```

---

## Konfiguracja Module Loadera (hook.ini)

Upewnij się, że `hook.ini` ma wpis na folder `modules`:

```ini
[general]
disableAutoLoad=false

[modules]
default=modules

[log]
disableConsole=false
disableFile=false
logFile=hook.log
```

---

## Weryfikacja działania

1. Uruchom grę ze Steam
2. Wejdź w walkę, pozwól postaciom stracić HP
3. Zakończ walkę
4. HP powinno wrócić do max **~0.5 sekundy po ekranie podsumowania walki**

W `hook.log` zobaczysz wpisy:
```
[HP-Restore] Mod zaladowany - HP Restore After Battle aktywny
[HP-Restore] Znaleziono FFX.exe - rozpoczynam monitorowanie walki
[HP-Restore] Walka zakonczona - przywracam HP...
[HP-Restore] Przywrocono HP dla 7 postaci
```

---

## Kalibracja offsetów (jeśli gra dostanie patch)

Jeśli mod przestanie działać po aktualizacji gry, offsety trzeba zaktualizować w `hp_restore.cpp`:

### Metoda – Cheat Engine

1. Uruchom grę + Cheat Engine, attach do `FFX.exe`
2. Szukaj HP Tidusa: w walce, po obrażeniach szukaj wartości np. `8500` (4 bytes)
3. Po znalezieniu adresu `currentHP` — adres `maxHP` jest **+0x04** dalej
4. Znajdź wskaźnik (pointer scan) do tego adresu
5. Zaktualizuj `BASE_PTR_OFFSET` i `PARTY_CHAIN_OFFSET` w kodzie

### Znane offsety struktury postaci (sprawdzone Steam 2016+)

| Offset | Typ    | Opis               |
|--------|--------|--------------------|
| +0x00  | uint8  | Char ID (0xFF=brak)|
| +0x04  | int32  | Current HP         |
| +0x08  | int32  | Max HP             |
| +0x24  | uint8  | Status (KO=0)      |

Rozmiar struktury: `0x1C0` bajtów (7 postaci = `7 × 0x1C0`)

---

## FAQ

**Q: Czy mod działa z FFX-2?**  
A: Częściowo – offsety dla X-2 są inne. Mod domyślnie hookuje tylko `FFX.exe`.  
Dodaj osobną logikę dla `FFX-2.exe` z właściwymi offsetami.

**Q: Czy mod resetuje Overdrive'y?**  
A: Nie. Mod pisze tylko do `currentHP` (offset `+0x04`). Overdrive meter jest  
na zupełnie innym offsecie i nie jest ruszany.

**Q: Czy działa z UnX / Untitled Project X?**  
A: Tak. UnX używa `dxgi.dll`, Module Loader używa `dinput8.dll` — brak konfliktu.

**Q: Czy mogę dostosować kiedy HP się regeneruje?**  
A: Tak – edytuj stałą `POLL_MS` (interwał skanowania) i `Sleep(500)` po wykryciu  
końca walki. Możesz też dodać warunek żeby nie regenerować HP po Overdrive'ach.

---

## Licencja

Do użytku osobistego. Nie rozprowadzaj bez zgody autora.
