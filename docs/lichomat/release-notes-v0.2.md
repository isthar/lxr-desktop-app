# LICHOMAT v0.2: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.2` w `isthar/lxr-desktop-app`, gdy przejdziesz na LXR sprawdzenia U4–U14 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.2` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.2 ma działać według testów na hoście (83) i w emulatorze (139). SHA-256 pliku aktualizacji dojdzie, gdy zbuduje go CI. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware" do końca „Licencja"; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.2 bazuje na v0.1, a ta na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dała v0.1, zostaje. v0.2 dodaje groove na poziomie ścieżki: własne tempo każdej ścieżki (polirytmia), humanize i opóźnienie ścieżki, warunki trigów i rotację ścieżki. Do tego dochodzą poprawki firmware z pracy nad emulatorem (F1–F12).

Ustawienia ścieżek zapisują się z wzorcem, w bloku na końcu pliku wzorca. Firmware 1.7 i v0.1 ten blok pomijają.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: v0.2 dopisuje blok do plików wzorców, a to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.2.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.2 unofficial`.

## Powrót do v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.2.img` i zainstaluj tak samo:

- v0.1: `LXR_V2_LICHOMAT_0.1.img` (plik z run sheet listy sprawdzeń, SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Oba wczytują wzorce zapisane przez v0.2 bez błędów: wszystkie ścieżki grają w `x1`, bez humanize i opóźnienia, a każdy krok z warunkiem gra w każdej pętli, jak prawdopodobieństwo 100. Nie ruszaj tam `prb` takich kroków: tamten edytor ustawi go na 100 lub mniej. Wzorzec zapisany tam od nowa traci blok, więc po powrocie do v0.2 jego ścieżki mają ustawienia domyślne; warunki trigów zostają, bo siedzą w bajcie prawdopodobieństwa kroku.

## Nowe

### Tempo ścieżki

- **Gdzie:** wybierz głos, przytrzymaj SHIFT i naciśnij drugi przycisk trybu: strona Euklid, `LEN stp rot mrp>`. Przekręć enkoder o cztery pola w prawo, za `mrp`: drugi ekran `tmp hmt hmv dly`. Wartości zmieniasz gałkami albo enkoderem. Działają na wybrany głos we wzorcu, który widzisz (przycisk głosu na tej stronie wybiera inny), i zapisują się z wzorcem; zmiana wzorca bez zapisu je odrzuca, jak inne edycje wzorca.
- **`tmp`:** jedenaście temp względem mastera: `1/4 1/3 1/2 2/3 3/4 x1 4/3 3/2 x2 x3 x4`. Przy 120 BPM krok ścieżki w `x1` trwa 125 ms, w `3/4` 166,7 ms, w `x2` 62,5 ms.
- **Start:** wszystkie ścieżki zaczynają od kroku 1 przy START i przy każdej zmianie wzorca, a potem biegną swobodnie, każda w swojej długości (1–64) i swoim tempie, więc pętle przesuwają się względem siebie.
- **Koniec wzorca** to dalej takt mastera: najdłuższa ścieżka liczona w krokach mastera, w jakimkolwiek tempie. Ścieżka `1/2` o 32 krokach kończy wzorzec co 32 kroki mastera, a jej własna pętla trwa 64.
- **Swing, shift i ratchety** skalują się z krokiem ścieżki: swing na jej nieparzystych krokach, shift w 1/8 jej kroku, ratchety równo w jej kroku.
- **Zegary** zostają w tempie mastera: MIDI clock out, jack clock out i dioda START/STOP, która mruga w ćwierćnutach mastera.
- **Diody kroków** (biegnące światło, kasowanie, follow) i **nagrywanie na żywo** wybranej ścieżki idą po jej własnych krokach.
- **Zmiana `tmp`** działa od następnego kroku ścieżki; ścieżka wracająca do `x1` dołącza do siatki mastera na następnym kroku mastera.
- **Przykłady** (120 BPM, długość 16):
  - 3 na 4: głos 2 na krokach 1, 5, 9, 13 w `3/4` przeciw stopie na ćwierćnutach: uderzenia co 666,7 ms, spotkanie ze stopą co 2 s;
  - hihat w `x2` na wszystkich krokach: 1/32, co 62,5 ms;
  - triole: `3/2` daje triolowe szesnastki (krok 83,3 ms), a `3/4` z nutą na co drugim kroku triolowe ćwierćnuty (co 333,3 ms);
  - ścieżka 5-krokowa w `3/4` (pętla 833 ms) przeciw stopie w 4/4: przesuwa się i wraca na to samo miejsce względem ćwierćnut co 2,5 s.

### Humanize i opóźnienie ścieżki

- **Gdzie:** ten sam ekran co tempo.
- **`hmt` 0–8 ms:** każdy krok ścieżki (razem ze swoimi ratchetami) przesuwa się losowo o ± do tylu ms.
- **`hmv` 0–25 %:** velocity każdego kroku zmienia się losowo o ± do tylu procent, najwyżej do 127.
- **`dly` −15 … +30 ms:** cała ścieżka gra stale wcześniej albo później.
- **Dokładność:** przesunięcia liczą się w całych tickach przy bieżącym tempie (1/96 kroku mastera, 1,30 ms przy 120 BPM). MIDI Note On wychodzi w ticku swojego zdarzenia; głos startuje na początku następnego bloku audio (do 1,45 ms), jak w v0.1.
- **Granice:** opóźnienie z humanize przesuwają uderzenie najwyżej o jeden krok ścieżki później, a cały offset (swing + shift + opóźnienie + humanize) najwyżej o 7/8 kroku ścieżki wcześniej. W `x1` górna granica nie działa (do 300 BPM); w `x4` zaczyna działać powyżej ok. 99 BPM.
- Rolle nie dostają humanize ani opóźnienia. Na LXR losowanie idzie ze sprzętowego generatora, więc każde przejście brzmi trochę inaczej.

### Warunki trigów

- **Gdzie:** na stronie kroku pole `prb` idzie dalej niż 100. Wartości 101–115 to warunki: `1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4 Fil !Fl 1st !1s Pre !Pr`.
- **`A:B`:** krok gra w pętli A z każdych B pętli ścieżki. Pętla to jej własna długość w jej tempie, liczona od START i od zmiany wzorca: `1:2` gra w pętlach 1, 3, 5…, `3:4` w pętlach 3, 7…
- **`1st`:** tylko w pierwszej pętli po START i po zmianie wzorca; **`!1s`:** we wszystkich poza nią.
- **`Pre`:** gra, gdy zagrał ostatni krok z warunkiem na tej ścieżce; **`!Pr`:** gdy nie zagrał (jak na Elektronie). Stan ustawiają kroki z prawdopodobieństwem poniżej 100 i z warunkami `A:B`, `Fil`, `!Fl`, `1st`, `!1s`; `Pre`, `!Pr` i 100 go nie zmieniają. Po START i po zmianie wzorca stan to „nie zagrał".
- **`Fil`, `!Fl`:** tryb fill przyjdzie w v0.4. W v0.2 `Fil` nigdy nie gra, a `!Fl` gra zawsze.
- Firmware 1.7 i v0.1 grają każde prawdopodobieństwo powyżej 100 jak 100, więc tam kroki z warunkami grają w każdej pętli.

### Rotacja ścieżki

- **SHIFT + krok 1** przesuwa wszystkie kroki wybranego głosu we wzorcu, który widzisz, o jeden krok wcześniej; **SHIFT + krok 2** o jeden krok później.
- Z krokiem idzie wszystko, co niesie: nuta, velocity, prawdopodobieństwo albo warunek, parameter locki, shift i ratchet.
- Rotacja działa w długości ścieżki: krok z końca wraca na początek (i odwrotnie), a kroki za długością zostają na miejscu.
- W 1.7 i v0.1 SHIFT + krok 1 i SHIFT + krok 2 nic nie robiły.

### Pliki

- Każdy plik wzorca (`.PAT` w projekcie i autosave `tmp.pat`) dostaje na końcu blok 34 bajtów z ustawieniami siedmiu ścieżek: tempo, humanize, opóźnienie. Obejmują go zapis i wczytanie wzorca i projektu, autosave oraz kopiowanie i czyszczenie wzorca i ścieżki.
- Wzorzec bez bloku (zapisany przez 1.7 albo v0.1) albo z blokiem nieznanym lub uszkodzonym wczytuje się z ustawieniami domyślnymi (`x1`, 0, 0, 0), nigdy z ustawieniami poprzedniego wzorca.
- Zgodność z 1.7 i v0.1 w drugą stronę: patrz „Powrót do v0.1 albo do oficjalnego firmware".

## Poprawione

Poprawki firmware z pracy nad emulatorem (F1–F12):

- Węzeł modulacji (LFO, velocity) bez celu nie czyta już pamięci spod adresu 0 (F1). Brzmienie się nie zmienia.
- Przy starcie bez karty gałka morph nie przechodzi już w stronę przypadkowych bajtów, tylko w stronę slotu kitu 0 (F2).
- Po wyborze wzorca przyciskami kroków strona Euklid pokazuje długość, kroki i rotację wybranego głosu, a nie przypadkowe bajty (F12).
- Oscylatory wavetable i FM na końcu okresu czytają swój wiersz tabeli zamiast następnego albo pamięci za tabelą (F9). To trochę zmienia dźwięk: atak wysokich głosów z mocną obwiednią pitch (powyżej 14 kHz) i falę rect raz na okres, poniżej 440 Hz najwyżej o 7 LSB, w wysokim rejestrze (1,76–14 kHz) wyraźniej.
- Pozostałe zmiany (F3–F8, F6b, F10, F11) nie zmieniają działania.

## Zmienione zachowanie

- Diody kroków i nagrywanie na żywo ścieżki w innym tempie niż `x1` idą po jej własnych krokach.
- SHIFT + krok 1 i SHIFT + krok 2 obracają ścieżkę.
- Strona Euklid ma drugi ekran (pierwszy kończy się znakiem `>`), a `prb` idzie do 115.
- Pliki `.PAT` i `tmp.pat` są o 34 bajty dłuższe.
- Dźwięk fali rect i bardzo wysokich głosów wavetable i FM (F9, wyżej).
- Ekran startowy pokazuje `LICHOMAT` i `v0.2 unofficial`.

## Bez zmian

- Wszystko z v0.1: poprawki timingu i MIDI, strona CPU, zachowanie zegarów.
- Wzorzec ze ścieżkami w `x1`, bez humanize, opóźnienia i warunków, gra jak w v0.1.
- Formaty kitów, projektów, piosenek i ustawień globalnych; MIDI CC i NRPN.

## Znane ograniczenia

- Przy zmianie wzorca ścieżka w `3/4` lub wolniejsza gubi wczesne uderzenie swojego nowego pierwszego kroku, jeśli offset (shift, opóźnienie, humanize) przesuwa je o więcej niż jeden krok mastera wcześniej, np. `3/4` z shift −7. Podobnie w v0.1 nie grają uderzenia sprzed startu.
- Przy szybkich tempach ścieżki działają granice z „Humanize i opóźnienie": `dly` −15 przy shift −7 nic już nie dodaje, a w `x4` powyżej ok. 99 BPM duże opóźnienie z humanize zatrzymuje się na jednym kroku ścieżki.
- Ustawień ścieżek (`tmp hmt hmv dly`) nie da się zmieniać przez MIDI CC ani NRPN.
- Po Song Position Pointer ścieżka w innym tempie niż `x1` biegnie dalej na swojej siatce od początku odtwarzania; jej krok, który trwa w miejscu skoku, nie gra.
- Zmiana `tmp` działa od następnego kroku ścieżki, a nie od razu.
- Tryb fill (`Fil`, `!Fl`) przyjdzie w v0.4.
- Ograniczenia v0.1 zostają, np. głos startuje na początku bloku audio (±1,45 ms).

## Pliki

- `LXR_V2_LICHOMAT_0.2.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.2`, SHA-256: (wpisany, gdy zbuduje go CI; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.2` w `isthar/lxr-desktop-app` (archiwa „Source code" wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths", „LXR" i „LXR-02" to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/demo/` (poza gitem, na Twoim Macu), opis w `build/demo/README-v0.2.txt`. Robi je `tools/demo_v02.sh` z gałęzi `v0.2`: 120 BPM, wyjście 1 stereo, 18 s, granie zaczyna się po ok. 1–2,5 s ustawiania panelu. Obok każdego WAV leży skrypt naciśnięć (`.txt`) i log MIDI (`.midi.log`).

- `v02-1-3-against-4.wav`: głos 2 w `3/4` na ćwierćnutach przeciw głosowi 1 na ćwierćnutach: uderzenia co 666,7 ms, spotkanie co 2 s.
- `v02-2-5-steps-at-x3_4.wav`: 5-krokowa ścieżka w `3/4` (głos 4 na krokach 1 i 3) przesuwa się względem ćwierćnut; układ wraca co 2,5 s.
- `v02-3-hats-x2.wav`: hihat w `x2` na wszystkich krokach: 32-ki co 62,5 ms; porównaj z 4.
- `v02-4-hats-straight.wav`: to samo w `x1`, co 125 ms: odniesienie dla 3 i 5.
- `v02-5-hats-humanized.wav`: jak 4, z `hmt` 6 i `hmv` 20: odstępy 111,75–138,25 ms, velocity 102–127; stopa zostaje równa.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1 nie trzeba powtarzać, poza H1 i H2 (U7).

- **U4–U9:** zmiany F1–F12: modulacja i CC 1 (U4), start bez karty i morph (U5), strony czyszczenia automatyki (U6), H1 i H2 jeszcze raz (U7), odsłuch F9 względem v0.1 (U8: nagraj te dźwięki na v0.1 przed instalacją v0.2), strona Euklid po wyborze wzorca (U9).
- **U10:** tempo ścieżki: 3 na 4, hihat w `x2`, diody kroków, MIDI clock, restart przy zmianie wzorca i w piosence.
- **U11:** humanize i opóźnienie: rozrzut czasu i velocity, `dly` +4 i −15, `x4` przy 255 BPM bez zgubionych uderzeń.
- **U12:** warunki trigów: `1:2`, `3:4`, `1st`, `Fil`, `!Fl`, `!Pr`, i ten sam wzorzec na 1.7.
- **U13:** rotacja z ratchetem i parameter lockiem, też przy długości 5.
- **U14:** pliki: wyłączenie i włączenie, zapis i wczytanie projektu, autosave `tmp.pat`, wzorce z 1.7, wzorzec v0.2 na 1.7 i z powrotem.

## Publikacja (tylko właściciel)

1. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i dwie sekcje „tylko dla Ciebie". Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.2` jako `docs/lichomat/release-notes-v0.2.md`, zrób commit i push `v0.2` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebieg CI (`.github/workflows/build.yml`) przejdzie.
2. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki" (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki" i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
3. Otaguj ten commit `v0.2`, wypchnij tag i utwórz z niego wydanie GitHub `v0.2` z tytułem `LICHOMAT v0.2`, z tekstem od „LICHOMAT to nieoficjalny firmware" do końca „Licencja" (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.2.img` z artefaktu.
