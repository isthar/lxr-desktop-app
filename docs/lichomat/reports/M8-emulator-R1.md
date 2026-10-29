# M8: emulator, krok R1 – cały firmware na wirtualnym urządzeniu (E5.1, E5.2)

Data: 2026-10-29. Gałąź `emulator`, tylko lokalnie (worktree `build/wt-emulator`), od `timing-core`
`db25288`. Nie jest wypchnięta: czeka na Twoją odpowiedź na Q19.

## Co powstało

**Krok R1 specyfikacji etapu 1b jest wykonany.** Cały firmware LICHOMAT (70 plików z `Core/`) kompiluje
się na Macu i działa na wirtualnym urządzeniu: bootuje z kartą i bez niej, wykonuje przebiegi pętli
głównej, przerwania, MIDI DIN i USB, panel, wyświetlacz, diody, kartę SD i gniazda. Wszystko w testach,
bez dźwięku na żywo: renderer WAV to R2, aplikacja z panelem to R4.

| Część | Wynik |
|---|---|
| Część 1 (plan `2026-10-27-emulator-r1a-firmware-changes.md`) | Zmiany firmware F1–F10, każda osobnym commitem z testem, i testy T etapu 1 także na ścisłej kopii rdzenia timingu. 55 testów na hoście. |
| Część 2 (plan `2026-10-27-emulator-r1b-virtual-device.md`) | Projekt `emu/`: stuby HAL/CMSIS/USB/FatFs, nakładka `main.h`, zegar 216 MHz i planista przerwań z hookiem warstwy, modele kodeka, LCD, diod, panelu, karty, DIN, USB, gniazd i strumieni losowych, libm z Arm Optimized Routines v23.01 (MIT). Testy M1–M13, M15, M19–M22 specyfikacji §12.1. |

**Liczby** (bramka firmware bez zmian od części 1):

| | Teraz |
|---|---|
| Testy emulatora | 103 identyfikatory ctest, wszystkie przechodzą w Debug (UBSan) i w Release, 0 ostrzeżeń |
| Testy firmware na hoście | 55, wszystkie przechodzą, 0 ostrzeżeń |
| DTCM + RAM | 391 796 B, 28 B poniżej limitu R6.2 |
| ITCM | 13 216 B (F9 zdjęło 16 B) |
| Obraz | 300 820 B |
| Commity na `emulator` | 11 w części 1, 29 w części 2 (z narzędziem `elf_equiv.py` i poprawkami po przeglądach); głowa `22827f7` |

## Jak to szło

Tryb oszczędny, jak ustaliliśmy: jeden agent wykonuje dwa zadania planu naraz, lekki przegląd po
mniej więcej czterech zadaniach, bez sceptyków i paneli krytyków. Plan części 2 napisał jeden agent na
podstawie nocnego szkicu kodu (ok. 5600 linii, nieprzejrzany), tym razem bez pisania kodu.

Cztery przeglądy:

- **Część 1:** 0 ważnych uwag. Jedna do dokumentów: F9 zmienia też falę rect we wszystkich pasmach
  (do 2541 LSB przy 1,76–14 kHz). Specyfikacja i test odsłuchowy U8 mówią o tym teraz.
- **R1b.1–R1b.4:** 3 ważne. Test M19 bywał losowo czerwony przez SIGPIPE; jego kanarek był kompilowany
  z `-O0`, więc nic nie sprawdzał; blokada drugiego włączenia urządzenia nie przerywała włączenia.
  Wszystko poprawione.
- **R1b.5–R1b.8:** model bez błędów, ale 2 ważne luki w testach (równy priorytet wewnątrz przerwania,
  dwa przypadki reguły przebiegów) i 3 drobne, w tym hook bloku spóźniony do 248 µs po zwolnieniu
  PRIMASK. Wszystko poprawione, każda poprawka z mutantem, który ją łapie.
- **R1b.9–R1b.16:** modele zgodne ze specyfikacją. 2 ważne luki: skan nazw M21 czytał tylko jedną
  sekcję binarki, więc teksty menu w tablicach były poza nim (dziś w binarkach nie ma żadnej nazwy
  LXR ani Erica Synths), i test M11 przepuszczał model wpuszczający 31 pakietów USB na 1 ms.
  Poprawione (`1d6dd63`, `b87b1f5`, `22827f7`): skan czyta teraz każdą sekcję, jak `strings -a -n 3`,
  i nadal nie znajduje żadnej nazwy; model USB był dobry, brakowało tylko testu.

Implementerzy sprawdzili ponad 120 mutantów. Przeżyły tylko takie, które nie zmieniają zachowania, i
jeden, którego nie da się pokazać na APFS. Mutanty, które przeżyły testy, znajdowali recenzenci; stąd
ich uwagi powyżej.

## Co się okazało po drodze

- **F10:** obraz firmware nie jest identyczny bajt w bajt, bo linker zmienia kolejność 58 skoków do
  ITCM. Wszystkie funkcje są identyczne instrukcja po instrukcji, pozostałe sekcje bajt w bajt.
  Wymaganie w specyfikacji mówi teraz o obrazie równoważnym; sprawdza to nowe `tools/elf_equiv.py`.
- **Model gałek (Q16):** firmware robi dokładnie 134 kroki na obrót gałki i 22 na parametrze-liście,
  jak w specyfikacji. Szkic liczył zmiany wartości, stąd wcześniejsze 96.
- **Wybudzenie wygaszacza** trwa 0–500 ms, zależnie od chwili naciśnięcia przycisku (koniec bieżącego
  kroku 500 ms). 361–465 ms w specyfikacji to były tylko próbki; §3.3 poprawiony.
- **Plan miał kilka złych liczb** (liczba przebiegów, oczekiwane bajty USB, liczba plików). Agenci szli
  za regułą ze specyfikacji i kodem firmware, nigdy nie poszerzali zakresów, i zapisali każde
  odstępstwo w commicie.

## Błędy firmware 1.7 znalezione przez emulator (do Twojej decyzji)

Żadnego nie poprawiałem w `Core/` – według §7 każda taka poprawka czeka na Twoją zgodę.

- **Q21:** gdy gałka nie stoi na 0° przy włączeniu, pierwsze `adc_checkPots()` bootu czyta przez
  wskaźnik NULL (`presetManager.c:2263`). Na urządzeniu nieszkodliwe. Propozycja: poprawka F11 w v0.2.
- **Q22:** `freq2PhaseIncr1024()` i `freq2PhaseIncr32767()` zamieniają float na uint32 bez nasycenia
  przy bardzo wysokiej wysokości dźwięku (przepis z U8: ok. 414 kHz). Na urządzeniu VCVT nasyca, więc
  dźwięk się nie zmienia. Propozycja: rozszerzyć F6 na te dwie funkcje.
- B13/B14 (oscylatory PWM czytają za tablicą) nadal czekają na test odsłuchowy U8; emulator wyłącza dla
  nich jedno sprawdzenie UBSan.

## Koszt

Od wznowienia 28.10 o 22:41 do końca: plan części 2, 8 agentów wykonawczych, 4 przeglądy i jeden
agent poprawek. Twój odczyt licznika: 34% tygodnia o 00:29; mój szacunek na koniec jest w podsumowaniu
na czacie. Mój wcześniejszy szacunek zużycia był zaniżony (24% zamiast 34%); `tools/agent/usage_estimate.py`
liczy teraz tempo z dwóch ostatnich Twoich odczytów.

## Co dalej

- **Ty:** Q19 (czy wypchnąć gałąź `emulator` na fork), Q21, Q22; testy v0.1 na LXR-02 (Q6); Q9–Q11.
- **Ja, po Twoim słowie:** plan R2 i R3 (renderer `lichomat-render` i testy porównujące nagrania z CI),
  potem R4 (aplikacja z panelem rysowanym przez Dear ImGui).
