# LICHOMAT v0.7: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.7` w `isthar/lxr-desktop-app`, gdy wyjdzie wydanie 0.6 i to, co 0.6 przyjmie przed wydaniem (częstotliwość pogłosu z 0.4), trafi do gałęzi `v0.7`, gdy przejdziesz na LXR sprawdzenia U38–U41 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.7` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.7 ma działać według testów na hoście (135) i w emulatorze (353). Pogłos gra w tym buildzie w tymczasowej częstotliwości szkicu 0.4 (połowa). SHA-256 pliku aktualizacji dojdzie, gdy CI zbuduje commit wydania. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware” do końca „Licencja”; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.7 bazuje na v0.6, ta na v0.5, ta na v0.4, ta na v0.3, ta na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1–v0.6, zostaje. v0.7 dodaje dynamikę groove: każda ścieżka może grać według szablonu głośności, czyli akcentów szesnastek zmierzonych na hihatach albumu referencyjnego, z głębokością 0–200 %. Każdy projekt ma cztery własne szablony, zapisywane z tego, co gra ścieżka (timing i głośność razem), a strona czyszczenia zeruje mikrotiming ścieżki.

**Ważne:** velocity zmienia głośność głosu tylko wtedy, gdy ten głos ma włączone `vol` (velocity → głośność, ekran `dst amt vol` na podstronie modulacji). Bez tego szablon głośności zmienia tylko MIDI Note On i modulację velocity.

Wzorzec z szablonem głośności dostaje w pliku trzeci blok (20 B) po blokach z 0.2 i 0.6. Firmware 1.7 i v0.2–v0.6 go pomijają, a wzorzec, w którym każda ścieżka ma `vtp` `off` i `vdp` 100, jest bajt w bajt taki jak z 0.6. Własne szablony zapisują się w pliku `USERTPL.LCH` w folderze projektu, którego 1.7 i v0.2–v0.6 nie otwierają.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.7.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.7 unofficial`.

## Powrót do v0.6, v0.5, v0.4, v0.3, v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.7.img` i zainstaluj tak samo:

- v0.6: `LXR_V2_LICHOMAT_0.6.img` (plik z run sheet listy sprawdzeń); ekran startowy `LICHOMAT` i `v0.6 unofficial`;
- v0.5: `LXR_V2_LICHOMAT_0.5.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.5 unofficial`;
- v0.4: `LXR_V2_LICHOMAT_0.4.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.4 unofficial`;
- v0.3: `LXR_V2_LICHOMAT_0.3.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.3 unofficial`;
- v0.2: `LXR_V2_LICHOMAT_0.2.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.2 unofficial`;
- v0.1: `LXR_V2_LICHOMAT_0.1.img` (SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Wszystkie wczytują projekty, wzorce, kity i `glo.cfg` zapisane przez v0.7 bez błędów. Trzeci blok wzorca pomijają: v0.6 gra wzorzec bez szablonów głośności, a `tpl` `us1`–`us4` jako `off`; v0.2–v0.5 i 1.7 grają go tak jak wzorzec z 0.6. `USERTPL.LCH` pomijają, a ich zapis projektu najpierw czyści folder projektu, więc projekt zapisany w nich traci własne szablony.

## Nowe

### Szablony głośności: `vtp vdp`

- **Co robi:** ścieżka może grać według szablonu głośności. Na każdej z 16 szesnastek taktu krok gra ciszej o tyle dB, ile szablon ma na tej pozycji, razy głębokość `vdp`. Pozycje z 0 dB grają głośnością kroku: szablon tylko ścisza, a krok z głośnością 0 dalej milczy. Szablon ustawia się osobno dla każdego głosu w każdym wzorcu, tak jak szablon groove.
- **Ważne: głośność zmienia się tylko z włączonym `vol` głosu** (velocity → głośność). To ustawienie kitu z 1.7: przycisk wyboru 3 otwiera podstronę modulacji głosu, a `dst amt vol` to jej drugi ekran dla głosów 1–4 i pierwszy dla talerza i hihatów. Z `vol` `off` głos gra każde uderzenie tak samo głośno, a velocity z szablonu dostają tylko modulacja velocity (`dst amt`) i MIDI out. Nie myl go z `vol` na podstronie MIX (przycisk wyboru 8), czyli głośnością głosu.
- **Gdzie:** wybierz głos, SHIFT + drugi przycisk trybu (strona Euklid), potem enkoder w prawo za `amt` (albo, zamiast enkodera, jeszcze trzy razy SHIFT + drugi przycisk trybu): czwarty ekran, `vtp vdp sav`, gałki 1–3. W trybie edycji `vtp` pokazuje długą nazwę.
- **`vdp`:** 0–200 %, domyślnie 100 (także we wzorcach z 0.6). 0 % nic nie zmienia, 200 % podwaja dB (najwyżej −84 dB).
- **Szablony** (dB przy 100 %, pozycje 1–16 od początku taktu; 1, 5, 9, 13 to ćwierćnuty, 3, 7, 11, 15 offbeatowe ósemki):

| `vtp` | Długa nazwa | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| `off` | | | | | | | | | | | | | | | | | |
| `of8` | `offbeat 8th` | −10 | −15 | 0 | −14 | −10 | −14 | 0 | −13 | −10 | −16 | 0 | −14 | −10 | −15 | 0 | −13 |
| `ful` | `full 16th` | −7 | −15 | 0 | −5 | −7 | −9 | −1 | −10 | −7 | −14 | 0 | −7 | −6 | −10 | −1 | −10 |
| `dep` | `deep ghost` | −14 | −12 | 0 | −19 | −14 | −11 | 0 | −19 | −15 | −12 | 0 | −16 | −13 | −12 | 0 | −21 |
| `qtr` | `quarters` | −5 | −15 | 0 | −20 | −6 | −17 | 0 | −16 | −5 | −17 | 0 | −15 | −6 | −17 | −1 | −16 |

- Wszystkie cztery są zmierzone na hihatach albumu referencyjnego i w każdym offbeatowe ósemki są najgłośniejsze. `of8` to mediana 8 utworów: ćwierćnuty 10 dB niżej, szesnastki między nimi 13–16 dB. `ful` to dwa utwory z gęstymi szesnastkami: ghosty bliżej akcentów. `dep` to utwór z głębokimi ghostami. `qtr` ma ćwierćnuty tuż pod offbeatem (5–6 dB niżej), a szesnastki głęboko.
- **Velocity:** głośność kroku × 10^(dB × `vdp` / 2000), zaokrąglona, najmniej 1. Krok na 127 z `of8` na 100 % gra velocity 40, 23, 127, 25, 40, 25, 127, 28, 40, 20, 127, 25, 40, 23, 127, 28; na 50 %: 71, 54, 127, 57, 71, 57, 127, 60, 71, 51, 127, 57, 71, 54, 127, 60; na 200 %: 13, 4, 127, 5, 13, 5, 127, 6, 13, 3, 127, 5, 13, 4, 127, 6.
- **Jak to się liczy:** szablon działa raz na krok, przed humanize głośności (`hmv`): ratchety kroku dzielą tę samą głośność, a `hmv` losuje wokół niej. Pozycja w szablonie jest ta sama co w szablonie groove: krok ścieżki od startu albo od zmiany wzorca, modulo 16. MIDI Note On wychodzi z velocity z szablonem, więc syntezator na MIDI out i DAW dostają akcenty także przy `vol` `off`.
- Zmiana `vtp` albo `vdp` w trakcie grania działa od następnego kroku.

### Własne szablony: `sav`, `us1`–`us4`

- **Co robi:** projekt ma cztery własne szablony, `us1`–`us4`. Każdy ma część timingu (−84…+84 ticków na pozycję) i część głośności (0…−42 dB na pozycję). `tpl` i `vtp` mają je na liście za wbudowanymi szablonami (w trybie edycji `user 1` … `user 4`), z `amt` i `vdp` jak każdy szablon. Nagranie jednego głosu może więc grać na każdym innym: timing, akcenty albo jedno i drugie.
- **Zapis:** na czwartym ekranie strony ścieżki przekręć enkoder na `sav`, naciśnij go (tryb edycji), przekręć na slot (dolna linia pokazuje `push: save` i slot) i naciśnij jeszcze raz. `sav` wraca na `---`. Zapisuje się wybrany głos w pokazanym wzorcu. Na wygaszonym ekranie (wygaszacz) kliknięcie tylko budzi ekran i niczego nie zapisuje.
- **Co się zapisuje (decyzja Q36: to, co gra):** kroki 1–16. Timing to `mic` kroku (z `shf`) razem z szablonem groove ścieżki na tej pozycji, do ±84 ticków. Głośność to głośność kroku przez szablon głośności ścieżki, w dB poniżej najgłośniejszego z tych kroków, zaokrąglona do całych dB. Ze ścieżki z `tpl off` i `vtp off` to po prostu wartości kroków. Swing (shuffle), `dly`, humanize i tempo ścieżki się nie zapisują.
- **Puste kroki:** krok wyłączony albo za długością ścieżki zostawia swoją pozycję w spokoju (0 ticków, 0 dB). Ścieżka bez żadnego włączonego kroku opróżnia slot; pusty szablon gra jak `off`.
- **Gdzie się zapisuje:** od razu na kartę, do `USERTPL.LCH` w folderze roboczym `/tmp`, więc zapis przetrwa wyłączenie. Zapis projektu zapisuje cztery szablony w `USERTPL.LCH` w folderze projektu (gdy któryś nie jest pusty), wczytanie projektu przywraca jego szablony (bez pliku: puste), a nowy projekt zaczyna z pustymi. Zapis w trakcie grania pisze kartę przy działającym dźwięku, jak zapis wzorca do `/tmp`.
- Ścieżka, która sama gra slot, do którego zapisujesz, bierze do zapisu jego starą zawartość.

### Zerowanie mikrotimingu: `clear [micro]?`

- Przytrzymaj SHIFT i naciśnij COPY: strona czyszczenia z 1.7. Enkoder za `clear [autom. 2]` pokazuje `clear [micro]?`. Naciśnij enkoder, potem puść SHIFT; puszczenie SHIFT przed naciśnięciem anuluje.
- Wszystkie kroki (64) wybranego głosu w pokazanym wzorcu dostają `shf` 0 i `mic` 0. Flamy, szablony (`tpl`, `vtp`) i reszta danych kroków zostają. Strona kroku od razu pokazuje 0. Jak każda edycja wzorca, zostaje po zapisie wzorca.

## Zgodność

| Plik z | Czyta go | Wynik |
|---|---|---|
| 1.7 i v0.1 | v0.7 | bez bloków: jak w v0.6, a `vtp` `off` z `vdp` 100 |
| v0.2–v0.6 | v0.7 | bez bloku 3: `vtp` `off` z `vdp` 100; gra jak tam |
| v0.7 | v0.6 | bloki 1 i 2 czytane, blok 3 pominięty: bez szablonów głośności; `tpl` `us1`–`us4` gra jak `off` |
| v0.7 | v0.2–v0.5 | jak wzorzec z v0.6: blok 1, każdy krok w zgrubnym `shf`, bez szablonów |
| v0.7 | 1.7 | jak wzorzec z v0.6: tylko zgrubne `shf` |
| v0.7 zapisany ponownie w v0.6 | v0.7 | bloki 1 i 2 z v0.6, bez bloku 3: szablony głośności giną, a `tpl` `us1`–`us4` wraca jako `off` |
| projekt v0.7 zapisany ponownie w 1.7 albo v0.2–v0.6 | v0.7 | `USERTPL.LCH` skasowany: własne szablony puste; wzorce jak w wierszach wyżej |

- Blok 3 to `LCHM`, wersja 3, 7 ścieżek i 7 × 2 B (szablon, głębokość): 20 B po bloku 2, a bez bloku 2 zaraz po bloku 1. Zapisuje się, gdy któraś ścieżka ma `vtp` albo `vdp` inne niż `off` i 100 %. Wzorzec z samym blokiem 1 i blokiem 3 ma 3 717 B, z wszystkimi trzema 3 961 B; bez szablonów głośności dalej 3 697 albo 3 941 B. Autozapis `tmp.pat` działa tak samo.
- `USERTPL.LCH` (134 B): `LCHM`, wersja 1, liczba 4, potem 4 × 32 B (16 przesunięć w tickach, 16 wartości w dB). Wartość spoza zakresu czyta się jako 0; inna wersja, inna liczba albo za krótki plik to brak pliku.
- Kity, `prj.nfo`, piosenki i `glo.cfg` (30 bajtów) się nie zmieniają.
- `vtp`, `vdp` i `sav` są tylko w menu, jak `tpl` i `amt`: nie mają MIDI CC ani NRPN. Żaden istniejący numer parametru się nie zmienia, więc kity, NRPN i morph zostają jak były.

## CPU

Koszt policzony statycznie na skompilowanym kodzie tego buildu, metodą notatek 0.5 i 0.6 (instrukcje, 13 cykli na dzielenie, 4 na wywołanie, współczynnik 1,5 z D41):

- Najdłuższa ścieżka bez pętli przez nowy kod to +164 cykle na wywołanie generatora kroku (na granicy kroku dwa na ścieżkę: późna część jednego kroku i wczesna następnego): odczyt szablonu głośności, pozycja wspólna z szablonem groove, tablica dB albo własny szablon, przeskalowanie velocity.
- Najgorszy blok, gdy wszystkie 7 ścieżek przechodzi na nowy krok na jednej granicy: ok. 1,10 % bloku. Wzór planu, który liczy każdą instrukcję funkcji raz, także w gałęziach, które się wykluczają, daje 2,90 % (2,35 % po odliczeniu kodu pozycji z v0.6, który kompilator teraz wbudowuje i powiela); decyzja Q37 przyjmuje najdłuższą ścieżkę. Szacunek planu przed kompilacją: 0,44 %.
- Średnio, przy 7 ścieżkach szesnastek przy 142 BPM, ok. 0,015 % (najgorszy blok co 72,8 bloku). Szablony nie dodają zdarzeń, więc koszt głosów się nie zmienia.
- Przy granicy z D41 `pk` zostaje najwyżej ok. 92,2 %, poniżej 95 z U20. U41 sprawdza to na ciężkim wzorcu z H12: `pk` z `vtp of8` na 7 ścieżkach i `tpl us1` na ścieżkach 2–7 najwyżej 1,5 punktu nad `pk` z U37 krok 2, `ovf` 0.

## Zmienione zachowanie

- Strona ścieżki ma czwarty ekran, `vtp vdp sav`. Enkoder przechodzi na nią za `amt`, a SHIFT + drugi przycisk trybu przechodzi przez cztery ekrany zamiast trzech.
- `tpl` ma za `drg` własne szablony `us1`–`us4`.
- Strona czyszczenia (SHIFT + COPY) ma `clear [micro]?` za `clear [autom. 2]`.
- Zapis, którego karta nie przyjmuje (pełna karta), pokazuje `SD Card error:` i `Card full!`; wcześniej `unknown!`.
- Zapis projektu, który nie może zapisać `USERTPL.LCH`, pokazuje błąd i nie czyści `/tmp`, więc zapisane tam szablony nie giną. Błąd odczytu `USERTPL.LCH` (nie brak pliku ani zła zawartość) przerywa wczytanie projektu, gdy jego kity, wzorce i piosenki są już wczytane, tak jak błąd w ostatnim przebiegu wczytywania.
- Długie nazwy w trybie edycji: `Vel tpl`, `Vel dpth`, `Save tpl`.
- Ekran startowy pokazuje `LICHOMAT` i `v0.7 unofficial`.

## Bez zmian

- Wszystko z v0.1–v0.6. Bez szablonów głośności i własnych szablonów v0.7 gra bit w bit jak v0.6: w emulatorze wszystkie 38 złotych renderów v0.6 są bez zmian, a na hoście seedy testu równoważności kolejki dają z szablonami na głębokości 0 te same zdarzenia co bez nich.
- Shuffle, humanize, opóźnienie ścieżki, własne tempo ścieżki, ratchety, rolle, `mic` i szablony groove liczą się jak w v0.6.
- Formaty kitów, projektów (`prj.nfo`) i piosenek, 30 bajtów `glo.cfg`, MIDI CC i NRPN.
- Pamięć: DTCM + RAM 470 188 z 471 032 B (+1 056 B RAM wobec v0.6, w budżecie z v0.6), DTCM 107 396 z 107 900 B, RAM 362 792 z 389 120 B, ITCM 13 744 z 16 384 B (bez zmian).

## Znane ograniczenia

- Szablony głośności i własne mają 16 pozycji taktu. Ścieżka dłuższa niż 16 kroków powtarza je co 16 kroków, a zapis (`sav`) bierze tylko kroki 1–16.
- Nagrywanie nie kompensuje szablonu głośności: nagrana nuta zapisuje zagraną velocity, a szablon gra na niej jeszcze raz. Nagrywaj z `vtp` `off`.
- Rolle grają ze stałą velocity rolla, bez szablonu głośności, tak jak bez szablonu groove i `mic`.
- Zapis własnego szablonu bierze `mic`, `shf` i szablony ścieżki, ale nie swing, `dly`, humanize ani tempo ścieżki: pozycja to numer kroku, jak dla ścieżki w `x1`.
- Szablon tylko ścisza: krok nie zagra głośniej niż jego własna głośność, a najcichszy zostaje na velocity 1. Przy 200 % ghosty mają velocity 3–6, więc tabela −32 dB to w praktyce −32,5 dB.
- Przester na głosie ściska ciche uderzenia: hihat z mocnym przesterem gra ghosty do ok. 3 dB głośniej, niż mówi tabela. Nawet przy `drv` 0 stopień przesteru z 1.7 ścisza ciche uderzenia o ok. 0,1 dB.
- Zapis (`sav`) w trakcie nagrywania na tej samej ścieżce może wziąć dla właśnie nagrywanego kroku stary i nowy timing naraz.
- Wspólny hihat (ścieżki 6 i 7): gdy oba grają na tym samym kroku, słychać otwarty, bez szablonu głośności zamkniętego; zdejmij otwarty z kroków zamkniętego.
- Pogłos gra w połowie częstotliwości, jak 0.4–0.6 (decyzja Q34); wersję oszczędną wybiera H12 i U20.
- Ograniczenia v0.1–v0.6 zostają, poza tymi, które v0.7 zamyka: są już szablony głośności, własne szablony i zerowanie mikrotimingu ścieżki. Dalej nie ma kwantyzowanego MIDI out obok przesuniętego.

## Pliki

- `LXR_V2_LICHOMAT_0.7.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.7`, SHA-256: (wpisany, gdy CI zbuduje commit wydania; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.7` w `isthar/lxr-desktop-app` (archiwa „Source code” wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths”, „LXR” i „LXR-02” to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Jak to sprawdzono w emulatorze (tylko dla Ciebie)

Gałąź `v0.7`, head `e4104c0`, ostatnia zmiana firmware `8cea498`; szczegóły w opisach commitów i w „Changes during execution” planu. Lokalny build daje `.bin` o SHA-256 `bfe82211…`, obraz 343 996 B (v0.6: 338 236 B); plik z CI będzie inny (inna kolejność linkowania newlib), jak przy wcześniejszych wersjach.

- Testy: 135 na hoście (125 w v0.6), wszystkie przechodzą pod UBSan i pod ASan + UBSan; 353 w emulatorze (346) pod UBSan i 324 (317) pod ASan + UBSan, bez aplikacji.
- Złote rendery: wszystkie 38 z v0.6 bez zmian, żaden nie został zapisany od nowa. To dowód, że bez szablonów głośności i własnych szablonów v0.7 gra jak v0.6. Nowe: G23_velocity, G23_velocity_50 i G23_velocity_200: klik Drum 1 na 16 krokach pustego wzorca 3 karty `groove`, `of8` na 100, 50 i 200 %, `vol` (velocity → głośność) włączone, przester i modulacja velocity wyłączone, głośność głosu 64 (na 127 najgłośniejsze uderzenie dochodzi do 32 750, do granicy przesterowania).
- Poziomy wobec velocity policzonej ze wzoru poza firmware: najwyżej 0,118 dB (100 %), 0,082 dB (50 %) i 0,154 dB (200 %); wobec samej tabeli 0,240 dB (100 %) i 0,112 dB (50 %). Limit to 0,5 dB. Każde uderzenie jest ok. 0,1 dB poniżej swojej velocity: to stopień przesteru z 1.7 za wzmocnieniem velocity, który przy `drv` 0 jest lekkim ekspanderem (ciche uderzenia do −0,136 dB; model daje 0,114 / 0,081 / 0,132 dB). Na LXR U38 pokaże to samo.
- Własny szablon (`V07_capture`): `sav`, `clear [micro]` i `tpl us1` na innej ścieżce grają ticki G20_micro: 68 uderzeń, rozrzut 1,93 ramki.
- Interfejs (`V07_ui`, `V07_screensaver`): czwarty ekran, długie nazwy, `push: save`, zapis do `/tmp`, `Card full!`, `clear [micro]?` na głosie i wzorcu, które pokazuje strona; kliknięcie na wygaszonym ekranie tylko go budzi.
- Pliki: blok 3 zaraz po bloku 1 (3 717 B) i po bloku 2 (3 961 B), zapisane przez menu; v0.6 i v0.5 renderują takie wzorce bez szablonów głośności, także z `vol` włączonym, a v0.7 inaczej (zadania 5 i 6). Zapis projektu na pełnej karcie (karta z wstrzykiwanym błędem w testach hosta) zgłasza błąd zamiast zapisać pusty `usertpl.lch`.
- Losowe przebiegi `tools/emu_fuzz.py`, teraz także z `vtp`, `vdp` i `sav` na wartościach 0–255, z zapisem własnego szablonu i `clear [micro]`, oraz na karcie z własnymi szablonami na krawędziach zakresów (±84 ticki, 0 i −42 dB i wartości tuż za nimi): 4 800 renderów (sekwencer na dwóch kartach, dźwięk i MIDI, w obu ustawieniach `trg`, pod UBSan i pod ASan + UBSan), bez żadnego znaleziska.
- Przeglądy: pełne po rdzeniu (zadania 1–3) i po kodzie łączącym i plikach (zadania 4–5), lekkie po interfejsie (zadania 6–7). Jedna prawdziwa usterka: na pełnej karcie zapis projektu meldował sukces z pustym `usertpl.lch` i czyścił `/tmp`, więc zapisane szablony ginęły; poprawiona test najpierw. Poza tym luki w testach, każda pokazana celową usterką, która przechodziła wszystkie testy, i zamknięta osobnym commitem.
- CPU: przeliczenie statyczne na skompilowanym kodzie, najdłuższa ścieżka ok. 1,10 % najgorszego bloku (sekcja „CPU”).
- CI na GitHubie: bez przebiegu dla `v0.7` (ani `v0.5`, `v0.6`); GitHub nie uruchamia workflowów w tym repozytorium od 31.10 (tracker E8.5).

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/wt-v07/build/demo/` (poza gitem, na Twoim Macu), opis w `README-v0.7.txt` tamże. Robi je `tools/demo_v07.sh` z gałęzi `v0.7`: karta init, 142 BPM, 8 taktów, wyjście 1 stereo; pogłos gra w nich w połowie częstotliwości (tymczasowo).

- `v07-hats-off.wav`, `v07-hats-of8.wav`, `v07-hats-ful.wav`, `v07-hats-dep.wav`, `v07-hats-qtr.wav`: zamknięty hihat (głos 6) na wszystkich szesnastkach na głośności 127, z `vol` włączonym i danym szablonem głośności na 100 %; otwarty hihat zdjęty z ćwierćnut (inaczej grałby je zamiast zamkniętego, na pełnej głośności), reszta karty bez zmian. `off` to odniesienie.
- `v07-hats-of8-sweep.wav`: `of8`, a `vdp` w każdym takcie rośnie krok po kroku od 0 % (krok 1) do 200 % (krok 16): takt zaczyna się równo i kończy mocno zaakcentowany.
- Hihat karty init ma włączony przester, który ściska ghosty: są do 3 dB głośniej, niż mówi tabela. Bez przesteru poziomy zgadzają się z tabelą co do 0,2 dB.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1–v0.6 nie trzeba powtarzać; U38–U41 idą w sesji po sprawdzeniach 0.6. U38 i U39 czytają poziomy i uderzenia w nagraniu wyjścia 1 w DAW, jak U27.

- **U38:** szablony głośności: klik na Drum 1 z `vol` włączonym, głośność głosu 64 i `drv` 0, `of8` na 100, 50 i 200 %: poziomy i velocity MIDI wobec tabeli; potem Twoje hihaty z każdym szablonem i `vdp` od 0 do 200; `vol` wyłączone; wzorzec w 0.6.
- **U39:** własny szablon z nagrania: klip MIDI z DAW z „e” 12 ms później na velocity 60 i „a” 6 ms wcześniej na 90, zapis w `us1` w trakcie grania (bez przerwy w dźwięku, bez nowego `ovf`), `tpl us1` i `vtp us1` na innym głosie; wyłączenie, projekt, 0.6.
- **U40:** `clear [micro]?` na nagranej ścieżce z `lay`.
- **U41:** CPU na ciężkim wzorcu z H12 z `vtp of8` na 7 ścieżkach i `tpl us1` na ścieżkach 2–7: `pk` najwyżej 1,5 punktu nad U37.

## Publikacja (tylko właściciel)

1. Najpierw wydanie 0.6 (które idzie po wydaniach 0.4 i 0.5). Potem to, co 0.6 przyjęło przed wydaniem (częstotliwość pogłosu z 0.4 i jej złote rendery), trafia do `v0.7`. Build tej gałęzi przechodzi U38–U41, U41 z pogłosem w częstotliwości wydania 0.4. Jeśli U41 nie przejdzie, najpierw poprawka kodu i U41 jeszcze raz.
2. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i sekcje „tylko dla Ciebie”, a w „Znanych ograniczeniach” punkt o pogłosie. Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.7` jako `docs/lichomat/release-notes-v0.7.md`, zrób commit i push `v0.7` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebiegi CI (`.github/workflows/build.yml` i `emulator.yml`) przejdą.
3. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki” (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki” i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
4. Otaguj ten commit `v0.7`, wypchnij tag i utwórz z niego wydanie GitHub `v0.7` z tytułem `LICHOMAT v0.7`, z tekstem od „LICHOMAT to nieoficjalny firmware” do końca „Licencja” (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.7.img` z artefaktu.
