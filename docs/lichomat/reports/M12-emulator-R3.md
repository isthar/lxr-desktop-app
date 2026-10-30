# M12: emulator, krok R3 – karty testowe, nagrania wzorcowe G1–G10, CI z ASan (E5.4) i poprawki F13–F15

Data: 2026-10-30. Gałąź `emulator`, wypchnięta na fork, głowa `56fe495`.
Plan: `docs/superpowers/plans/2026-10-29-emulator-r2-r3-renderer-goldens.md`, zadania R3.1–R3.6.

## Co powstało

**Nagrania wzorcowe (goldens).** 18 renderów, każdy z zapisanym WAV i logami MIDI, zegara i panelu,
z warunkami (`#expect`) i wariantem „zepsutym”, który musi je złamać. Jeśli zmiana w firmware zmieni
dźwięk, MIDI, zegar albo ekran w którymkolwiek z nich, test to wyłapie i pokaże pierwszą różnicę.

| Nagranie | Co pilnuje |
|---|---|
| G1 | Wzorzec przy 120 BPM, 4 takty: dźwięk, 112 nut MIDI |
| G2 (7) | Efekty FX: tube, fold, clip, ring mod, kompresor, delay, ping-pong; strona FX na ekranie |
| G3 | Tryb song: trzy wzorce po kolei, koniec songu |
| G4 | Panel: wyciszenia na diodach, roll, zmiana wzorca na końcu taktu |
| G5 | Master: MIDI clock out 24 PPQ, Start/Stop, jack zegara, Program Change |
| G6 | F8 za Note On na pierwszym kroku (poprawka z v0.1) |
| G7 | Slave po DIN: zegar, Start, Stop, Song Position Pointer, Continue, Program Change |
| G8 (2) | Slave po USB przy 120 i 137 BPM, zegar w paczkach co 11,6 ms |
| G9 (2) | Zegar z jacka 4 i 24 PPQ z bramką reset |
| G10 | Prawdopodobieństwo kroku z ziarnem losowania |

**Narzędzia.** `tools/emu_golden.py` sprawdza (`check`), zatwierdza (`bless`, odmawia, gdy warunek
pada) i porównuje z dowolną wcześniejszą wersją (`diff`). Karty testowe `init`, `fx` i `song` zapisuje
sam firmware skryptem `emu/tests/cards/make.sh`, a test M23 pilnuje, żeby dało się je odtworzyć bajt
w bajt.

**CI na GitHubie.** `emulator.yml` buduje emulator na macOS w dwóch wersjach przy każdym pushu:
Release (bez sanitizerów, wszystkie testy i nagrania wzorcowe) i Debug z ASan i UBSan.
`tools/check.sh` buduje i testuje teraz także emulator.

## Liczby

| | Teraz |
|---|---|
| Testy emulatora | 161 identyfikatorów w Debug (UBSan), 162 w Release (156/157 po R3.6, +3 po przeglądzie, +2 F13 i F14) |
| Testy firmware na hoście | 57 (55 przed R3, +2 F15) |
| Ostrzeżenia kompilatora | 0 |
| Szybkość renderu | 88,8× czasu rzeczywistego w Release, 13× w Debug |
| Pamięć firmware na tej gałęzi | DTCM 107 388 B i RAM 284 408 B bez zmian; obraz 301 164 B (+352 B przez F13) |
| CI | Zielone w obu zadaniach od `56fe495` (przebieg 41134984731): Release ze wszystkimi nagraniami zgodnymi na runnerze GitHuba i Debug z ASan i UBSan, 161 z 161. Pierwszy pełny przebieg z ASan nie zgłosił żadnego błędu pamięci, także w M16 (zapis poza bufor FX) |

## Błędy firmware 1.7, które znalazł emulator

Według Twojej reguły z 29.10 zdecydowałem sam (Q25, Q26). Każda poprawka ma test, który najpierw
padał, i jest też na v0.3.

- **F13** (`Distortion.c`): krzywa diody efektu clip przy sygnale +1 czytała jeden element za końcem
  tabeli. Na LXR każda dodatnio obcięta próbka brała przypadkową liczbę z pamięci za tabelą. Teraz
  bierze ostatni element tabeli, czyli to, co 1.7 dawał, gdy ta liczba była skończona.
- **F14** (`transientGenerator.c`): przesunięcie w lewo ujemnej próbki transjentu, niezdefiniowane
  w C. Na LXR bez zmian (GCC liczy to jak mnożenie przez 256, obraz bajt w bajt ten sam).
- **F15** (`Snare.c`, `Oscillator.c`, `ResonantFilter.c`): konwersje float na liczbę całkowitą poza
  zakresem. Teraz robią w C dokładnie to, co procesor LXR robi sprzętowo; obraz bajt w bajt ten sam.

F14 i F15 znalazła Twoja paczka DLC Hrtl: jej prawdziwe kity przechodzą ścieżkami silnika dźwięku,
których moje karty nie dotykały. Teraz wszystkie 16 wzorców demo gra pod UBSan bez błędu. Pliki
paczki zostają tylko na Twoim Macu.

## Przegląd i poprawki

Jeden lekki przegląd R3: jedna ważna uwaga i cztery drobne, wszystkie poprawione (`69aaa12`, `11bc93c`).
- **Ważna:** przy parametrach FX karty `fx` (wszystkie 0) tor efektów nic nie dawał, więc pięć nagrań G2
  było identycznych i nie wyłapałoby regresji w algorytmach FX. Karta ma teraz parametry FX na 64 i
  siedem nagrań G2 jest parami różnych. Wcześniej blokował to błąd F13.
- **Drobne:** `diff` przy renderach różnej długości; warunki z oknem poza końcem renderu przechodziły
  bez sprawdzania; G9 przy 24 PPQ i G8 przy 137 BPM nie sprawdzały PPQ ani tempa; zgubiony plik
  nagrania przepadał bez śladu.

CI z ASan potrzebowało trzech poprawek po drodze:
- pierwszy przebieg padł przy kompilacji: SDK macOS oznacza `sprintf` jako przestarzałe, gdy ASan
  wyłącza `_FORTIFY_SOURCE` (`9ecd920`, teraz `snprintf`);
- drugi nie ruszył, bo GitHub nie miał wolnych runnerów macOS arm64; puszczony ponownie;
- trzeci przeszedł 160 z 161: test bramki licencji oczekiwał napisów w sekcji `__TEXT,__cstring`, a pod
  ASan clang kładzie je w `__TEXT,__asan_cstring`. Bramka działała dobrze; test czyta teraz sekcję
  z pliku obiektowego (`56fe495`).

## Do odsłuchu

Rendery nagrań wzorcowych: `/Users/tk/Code/lxr/build/wt-emulator/build/emu/goldens-out/<nagranie>/out.wav`
(obok logi MIDI, zegara i panelu). Rendery paczki DLC: `/Users/tk/Code/lxr/build/dlc/sweep/` (16 wzorców
demo) i `/Users/tk/Code/lxr/build/dlc/demo/` (funkcje LICHOMAT na tych kitach), opisy w `README.txt`.

## Co dalej

- **Zrobione już po R3:** R3 jest scalone do `v0.3` (`7ee96d5`, `c1c041e`, `7a6a0a3`, `5a7008d`), więc v0.3 i
  kolejne wersje mają te same nagrania wzorcowe i CI. Karty testowe zapisane przez firmware v0.3 różnią
  się tylko oczekiwanymi blokami (`.PAT` +34 B, `prj.nfo` +14 B). Z 19 nagrań zmieniło się tylko G4,
  i tylko w logu panelu: strona oscylatora Drum 1 pokazuje strzałkę `>` drugiego ekranu z `slt` (D25).
  Dźwięk, MIDI i zegar wszystkich nagrań są bez zmian. Na v0.3: host 92, emulator 173 (Debug) i 174
  (Release), budżet pamięci bez zmian.
- **Kolejność (Q27):** R3 było odłożone na koniec etapu 2 (Twoja decyzja z 29.10). Zrobiłem je teraz,
  bo reszta etapu 2 (v0.4, atmosfera) czeka na odczyt CPU (H12); zapowiedziałem to o 20:37. Aplikacja
  emulatora (E5.5, R4–R5) zostaje odłożona, jak zdecydowałeś.
- **Ty:** jak dotąd – testy na LXR-02 (Q6 z odczytem CPU, H12; potem U4–U19); odsłuch renderów.
