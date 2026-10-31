# M15: jacki zegara jako audio w aplikacji LICHOMAT Emu (emulator R5, E5.5)

Data: 2026-10-31. Gałąź `emulator`, wypchnięta na fork, głowa `c70971f`; scalona też do `v0.3` i `v0.4`.
Plan: `docs/superpowers/plans/2026-10-31-emulator-r5-clock-audio.md`, 4 zadania, decyzje F1–F15 planu
(to nie są poprawki firmware F1–F18). Specyfikacja: §6.8, uzupełniona o to, jak R5 zostało zbudowane.

**R5 jest gotowe w kodzie.** Z nim emulator ma wszystko ze specyfikacji etapu 1b (R1–R5). Zostają Twoje
sprawdzenia A1–A12 w aplikacji.

## Co doszło

| Funkcja | Jak jej używać |
|---|---|
| CLK OUT jako audio | Okno Audio: kanał wyjścia 1–64. Zegar idzie na ten kanał jako impulsy +1,0 o długości jednego impulsu 96 PPQN (5,2 ms przy 120 BPM), dokładnie w swoich ramkach. Dźwięk jest wtedy opóźniony o jeden blok (1,45 ms). Na urządzeniu 2-kanałowym miks gra mono na drugim kanale |
| CLK IN i RST IN z wejścia audio | Okno Audio: kanał zegara i opcjonalnie kanał resetu, każdy z progiem (domyślnie połowa skali) i histerezą. Zbocze wchodzi z opóźnieniem D, które przy włączonym wejściu obejmuje też opóźnienie wejścia (F7) |
| Zgoda na mikrofon | macOS pyta tylko przy pierwszym włączeniu opcji wejścia. Odmowa zostawia opcję wyłączoną z komunikatem. Aplikacja podpisana ad hoc może zapytać znów po każdym nowym buildzie |
| Diagnostyka | Kanały otwarte i kanały urządzenia, stan obu opcji, opóźnienie wejścia, pominięte wywołania wejścia. Gdy wybranego kanału zegara nie ma na urządzeniu, aplikacja to mówi |

Obie opcje są domyślnie wyłączone.

## Liczby

| | Teraz |
|---|---|
| Testy emulatora (`emulator`) | 288 w Debug, 289 w Release, 260 bez aplikacji (264/265/244 przed R5); wszystkie przechodzą |
| Zmierzone na fałszywym urządzeniu | 17 zboczy CLK OUT na 17 impulsów firmware, każde w swojej ramce; slave z zegara audio: każde zbocze wchodzi w czasie z mapy + D, D = 70,5 ms (L 69,5 ms, z tego 12,5 ms opóźnienia wejścia) |
| Ostrzeżenia kompilatora | 0 |
| Lokalna próba CI | `tools/ci_dry_run.rb` przechodzi (Release i UBSan) |

## Jak powstało

- Plan (1349 linii), wykonanie po dwa zadania, jeden lekki przegląd: 1 blokująca uwaga (wejście
  milkło po zmianie rozmiaru bufora na wspólnym interfejsie wyjścia i wejścia, czyli dokładnie w
  układzie A12), 1 ważna (kolejność wyłączenia resetu z panelu) i 6 drobnych; wszystkie poprawione.
- Niestabilny test: `R4_dev_midiout` padał, gdy obok startowały rendery innych testów. Przyczyna: przy
  starcie firmware robi pracę po ekranie startowym bez tempa zegara ściennego, więc pierwsze 1–9 taktów
  MIDI clock wychodzą 16–131 ms za późno; od 10. taktu opóźnienie to 0,07 ms. Test sprawdza odstępy od
  10. taktu, jak sąsiedni pomiar; A4 dotyczy stanu ustalonego.
- Bramka nazw M21 pomija tablice z samymi offsetami (`__unwind_info`, `__eh_frame`, `__gcc_except_tab`):
  offset funkcji przypadkiem ułożył się w bajty „LxR”. Sekcje, w których mogą być napisy, są dalej
  skanowane.
- Ustalone przy okazji: po ostatnim impulsie zegara slave gra jeszcze krok do następnego celu, jak opisuje
  `seq_core.h`.

## Co dalej

- **Ty:** A12 potrzebuje kabla: kanał zegara z interfejsu do CLK IN LXR (2 minuty bez poślizgu) i
  impulsy z LXR do wejścia interfejsu. Reszta jak w M14 (A1–A11), plus H12 dla pogłosu v0.4.
- **Ja:** odświeżone kopie aplikacji v0.1 i v0.4 w scratchpadzie; potem czekam na Twoje wyniki albo na
  wybór kolejnej pracy.
