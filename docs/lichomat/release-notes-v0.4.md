# LICHOMAT v0.4: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.4` w `isthar/lxr-desktop-app`, gdy pomiar H12 i sprawdzenie U20 ustalą częstotliwość pogłosu, gdy przejdziesz na LXR sprawdzenia U21–U26 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.4` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.4 ma działać według testów na hoście (104) i w emulatorze (184). **Częstotliwość pogłosu jest tymczasowa** (połowa, zob. „Częstotliwość pogłosu") do czasu H12 i U20. SHA-256 pliku aktualizacji dojdzie, gdy CI zbuduje commit wydania. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware" do końca „Licencja"; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.4 bazuje na v0.3, ta na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1–v0.3, zostaje. v0.4 dodaje atmosferę: pogłos za slotem efektu (delay → pogłos) z freeze, rumble (pogłos przyciszany przez wybrany głos), delay zsynchronizowany z tempem i filtr DJ na obu wyjściach stereo.

Nowe ustawienia kitu zapisują się w 13-bajtowym bloku za 247 bajtami parametrów każdego pliku kitu (`.SND` i `tmp.snd`, 255 → 268 B). Firmware 1.7 i v0.1–v0.3 ten blok pomijają.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: v0.4 dopisuje blok do plików kitów, a to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.4.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.4 unofficial`.

## Powrót do v0.3, v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.4.img` i zainstaluj tak samo:

- v0.3: `LXR_V2_LICHOMAT_0.3.img` (plik z run sheet listy sprawdzeń); ekran startowy `LICHOMAT` i `v0.3 unofficial`;
- v0.2: `LXR_V2_LICHOMAT_0.2.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.2 unofficial`;
- v0.1: `LXR_V2_LICHOMAT_0.1.img` (SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Wszystkie wczytują projekty, wzorce i kity zapisane przez v0.4 bez błędów i grają kit swoim efektem slotu: delay zsynchronizowany gra tam swój wolny czas (`tim`, `rng`), bez pogłosu i bez filtra DJ. Kit zapisany tam od nowa traci blok (268 → 255 B). v0.4 wczytuje taki kit, tak jak każdy kit z 1.7 i z v0.1–v0.3, z ustawieniami domyślnymi: `lvl` 0 (pogłos wyłączony), `siz` 100, `dec` 64, `ton` 64, `pre` 10, `mod` 40, `syn` `off`, `dck` `off`, `ddp` 100, `djr` 0. `glo.cfg` ma dalej 30 bajtów.

## Nowe

### Pogłos

- **Gdzie:** strona FX (SHIFT + krok 15). Pierwsze naciśnięcie otwiera podstronę slotu, jak w 1.7; każde następne SHIFT + krok 15 na stronie FX przechodzi dalej: podstrona pogłosu (`lvl siz dec ton` na pierwszym ekranie, `pre mod frz syn` na drugim), potem podstrona `dck ddp djf djr`, potem znowu slot.
- **Co robi:** pogłos stoi na szynie FX za slotem efektu (dystorsja, ring, kompresor albo delay), więc echa delaya też się pogłosują. Wyjście jest stereo. `lvl` 0 wyłącza pogłos (wtedy nie liczy się wcale); wyżej głośność mokrego sygnału rośnie jak (`lvl`/127)².
- `siz` to wielkość sali, `pre` predelay, `mod` wolny ruch ogona (LFO 0,2–1,5 Hz). `dec` to czas wybrzmienia RT60 od 0,3 s (0) do 19,2 s (127), w sekundach przy każdym `siz`.
- **`ton`:** 0 to ciemny dolnoprzepustowy ok. 1,5 kHz, 64 płasko, 127 łagodny górnoprzepustowy ok. 400 Hz (6 dB na oktawę). Kroki są równe w oktawach, więc środek każdej strony ma tyle kroków co jej końce. Zmiana działa od razu także na ogon, który już brzmi; poziom zostaje mniej więcej ten sam.

### Freeze

- **Gdzie:** w trybie wykonawczym SHIFT + krok 3 włącza i wyłącza freeze; dopóki trzymasz SHIFT, dioda kroku 3 pokazuje stan. Także `frz` na podstronie pogłosu i NRPN 99 (0 wyłącza, 1–127 włącza).
- **Co robi:** ogon trzyma się bez końca, z wolnym ruchem. Traci ok. 0,3 dB na minutę przy każdym `siz`, więc nigdy nie rośnie. Nowe uderzenia nie wchodzą do zamrożonego ogona: słychać je na sucho. Wyłączenie wraca do ustawień `dec`, `ton` i `mod`.
- Freeze to kontrolka na żywo: wczytanie kitu i morph go nie zmieniają, a kit zapisuje się zawsze z `frz` `off`.

### Rumble

- **Gdzie:** podstrona `dck ddp djf djr` (SHIFT + krok 15 na stronie FX, po podstronie pogłosu).
- **Co robi:** `dck` wybiera głos, który przycisza pogłos: `off`, `dr1`, `dr2`, `dr3`, `snr`, `cym`, `hat` (ścieżki 6 i 7 dzielą hihat); `ddp` 0–127 to głębokość. Detektor szczytów słucha bloku tego głosu (zwolnienie ok. 80 ms), bez opóźnienia.
- Klasyczny rumble: stopa do szyny FX, ciemny pogłos, `dck` `dr1`: ogon puchnie między uderzeniami i chowa się pod nimi.

### Delay zsynchronizowany z tempem

- **Gdzie:** `syn` na drugim ekranie podstrony pogłosu (gałka 4): `off 16t 16 8t 16d 8 4t 8d 4 4d`.
- **Co robi:** czas delaya idzie za tempem, także za zewnętrznym zegarem MIDI; zmiana czasu przechodzi przez 10-milisekundowe przenikanie delaya. Gdy `syn` nie jest `off`, `tim` i `rng` nie działają (ich wartości zostają w kicie).
- **Za długi czas dzieli się na pół, aż się zmieści:** zwykła linia mieści 32 703 próbki (ok. 741 ms), ping-pong 16 319 (ok. 370 ms). Zwykła linia bierze `4` od 81 BPM i `4d` od 122 BPM; ping-pong bierze `8d` od 122 BPM, `4t` od 109 BPM, a `4` dopiero od 163 BPM. Połowa zostaje na siatce: `4d` przy 120 BPM gra jak `8d` (375 ms).

### Filtr DJ

- **Gdzie:** `djf` i `djr` na drugim ekranie strony trybu wykonawczego (gałki 3 i 4) i na podstronie `dck ddp djf djr`; NRPN 96 (`djf`) i 97 (`djr`).
- **Co robi:** jeden filtr na obu wyjściach stereo (1 i 2), za szyną FX. 0–62: dolnoprzepustowy od 19,5 Hz do 17,9 kHz; 63–65: płasko (filtr się nie liczy); 66–127: górnoprzepustowy od 22,4 Hz do ok. 20 kHz. `djr` 0–127: rezonans (Q 0,5–5). Wejście i wyjście z płaskiego środka idzie bez kliku; skok z jednej strony na drugą przechodzi przez płasko w dwóch blokach (2,9 ms).
- `djf` to kontrolka na żywo, jak freeze: wczytanie kitu i morph go nie zmieniają, a kit zapisuje się zawsze z `djf` 64 (płasko). `djr` należy do kitu.

### MIDI

- NRPN 96 `djf`, 97 `djr`, 98 `lvl`, 99 `frz`, 116 `dec` (7-bitowy NRPN: CC 99 = 0, CC 98 = numer, CC 6 = wartość).
- Siedem pozostałych ustawień (`siz`, `ton`, `pre`, `mod`, `dck`, `ddp`, `syn`) jest tylko w menu; NRPN do nich jest odrzucany.

## Częstotliwość pogłosu

W tym szkicu pogłos liczy się w połowie częstotliwości próbkowania. **To ustawienie tymczasowe:** ostateczne wybiera reguła z pomiaru CPU H12 (najwyższe `pk` kroku 3, ciężki wzorzec z delayem, P):

- pełna częstotliwość, gdy P ≤ 46,24;
- połowa, gdy P ≤ 63,28;
- inaczej połowa w wersji oszczędnej (bez wczesnych odbić, mokry sygnał mono).

Granice to 85 % minus szacowany koszt pogłosu (35,07 % przy pełnej częstotliwości, 18,03 % przy połowie) i filtra DJ (3,69 %). Potem U20 czyta `pk` z włączonym pogłosem; jeśli dojdzie do 95, wydanie dostaje następny tryb w dół (pełna → połowa → oszczędna). Przy połowie ten sam `siz` daje większą salę, a `pre` sięga 2–92 ms (przy pełnej 1–46 ms); `dec` zostaje w sekundach przy obu.

## Zmienione zachowanie

- SHIFT + krok 15 na stronie FX przechodzi między podstronami slotu, pogłosu i `dck ddp djf djr` (w 1.7 zostawał na podstronie slotu).
- SHIFT + krok 3 w trybie wykonawczym przełącza freeze (w 1.7 nic nie robił); dioda kroku 3 przy trzymanym SHIFT pokazuje freeze.
- Strona trybu wykonawczego ma `djf` i `djr` na drugim ekranie, po `scn`.
- Plik kitu ma 268 bajtów zamiast 255. Bajt typu pogłosu z 1.7 (efekt wyłączony tam z kompilacji) trzyma teraz `dec`, a cztery nieużywane bajty kitu `djf`, `djr`, `lvl` i `frz`.
- Ekran startowy pokazuje `LICHOMAT` i `v0.4 unofficial`.

## Bez zmian

- Wszystko z v0.1–v0.3.
- Kit z `lvl` 0, `djf` w środku i `syn` `off` brzmi jak w v0.3, bit w bit (18 złotych renderów emulatora z v0.3 bez zmian); tak gra każdy kit z 1.7 i z v0.1–v0.3.
- Formaty wzorców, projektów, piosenek i `glo.cfg` (30 bajtów); MIDI CC.
- Pamięć: DTCM + RAM 452 636 z 455 544 B budżetu, DTCM 107 396 z 107 900 B, RAM 345 240 z 389 120 B; bufor pogłosu (57 344 B) jest w RAM, delay zachowuje swój bufor w DTCM.

## Znane ograniczenia

- Częstotliwość pogłosu czeka na H12 i U20: w tym szkicu połowa, tymczasowo (zob. „Częstotliwość pogłosu").
- `djf`, `djr`, `lvl`, `frz` i `dec` nie mają LFO, velocity ani p-locków: tylko menu i NRPN 96–99 i 116.
- Zapis kitu w 1.7 (także w v0.1–v0.3) usuwa blok; v0.4 wczytuje potem taki kit z ustawieniami domyślnymi, z wyłączonym pogłosem.
- `siz`, `ton`, `pre`, `mod`, `dck`, `ddp` i `syn` nie mają NRPN.
- Za długi zsynchronizowany czas gra o połowę (albo ćwierć) krócej; ping-pong bierze `4` dopiero od 163 BPM.
- Zamrożony ogon nie przyjmuje nowego dźwięku.
- Ograniczenia v0.1–v0.3 zostają, np. głos startuje na początku bloku audio (±1,45 ms).

## Pliki

- `LXR_V2_LICHOMAT_0.4.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.4`, SHA-256: (wpisany, gdy CI zbuduje commit wydania; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.4` w `isthar/lxr-desktop-app` (archiwa „Source code" wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths", „LXR" i „LXR-02" to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/demo/` (poza gitem, na Twoim Macu), opis w `build/demo/README-v0.4.txt`. Robi je `tools/demo_v04.sh` z gałęzi `v0.4`: 125 BPM, wyjście 1 stereo, karty `emu/tests/cards`; pogłos gra w nich w połowie częstotliwości (tymczasowo). Pierwsza linia każdego `.txt` obok WAV podaje opcje renderera. Renderer nie ustawia celów LFO, więc w przepisach 8 i 9 LFO zastępują kroki parametru co 80 ms. Sprawdzone z plików WAV (poziomy i ogony), bez słuchania.

- `v04-1-delay-sync-1-8d.wav`: delay zsynchronizowany do `8d` (360 ms): jedno uderzenie i jego echa, potem 4 takty i ogon ech.
- `v04-2-delay-wolny-czas.wav`: to samo z wolnym czasem delaya (`syn` `off`), do porównania z 1.
- `v04-3-dj-filtr-sweep.wav`: filtr DJ przez 8 taktów od 0 do 127: dolnoprzepustowy się otwiera, w środku płasko, potem górnoprzepustowy.
- `v04-4-dj-filtr-sweep-reso-100.wav`: ten sam sweep z `djr` 100.
- `v04-5-poglos.wav`: pogłos `lvl` 110, `siz` 110, `dec` 90, `ton` 50: jedno uderzenie z ogonem, potem 2 takty i ogon po zatrzymaniu (ok. −10 dB na sekundę).
- `v04-6-freeze.wav`: 2 takty, freeze na ostatnim uderzeniu; ogon trzyma się 8 s w granicach 0,3 dB, potem odmarza i wygasa.
- `v04-7-rumble-stopa.wav`: sama stopa w ciemnym pogłosie (`ton` 20, `dec` 80) z `dck` `dr1`, `ddp` 110: zaraz po uderzeniu ogon jest o ok. 17 dB ciszej niż bez przyciszania, potem puchnie do następnego.
- `v04-8-przepis-noise-wash.wav`: przepis „noise wash": szum werbla z filtrem pasmowym w ruchu, `siz` 127 `dec` 115 `ton` 40 `mod` 80, na końcu 6 s freeze.
- `v04-9-przepis-fm-drone.wav`: przepis „FM drone": Drum 2 z FM w ruchu, `siz` 110 `dec` 105 `ton` 30.
- `v04-10-przepis-rumble-kick.wav`: przepis „rumble kick": stopa w pogłosie jak w 7, werbel, talerz i hihat na sucho.
- `v04-11-przepis-dub-stab.wav`: przepis „dub stab": krótki talerz przez delay `8d` z `fbk` 90 i pogłos `dec` 80, stopa na sucho.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1–v0.3 nie trzeba powtarzać. Najpierw H12 na v0.3: jego `pk` wybiera częstotliwość pogłosu; resztę sprawdzeń v0.4 rób dopiero na buildzie, którego częstotliwość ustaliły H12 i U20.

- **U20:** CPU z pogłosem (`lvl` 100, `siz` 127, `dec` 127), potem z `djf` 30, z freeze i z `lvl` 0.
- **U21:** pogłos i freeze: ogon i `ton` od 0 do 127, SHIFT + krok 3 i 60 s freeze, `frz`, NRPN 99, 98 i 116, freeze przy wczytaniu kitu.
- **U22:** rumble przy 125 i 140 BPM.
- **U23:** delay zsynchronizowany: `8d`, zewnętrzny zegar, połowa przy `4d` i przy ping-pongu.
- **U24:** filtr DJ na obu wyjściach, szybkie ruchy, NRPN 96, wczytanie kitu.
- **U25:** zgodność z 1.7: rozmiary plików, kit w 1.7 i z powrotem.
- **U26:** ekran startowy i cztery przepisy z `docs/lichomat/kit-recipes-v0.4.md`.

## Poprawka do planu v0.3 (tylko dla Ciebie)

F17 planu v0.3 podawał, że numer parametru `UNUSED_PAR_GLO_SAT` jest używany przez `mixer.c`. To błąd: nasycenie z tym parametrem jest w `mixer.c` w komentarzu, a jego przypadek w menu jest pusty, więc numer jest wolny. Nic w v0.3 od tego nie zależy; v0.4 go nie bierze.

## Publikacja (tylko właściciel)

1. Gdy H12 i U20 ustalą częstotliwość pogłosu, a build tej częstotliwości przejdzie U21–U26, sekcja „Częstotliwość pogłosu" dostaje ostateczny tekst (bez „tymczasowe"), a „Znane ograniczenia" tracą pierwszy punkt.
2. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i sekcje „tylko dla Ciebie". Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.4` jako `docs/lichomat/release-notes-v0.4.md`, zrób commit i push `v0.4` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebiegi CI (`.github/workflows/build.yml` i `emulator.yml`) przejdą.
3. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki" (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki" i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
4. Otaguj ten commit `v0.4`, wypchnij tag i utwórz z niego wydanie GitHub `v0.4` z tytułem `LICHOMAT v0.4`, z tekstem od „LICHOMAT to nieoficjalny firmware" do końca „Licencja" (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.4.img` z artefaktu.
