# LICHOMAT v0.6: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.6` w `isthar/lxr-desktop-app`, gdy wyjdzie wydanie 0.5 i to, co 0.5 przyjmie przed wydaniem (częstotliwość pogłosu z 0.4), trafi do gałęzi `v0.6`, gdy przejdziesz na LXR sprawdzenia U33–U37 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.6` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.6 ma działać według testów na hoście (125) i w emulatorze (342). Pogłos gra w tym buildzie w tymczasowej częstotliwości szkicu 0.4 (połowa). SHA-256 pliku aktualizacji dojdzie, gdy CI zbuduje commit wydania. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware” do końca „Licencja”; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.6 bazuje na v0.5, ta na v0.4, ta na v0.3, ta na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1–v0.5, zostaje. v0.6 dodaje mikrotiming: każdy krok ma własne przesunięcie z dokładnością do jednego ticku (1/96 kroku, 1,30 ms przy 120 BPM), każda ścieżka może grać według jednego z siedmiu szablonów groove z ustawianą siłą, a nagrywanie na żywo zapisuje zagrane przesunięcie, zamiast je kwantyzować.

Wzorzec, który używa mikrotimingu albo szablonu, dostaje w pliku drugi blok (244 B) po bloku z 0.2. Firmware 1.7 i v0.2–v0.5 go pomijają, a wzorzec bez mikrotimingu i szablonów jest bajt w bajt taki jak z 0.5. Nowe ustawienie `rqn` zapisuje się w bajcie 19 pliku `glo.cfg`, którego firmware 1.7 nie czyta.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.6.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.6 unofficial`.

## Powrót do v0.5, v0.4, v0.3, v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze możesz wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.6.img` i zainstaluj tak samo:

- v0.5: `LXR_V2_LICHOMAT_0.5.img` (plik z run sheet listy sprawdzeń); ekran startowy `LICHOMAT` i `v0.5 unofficial`;
- v0.4: `LXR_V2_LICHOMAT_0.4.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.4 unofficial`;
- v0.3: `LXR_V2_LICHOMAT_0.3.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.3 unofficial`;
- v0.2: `LXR_V2_LICHOMAT_0.2.img` (plik z run sheet); ekran startowy `LICHOMAT` i `v0.2 unofficial`;
- v0.1: `LXR_V2_LICHOMAT_0.1.img` (SHA-256 `55091864…`); ekran startowy `LICHOMAT` i `v0.1 unofficial`;
- oficjalny 1.7: `LXR_V2_V1.7.img` od Erica Synths albo zbudowany ze źródeł upstream w commicie `a43cd4f`, jak opisuje README.md; ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`.

Wszystkie wczytują projekty, wzorce, kity i `glo.cfg` zapisane przez v0.6 bez błędów. Drugi blok wzorca pomijają: v0.2–v0.5 grają każdy krok w jego zgrubnym przesunięciu `shf`, najbliższej 1/8 kroku, z tempem ścieżki, humanize i opóźnieniem, ale bez szablonów; v0.1 i 1.7 grają tylko zgrubne `shf`. Bajt 19 `glo.cfg` pomijają. `glo.cfg` zapisany w 1.7 ma w tym bajcie 0, więc v0.6 czyta go jako `rqn` `off`.

## Nowe

### Mikrotiming kroku: `mic`

- **Co robi:** każdy krok ma własne przesunięcie `mic` w tickach, 1/96 kroku swojej ścieżki, od −84 do +84 (7/8 kroku w każdą stronę), 0 na siatce. Tick to 1,30 ms przy 120 BPM i 1,10 ms przy 142. Do 0.5 krok dało się przesunąć tylko o `shf`, co 1/8 kroku (15,6 ms przy 120 BPM).
- **Gdzie:** w trybie kroku wybierz krok. Drugi ekran jego strony to `flm shf mic`, `mic` jest na gałce 3. Gałka i enkoder zmieniają go o jeden tick na ruch. W trybie edycji górna linia pokazuje `Step` i `Micro`, a dolna obok ticków przesunięcie w ms przy bieżącym BPM i tempie ścieżki (+19 przy 120 BPM: `24.7ms`, przy 142: `20.9ms`). Enkoder za `mic` przechodzi na podstronę p-locków, jak wcześniej za `shf`.
- **`shf` to teraz zgrubny widok `mic`:** jedna jednostka `shf` to 12 ticków. Edycja `shf` zachowuje część drobną (`shf` +1 → +2 zmienia `mic` +14 na +26) i zatrzymuje `mic` na ±84; edycja `mic` ustawia `shf` na najbliższą jednostkę (`mic` +19 pokazuje `shf` +2). Krok z 0.5 z `shf` +2 pokazuje `mic` +24.
- `mic` należy do kroku pokazanego wzorca i zapisuje się z nim. Jak każda edycja wzorca znika przy zmianie wzorca, jeśli wzorzec nie jest zapisany.
- Przy własnym tempie ścieżki (`tmp`) tick to 1/96 kroku tej ścieżki, tak jak `shf` jest 1/8 jej kroku.

### Szablony groove: `tpl amt`

- **Co robi:** ścieżka może grać według szablonu, czyli jednego przesunięcia na każdą z 16 szesnastek taktu, przeskalowanego przez `amt`. Ćwierćnuty zostają na siatce we wszystkich szablonach. Szablon ustawia się osobno dla każdego głosu w każdym wzorcu: stopa może grać prosto, a hihaty i perkusja z groove. Shuffle, jeden na cały wzorzec, tego nie umie.
- **Gdzie:** wybierz głos, SHIFT + drugi przycisk trybu (strona Euklid), potem enkoder w prawo za `dly`: trzeci ekran, `tpl amt`, gałki 1 i 2. W trybie edycji `tpl` pokazuje długą nazwę.
- **`amt`:** 0–200 %, domyślnie 100 (także we wzorcach z 0.5). `swg` na 200 % to feeling triolowy.
- **Szablony** (przesunięcia w tickach przy 100 %, pozycje 1–16 od początku taktu):

| `tpl` | Długa nazwa | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| `off` | | | | | | | | | | | | | | | | | |
| `swg` | `even swing` | 0 | 16 | 0 | 16 | 0 | 16 | 0 | 16 | 0 | 16 | 0 | 16 | 0 | 16 | 0 | 16 |
| `lay` | `lay back` | 0 | 18 | 0 | 5 | 0 | 18 | 0 | 5 | 0 | 18 | 0 | 5 | 0 | 18 | 0 | 5 |
| `lyb` | `lay bar` | 0 | 15 | 0 | 11 | 0 | 24 | 0 | 5 | 0 | 17 | 0 | 1 | 0 | 16 | 0 | 3 |
| `rlx` | `relax bar` | 0 | 15 | 1 | 3 | 0 | 16 | 2 | 22 | 0 | 14 | 2 | 16 | 0 | 9 | 2 | 18 |
| `2bt` | `two beat` | 0 | 7 | 3 | 8 | 0 | 1 | 2 | 1 | 0 | 7 | 3 | 8 | 0 | 1 | 2 | 1 |
| `psh` | `push off` | 0 | 0 | −4 | 0 | 0 | 0 | −4 | 0 | 0 | 0 | −4 | 0 | 0 | 0 | −4 | 0 |
| `drg` | `drag off` | 0 | 0 | 6 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 6 | 0 | 0 | 0 | 6 | 0 |

- `lay`, `lyb`, `rlx` i `2bt` są zmierzone z utworów referencyjnych przy 142–143 BPM; `swg`, `psh` i `drg` są ogólne. `lay` przy 142 BPM to +19,8 ms na „e” każdej ćwierćnuty i +5,5 ms na „a”. `swg` na 56 % to ok. 10 ms przy 142 BPM.
- **Jak to się sumuje:** `mic` (z `shf`) i szablon dodają się i razem zatrzymują na 7/8 kroku w każdą stronę. Shuffle, opóźnienie ścieżki i humanize dochodzą na końcu, jak wcześniej. Szablon i shuffle też się sumują, więc do pracy z szablonami najlepiej shuffle 0.
- **Pozycja w szablonie** to liczba kroków ścieżki od jej startu albo od zmiany wzorca, modulo 16. Groove idzie więc z taktem wzorca, także na ścieżkach o długości 12 albo 7. Ścieżka z własnym tempem ×2 rozkłada 16 pozycji szablonu na pół taktu mastera.
- Zmiana `tpl` albo `amt` w trakcie grania działa od następnego wygenerowanego kroku, jak zmiana shuffle.
- Ratchety przesuwają się w całości razem z pierwszym uderzeniem. Rolle nie biorą ani szablonu, ani `mic`.

### Nagrywanie z mikrotimingiem: `rqn`

- **Co robi:** z `rqn` `off` (domyślne) nagrywanie na żywo zapisuje nutę na najbliższym kroku, a jej zagrane przesunięcie w `mic` tego kroku, z dokładnością do ticku (błąd najwyżej 0,65 ms przy 120 BPM, 0,55 ms przy 142). Nagranie gra potem tak, jak było zagrane. Do 0.5 nagrana nuta lądowała na siatce; `rqn` `on` nagrywa tak jak 0.5.
- **Od czego liczy się przesunięcie:** od miejsca, w którym krok gra, czyli siatki ze swingiem, szablonem i `dly`. Nuta zagrana dokładnie w groove szablonu zapisuje więc `mic` 0, a `mic` pokazuje, o ile zagrana nuta odeszła od groove. Gdy po nagraniu zmienisz szablon, shuffle albo `dly`, nagrane nuty przesuną się razem z nimi. `mic` jest ograniczany tak, żeby razem z szablonem zmieścił się w 7/8 kroku, więc pokazuje to, co gra.
- **Wybór kroku:** bliższy z bieżącego i następnego kroku ścieżki, według miejsc, w których grają; przy remisie następny. Nuta zapisana na następnym kroku nie zagra drugi raz, gdy sekwencer do niego dojdzie (jak od 0.2). Bez szablonu i bez `dly` to ta sama reguła co w 0.5.
- **Czas nuty:** nuty MIDI z DIN i USB liczą się od chwili przyjścia (znacznik czasu z 0.5), w obu ustawieniach `trg`; jako slave zegara MIDI nagrywanie idzie za zewnętrznym zegarem. Jednorazowy roll z panelu (tryb performance, `One`) liczy się od chwili, w której sekwencer go przyjmuje: przycisk nie ma znacznika czasu, różnica to najwyżej 1,45 ms. Uderzenia trzymanego rolla nagrywają się w swoich krokach, z timingiem bez zmian, jak w 0.5.
- **Bez kompensacji opóźnienia:** nuta MIDI brzmi przy nagrywaniu tyle samo po swoim czasie co przy odtwarzaniu.
- **Nakładanie:** nagrywanie na LXR zawsze nakłada. Nuta na kroku, który już gra, zastępuje jego nutę, velocity, prawdopodobieństwo i (z `rqn` `off`) timing; flam i p-locki zostają.
- **Gdzie:** SHIFT + REC otwiera stronę nagrywania, `aut rqn`. `rqn` jest ustawieniem globalnym i zapisuje się z ustawieniami globalnymi (strona zapisu, typ global, jak w 1.7).

### MIDI out

- Note On i Note Off wychodzą w czasie przesuniętego uderzenia, jak od 0.2 przy `shf`, swingu, opóźnieniu i humanize. Syntezator na MIDI out gra więc razem ze ścieżką, a DAW nagrywający MIDI z LXR dostaje groove.
- Zegar MIDI, gniazdo clock out, diody i chase light zostają na siatce mastera.

## Zgodność

| Plik z | Czyta go | Wynik |
|---|---|---|
| 1.7 i v0.1 | v0.6 | bez bloków: domyślne z v0.2, część drobna 0 (`mic` to zgrubne `shf` z pliku), szablon `off`; gra jak w 1.7 i v0.5 |
| v0.2–v0.5 | v0.6 | tylko blok 1: gra jak w v0.5 |
| v0.6 | v0.2–v0.5 | blok 1 jest czytany i plik zamknięty: tempo, humanize i opóźnienie zostają; każdy krok w zgrubnym `shf`, najbliższej 1/8 kroku (remis w górę); bez szablonów |
| v0.6 | 1.7 | czytany do numeru kitu: tylko zgrubne `shf`; tempo, humanize, opóźnienie, część drobna i szablony giną |
| v0.6 zapisany ponownie w 1.7 albo v0.5 | v0.6 | to, co zachował tamten firmware: tylko zgrubne `shf` (1.7) albo zgrubne `shf` z blokiem 1 (v0.5) |

- Wzorzec bez mikrotimingu i szablonów jest bajt w bajt taki jak z 0.5 (3 697 B na karcie); z nimi ma 3 941 B. Autozapis `tmp.pat` działa tak samo.
- Kity, `prj.nfo` i piosenki się nie zmieniają. `glo.cfg` ma dalej 30 bajtów; bajt 19 (w 1.7 nieużywany parametr `PAR_TRIG_GATE_MODE`) to teraz `rqn`: 0 to `off`, 1 to `on`, każda inna wartość daje `off`. Firmware 1.7 zapisuje tam 0 i nigdy tego bajtu nie czyta.
- `mic`, `tpl` i `amt` są tylko w menu, jak `tmp`: nie mają MIDI CC ani NRPN. Żaden istniejący numer parametru się nie zmienia, więc kity, NRPN i morph zostają jak były.

## CPU

Koszt policzony statycznie, metodą notatek 0.5 (instrukcje, 13 cykli na dzielenie, 4 na wywołanie, współczynnik 1,5 z D41), dla kodu tego buildu:

- Każdy krok, którego miejsce liczy sekwencer, kosztuje ok. 216 cykli więcej: dwa nowe odczyty (część drobna i szablon), pozycja w szablonie, przeliczenie na ticki ścieżki, suma i ograniczenie.
- Najgorszy blok, gdy wszystkie 7 ścieżek przechodzi na nowy krok na jednej granicy: ok. 1,45 % bloku. Kod, który naprawdę się wykonuje, jest krótszy (szacunek specyfikacji: ok. 0,56 %), więc prawdziwy koszt leży pomiędzy.
- Średnio, przy 7 ścieżkach szesnastek przy 142 BPM, ok. 0,02 %. Mikrotiming nie dodaje zdarzeń, więc koszt głosów z 0.5 się nie zmienia.
- Jedna nagrana nuta: ok. 0,12 % bloku, raz na nutę (szacunek specyfikacji).
- Przy granicy 85 % z D41 i najgorszym bloku 0.5 `pk` zostaje najwyżej ok. 89,64 + 1,45 = 91,1, poniżej 95. U37 sprawdza to na ciężkim wzorcu z H12: `pk` z szablonami na ścieżkach 2–7 i `mic` na każdym kroku najwyżej 1,5 punktu nad `pk` bez nich, `ovf` 0.

## Zmienione zachowanie

- `shf` jest zgrubnym widokiem `mic`: krok z 0.5 z `shf` +2 pokazuje `mic` +24, a edycja `shf` zachowuje część drobną.
- Nagrywanie na żywo z `rqn` `off` (domyślnie) zapisuje zagrane przesunięcie w `mic` kroku; w 0.5 nuta lądowała na siatce. `rqn` `on` przywraca zachowanie 0.5.
- Krok przy nagrywaniu wybiera się według czasu przyjścia nuty MIDI i miejsc kroków z szablonem i `dly`. Bez szablonu i `dly` to reguła 0.5.
- Strona ścieżki ma trzeci ekran, `tpl amt`; enkoder przechodzi na nią za `dly`.
- Strona nagrywania (SHIFT + REC) ma `rqn` po `aut`.
- Bajt 19 `glo.cfg` to `rqn`.
- Ekran startowy pokazuje `LICHOMAT` i `v0.6 unofficial`.
- Długie nazwy w trybie edycji mają najwyżej 8 znaków: `Slide tm`, `SceneChg`, `rev lvl`, `rev mod`, `rev frz`, `dly sync`, `duck dpt`, `DJ filtr`, `Trig tmg`. Dłuższe nazwy z fw 1.7, 0.4 i 0.5 pisały poza górną linię LCD (poprawka E4.10).
- Oscylator PWM nie czyta już tablicy piły o jeden za wierszem (błąd fw 1.7 B14, poprawka E4.11). Poniżej 14 080 Hz dźwięk jest bit w bit ten sam; zmienia się tylko najwyższy wiersz tablicy (od 14 080 Hz). Nasycenie fazy PWM (B13) zostaje, bo jego poprawka zmienia brzmienie PWM i czeka na Twój odsłuch.
- Delay ping-pong (`Pp`) przerzuca echa na przemian także dla głosów w centrum: pierwsze echo po prawej, drugie po lewej i tak dalej. W fw 1.7 echa głosu z centrum brzmiały tak samo po obu stronach (poprawka E7.8). Echa głosu z centrum są tak samo głośne jak w fw 1.7, a głosu spanoramowanego do końca o 3 dB cichsze. Głos skrajnie w prawo ma teraz pierwsze echo po prawej, tak jak dźwięk suchy (w fw 1.7 po lewej).

## Bez zmian

- Wszystko z v0.1–v0.5. Bez mikrotimingu i szablonów `trg` `smp` gra bit w bit jak v0.5, a `trg` `blk` jak v0.4: w emulatorze wszystkie 32 złote rendery v0.5 są bez zmian.
- Shuffle (jeden na wzorzec, na nieparzystych krokach), humanize, opóźnienie ścieżki, własne tempo ścieżki, ratchety i rolle liczą się jak wcześniej, a reguła choke ścieżki jest ta sama.
- Formaty kitów, projektów i piosenek, 30 bajtów `glo.cfg`, MIDI CC i NRPN.
- Pamięć: DTCM + RAM 469 132 z 471 032 B budżetu (budżet rośnie o 15 488 B na mikrotiming i szablony 64 wzorców w RAM), DTCM 107 396 z 107 900 B (bez zmian), RAM 361 736 z 389 120 B, ITCM 13 744 z 16 384 B (+16 B na poprawkę PWM).

## Znane ograniczenia

- Pozycja w szablonie zaczyna się od nowa ze wzorcem, a swing (nieparzyste kroki) we wzorcu o nieparzystej długości przełącza się co pętlę. W takim wzorcu szablon i swing mogą się rozjechać. Tak jest z założenia.
- Szablony są w tickach, więc ten sam szablon jest w ms o 18 % dłuższy przy 120 BPM niż przy 142, tak jak `shf` i shuffle.
- Rolle nie biorą szablonu ani `mic`: trzymany roll na ścieżce z szablonem gra na prostej siatce ze swingiem.
- `mic` kończy się na ±84 (87,5 % kroku), a `mic` z szablonem razem na 7/8 kroku, cokolwiek doda szablon. Przy +84 uderzenie leży 12 ticków przed następnym krokiem (flam 15,6 ms przy 120 BPM) i grają oba. Krok, który swing albo `dly` przesuną za pierwsze uderzenie następnego kroku tej samej ścieżki, jest pomijany przez choke ścieżki, jak przy `shf` w 0.5.
- Wspólny hihat (ścieżki 6 i 7): wygrywa ten, który uderza później. Zamknięty przesunięty za otwarty z tego samego kroku go ucina.
- Nagrywanie: nuta, którą sekwencer przyjmie ponad pół kroku po jej przyjściu, ląduje na złym kroku. To wymaga zatrzymanej pętli głównej; opóźnienia parsowania to milisekundy.
- Nagrywanie: tuż po powrocie ścieżki z własnego tempa do ×1 jeden krok może mieć źle policzony początek, więc nuta nagrana w tym jednym kroku może trafić na następny (jak w 0.5).
- Nagrywanie: gdy nagrywany krok ma już w kolejce uderzenie z dodatnim przesunięciem, które jeszcze nie zabrzmiało, brzmi i nuta na żywo, i to uderzenie, raz (jak w 0.5).
- Nie ma własnych szablonów, szablonów velocity, kwantyzowanego MIDI out obok przesuniętego ani funkcji, która zeruje mikrotiming ścieżki.
- Pogłos gra w tymczasowej częstotliwości szkicu 0.4 (połowa), dopóki wydanie 0.4 jej nie ustali (H12, U20) i nie trafi do 0.5 i 0.6.
- Ograniczenia v0.1–v0.5 zostają.

## Pliki

- `LXR_V2_LICHOMAT_0.6.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.6`, SHA-256: (wpisany, gdy CI zbuduje commit wydania; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.6` w `isthar/lxr-desktop-app` (archiwa „Source code” wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths”, „LXR” i „LXR-02” to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Jak to sprawdzono w emulatorze (tylko dla Ciebie)

Gałąź `v0.6`, head `6a822ed`, ostatnia zmiana firmware `0e4559c`; szczegóły w opisach commitów. Lokalny build daje `.bin` o SHA-256 `8a3827d6…`; plik z CI będzie inny (inna kolejność linkowania newlib), jak przy wcześniejszych wersjach.

- Testy: 125 na hoście (114 w v0.5), wszystkie przechodzą pod UBSan i pod ASan + UBSan; 342 w emulatorze (329) pod UBSan i 314 (301) pod ASan + UBSan, bez aplikacji.
- Złote rendery: wszystkie 32 z v0.5 bez zmian, żaden nie został zapisany od nowa. To dowód, że bez mikrotimingu i szablonów v0.6 gra jak v0.5 (`smp`) i v0.4 (`blk`). Nowe: G20_micro, G21_template, G21_crossing, G22_record, G22_record_q i G22_record_tpl, na nowej karcie `groove` (klik z karty onset).
- Uderzenia wobec ticków policzonych niezależnie od firmware (model wzoru §4.1 specyfikacji w `render_test.py`), rozrzut zawsze poniżej 2 ramek:
  - G20 (`mic` na krawędziach podziału na `shf` i część drobną, ratchet ×2 z częścią drobną, Drum 2 na ×3/4): 68 uderzeń, rozrzut 1,97 ramki, i 12 uderzeń, 1,94;
  - G21 (`lyb` 100 % na Drum 1, `lay` 150 % z `dly` +3 ms na Drum 2, shuffle 20, 142 BPM): 64 i 64 uderzenia, 1,96 i 1,97;
  - G21_crossing: 8 uderzeń pominiętych przez choke w 2 taktach, tyle, ile liczy model.
- Nagrywanie (G22, 142 BPM): 16 nut MIDI z DIN, „e” o 11 ticków później (12,1 ms), „a” o 5 wcześniej (−5,5 ms). Odtwarzanie, 32 uderzenia w taktach 2–3: rozrzut 1,95 ramki wobec zagranych czasów. W pliku wzorca `mic` +11 i −5; z `lay` −7 i −10, a dźwięk jest ten sam co bez szablonu (WAV identyczny); z `rqn` `on` wszystkie `mic` 0, uderzenia na siatce (rozrzut 1,90), plik 3 697 B bez drugiego bloku.
- Pliki wzorców karty `groove`: każdy krok, szablon, tempo i opóźnienie takie, jak wynika ze skryptu karty; z mikrotimingiem 3 941 B, bez niego 3 697 B.
- Losowe przebiegi `tools/emu_fuzz.py`, teraz także z `mic`, `tpl` i `amt` na wartościach brzegowych 0–255: 1 800 renderów pod UBSan w obu ustawieniach `trg`, bez żadnego znaleziska. Pod ASan bez znalezisk w MIDI i sekwencerze; w dźwięku tylko stary odczyt tablicy PWM o jeden za wierszem w oscylatorze z 1.7 (tracker E4.11), w kodzie, którego v0.6 nie dotyka.
- Przeglądy: cztery (zadania 1–3, 4–5, 6 i 7), bez żadnej usterki krytycznej ani ważnej w kodzie firmware. Znalazły luki w testach i drobne sprawy UI, a po zadaniu 7 trzy drobne zabezpieczenia: nagrywanie tylko wewnątrz kroku sekwencera, blokadę przy edycji bajtów timingu z menu i ograniczenie `mic` do zakresu, który gra. Wszystko poprawione z testami, a każdy nowy test najpierw padał na celowo wprowadzonej usterce.
- CPU: przeliczenie statyczne na skompilowanym kodzie, ok. 1,45 % najgorszego bloku (sekcja „CPU”).
- CI na GitHubie: bez przebiegu dla `v0.6` (ani `v0.5`); GitHub nie uruchamia workflowów w tym repozytorium od 31.10 (tracker E8.5).

## Czego posłuchać (tylko dla Ciebie)

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/wt-v06/build/demo/` (poza gitem, na Twoim Macu), opis w `README-v0.6.txt` tamże. Robi je `tools/demo_v06.sh` z gałęzi `v0.6`: wyjście 1 stereo, karty `emu/tests/cards`; pogłos gra w nich w połowie częstotliwości (tymczasowo).

- `v06-hats-off.wav`, `v06-hats-swg.wav`, `v06-hats-lay.wav`, `v06-hats-lyb.wav`, `v06-hats-rlx.wav`, `v06-hats-2bt.wav`, `v06-hats-psh.wav`, `v06-hats-drg.wav`: karta init przy 142 BPM, 8 taktów, zamknięty hihat (głos 6) na wszystkich szesnastkach z danym szablonem na 100 %, reszta karty prosto. `off` to odniesienie.
- `v06-hats-lay50.wav` i `v06-hats-lay150.wav`: `lay` na 50 i 150 %.
- `v06-record-take.wav`: karta `groove`, wzorzec 3, 142 BPM, skrypt G22_record: REC, 16 nut MIDI w takcie 1 („e” +12,1 ms, „a” −5,5 ms), REC wyłączony; takty 2 i 3 odtwarzają nagranie z tym samym mikrotimingiem. Klik jest na lewym kanale.

Który szablon na którą ścieżkę: `docs/lichomat/groove-recipes-v0.6.md`.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu (tracker E8.6). Sprawdzeń v0.1–v0.5 nie trzeba powtarzać. U33–U35 czytają uderzenia na poziomie próbek w nagraniu wyjścia 1 w DAW, jak U27; U33 z klikiem z `docs/lichomat/kit-recipes-v0.5.md`, U35 najlepiej z tym klikiem na Drum 3.

- **U33:** mikrotiming: klik na 16 krokach z `mic` od ±1 do ±47 ticków przy 120 BPM, w `smp` i w `blk`; `shf` jako zgrubny widok; ms w trybie edycji.
- **U34:** szablony: `lay` na zamkniętym hihacie przy 142 BPM na 100, 50 i 150 %, stopa prosto; potem każdy szablon z Twoim groove.
- **U35:** nagrywanie: klip MIDI z DAW z „e” 12 ms później i „a” 6 ms wcześniej, z `rqn` `off`, `on` i z `lay`; potem hihat grany ręcznie.
- **U36:** pliki: zapis i wczytanie wzorca, projektu i `tmp.pat`; powrót do 1.7 i do 0.5; rozmiar pliku wzorca.
- **U37:** CPU na ciężkim wzorcu z H12 z szablonami na ścieżkach 2–7 i `mic` na każdym kroku.

## Publikacja (tylko właściciel)

1. Najpierw wydanie 0.5 (które idzie po wydaniu 0.4). Potem to, co 0.5 przyjęło przed wydaniem (częstotliwość pogłosu z 0.4 i jej złote rendery), trafia do `v0.6`. Build tej gałęzi przechodzi U33–U37, U37 z pogłosem w częstotliwości wydania 0.4. Jeśli U37 nie przejdzie, najpierw poprawka kodu i U37 jeszcze raz.
2. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i sekcje „tylko dla Ciebie”, a w „Znanych ograniczeniach” punkt o pogłosie. Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.6` jako `docs/lichomat/release-notes-v0.6.md`, zrób commit i push `v0.6` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebiegi CI (`.github/workflows/build.yml` i `emulator.yml`) przejdą.
3. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki” (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik. Gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, build się nie powtórzył: najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki” i w run sheet, zrób commit i push, i od tej pory używaj nowego commitu i jego przebiegu CI.
4. Otaguj ten commit `v0.6`, wypchnij tag i utwórz z niego wydanie GitHub `v0.6` z tytułem `LICHOMAT v0.6`, z tekstem od „LICHOMAT to nieoficjalny firmware” do końca „Licencja” (tak jak w tym pliku: wydanie GitHub pokazuje każde złamanie linii) i z dołączonym `LXR_V2_LICHOMAT_0.6.img` z artefaktu.
