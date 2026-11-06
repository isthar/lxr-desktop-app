# LICHOMAT: tryb „host” w seq_core (wtyczka idzie za Live'em co do próbki): szkic

| | |
|---|---|
| Data | 2026-11-06 |
| Status | Szkic do Twoich poprawek (tracker E9.3). Planu nie zaczynam, dopóki nie zrobisz P4 z `docs/lichomat/plugin-checks.md`: dopiero wtedy wiadomo, czy ograniczenia prototypu w ogóle Ci przeszkadzają |
| Podstawa | Prototyp wtyczki E9.2 (gałąź `plugin`), rdzeń `seq_core` z v0.6 i v0.7 |
| Nie zmienia | Firmware na LXR: wszystko poniżej jest kompilowane tylko w emulatorze i wtyczce, obraz `.img` zostaje bit w bit ten sam |

## 1. Po co

Prototyp zamienia transport Live'a na MIDI clock i Song Position, a firmware gra jako slave. Działa,
ale ma trzy ograniczenia, które w Live mogą być słyszalne:

- **Przeskok pętli to stop, pozycja i continue.** Uderzenia pierwszego kroku pętli przesunięte przed
  „raz” (mikrotiming, swing, szablony czasu z 0.6) giną, bo rdzeń generuje je krok wcześniej, a po
  skoku nie wie, że następny krok to początek pętli. Giną też uderzenia ostatniego kroku pętli, które
  wybrzmiewają po jej końcu (przesunięte później, ratchety): skok czyści kolejkę.
- **Pozycja tylko co szesnastkę.** Start ze środka szesnastki i pętla, która nie zaczyna się na
  szesnastce, są zaokrąglane w górę.
- **Tempo z dopasowania.** Slave liczy tempo z zegara metodą najmniejszych kwadratów. W testach po
  zmianie tempa idzie za nim od razu, ale przy automatyce tempa w Live (rampa) trzyma się zegara, a
  nie dokładnej pozycji.

## 2. Co proponuję

1. **Trzeci tryb synchronizacji w rdzeniu: „host”**, obok wewnętrznego i slave. W każdym bloku audio
   wtyczka podaje rdzeniowi pozycję w ćwierćnutach na pierwszej próbce bloku, tempo, stan
   transportu i granice pętli. Rdzeń liczy czas jak w trybie wewnętrznym (punkt zaczepienia
   `anchorTime`/`anchorTick`), ale punkt zaczepienia ustawia każdy blok z hosta. Bez dopasowania
   tempa i bez opóźnienia zegara.
2. **Przeskok pętli jako przewidziany skok.**
   - Przy granicy kroku, za którą przypada koniec pętli, rdzeń liczy liczniki ścieżek dla początku
     pętli (tak jak dziś Song Position) i generuje wczesną część pierwszego kroku pętli z tych
     liczników. Mechanizm istnieje już dla końca wzorca (`restartAt`, `seqc_generateFirst`), więc
     przeskok pętli działa jak powrót na początek wzorca.
   - Kolejka zostaje: uderzenia ostatniego kroku, które wybrzmiewają po końcu pętli, grają.
   - Jeśli Live zmieni pętlę po przewidzeniu, rdzeń usuwa przewidziane uderzenia
     (`seqc_queueRemoveLater`, jak przy zmianie wzorca w ostatniej chwili).
3. **Czego tryb „host” nie robi na początek** (wtedy wtyczka wraca do dzisiejszego MIDI clock):
   - pętla, która nie zaczyna się albo nie kończy na szesnastce;
   - tryb song LXR, bo skok w songu wczytuje kity, a tego nie da się zrobić „na zapas”;
   - przesuwanie pozycji wstecz w trakcie grania inaczej niż pętlą (kliknięcie w linijkę przy
     grającym transporcie): to stop i start od nowej pozycji, jak dziś.
4. **Droga transportu do rdzenia: nowe wejście emulatora**, jak impuls zegara z gniazda
   (`emu_inClockPulse`).
   - Silnik wtyczki wstrzykuje „transport hosta” z dokładnym czasem, w cyklach, na próbce, której
     dotyczy.
   - Firmware ma osobną małą kolejkę, którą `seq_tick()` czyta przed `seqc_process`.
   - Nie przez MIDI: SysEx jest za długi dla limitu 16 pakietów USB na 1 ms, wpisy kolejki mają 8 B,
     a parser czyta MIDI dopiero po `seqc_process`.
5. **Tylko w buildzie emulatora i wtyczki** (`LICHOMAT_HOST_SYNC`).
   - Firmware na LXR nie ma tej ścieżki w ogóle, więc `elf_equiv` pokazuje obraz bit w bit ten sam,
     a Twoje testy na sprzęcie się nie zmieniają.
   - Rdzeń jest testowany na hoście z włączonym trybem, w obu wariantach (z i bez fast-math), tak jak
     reszta `seq_core`.

## 3. Najtrudniejsze miejsca (z rozpoznania kodu)

- **Rdzeń zakłada, że czas i pozycja idą tylko do przodu.** Każde zdarzenie w kolejce ma bezwzględny
  tick, a `absStep` służy naraz do kolejności uderzeń, dławienia, losowań, parzystości swingu i fazy
  clock out. Rekomendacja: oś czasu rdzenia zostaje monotoniczna, a pętla dokłada przesunięcie
  „host → rdzeń”. Swing, pozycja szablonu (`q`), liczniki warunków (1:4 itd.) i faza clock out idą za
  pozycją hosta. Bez przepisywania tego, co już jest w kolejce.
- **Przewidywanie krok wcześniej.** Silnik widzi dziś tylko ok. 1,5 ms przyszłego transportu, a
  wczesna część kroku powstaje cały krok wcześniej. Granice pętli przychodzą z każdym blokiem
  (ProcessContext Live'a), więc przewidywanie opiera się na nich, nie na przyszłej pozycji.
- **Każda funkcja zależna od trybu** (pozycja w czasie, termin następnego zdarzenia, nagrywanie,
  przeliczanie ms na ticki) dostaje gałąź „host”. Testy muszą pokazać, że tryby wewnętrzny i slave
  zostają bit w bit te same: 41 nagrań wzorcowych, testy T16–T18 i `P10_plugin_native`.

## 4. Jak sprawdzę bez Ciebie

- Testy hosta rdzenia:
  - pętla 1 takt z mikrotimingiem −84 na pierwszym kroku: każde przejście gra uderzenie przed „raz”;
  - uderzenia po końcu pętli grają;
  - rampa tempa: każde uderzenie co do ticka tam, gdzie mówi pozycja hosta;
  - zmiana pętli po przewidzeniu;
  - start ze środka taktu.
- Emulator i wtyczka:
  - te same scenariusze przez host testowy wtyczki, porównane z wyliczonymi z pozycji Live'a;
  - wszystkie dzisiejsze testy wtyczki (72) i nagrania wzorcowe bez zmian w trybach MIDI.
- Obraz LXR: `elf_equiv` EQUIVALENT wobec v0.7.

## 5. Kolejność

1. **Najpierw Ty:** P1–P8 prototypu, zwłaszcza P4 (pętla, tempo, start od taktu 3).
2. **Potem decyzja:**
   - jeśli w P4 nic Ci nie przeszkadza, tryb „host” może poczekać za E9.4 (AU, dwie szyny stereo,
     automatyka);
   - jeśli pętla z mikrotimingiem gubi uderzenia, robię plan E9.3 i wdrożenie.
3. **Wdrożenie:** gałąź od `plugin` po scaleniu v0.7, bo tryb musi znać szablony z 0.6 i 0.7.

## 6. Otwarte do Twojej poprawki (z moimi domyślnymi)

| | Domyślnie |
|---|---|
| Kiedy | Po Twoim P4 |
| Tylko emulator i wtyczka | Tak (`LICHOMAT_HOST_SYNC`), LXR bez zmian |
| Pętle nie na szesnastce, tryb song | Na początek po staremu (MIDI clock) |
| Kliknięcie w linijkę przy grającym transporcie | Stop i start od nowej pozycji |
