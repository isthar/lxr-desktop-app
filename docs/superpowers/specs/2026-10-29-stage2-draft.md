# LICHOMAT etap 2: szkic z rekomendacjami (polirytmia, lekki groove, atmosfera)

| | |
|---|---|
| Data | 2026-10-29 |
| Status | Szkic do Twoich poprawek. Według reguły z 29.10 idę dalej z tymi rekomendacjami, dopóki ich nie zmienisz. |
| Podstawa | `timing-core` (v0.1) i `emulator` (F1–F12, F6b) |
| Zastępuje | Listę etapu 2 i 3 z `2026-10-23-timing-core-design.md` §1 oraz zadania E6.x i E7.x w trackerze, tam gdzie się różni |

## 1. Dla kogo i po co

Twój opis z 29.10: industrial techno, groove, minimal, czasem acid; klimaty Stoor, BCCO, Mutual Rytm, SNTS.
- Nie potrzebujesz mocnego swingu, tylko lekkiego humanize.
- Bardzo ważne są ambienty, atmosfera i washe.
- Polirytmia obowiązkowo.

Pierwotny plan etapu 2 był ułożony pod hardgroove z mocnym swingiem: szablony groove, pełny mikrotiming,
nowy format wzorców. Ten szkic przestawia priorytety pod Twój opis.

## 2. Co LXR-02 już ma (fw 1.7 + v0.1)

| Obszar | Stan |
|---|---|
| Krok | Głośność, prawdopodobieństwo 0–100%, nuta (transpozycja oscylatorów), 2 locki parametrów, flam/ratchet ×1–×8, przesunięcie ±7 × 12 ticków (z 96 na krok) |
| Ścieżka | Długość 1–64 per wzorzec, więc **polimetria już jest**. Euclid (1–16 kroków z rotacją), mute tylko w RAM |
| Wzorzec | Jeden shuffle na wzorzec, kit, łańcuch wzorców, songi 64 × 64 |
| Głosy | Drum 1–3 (oscylator + modulator, FM, 6 kształtów z szumem), snare (ton + szum), cymbal i hihat (FM 3-oscylatorowe); filtr SVF z 7 typami, distortion, decymacja |
| Modulacja | 6 LFO (od ok. 0,0001 Hz do 200 Hz albo synchronizowane 4/1–1/32), 6 modulatorów od velocity; decay obwiedni do ok. 3 min, a na 127 dźwięk trzyma się bez końca |
| Efekty | Jeden slot FX: off, drive, ring, comp, delay. Delay mono albo ping-pong, do 0,74 s, **bez synchronizacji z tempem**. Pogłos „Plt” jest w kodzie, ale wyłączony (`USE_REVERB 0`, `FxBus.h:26`) |
| Wyjście | Każdy głos na St1, St2, L1, R1, L2, R2 albo FX; **brak obróbki mastera** (bez filtra DJ) |
| Pamięć | Wolne: ok. 106 KB w RAM, ok. 23 KB w DTCM razem ze stosem, 3 KB w ITCM |
| CPU | Nieznane: nikt jeszcze nie odczytał strony CPU na urządzeniu (test H12) |

Brakuje: dzielnika tempa per ścieżka (polirytmii), humanize, warunków trigów, scen mute, filtra DJ,
pogłosu, synchronizacji delaya, slide'u.

## 3. Rekomendacje w skrócie

1. **Najpierw polirytmia i lekki groove** (v0.2). Nie zależą od pomiarów CPU, więc mogę je robić,
   zanim przetestujesz v0.1.
2. **Potem atmosfera** (v0.3): pogłos jako osobny efekt obok delaya, freeze, rumble z duckingiem od stopy,
   delay synchronizowany z tempem, filtr DJ. Najpierw potrzebuję Twojego odczytu CPU (H12), bo fw 1.7
   wyłączył pogłos najpewniej przez brak mocy.
3. **Na koniec występ i acid** (v0.4): sceny mute, tryb fill, rolle 1/32 i triole, slide dla Drum 1–3.
4. **Odkładam** szablony swingu, pełny mikrotiming z nowym formatem PatV04, nagrywanie z mikrotimingiem
   i wyzwalanie z dokładnością do próbki. Obecne przesunięcie kroku ±7 × 12 ticków i shuffle wystarczą
   na lekki groove. Wrócę do nich, jeśli humanize zabrzmi zbyt „ziarniście”.
5. **Pliki z fw 1.7 dalej się wczytują**, a fw 1.7 wczytuje pliki LICHOMAT, tracąc tylko nowe ustawienia
   (§7).

Jeśli wolisz atmosferę przed polirytmią: odczytaj stronę CPU (H12) przy testach v0.1, a zamienię kolejność.

## 4. v0.2: polirytmia i lekki groove

### 4.1 Tempo ścieżki (polirytmia)

- Każda ścieżka dostaje **tempo względne**: ×1/4, ×1/3, ×1/2, ×2/3, ×3/4, ×1, ×4/3, ×3/2, ×2, ×3, ×4.
  Przykłady:
  - ×3/4 przy długości 16 daje trójkę na czwórkę;
  - ×2 daje hihat w 1/32;
  - ×2/3 daje triolowe ćwierćnuty.
- Z długością 1–64 daje to polimetrię i polirytmię naraz, np. ścieżka 5-krokowa w ×3/4 przeciw stopie
  w 4/4.
- **Wyrównanie:** wszystkie ścieżki zaczynają od kroku 1 przy starcie i przy zmianie wzorca, a potem biegną
  swobodnie.
  - Alternatywa to wyrównanie po każdym takcie mastera. Wybieram swobodny bieg, bo długie, przesuwające
    się pętle to rdzeń minimalu.
- **Rdzeń (`seq_core`):** dziś każda ścieżka przesuwa się o krok na wspólnej granicy kroku.
  - W v0.2 każda ścieżka dostaje własną fazę w tickach (96 na krok × tempo ścieżki) i własną granicę kroku.
  - Wyprzedzenie zostaje jak w etapie 1: jeden krok ścieżki, a przesunięcia trzymają się poniżej kroku.
  - Shuffle działa na nieparzystych krokach każdej ścieżki, w jej własnym tempie.
  - MIDI clock out i jack zegara zostają proste, w tempie mastera.
- To największa zmiana etapu 2. Testy: rozszerzenie testów T rdzenia na hoście i rendery w emulatorze,
  np. trzy na cztery z liczeniem uderzeń w WAV.

### 4.2 Humanize i opóźnienie ścieżki

- **Humanize per ścieżka:**
  - czas: losowo ±0–8 ms (domyślnie 0);
  - głośność: losowo ±0–25% (domyślnie 0).
  - Losowość idzie z generatora sprzętowego (w emulatorze z ziarna `--seed`, więc rendery są powtarzalne).
- **Opóźnienie ścieżki w ms:** od −15 do +30 ms (np. hihaty 4 ms później, snare 3 ms wcześniej).
  Ujemne mieści się w wyprzedzeniu rdzenia.
- Do czasu wyzwalania z dokładnością do próbki start głosu jest kwantowany do bloku audio 1,45 ms.
  - Przy lekkim humanize to ok. 5–11 różnych pozycji, czyli wystarczy.
  - Wiadomości MIDI out idą z dokładnym czasem.

### 4.3 Warunki trigów, bez zmiany formatu kroku

- Bajt prawdopodobieństwa kroku ma dziś wartości 0–100; **101–127 są wolne**. Wartości 101 i wyżej
  kodują warunki:
  - 1:2, 2:2, 1:3, 2:3, 3:3, 1:4, 2:4, 3:4, 4:4 (który obieg wzorca z ilu);
  - FILL i !FILL;
  - FIRST i !FIRST (pierwszy obieg po starcie lub zmianie);
  - PRE i !PRE (czy poprzedni warunek na tej ścieżce zagrał).
- Krok ma albo prawdopodobieństwo, albo warunek, jak w maszynach Elektronu, gdzie prawdopodobieństwo jest
  jednym z warunków.
- fw 1.7 traktuje każdą wartość powyżej 100 jako „zawsze gra”, więc wzorzec z warunkami gra tam po prostu
  wszystkie kroki.
- Na stronie kroku parametr prawdopodobieństwa po 100% przechodzi w nazwy warunków.

### 4.4 Rotacja

- Akcja edycji: przesuń kroki ścieżki o jeden w lewo albo w prawo, razem z lockami i timingiem.
  Przypisanie przycisków ustalę w planie: SHIFT + bar± na stronie ścieżki, jeśli ta kombinacja jest wolna.

## 5. v0.3: atmosfera

Bramka: Twój odczyt strony CPU (H12) na v0.1 przy gęstym wzorcu. Bez tego nie wiem, czy pogłos się zmieści.

- **Pogłos jako osobny efekt**, a nie w jednym slocie FX z delayem, żeby delay i pogłos grały razem
  (delay → pogłos):
  - punkt wyjścia to wyłączony „Plt” z fw 1.7, z poprawką jego błędu: druga pętla używa bufora pierwszej,
    `Reverb.c:375` i `:393`;
  - własny bufor w RAM: ok. 64 KB z wolnych 106 KB;
  - parametry: rozmiar, decay, tłumienie (tone), predelay, modulacja.
  - Jeśli CPU nie wystarczy: pogłos liczony w połowie częstotliwości (22 kHz). To typowy zabieg, który
    przy ciemnych ogonach brzmi dobrze.
- **Freeze** (E7.5): przycisk albo CC zamraża ogon, z wolną modulacją, bez końca. To baza washy i padów
  z jednego uderzenia.
- **Rumble** (E7.6): pogłos stopy, ściszany obwiednią samej stopy (ducking). To klasyka industrial
  techno; ustawienie, z której ścieżki idzie ducking.
- **Delay z tempem** (E7.4): czasy 1/16–1/4, z kropką i triolowe. Do 0,74 s mieści się 1/4 przy
  ≥ 82 BPM i 1/4 z kropką przy ≥ 122 BPM. Dłuższe czasy wymagają większego bufora: zdecyduję po
  pomiarze pamięci pogłosu.
- **Filtr DJ na masterze:** jedna gałka LP ← 0 → HP z rezonansem, na obu wyjściach stereo; do przypisania
  pod MIDI CC i na stronę wykonawczą.
- **Washe i atmo z obecnych głosów:** szum i FM z wolnym LFO na filtrze, długi decay i do tego pogłos z
  freeze. Do v0.3 dołączę kilka przepisów na kity (tekst, nie dane Erica) i rendery z emulatora do
  odsłuchu.

## 6. v0.4: występ i acid

- **Sceny mute:** 8 na projekt, zapis i przywołanie przyciskiem; zmiana na granicy kroku albo taktu.
- **Tryb fill:** trzymany przycisk; kroki z warunkiem FILL grają, a z !FILL milkną (§4.3).
- **Rolle:** stawki 1/32, 1/3, 1/6, 1/12 są w kodzie fw 1.7 wykomentowane (`sequencer.c:1059–1143`);
  włączę je, jeśli zmieszczą się w rdzeniu.
- **Slide dla Drum 1–3 (acid):**
  - flaga w wolnym bicie 7 nuty kroku: wysokość płynie od poprzedniej nuty przez czas ustawiany per głos;
  - akcent przez głośność kroku z modulatorem velocity → cutoff, który już istnieje;
  - fw 1.7 zignoruje flagę.

## 7. Pliki i zgodność

- Format wzorca (PatV03, 3663 B) nie ma wolnych bajtów. Nowe ustawienia per ścieżka, czyli tempo,
  humanize i opóźnienie (4 B × 7 ścieżek), idą do **bloku rozszerzenia na końcu pliku `.pat`**:
  znacznik i wersja, potem dane.
- fw 1.7 czyta plik po kolei i, według pierwszego czytania kodu, nie patrzy dalej. Plan musi to sprawdzić
  na `presetManager.c` i na `tmp.pat`, zanim wybierze to rozwiązanie. Inaczej nowe ustawienia pójdą do
  osobnego pliku obok wzorca.
- RAM na nowe ustawienia: 64 wzorce × 28 B = 1792 B, plus fazy ścieżek w rdzeniu.
- Sceny mute zapisuje się w pliku projektu albo w osobnym pliku; ustalę to w planie v0.4.
- Warunki trigów i slide mieszczą się w istniejących bitach kroku.

## 8. Jak pracuję

- Każde wydanie ma osobny plan i powstaje na gałęzi od `emulator`. Ta gałąź ma już poprawki F1–F12, więc
  v0.2 zawiera je wszystkie.
- Tryb oszczędny: jeden wykonawca na dwa zadania i jeden lekki przegląd na ok. cztery zadania, bez krytyków;
  dokumenty krótsze niż w etapie 1.
- Każda zmiana brzmienia ma render w emulatorze. Rendery do odsłuchu trafiają do `build/demo/`, a raport
  wydania mówi, czego posłuchać.
- **Ty:**
  - testy v0.1 na LXR-02 (Q6), w tym odczyt strony CPU (H12);
  - odsłuch renderów;
  - poprawki do tego szkicu, kiedy zechcesz.

## 9. Otwarte do Twojej poprawki (z moimi domyślnymi)

| | Domyślnie |
|---|---|
| Kolejność v0.2 → v0.3 → v0.4 | Polirytmia, potem atmosfera, potem występ i acid |
| Wyrównanie ścieżek z innym tempem | Swobodny bieg od startu i zmiany wzorca |
| Zakres humanize | Czas ±8 ms, głośność ±25% |
| Pogłos | Jeden algorytm (rozwinięty „Plt”) z freeze i rumble |
| Slide | Tylko Drum 1–3 |
