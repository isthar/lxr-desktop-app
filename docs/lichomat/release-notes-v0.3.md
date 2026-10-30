# LICHOMAT v0.3: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.3` w `isthar/lxr-desktop-app`, gdy przejdziesz na LXR sprawdzenia U15–U19 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.3` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.3 ma działać według testów na hoście (90) i w emulatorze (145). SHA-256 pliku aktualizacji dojdzie, gdy zbuduje go CI. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware" do końca „Licencja"; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.3 bazuje na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1 i v0.2, zostaje. v0.3 dodaje narzędzia do grania na żywo i do linii acid: tryb fill, osiem scen mute, rolle od całego taktu do 1/32 razem z triolami oraz slide dla Drum 1–3.

Sceny zapisują się z projektem, za jego nazwą w pliku `prj.nfo`; znacznik slide siedzi w bajcie prawdopodobieństwa kroku, a czas slide w trzech bajtach kitu, których 1.7 nie używa. Firmware 1.7, v0.1 i v0.2 te dane pomijają.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: v0.3 dopisuje blok do plików projektów, a to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.3.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.3 unofficial`.

## Powrót do v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.3.img` i zainstaluj tak samo:

- v0.2: `LXR_V2_LICHOMAT_0.2.img` (plik z run sheet listy sprawdzeń); ekran startowy `LICHOMAT` i `v0.2 unofficial`;
- v0.1: `LXR_V2_LICHOMAT_0.1.img` (SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Wszystkie trzy wczytują projekty, wzorce i kity zapisane przez v0.3 bez błędów. Krok ze slide gra tam bez slide, ze swoją nutą i z poprawnym MIDI; jeśli miał prawdopodobieństwo albo warunek, gra jak przy 100. Nie ruszaj tam `prb` takich kroków: tamten edytor zdejmie znacznik slide. Projekt zapisany tam od nowa traci sceny mute. Kit zachowuje czas slide (`slt`) także po wczytaniu i zapisie w 1.7.

## Nowe

### Tryb fill

- **Gdzie:** w trybie wykonawczym (drugi przycisk trybu, bez SHIFT) przytrzymaj krok 8. Dopóki go trzymasz, tryb fill jest włączony, a dioda kroku 8 świeci.
- **Co robi:** kroki z warunkiem `Fil` grają, a kroki z `!Fl` milkną (warunki z v0.2, pole `prb` na stronie kroku). Puszczenie kroku 8 kończy fill, nawet gdy trzymasz przy tym SHIFT, LOAD albo SAVE.
- **Kiedy:** zmiana działa od pierwszego kroku, który jeszcze się nie zaczął, na każdej ścieżce w jej tempie. Krok przesunięty do przodu (ujemny shift albo `dly`) rozstrzyga się krok wcześniej.
- Poza trybem wykonawczym krok 8 edytuje krok jak dotąd.
- **Czego posłuchać:** werbel z `Fil` na końcu taktu wchodzi tylko przy trzymanym kroku 8, a hihat z `!Fl` wtedy milknie.

### Sceny mute

- **Gdzie:** w trybie wykonawczym kroki 9–16 przywołują sceny 1–8. Żeby zapisać scenę, wycisz głosy przyciskami głosów, przytrzymaj COPY i naciśnij krok 9–16: scena zapamiętuje, które głosy są wyciszone.
- **Kiedy działa:** ustawienie `scn` (drugi ekran strony trybu wykonawczego, globalne). `bar`: na początku następnego taktu ścieżki mastera (co 16 kroków mastera), domyślnie. `stp`: na następnym kroku mastera. Po zatrzymaniu: od razu. Do tej chwili dioda przywołanego kroku mruga, a potem diody głosów pokazują scenę. Przy kilku przywołaniach przed granicą wygrywa ostatnie.
- Pusta scena nic nie zmienia.
- **Zapis:** sceny zapisują się z każdym zapisem projektu i wczytują z każdym wczytaniem projektu, także przy włączeniu LXR. Nowy projekt nie ma scen. Bieżące wyciszenia, jak dotąd, nie zapisują się nigdzie.

### Rolle

- **Gdzie:** pole `rol` na stronie trybu wykonawczego; kroki 1–7 trzymane w tym trybie rollują głosy 1–7, jak w 1.7.
- **Jedenaście temp**, od wolnego do szybkiego: `One` (jedno uderzenie na naciśnięcie), `1/1` (jedno na takt), `1/2`, `1/3`, `1/4`, `1/6`, `1/8`, `12`, `16`, `24`, `32`. `1/3`, `1/6`, `12` i `24` to triole; `32` daje dwa uderzenia na krok.
- Uderzenia są dokładne w tickach, także triole, i trzymają się kroków ścieżki: przy 120 BPM `32` co 62,5 ms, `24` co 83,3 ms, `12` co 166,7 ms, `16` co 125 ms.
- **Własne tempo ścieżki:** roll idzie po krokach swojej ścieżki, więc w `3/4` (v0.2) `12` daje uderzenia co 222,2 ms.
- Każde uderzenie dostaje swing swojego kroku, jak ratchety.

### Slide dla Drum 1–3

- **Gdzie:** na stronie kroku nowe pole `sld` (`off`/`on`), za `prb`. Czas slide `slt` (0–127, × 4 ms, czyli do 508 ms) jest na drugim ekranie strony oscylatora głosów 1–3 (przycisk wyboru 1) i należy do kitu.
- **Co robi:** krok z `sld` na Drum 1–3 wyzwala się jak zwykle (obwiednie startują od nowa), ale wysokość oscylatora i modulatora zaczyna się od poprzedniej nuty głosu i jedzie liniowo w półtonach do nowej nuty przez czas `slt`. `slt` 0 to brak slide.
- Przy ratchecie glide zaczyna się na pierwszym uderzeniu kroku i trwa dalej przez jego kolejne uderzenia; rolle nigdy nie mają slide.
- Akcent (głośność kroku, też z modulacją velocity) działa jak dotąd; MIDI out wysyła te same nuty co bez slide.
- Na głosach 4–7 `sld` się zapisuje, ale nic nie robi.
- **Czego posłuchać:** linia z nutami co oktawę i kwintę, ze `sld` na części kroków: dźwięk podjeżdża od poprzedniej nuty, jak w 303.

## Zmienione zachowanie

- Krok 8 w trybie wykonawczym to tryb fill (w 1.7 nic nie robił).
- Kroki 9–16 w trybie wykonawczym przywołują sceny, a COPY + krok 9–16 je zapisuje (w 1.7 nic nie robiły).
- Lista temp rolla ma jedenaście pozycji zamiast sześciu, więc te same numery wskazują inne tempa; zewnętrzny NRPN dla rolla wybiera z nowej listy. Domyślne tempo to dalej `16`.
- Strona kroku ma `sld` między `prb` a drugim ekranem (`flm`, `shf`); enkoder dochodzi do `flm` o jedno pole później.
- Strona oscylatora Drum 1–3 ma `slt` na drugim ekranie, a strona trybu wykonawczego ma `scn` na drugim ekranie, po `shu`.
- Plik `prj.nfo` każdego projektu ma 22 bajty zamiast 8.
- Ekran startowy pokazuje `LICHOMAT` i `v0.3 unofficial`.

## Bez zmian

- Wszystko z v0.1 i v0.2: timing, MIDI, strona CPU, tempo ścieżki, humanize, opóźnienie, warunki trigów, rotacja.
- Wzorzec bez kroków ze slide i bez warunków `Fil`/`!Fl` gra jak w v0.2; temp rolla `One`, `1/1`, `1/2`, `1/4`, `1/8`, `16` grają jak dotąd.
- Formaty wzorców, kitów, piosenek i ustawień globalnych (`scn` zajmuje bajt, który 1.7 zapisywał i pomijał); MIDI CC.

## Znane ograniczenia

- Tryb fill, sceny i slide nie mają MIDI CC.
- Sceny zapisują się tylko z projektem, nie w autosave; zapis projektu w 1.7 je usuwa.
- Uderzenie przesunięte przed granicę sceny (ujemny shift albo `dly` pierwszego kroku taktu) idzie jeszcze według starych wyciszeń.
- Fill działa od następnego kroku, który się jeszcze nie zaczął, a nie od razu w trakcie kroku.
- W 1.7 i v0.2 krok ze slide gra bez slide, a jeśli miał prawdopodobieństwo albo warunek, gra jak przy 100.
- Kity zapisane przed v0.3 mogą mieć w `slt` wartości ze starszego firmware (te bajty kiedyś trzymały nuty MIDI głosów). Działają tylko na krokach ze slide, a te robi tylko v0.3; ustaw `slt` przed pierwszym slide.
- Przy shuffle od ok. 95 roll `32` gubi drugie uderzenie nieparzystych kroków, tak jak ratchety od v0.1.
- Ograniczenia v0.1 i v0.2 zostają, np. głos startuje na początku bloku audio (±1,45 ms).

## Pliki

- `LXR_V2_LICHOMAT_0.3.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.3`, SHA-256: (wpisany, gdy zbuduje go CI; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.3` w `isthar/lxr-desktop-app` (archiwa „Source code" wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths", „LXR" i „LXR-02" to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/demo/` (poza gitem, na Twoim Macu), opis w `build/demo/README-v0.3.txt`. Robi je `tools/demo_v03.sh` z gałęzi `v0.3`: 120 BPM, wyjście 1 stereo, granie zaczyna się po ok. 1–7 s ustawiania panelu. Obok każdego WAV leży skrypt naciśnięć (`.txt`) i log MIDI (`.midi.log`); głos N wysyła na kanale MIDI N. Sprawdziłem je po logach MIDI, bez słuchania.

- `v03-1-fill-co-4-takty.wav`: fill trzymany przez takt 4 i takt 8: werbel z `Fil` gra kroki 14–16 tylko w tych taktach, a hihat z `!Fl` wtedy milknie.
- `v03-2-sceny-mute-na-takcie.wav`: trzy sceny przywołane w połowie taktu: bez hihatu od 4,0 s, bez stopy i werbla od 8,0 s, znowu wszystko od 12,0 s po starcie.
- `v03-3-roll-1-32.wav`: roll `32` na hihacie, dwa razy po 2 s, uderzenia co 62,5 ms.
- `v03-4-roll-triola-1-24.wav`: roll `24` (triolowe szesnastki), co 83,3 ms.
- `v03-5-acid-slide-drum1.wav`: linia acid na Drum 1 (16 kroków, nuty 51–63) ze `sld` na krokach 3, 5, 7, 10, 11, 13 i 15 i `slt` 20 (80 ms).
- `v03-6-acid-bez-slide.wav`: ta sama linia bez slide, do porównania z 5.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1 i v0.2 nie trzeba powtarzać.

- **U15:** fill: `Fil` i `!Fl` przy trzymanym kroku 8, koniec fill przy trzymanym SHIFT albo LOAD, krok 8 poza trybem wykonawczym.
- **U16:** sceny: zapis przez COPY, przywołanie na takcie (`bar`), na kroku (`stp`) i po zatrzymaniu, pusta scena, sceny po wyłączeniu i włączeniu, projekt w 1.7 i z powrotem.
- **U17:** rolle: odstępy przy `32`, `24`, `12`, `16`, `12` przy `3/4`, `One`.
- **U18:** slide: czasy glide przy `slt` 15, 127 i 0, poziom ze `sld` i bez, głos 4, wzorzec i kit w 1.7 i z powrotem.
- **U19:** ekran startowy, plik aktualizacji i wartości `slt` Twoich kitów.

## Publikacja (tylko właściciel)

1. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i dwie sekcje „tylko dla Ciebie". Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.3` jako `docs/lichomat/release-notes-v0.3.md`, zrób commit i push `v0.3` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebieg CI (`.github/workflows/build.yml`) przejdzie.
2. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki" (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki" i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
3. Otaguj ten commit `v0.3`, wypchnij tag i utwórz z niego wydanie GitHub `v0.3` z tytułem `LICHOMAT v0.3`, z tekstem od „LICHOMAT to nieoficjalny firmware" do końca „Licencja" (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.3.img` z artefaktu.
