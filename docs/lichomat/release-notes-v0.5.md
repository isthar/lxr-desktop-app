# LICHOMAT v0.5: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.5` w `isthar/lxr-desktop-app`, gdy wyjdzie wydanie 0.4 i jego ostatnie zmiany (częstotliwość pogłosu po H12 i U20, A7–A11) trafią do gałęzi `v0.5`, gdy przejdziesz na LXR sprawdzenia U27–U32 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.5` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.5 ma działać według testów na hoście (114) i w emulatorze (328). Pogłos gra w tym buildzie w tymczasowej częstotliwości szkicu 0.4 (połowa). SHA-256 pliku aktualizacji dojdzie, gdy CI zbuduje commit wydania. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware” do końca „Licencja”; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.5 bazuje na v0.4, ta na v0.3, ta na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1–v0.4, zostaje. v0.5 dodaje timing co do próbki: każdy głos startuje w próbce swojego zdarzenia, zawsze 2,9 ms po nim, a nie z następnym blokiem audio.

Nowe ustawienie `trg` zapisuje się w bajcie 9 pliku `glo.cfg`, którego firmware 1.7 nie czyta. Żaden plik nie zmienia rozmiaru ani formatu.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.5.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.5 unofficial`.

## Powrót do v0.4, v0.3, v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.5.img` i zainstaluj tak samo:

- v0.4: `LXR_V2_LICHOMAT_0.4.img` (plik z run sheet listy sprawdzeń); ekran startowy `LICHOMAT` i `v0.4 unofficial`;
- v0.3: `LXR_V2_LICHOMAT_0.3.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.3 unofficial`;
- v0.2: `LXR_V2_LICHOMAT_0.2.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.2 unofficial`;
- v0.1: `LXR_V2_LICHOMAT_0.1.img` (SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Wszystkie wczytują projekty, wzorce, kity i `glo.cfg` zapisane przez v0.5 bez błędów. Bajt 9 `glo.cfg` pomijają, a głosy startują tam na początku bloku audio, jak w 0.4. `glo.cfg` zapisany w 1.7 ma w tym bajcie 0, więc v0.5 czyta go jako `trg` `smp`.

## Nowe

### Każdy głos w próbce swojego zdarzenia

- **Co robi:** do 0.4 głos startował z następnym blokiem audio po swoim zdarzeniu. Blok to 64 próbki (1,45 ms), więc dźwięk przychodził 64–128 próbek po zdarzeniu, przy każdym uderzeniu z innym opóźnieniem. W 0.5 każdy głos startuje w próbce swojego zdarzenia, z dokładnością do jednej próbki.
- **Czego to dotyczy:** wszystkiego, co ma swój czas: kroków na siatce, przesunięć kroków, shuffle, humanize, opóźnienia ścieżki (`dly`), ratchetów, rolli (także 1/32 i triol), własnego tempa każdej ścieżki, nut MIDI z DIN i z USB i grania jako slave zegara MIDI.
- Dwa uderzenia o jeden tick od siebie (1,30 ms przy 120 BPM) leżą teraz zawsze 56 albo 58 próbek od siebie; w 0.4 raz razem, raz 64 próbki osobno. Flam brzmi więc tak samo przy każdym uderzeniu.
- Uderzenia z jednego ticku startują dalej w tej samej próbce.
- Brzmienie nuty się nie zmienia (wyjątki są w „Znanych ograniczeniach”): każdy krok sterowania nuty (obwiednie, snap, slide) trwa 64 próbki i przychodzi w tym samym miejscu nuty co w 0.4. W emulatorze każde z 36 uderzeń stopy z karty init przy 120, 125 i 130 BPM jest bit w bit takie samo jak każde inne i jak uderzenie z 0.4.

### Stałe opóźnienie 2,9 ms

- Od zdarzenia do dźwięku jest zawsze 128 próbek (2,90 ms). W 0.4 było 64–128 próbek, średnio 2,18 ms.
- Wobec MIDI out i gniazda clock out dźwięk przychodzi więc o 0–1,45 ms później niż w 0.4, średnio o 0,73 ms, i za każdym razem o tyle samo. Offset w DAW (opóźnienie ścieżki MIDI albo nagrania) bierze to raz na zawsze; U28 mierzy tę wartość.
- Jako slave zegara MIDI LXR gra stałe 2,90 ms za przychodzącym zegarem; DAW może to dokładnie skompensować.

### Ustawienie `trg`

- **Gdzie:** menu globalne (SHIFT + krok 12), druga podstrona, `kcm cki cko trg`: gałka 4.
- **Wartości:** `smp` (nowy timing, domyślny) i `blk` (timing 0.4: głos startuje na początku następnego bloku). `blk` to droga kodu z 0.4; w emulatorze render w `blk` jest bit w bit taki jak z 0.4. Służy do porównania A/B i jako zapas, gdyby zabrakło CPU.
- Zapisuje się z ustawieniami globalnymi (strona zapisu, typ global, jak w 1.7). Zmiana w trakcie grania działa od następnego uderzenia.

## Zgodność

- Żaden format pliku się nie zmienia: wzorce, kity, `prj.nfo` i bloki v0.2–v0.4 zostają jak były, `glo.cfg` ma dalej 30 bajtów.
- `trg` zajmuje bajt 9 `glo.cfg` (w 1.7 nieużywany parametr `PAR_QUANTISATION`): 0 to `smp`, 1 to `blk`, każda inna wartość daje `smp`. Firmware 1.7 zapisuje tam 0 i nigdy tego bajtu nie czyta: `glo.cfg` z 1.7 daje więc `smp`, a `glo.cfg` z 0.5 wczytuje się w 1.7 bez błędu.

## CPU

Koszt policzony statycznie, metodą raportu `docs/lichomat/reports/v0.4-reverb-cost.md`, ze współczynnikiem 1,5 z D41, dla kodu tego buildu:

- Stały koszt każdego bloku: ok. 0,065 %.
- Brzmiący głos, którego nuta nie zaczęła się na granicy bloku, liczy w każdym bloku jeden krok sterowania w swoim miejscu. Gdy tak brzmi wszystkich sześć głosów, średnia (`CPU`) rośnie o ok. 1,24 punktu; sama stopa 4/4 z karty init o ok. 0,11 punktu (taki krok ma w 57 % bloków).
- Najgorszy blok, gdy wszystkich sześć głosów uderza w jednym bloku: ok. +3,64 punktu `pk`, z sześcioma retriggerami LFO ok. +4,64. Przy granicy 85 % z D41 `pk` zostaje więc najwyżej ok. 89,6, poniżej 95.
- Gęste bloki (kilka uderzeń naraz) podnoszą przede wszystkim `pk`; zwykły groove podnosi go mniej niż najgorszy przypadek.
- U30 sprawdza to na ciężkim wzorcu z H12: `pk` w `smp` najwyżej ok. 3,7 punktu nad `blk` (4,7 z retriggerami LFO), poniżej 95, `ovf` 0. Jeśli nie, poprawką jest build, który ogranicza podziały do trzech głosów na blok (`VSCHED_SPLIT_CAP` 3; najgorszy blok ok. 2,5 %, 3,5 % z retriggerami LFO). Liczą się w nim tylko głosy, które w danym bloku mają uderzenie: gdy więcej niż trzy głosy uderzają w jednym bloku poza jego granicą, te uderzenia startują na początku bloku, jak w 0.4, a głosy, które tylko brzmią, grają dalej bez zmian. Twój własny zapas to `trg blk`.

## Zmienione zachowanie

- Głosy startują w próbce swojego zdarzenia, stałe 2,9 ms po nim (w 0.4 na początku następnego bloku, 1,45–2,9 ms po nim).
- Druga podstrona menu globalnego ma `trg` po `cko`.
- Ekran startowy pokazuje `LICHOMAT` i `v0.5 unofficial`.

## Bez zmian

- Wszystko z v0.1–v0.4.
- MIDI out, gniazda zegara, diody, nagrywanie i reguły choke sekwencera mają timing 0.4: w emulatorze logi MIDI, zegara i panelu 23 złotych renderów są bajt w bajt takie jak z 0.4.
- Formaty wzorców, kitów, projektów, piosenek i `glo.cfg`; MIDI CC i NRPN.
- Efekty i master: liczą się raz na blok, jak w 0.4.
- Pamięć: DTCM + RAM 453 684 z 455 544 B budżetu, DTCM 107 396 z 107 900 B, RAM 346 288 z 389 120 B, ITCM 13 728 z 16 384 B.

## Znane ograniczenia

- P-lock albo modulacja, która celuje w parametr innego głosu, działa od podziału: w tym samym bloku dla głosów, które mikser liczy później, a dla liczonych wcześniej od następnego bloku. W 0.4 wszystko działało od początku bloku.
- LFO retriggerowane przez nutę, którego cel jest ustawiany przy wysyłaniu wartości LFO (np. cutoff filtra Drum 1–3), przesuwa się względem uderzenia nawet o 62 próbki (1,4 ms), przy każdym uderzeniu inaczej. Cel czytany w kroku sterowania głosu (np. głębokość obwiedni wysokości) idzie równo z nutą.
- Nuty MIDI przez USB mają ziarnistość ramek USB 1 ms: wszystkie nuty jednego transferu dzielą jeden znacznik czasu.
- Przełączenie `trg` z `smp` na `blk` w trakcie grania zostawia uderzenia już zaplanowane: przez ok. 1,5 bloku starsze uderzenie z `smp` może jeszcze raz uruchomić głos po nowszym uderzeniu z `blk` tego samego głosu. Dzieje się to raz, przy przełączeniu.
- W rzadkich blokach głos startuje jak w 0.4, na początku bloku: przy wczytywaniu kitu (kit autoload przy zmianie wzorca), po pominiętym bloku, gdy bajt MIDI czekał dłużej niż blok, i gdy przerwanie DMA przyjdzie o ponad pół bloku za późno (np. przy dostępie do karty). Takie uderzenie nigdy nie jest gorsze niż w 0.4.
- Pogłos gra w połowie częstotliwości, jak 0.4 (decyzja Q34); wersję oszczędną wybiera H12 i U20. Gałęzie `v0.5-half` i `v0.5-lean` mają już obie wersje z poprawką offsetu DC pogłosu z 0.4 (E7.7); zob. notatki 0.4.
- Ograniczenia v0.1–v0.4 zostają, poza jednym: w `smp` głos nie startuje już na początku bloku audio (±1,45 ms).

## Pliki

- `LXR_V2_LICHOMAT_0.5.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.5`, SHA-256: (wpisany, gdy CI zbuduje commit wydania; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.5` w `isthar/lxr-desktop-app` (archiwa „Source code” wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths”, „LXR” i „LXR-02” to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Jak to sprawdzono w emulatorze (tylko dla Ciebie)

Gałąź `v0.5`, head `20e0079`; szczegóły w opisach commitów.

- Testy: 114 na hoście i 328 w emulatorze, wszystkie przechodzą (R5_dev_input pominięty, jak wcześniej).
- Uderzenia: G16 (karta onset, 120 BPM: przesunięcia, ratchety, shuffle, delay, humanize, rolle; 164 uderzenia) ma rozrzut 1,99 próbki wokół zdarzenia + 128, czyli ±1 próbka; G17 (nuty MIDI z DIN i USB) 1,84; G18 (slave zegara DIN, 128 BPM) 1,97; G19 (zapis projektu w trakcie grania) 1,98. To samo G16 w `blk`: rozrzut 64 próbki.
- `blk` to 0.4: pięć złotych renderów `_blk` ma wpisy skopiowane z 0.4 i nie da się ich przepisać.
- Logi MIDI, zegara i panelu wszystkich 23 starych złotych renderów: bajt w bajt jak z 0.4.
- Głośność: z 48 grających kanałów złotych renderów RMS przesuwa się o 0,1 dB lub więcej tylko w dwóch, G2_ringmod L1 i R1, o −0,11 dB: swobodnie biegnąca nośna modulatora ring spotyka każde uderzenie w jego prawdziwym miejscu. Reszta: 0,07 dB lub mniej. G11_dsync nie zmienia się wcale (jego jedyne uderzenie wypada na początku bloku).
- Stopa: 36 uderzeń stopy z karty init przy 120, 125 i 130 BPM, każde bit w bit takie jak każde inne i jak w `blk`. Wcześniejsza wersja tego buildu skracała drugi krok sterowania nuty: reszta po odjęciu uderzenia mediany wynosiła −11 dB (mediana) i −4,8 dB (najgorzej), a ciało stopy 49 Hz spóźniało się do 171 próbek (3,9 ms). Poprawiła to trwała faza nuty (`83a9021`), z drugim przepisaniem złotych renderów (`20e0079`).
- Losowe przebiegi `tools/emu_fuzz.py`: 1 800 renderów w obu ustawieniach `trg`, potem jeszcze 600, bez żadnego znaleziska.
- Przegląd kodu znalazł i poprawił: znacznik DMA spóźniony o ponad pół bloku dawał za długie okno bloku (teraz ten blok gra jak w 0.4, `529277e`); zagnieżdżone `soundengine_process()` przy wczytywaniu kitu odmaskowywało przerwanie TIM2 (błąd był też w 0.4; teraz przywraca stan, który zastało, `bc7e215`); brakowało testu prawdziwego miksera (M25_mixer_split, `7819708`).

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/wt-v05/build/demo-v05/` (poza gitem, na Twoim Macu), opis w `README-v0.5.txt` tamże. Robi je `tools/demo_v05.sh` z gałęzi `v0.5`: wyjście 1 stereo, karty `emu/tests/cards`; pogłos gra w nich w połowie częstotliwości (tymczasowo).

- `v05-groove-onset-smp.wav` i `v05-groove-onset-blk.wav`: karta onset przy 120 BPM, skrypt G16: klik Drum 1 z przesunięciami −7…+7, ratchetami ×2–×8, shuffle, delay i humanize, roll 1/32 i triolowy. W `smp` każdy klik leży na swoim czasie; w `blk` kliki skaczą o blok.
- `v05-init-125-smp.wav` i `v05-init-125-blk.wav`: karta init, wzorzec 0, 125 BPM, 8 taktów.
- `v05-kick-125-smp.wav` i `v05-kick-125-blk.wav`: sama stopa 4/4 z karty init (wzorzec 1), 125 BPM, 8 taktów, z pomiaru stopy. Każde uderzenie jest w obu bit w bit to samo; różni je tylko miejsce uderzeń.
- `v05-smp-*.wav` i `v05-blk-*.wav`: dema 0.4 (opisane w `README-v0.4.txt`, tam pod nazwami `v04-*`) w obu ustawieniach `trg`.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1–v0.4 nie trzeba powtarzać. Do U27–U29 zbuduj najpierw klik z `docs/lichomat/kit-recipes-v0.5.md` i nagrywaj wyjście 1 w DAW, czytając uderzenia na poziomie próbek.

- **U27:** uderzenia z sekwencera: klik na 16 krokach w `smp` i w `blk`, potem flam z drugim klikiem i `dly` +1.
- **U28:** nuty MIDI przez DIN i przez USB, w `smp` i w `blk`; średni offset do ustawienia w DAW.
- **U29:** slave zegara MIDI przy 128 BPM przez DIN i przez USB.
- **U30:** CPU na ciężkim wzorcu z H12 w `blk`, w `smp` i w `smp` z retriggerami LFO na głosach 1–6.
- **U31:** brzmienie w `smp` i `blk`: snap, slide, LFO jako obwiednia wysokości, hihat otwarty i zamknięty, p-locki, humanize; przełączanie `trg` w trakcie grania.
- **U32:** zapis `trg`, powrót do 1.7 i z powrotem, zapisy i wczytanie kitu w trakcie grania.

## Publikacja (tylko właściciel)

1. Najpierw wydanie 0.4. Potem jego ostatnie zmiany (A7–A11, częstotliwość pogłosu) trafiają do `v0.5`, a złote rendery G13–G15 i `G13_reverb_blk` dostają wtedy nowe wpisy (plan v0.5, P18). Build tej gałęzi przechodzi U27–U32, U30 z pogłosem w częstotliwości wydania 0.4. Jeśli U30 nie przejdzie, najpierw build poprawkowy z `VSCHED_SPLIT_CAP` 3 i U30 jeszcze raz; wtedy sekcja „CPU” dostaje tekst o tym limicie jako ustawieniu wydania.
2. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i sekcje „tylko dla Ciebie”, a w „Znanych ograniczeniach” punkt o pogłosie. Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.5` jako `docs/lichomat/release-notes-v0.5.md`, zrób commit i push `v0.5` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebiegi CI (`.github/workflows/build.yml` i `emulator.yml`) przejdą.
3. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki” (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki” i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
4. Otaguj ten commit `v0.5`, wypchnij tag i utwórz z niego wydanie GitHub `v0.5` z tytułem `LICHOMAT v0.5`, z tekstem od „LICHOMAT to nieoficjalny firmware” do końca „Licencja” (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.5.img` z artefaktu.
