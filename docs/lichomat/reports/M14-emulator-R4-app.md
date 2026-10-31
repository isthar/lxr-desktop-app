# M14: aplikacja LICHOMAT Emu (emulator, R4, E5.5)

Data: 2026-10-31. Gałąź `emulator`, wypchnięta na fork, głowa `444d845`.
Plan: `docs/superpowers/plans/2026-10-31-emulator-r4-app.md`, 13 zadań, decyzje E1–E33 (E32 i E33 doszły
w trakcie, opisane na końcu planu). Specyfikacja: `docs/superpowers/specs/2026-10-27-emulator-design.md` §6.

**Aplikacja jest gotowa w kodzie.** Cały firmware gra na Macu w oknie z panelem w układzie LXR-02, z
dźwiękiem przez CoreAudio i wirtualnymi portami MIDI „LICHOMAT Emu”. Zostają Twoje sprawdzenia A1–A11
(`§12.3` specyfikacji) i R5, czyli opcje jacków zegara jako audio (mały plan).

## Co jest w aplikacji

| Funkcja | Jak działa |
|---|---|
| Panel | Układ jak na LXR-02 (Twoja decyzja z 10:13): pasek złącz u góry, LCD z 4 gałkami i enkoderem DATA, suwaki, ◄ BAR ►, przyciski głosów, trzy grupy przycisków, 16 kroków. Napis LICHOMAT czcionką DIN Condensed Bold z systemu. Bez nazw, logo, nadruków i kolorów urządzenia. Suwaka VOLUME nie ma (firmware czyta 6 suwaków) |
| Mysz i klawisze | Spacja to PLAY/STOP, Shift to SHIFT, prawy klik zatrzaskuje przycisk (SHIFT + krok samą myszą). Gałka: 1° na punkt przeciągania, 5° na ząbek kółka. Enkoder: kółko albo przeciąganie, klik to wciśnięcie |
| Dźwięk | CoreAudio, wyjście domyślne; miks out 1 + out 2, samo out 1, samo out 2 albo 4 kanały |
| MIDI | Wirtualne porty „LICHOMAT Emu” (USB LXR) i wybrane urządzenie jako DIN; master i slave. Unikalne ID portów zostają po power cycle, więc DAW nie gubi połączeń |
| Opóźnienie slave D | Liczone z opóźnień wyjścia audio: na wbudowanym wyjściu 48 kHz i 512 ramkach L = 56,96 ms, D = 57,96 ms. Można je zmniejszyć do 0 |
| Karta | Własny folder aplikacji; import z karty SD (z kopią zapasową i wycofaniem przy błędzie), eksport całej karty albo jednego projektu na Twoją kartę |
| Bezpieczeństwo | Watchdog (zawieszenie firmware po 10 s: komunikat i Restart), notatka po crashu, power cycle |
| Diagnostyka | Formaty strumieni, bloki na sekundę, niedobory, D i L, opóźnienia wejścia, obciążenie wątku firmware, liczniki |

## Liczby

| | Teraz |
|---|---|
| Testy emulatora | 264 w Debug, 265 w Release, 244 bez aplikacji (176 przed R4), wszystkie przechodzą; jeden test raz padł zaraz po przebudowie i nie powtórzył się w 5 kolejnych przebiegach (do obserwacji); testy z urządzeniem audio i MIDI pomijają się bez nich (CI) |
| Ostrzeżenia kompilatora | 0 (Debug z UBSan i Release) |
| Aplikacja (Release) | 1,9 MB, spakowana 0,95 MB; podpis ad hoc, `codesign --verify --deep --strict` przechodzi; tylko biblioteki systemowe |
| Kod | 19 commitów, ok. 11 200 linii (bez Dear ImGui), z tego 7 500 w `emu/app` |
| Dear ImGui | v1.92.9b, 15 plików, każdy zgodny co do hasha z tagiem upstream (Twoja zgoda z 06:51) |
| Zmierzone na tym Macu | 689,70 bloku/s (A11: 689,7 ± 0,1), zero niedoborów, wątek firmware w czasie rzeczywistym; MIDI clock out co 19,7–22,0 ms (A4: 20,83 ± 1,5 ms), pakiety 0,02–0,13 ms po znaczniku |
| CI na GitHubie | Bez przebiegu: GitHub od 31.10, 00:11 UTC nie uruchamia workflowów w tym repozytorium; lokalna próba CI (`tools/ci_dry_run.rb`) przechodzi |

## Jak powstało

- Plan R4 (4,5 tys. linii), jego przegląd: 16 uwag, w tym 2 blokujące; wszystko naniesione przed
  pierwszym zadaniem.
- Wykonanie po dwa zadania na wykonawcę, trzy lekkie przeglądy kodu (zadania 1–4, 5–9, 10–12): 5, 9 i
  10 uwag, wszystkie poprawione z testami, które najpierw padały. Najważniejsze:
  - SysEx z USB wysyłany bajt po bajcie (CIN 0xF) zamieniał się w błędne pakiety CoreMIDI;
  - zegar MIDI bez wyjścia audio szedł bez zapasu, pierwsze takty do 17 ms za późno;
  - DIN czekał w kolejce za USB;
  - eksport projektu z nadpisaniem bez wycofania;
  - handler crasha zapętlał się przy SIGTRAP;
  - każdy restart czekał 5 s, a dwa kliknięcia Restart mogły uruchomić dwie kopie aplikacji;
  - dioda CLK OUT gubiła impulsy (A8).
- Znalezione po drodze w firmware (bez zmian w firmware):
  - `Core/Src/graphic.c` ma zmienną globalną `float time`, która w jednym pliku wykonywalnym
    przesłaniała funkcję `time()` z biblioteki i wywracała aplikację przy starcie. Emulator kompiluje ten
    plik ze zmienioną nazwą symbolu, a test `R4_gui_clash` łapie każdą taką kolizję;
  - przy Start firmware wysyła F8 dokładnie 1 ms po FA, niezależnie od fazy zegara, więc interwał, w
    którym wypada Start, ma od 0 do 20,8 ms. A4 ten jeden interwał pomija.

## Jak uruchomić

Kopia z Twoją kartą DLC, którą oglądałeś, teraz w wersji Release ze wszystkimi poprawkami (bez wpływu na oryginał):

```sh
S=/private/tmp/claude-501/-Users-tk-Code-lxr/96b76bdc-1e78-46dc-a0e8-44d89ec424a5/scratchpad/emu-show
LICHOMAT_EMU_HOME="$S/home" "$S/LICHOMAT Emu.app/Contents/MacOS/LICHOMAT Emu" &
```

Własny build (w `build/wt-emulator`):

```sh
source tools/env.sh
cmake -S emu -B build/emu-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/emu-release
open "build/emu-release/LICHOMAT Emu.app"
```

Bez `LICHOMAT_EMU_HOME` aplikacja używa `~/Library/Application Support/LICHOMAT Emu`; import z karty SD
jest w menu Card.

## Co dalej

- **Ty:** A1–A11 (specyfikacja §12.3, opis w `emu/README.md`), najlepiej razem z sesją na LXR, bo A2, A3
  i A9 porównują z urządzeniem. Dalej czeka H12 (jedna liczba `pk`), która ustala częstotliwość
  pogłosu v0.4.
- **Ja:** scalenie `emulator` do `v0.3` i `v0.4`, żeby aplikacja grała też ich firmware (z pogłosem
  v0.4); potem R5 (jacki zegara jako audio). CI, gdy GitHub znów uruchomi przebiegi.
