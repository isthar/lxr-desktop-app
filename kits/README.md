<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

# Zestawy brzmień LICHOMAT

Dwanaście własnych kitów dla LICHOMAT v0.4 i nowszych. Cztery (sloty 60–63) to każdy inny styl:
hipnotyczne groove techno, dub techno, surowe/industrialne i minimal z FM. Cztery kolejne (sloty
56–59) są dopasowane do pomiarów albumu referencyjnego w stylu BCCO / Mutual Rytm (sekcja „Kity
z pomiarów”). Cztery najnowsze (sloty 52–55) powstały z przepisów laboratorium brzmień: ghost kick
albo niski bed i rzadkie zdarzenia w pogłosie (sekcja „Kity z laboratorium brzmień”). Pliki kitów
zapisał sam firmware: emulator (`lichomat-render`) ustawił parametry przez ścieżkę edycji menu
i zapisał kit i projekt, więc format, łącznie z 13-bajtowym blokiem v0.4, jest taki, jak na
urządzeniu. Nic tu nie pochodzi z kitów
fabrycznych ani z DLC. Demo to rendery emulatora, nie nagrania z LXR; poziomy są zmierzone z plików
WAV, bez odsłuchu.

| Plik na karcie | Slot | Nazwa | Styl | Demo |
|---|---|---|---|---|
| `card/PROJ00/52-ECHO.SND` | 52 | `ECHO` | groove techno: ghost kick, rzadki ryk daleko w pogłosie | `demo/echo.wav` |
| `card/PROJ00/53-FALA.SND` | 53 | `FALA` | groove techno: ghost kick, riser i boom na granicy frazy | `demo/fala.wav` |
| `card/PROJ00/54-RYK.SND` | 54 | `RYK` | groove techno: ghost kick, ryk tylko w echach i pogłosie | `demo/ryk.wav` |
| `card/PROJ00/55-TLO.SND` | 55 | `TLO` | groove techno: niski bed z rumble, downlifter i jąkanie | `demo/tlo.wav` |
| `card/PROJ00/56-PULS.SND` | 56 | `PULS` | groove techno: mediana albumu referencyjnego | `demo/puls.wav` |
| `card/PROJ00/57-HUSTA.SND` | 57 | `HUSTAWKA` | groove techno: mocny swing, szeroka góra | `demo/hustawka.wav` |
| `card/PROJ00/58-PROST.SND` | 58 | `PROSTO` | groove techno: proste hihaty, bed jak bas | `demo/prosto.wav` |
| `card/PROJ00/59-PRZES.SND` | 59 | `PRZESTER` | groove techno: przesterowana stopa | `demo/przester.wav` |
| `card/PROJ00/60-WIR.SND` | 60 | `WIR` | hipnotyczne groove techno | `demo/wir.wav` |
| `card/PROJ00/61-MGLA.SND` | 61 | `MGLA` | dub techno | `demo/mgla.wav` |
| `card/PROJ00/62-RDZA.SND` | 62 | `RDZA` | surowe / industrialne | `demo/rdza.wav` |
| `card/PROJ00/63-IGLA.SND` | 63 | `IGLA` | minimal / FM | `demo/igla.wav` |

Ustawienia każdego kitu, z komentarzami, są w `wir.txt`, `mgla.txt`, `rdza.txt`, `igla.txt`, `puls.txt`,
`hustawka.txt`, `prosto.txt`, `przester.txt`, `echo.txt`, `fala.txt`, `ryk.txt` i `tlo.txt`: z nich
`tools/kit_build.py` buduje pliki. Nazwy parametrów to nazwy z `emu/render/render_params.h`, wartości
takie, jakie trzyma menu (listy liczone od 0).

Każdy z kitów 60–63 ma też trochę ruchu: jedno LFO zsynchronizowane z tempem, a WIR i MGLA także velocity na
poziom jednego głosu. Cel LFO (`target_lfoN`) i velocity (`vel_dest_N`) to numer na liście celów menu
(`modTargets` w `Core/Src/menu/Cc2Text.c`, 0 to `off`), np. 196 to wybrzmienie zamkniętego hihatu.
`voice_lfoN` stoi przed `target_lfoN`, bo zmiana głosu przestawia cel na ten sam rodzaj w nowym
głosie, tak jak w menu. LFO chodzą swobodnie (`retrigger` `off`), więc faza względem taktu zależy od
chwili włączenia. Głębokość działa w obie strony wokół ustawionej wartości. Velocity w tych kitach nie
zmienia samej głośności (`volume_mod_on_off` 0, jak w kicie startowym), więc kroki grają równo, poza
głosami, gdzie velocity ma cel `vol`.

## Demo

Dema kitów 60–63 to ten sam wzorzec `classic` (16 kroków, 8 taktów, 128 BPM, `trg smp`, wyjście 1
stereo); dema kitów 56–59 grają wzorzec `album`, opisany w sekcji „Kity z pomiarów”, a dema kitów 52–55
wzorce `ghost` i `bed`, opisane w sekcji „Kity z laboratorium brzmień”. Wzorzec `classic`:

- **Drum 1:** stopa na 1, 5, 9, 13.
- **Drum 2:** szesnastki z akcentami 3-3-2 (kroki 4, 7, 10, 16), ciche przednutki; 6 i 14 grają co drugi takt (`2:2`).
- **Drum 3:** kroki 4 i 11, a 14 co czwarty takt (`4:4`).
- **Snare:** 5 i 13 (na 2 i 4), cichy krok 16 co czwarty takt.
- **Cymbal:** offbeaty 3, 7, 11, 15.
- **Hihat:** zamknięty na 2, 6, 8, 10, 14, 16, otwarty (ścieżka 7) na offbeatach 3, 7, 11, 15. Zamknięty
  ucina otwarty, więc otwarte na 7 i 15 są krótkie, a na 3 i 11 dłuższe.

Wzorzec nie jest częścią zestawu; jest w `PATTERN` w `tools/kit_build.py`, żeby go łatwo wklepać ręcznie.

## WIR: hipnotyczne groove techno

**Pomysł:** głęboka, okrągła stopa z krótkim klikiem, ciasny klaps, metaliczny hihat i dwa toczące się
perkusjonalia, które grają w krótkim, ciemnym pokoju. Stopa dominuje, reszta kręci się wokół niej.

- **Drum 1 (stopa):** sinus G1 +⅕ półtonu (49,6 Hz), szybki spadek wysokości od ok. 3,9× (`modamnt1` 38,
  `mod_eg1` 26), długie ciało (`velod1` 30), transient `Clk` (`trans1_vol` 55), LP prawie otwarty,
  ciepłe nasycenie (`drive1` 84).
- **Drum 2:** niski tom/bongo (trójkąt E3) z odrobiną FM, krótki (`velod2` 8), na szesnastki; do FX.
- **Drum 3:** wyższy metaliczny perkusjonalny dźwięk (sinus E4, FM o nieharmonicznym stosunku); do FX.
- **Snare:** ciasny klaps: prawie sam szum (`mix1` 112), BP 86, powtórzenia (`repeat4` 3), transient `Clp`.
- **Cymbal:** cichy, suchy metaliczny ride na offbeatach.
- **Hihat:** prostokąty w FM, HP 106; zamknięty krótki (`velod6_closed` 6), otwarty dłuższy (28).
- **FX:** slot `off`; pogłos `lvl` 55, `siz` 70, `dec` 40 (ok. 1,1 s), `ton` 35, `pre` 8, `mod` 30 dla
  Drum 2 i Drum 3. Stopa, klaps i hihat na sucho (`St1`).
- **Ruch:** LFO 6 (`sin`, `syn` `4/1`, głębokość 3) na wybrzmieniu zamkniętego hihatu: `velod6_closed`
  chodzi między 3 a 9, cykl trwa 4 takty, więc hihat co chwilę się lekko otwiera i znowu zamyka.
  Velocity 6 na `vol6` (`velo_mod_amt_6` 20, `vol6` 33): akcenty (80) głośniej, przednutki (55) ciszej,
  otwarte (100, 90) najgłośniej; między krokami hihatu jest teraz ok. 2,8 dB różnicy.

## MGLA: dub techno

**Pomysł:** miękka, długa stopa, rim na szesnastkach i akordowy stab FM na Drum 3, oba wrzucone
w delay na kropkowaną ósemkę i w długi, ciemny pogłos, który stopa przycisza.

- **Drum 1 (stopa):** sinus G1 (49,0 Hz), łagodny spadek wysokości (ok. 2,6×), długa (`velod1` 44), bez
  kliku (`Snp` na 12), LP 72: miękka. Na sucho.
- **Drum 2 (rim):** krótki, wysoki (E5), transient `Rim`, BP 100; do FX.
- **Drum 3 (stab):** FM z modulatorem kwintę nad nośną (A3 i E4): widmo harmoniczne wokół niskiego
  pryma, z oktawami, kwintą i tercją wielką, więc brzmi jak akord. Indeks FM opada z obwiednią
  modulacji (`mod_eg3` 40, `modamnt3` 0: bez spadku wysokości), LP 78 z rezonansem 48; do FX.
- **Snare:** miękki klaps (`repeat4` 2), na sucho.
- **Cymbal:** cichy shaker z szumu z HP; **Hihat:** szum z HP, otwarty długi (`velod6_open` 36).
- **FX:** slot `del`, `typ` `Del`, `syn` `8d` (352 ms przy 128 BPM), `fbk` 84, `ton` 48, `d/w` 58. Pogłos
  `lvl` 104, `siz` 112, `dec` 86 (ok. 5 s), `ton` 28, `pre` 14, `mod` 60. Rumble: `dck` `dr1`, `ddp` 70:
  ogon chowa się pod stopą.
- **Ruch:** LFO 3 (`tri`, `syn` `4/1`, głębokość 40) na indeksie FM stabu (`fmamnt3` 62, czyli 22–102):
  w jednych taktach akord jest jaśniejszy, w innych bardziej okrągły. Nie filtr: LP 78 jest ponad
  stabem, a dopiero głębokie zejście w dół go dosięga i wtedy dusi stab. Velocity 3 na `vol3`
  (`velo_mod_amt_3` 16, `vol3` 45): stab na 100 jest ok. 1,1 dB głośniejszy niż wypełnienie na 70.

## RDZA: surowe / industrialne

**Pomysł:** przesterowana, mocna stopa, która sama karmi ciemny pogłos i sama go przycisza (rumble),
szumowy przesterowany werbel, ostre hihaty i metal.

- **Drum 1 (stopa):** sinus A1 (54,6 Hz), twardy spadek wysokości od ok. 6×, transient `Kik`, mocny
  przester (`drive1` 108, `filter_drive_1` 50), lekka decymacja (`voice_decimation1` 112); `out` `FX`.
- **Drum 2:** metaliczne uderzenie: sinus A2 w mocnym FM (`fmamnt2` 90), przester (`drive2` 90), HP.
- **Drum 3:** brzęk: prostokąt D4 w mocnym FM, BP 92 z rezonansem 60.
- **Snare:** szumowy (`mix1` 120), HP 44, przester (`snare_distortion` 85), decymacja, transient `Snr`.
- **Cymbal:** krótki, ostry metal (prostokąty w FM, przester 90); **Hihat:** prostokąty w FM, HP 110,
  rezonans 40, przester 70, decymacja.
- **FX:** slot `off`. Pogłos `lvl` 104, `siz` 110, `dec` 76 (ok. 3,7 s), `ton` 14 (ciemny), `pre` 6,
  `mod` 40. Rumble: `dck` `dr1`, `ddp` 112: ogon puchnie między stopami i chowa się pod każdą.
- Przester głosów działa za `vol`, więc głośność werbla i hihatów ustawiają bardziej `snare_distortion`
  i `hat_distortion` niż `vol`.
- **Ruch:** LFO 6 (`tri`, `syn` `2/1`, głębokość 16) na HP hihatu (`filter_freq_6` 110, czyli 94–126):
  co 2 takty hihaty chudną i wracają, przester zostaje.

## IGLA: minimal / FM

**Pomysł:** krótka, klikająca stopa, perkusja FM na Drum 2, Drum 3 i talerzu, suchy snap i krótkie,
tykające hihaty; trochę delaya na Drum 3.

- **Drum 1 (stopa):** sinus A1 (55,0 Hz), bardzo szybki spadek wysokości od ok. 5× (`mod_eg1` 9),
  krótka (`velod1` 20), transient `Tik`, bez filtra.
- **Drum 2:** klocek FM: sinus A4, modulator E6 (3:1), krótki.
- **Drum 3:** blip FM z krótkim „pew" (E5, modulator +17 półtonów); do FX.
- **Snare:** suchy snap: krótki ton, jasny szum, HP 58, transient `Rim`.
- **Cymbal:** sinusy w FM, krótkie metaliczne tyknięcie, BP 96.
- **Hihat:** sinusy w FM, HP 114; zamknięty bardzo krótki (`velod6_closed` 3), otwarty 12.
- **FX:** slot `del`, `typ` `Pp`, `syn` `16d` (176 ms przy 128 BPM: echa padają między krokami),
  `fbk` 60, `d/w` 50. Mały pokój: `lvl` 50, `siz` 50, `dec` 30 (ok. 0,8 s), `ton` 45.
- **Ruch:** LFO 2 (`tri`, `syn` `4/1`, głębokość 40) na indeksie FM klocka na Drum 2 (`fmamnt2` 58,
  czyli 18–98): szesnastki przechodzą w 4 taktach od drewna do szkła i z powrotem.

## Pomiary

Z plików demo (`tools/kit_build.py`, zwykły Python): szczyt i RMS całego dema (oba kanały), podstawowa
stopy z przejść przez zero między 40 a 160 ms po uderzeniu i wybrzmienie każdego głosu do −40 dB
od szczytu (głos sam, bez slotu i pogłosu; „>” to do następnego uderzenia tego głosu).

| Zestaw | Szczyt | RMS | Stopa | Drum 1 | Drum 2 | Drum 3 | Snare | Cymbal | Hihat zamkn. / otw. |
|---|---|---|---|---|---|---|---|---|---|
| WIR | −1,4 dBFS | −11,4 dBFS | 49,6 Hz | 450 ms | 98 ms | 102 ms | 174 ms | 90 ms | 32* / >348 ms |
| MGLA | −1,5 dBFS | −11,8 dBFS | 49,0 Hz | >458 ms | 44 ms | 368 ms | 218 ms | 64 ms | 34 / >344 ms |
| RDZA | −1,3 dBFS | −9,8 dBFS | 54,6 Hz | 450 ms | 40 ms | 98 ms | 198 ms | 12 ms | 74 / >348 ms |
| IGLA | −1,6 dBFS | −12,6 dBFS | 55,0 Hz | 272 ms | 72 ms | 56 ms | 92 ms | 16 ms | 32 / 132 ms |

Żadne demo nie przekracza −1 dBFS. RMS jest w granicach ±3 dB od poziomu odniesienia: mediana RMS
16 wzorców Twojej karty DLC w emulatorze to ok. −9,7 dBFS (tylko pomiar głośności; nic z tej karty
nie jest tu użyte). Wzorce DLC dochodzą do 0 dBFS, te kity mają ok. 1,3 dB zapasu.

\* Pierwszy zamknięty hihat dema; LFO zmienia to wybrzmienie w cyklu 4 taktów (bez LFO 62 ms).

**Ruch:** każdy głos z modulacją sam (suwaki reszty na 0), 8 taktów dema, takt po takcie, w porównaniu
z tym samym kitem bez modulacji: rozrzut (największa minus najmniejsza różnica w takcie) RMS albo
jasności. Jasność to RMS pierwszej różnicy sygnału względem jego RMS, rośnie ze średnią
częstotliwością. Cel: ruch wyraźny w pomiarze, ale mały, od 1 do 6 dB (bez odsłuchu).

| Zestaw | Co się rusza | Miara | Rozrzut w 8 taktach |
|---|---|---|---|
| WIR | wybrzmienie zamkniętego hihatu | RMS zamkniętych uderzeń, na sucho | 2,8 dB |
| MGLA | indeks FM stabu | jasność stabu, na sucho (z delayem i pogłosem) | 1,1 dB (0,6 dB) |
| RDZA | HP hihatu | RMS hihatu | 1,9 dB |
| IGLA | indeks FM Drum 2 | jasność Drum 2 | 1,2 dB |

Szczyt i RMS całych dem zmieniły się najwyżej o 0,2 dB. Z `trg blk` zamiast `trg smp` szczyt dem
jest od −2,1 do −1,3 dBFS, a RMS różni się najwyżej o 0,1 dB.

## Kity z pomiarów: PULS, HUSTAWKA, PROSTO, PRZESTER

Cztery kity w slotach 56–59, dopasowane do pomiarów albumu referencyjnego w stylu BCCO / Mutual
Rytm (groove techno, 142–144 BPM, stopa 4/4). Stare kity zostają w slotach 60–63, pliki bajt w bajt
te same. Z albumu nie ma tu nic poza liczbami docelowymi w tabelach: ani dźwięku, ani fragmentów,
ani szablonów.

| Kit | Wariant albumu | Tempo dema | Shuffle | Bed |
|---|---|---|---|---|
| `PULS` | mediana albumu | 142 BPM | 18 (10 ms) | ciemny sinus 47 Hz, znika po brakującej stopie |
| `HUSTAWKA` | mocny swing, szeroka góra | 143 BPM | 45 (25 ms) | sinus 48,4 Hz i pogłos, gra dalej bez stopy |
| `PROSTO` | proste hihaty, bed jak bas | 143 BPM | 0 | bas E2 (83 Hz) |
| `PRZESTER` | przesterowana stopa, węższa góra | 142 BPM | 24 (13 ms) | ciemny sinus 45,8 Hz |

**Jak dopasowane.** Stopa pomiarem, nie na ucho: skrypt renderował sam Drum 1 przez
`lichomat-render` z kandydatami `--param` (siatka 64 renderów `modamnt1` × `mod_eg1` ×
`pitch_slope1`, dwa zawężenia, potem wybrzmienie, drive i transjent; razem ok. 220 krótkich renderów)
i liczył to samo co pomiar albumu: przebieg wysokości z przejść przez zero, dopasowanie
f(t) = fe + (f0 − fe)·e^(−t/τ), czasy −3 i −6 dB, klik i THD. Gotowe kity mierzy ten sam program co
album: 32 takty dema (stopa uśredniona ze wszystkich uderzeń, pasmo 30–150 Hz w jednym uderzeniu,
hihaty na siatce 16-tek, swing, środek), a spadek bedu z osobnego renderu, w którym stopa (i bed,
poza `HUSTAWKA`) nie gra na 4. ćwierćnutę. Wszystkie liczby „wynik” poniżej to te pomiary.

**Bed: dwie drogi.** (a) Pogłos v0.4 z rumble (stopa do FX, `dck` `dr1`, ciemny `ton`, krótki `dec`):
nawet przy `lvl` 127 bed był 16–21 dB pod stopą (cel 4–6,5 dB), po brakującej stopie opadał tylko o
4–18 dB i miał szczyt 51–56 Hz, na wysokości stopy. (b) Drum 2 jako głos bedu: niski sinus na
offbeatach z wybrzmieniem prawie jak bramka (`vol_slope2` 120), które kończy się przy następnej
stopie: obwiednia 30–150 Hz w uderzeniu różni się od mediany albumu o 1,0–1,5 dB (RMS). Bed
uruchamiany razem ze stopą, z wolnym atakiem, wyszedł gorzej (4–15 dB): atak ponownie wyzwolonego
głosu rusza od bieżącej wartości, więc bed nie chowa się pod stopą. Wybrane (b); `HUSTAWKA` dokłada
do niego (a), żeby dół grał dalej, gdy stopa milknie.

**Wzorzec demo `album`** (`ALBUM_PATTERN` w `tools/kit_build.py`, kroki 1–16, głośność kroku 0–127;
pliki kitów mają `volume_mod_on_off` 1 na Drum 3, Cymbal i Hihat, więc głośność kroku działa):

- **Drum 1:** stopa 1, 5, 9, 13.
- **Drum 2:** bed na offbeatach 3, 7, 11, 15.
- **Drum 3:** figura 3 (70), 4 (110), 7 (64), 8 (110), 11 (70), 12 (110), 14 (64), 16 (110): akcent na
  ostatniej 16-tce każdej ćwierćnuty.
- **Snare:** cichy szum na 2 i 4 (kroki 5, 13).
- **Cymbal:** przednutki na parzystych 16-tkach: 2, 6, 10, 14 (20, ok. −16 dB) i 4, 8, 12, 16 (28,
  ok. −13 dB).
- **Hihat:** zamknięty (ścieżka 6) na ćwierćnutach 1, 5, 9, 13 (40, ok. −10 dB), otwarty, krótki
  (ścieżka 7) na offbeatach 3, 7, 11, 15 (127).

Hihaty grają więc każdą 16-tkę, z akcentem na offbeatach, jak na albumie. Swing (`shuffle`)
przesuwa parzyste 16-tki; zmierzony w rendererze: 18 to 9,9 ms przy 142 BPM, 24 to 13,2 ms, 45 to
25,1 ms przy 143 BPM.

**Shuffle to ustawienie wzorca, nie kitu.** Kit go nie zapisuje. Ustaw `Shuffle` (strona globalna:
SHIFT + LOAD/SAVE, albo strona performance) na wartość z tabeli wyżej i zapisz wzorzec, na którym
grasz kit; wzorzec wczytuje swój shuffle przy zmianie wzorca.

Tabele poniżej: „cel” to liczby z pomiaru albumu (mediana i rozrzut, albo utwory, z których kit
bierze wariant), „wynik” to pomiar dema tego kitu. Szczyt / RMS dem: PULS −1,9 / −9,6 dBFS,
HUSTAWKA −1,3 / −10,7, PROSTO −2,7 / −11,1, PRZESTER −1,8 / −8,8; żadne nie przekracza −1 dBFS.

### PULS: mediana albumu

| Wielkość | Cel | Wynik |
|---|---|---|
| Stopa: wysokość startowa f0 | 263 Hz (235–290) | 262 Hz |
| Stopa: stała czasowa τ | 32 ms (25–35) | 33 ms |
| Stopa: wysokość ustalona fe | 50,6 Hz (46–58) | 48,9 Hz |
| Stopa w 10/20/40/80/150 ms | 205/160/110/66/51 Hz | 208/157/110/69/51 Hz |
| Stopa: −3 dB / −6 dB | ok. 170 / 210 ms (−6 dB: 150–235) | 180 / 214 ms |
| Klik (>2 kHz, 0–10 ms, względem ciała) | −31 dB (−27…−38) | −50 dB |
| THD stopy | 6 % (3–8) | 5,8 % |
| Bed: poziom względem stopy | −4,4 dB (−4…−6,5) | −4,2 dB |
| Bed: powrót (w 1,5 dB od plateau) | 245 ms, 0,58 ćwierćnuty (175–305) | 255 ms, 0,60 |
| Bed: szczyt widma | 44–49 Hz | 47,1 Hz |
| Bed: +50 / +100 ms po brakującej stopie | ≥ 15 dB w dół w 50–100 ms | −23 / −44 dB |
| Hihat offbeat: centroid | 8,3 kHz | 8,3 kHz |
| Hihat offbeat: −10 dB / −20 dB (dopasowanie) | 31 / 78 ms | 29 / 80 ms |
| Swing parzystych 16-tek | ok. 10 ms (55 %) | 9,9 ms |
| Szerokość góry (side/mid > 6 kHz) | ok. −4,5 dB | −5,7 dB |
| Szum na 2 i 4 (względem stopy) | −21…−27 dB | −34 dB |
| Figura | jedna klasa wysokości, 220–590 Hz | D4 |

- **Drum 1 (stopa):** sinus G#1 −0,4 półtonu (50,7 Hz: `coarse1` 32, `fine1` 14), spadek wysokości
  `modamnt1` 50, `mod_eg1` 58, `pitch_slope1` 17; `velod1` 19, `vol_slope1` 104 (trzyma, potem szybko
  opada); bez transjentu i filtra, `drive1` 37, `vol1` 100.
- **Drum 2 (bed):** sinus F#1 +0,3 półtonu (47 Hz: `coarse2` 30, `fine2` 100), `velod2` 16,
  `vol_slope2` 120, `vol2` 64.
- **Drum 3 (figura):** sinus D4 (`coarse3` 62) z FM (`fmamnt3` 20, `fm_freq3` 76), krótki pstryk
  wysokości (`modamnt3` 6), `velod3` 7, `vol_slope3` 30, `vol3` 34. LFO 3 (`tri`, `4/1`, głębokość 16)
  na `fmamnt3`: figura w 4 taktach jaśnieje i ciemnieje.
- **Snare (szum na 2 i 4):** szum z odrobiną sinusa (`mix1` 104), BP 76 (ok. 2 kHz), `velod4` 7,
  `snare_distortion` 20, `vol4` 127.
- **Cymbal (przednutki):** szum przez HP 97, `velod5` 4, `vol5` 28.
- **Hihat:** szum przez HP 101, zamknięty `velod6_closed` 4, otwarty `velod6_open` 14,
  `vol_slope6` 8 (szybki spadek i dłuższy ogon), `vol6` 127. LFO 6 (losowe, `syn` `16`, głębokość 127)
  na `pan6`: każda 16-tka w innym miejscu panoramy.
- **FX:** wyłączone, bez pogłosu.

### HUSTAWKA: mocny swing, szeroka góra

Wariant utworów z najmocniejszym swingiem (w tabeli dwa takie utwory: „A / B”).

| Wielkość | Cel | Wynik |
|---|---|---|
| Stopa: wysokość startowa f0 | 234 / 262 Hz | 242 Hz |
| Stopa: stała czasowa τ | 33 / 29 ms | 33 ms |
| Stopa: wysokość ustalona fe | 45,9 / 50,0 Hz | 46,6 Hz |
| Stopa w 10/20/40/80/150 ms | 184/144/100/65/48 i 200/154/106/63/51 Hz | 192/147/102/65/49 Hz |
| Stopa: −3 dB / −6 dB | 218 / 214 i 157 / 236 ms | 217 / 214 ms |
| Klik | −38 / −34 dB | −51 dB |
| THD stopy | 3,4 / 8,2 % | 7,0 % |
| Bed: poziom względem stopy | −4,5 / −4,2 dB | −5,3 dB |
| Bed: powrót | 305 / 290 ms (0,72 / 0,69) | 310 ms (0,74) |
| Bed: szczyt widma | 48,5 Hz | 48,4 Hz |
| Bed: +50 / +100 ms po brakującej stopie | −5,6 / −7,7 i −2,4 / −1,9 dB (gra dalej) | −0,6 / −4,3 dB |
| Hihat offbeat: centroid | 8,2 / 8,1 kHz | 8,0 kHz |
| Hihat offbeat: −10 dB / −20 dB | 32 / 82 i 44 / 82 ms | 35 / 95 ms |
| Swing parzystych 16-tek | 26,3 / 23,5 ms | 24,8 ms |
| Szerokość góry (side/mid > 6 kHz, korelacja L/P) | −5,4 / −0,1 dB (0,55 / 0,01) | −3,9 dB (0,42) |
| Szum na 2 i 4 | brak / −26 dB | −35 dB |
| Figura | C#4 / G4 | G4 |

- **Drum 1 (stopa):** sinus G1 +0,1 półtonu (49,3 Hz: `coarse1` 31, `fine1` 79), `modamnt1` 50,
  `mod_eg1` 58, `pitch_slope1` 15; `velod1` 20, `vol_slope1` 106, `drive1` 37, `vol1` 86.
- **Drum 2 (bed):** sinus G1 −0,2 półtonu (48,4 Hz: `coarse2` 31, `fine2` 39), `velod2` 16,
  `vol_slope2` 120, `vol2` 68, `out` `FX`: wysłany też do pogłosu.
- **Pogłos (rumble):** `lvl` 112, `siz` 90, `dec` 76 (ok. 3,7 s), `ton` 5 (ciemny), `pre` 0, `mod` 40,
  `dck` `dr1`, `ddp` 127. Stopa chowa go całkiem; wraca późno po każdej stopie i trzyma dół, gdy stopa
  milknie. Slot FX wyłączony.
- **Drum 3 (figura):** jak w PULS, na G4 (`coarse3` 67).
- **Snare, Cymbal:** jak w PULS, szum na 2 i 4 ciszej (`vol4` 100).
- **Hihat:** jak w PULS, HP 101, `velod6_open` 15, `vol_slope6` 10. Losowa panorama na hihacie
  (LFO 6) i na przednutkach (LFO 5, `pan5`, cel 184), obie z głębokością 127.

**Uwaga:** ten kit zależy od pogłosu, a częstotliwość próbkowania pogłosu jest tymczasowa (połowa
częstotliwości, do odczytu CPU z urządzenia, H12). Po zmianie trzeba go zmierzyć jeszcze raz
(`python3 tools/kit_build.py hustawka` i porównać bed).

### PROSTO: proste hihaty, bed jak bas

Wariant utworów bez swingu; w jednym z nich bed ma szczyt na E2, jak linia basu.

| Wielkość | Cel | Wynik |
|---|---|---|
| Stopa: wysokość startowa f0 | 284 / 251 Hz | 268 Hz |
| Stopa: stała czasowa τ | 30 / 35 ms | 34 ms |
| Stopa: wysokość ustalona fe | 50,9 / 53,6 Hz | 49,3 Hz |
| Stopa w 10/20/40/80/150 ms | 202/174/113/68/53 i 223/165/118/70/52 Hz | 218/165/115/72/53 Hz |
| Stopa: −3 dB / −6 dB | 130 / 167 i 147 / 151 ms | 139 / 161 ms |
| Klik | −32 / −30 dB | −51 dB |
| THD stopy | 6,0 / 3,7 % | 5,7 % |
| Bed: poziom względem stopy | −4,3 / −5,7 dB | −5,9 dB |
| Bed: powrót | 190 / 175 ms (0,45 / 0,42) | 220 ms (0,52); 175 ms z przesunięciem −3 |
| Bed: szczyt widma | 48,5 / 83,4 Hz | 83,4 Hz |
| Bed: +50 / +100 ms po brakującej stopie | −15,8 / −18,2 i −16,8 / −14,1 dB | −22 / −25 dB |
| Hihat offbeat: centroid | 7,5 / 8,4 kHz | 8,1 kHz |
| Hihat offbeat: −10 dB / −20 dB | 46 / 93 i 26 / 58 ms | 31 / 81 ms |
| Swing | brak | 0 ms |
| Szerokość góry | −4,4 / −4,7 dB | −4,8 dB |
| Szum na 2 i 4 | −27 dB / brak | −38 dB |
| Figura | A4 / B3 | B3 |

- **Drum 1 (stopa):** sinus G#1 +0,3 półtonu (52,7 Hz: `coarse1` 32, `fine1` 98), `modamnt1` 50,
  `mod_eg1` 58, `pitch_slope1` 17; krótsze ciało: `velod1` 14, `vol_slope1` 108; `drive1` 32.
- **Drum 2 (bas):** piła E2 +0,2 półtonu (83 Hz: `coarse2` 40, `fine2` 92) przez LP 20, `reso_2` 20,
  `velod2` 16, `vol_slope2` 120, `vol2` 120. Urywa się przed każdą stopą.
- **Drum 3 (figura):** jak w PULS, na B3 (`coarse3` 59), kwinta nad basem.
- **Snare, Cymbal:** jak w PULS; szum na 2 i 4 ciszej (`vol4` 100), przednutki głośniej (`vol5` 40).
- **Hihat:** jak w PULS, HP 99, `velod6_open` 12, `vol_slope6` 12.

Na kroku offbeatu (211 ms po stopie) bas wraca po ok. 220 ms. W albumie wraca po 175–190 ms. Żeby
to uzyskać, przesuń kroki basu (ścieżka 2) o −3 (`shf` na stronie kroku: −36 tików, ok. 39 ms
wcześniej przy 143 BPM): zmierzony powrót 175 ms, spadek po stopie 170 ms / −7,7 dB.

### PRZESTER: przesterowana stopa, węższa góra

Wariant jednego utworu, z mocniej przesterowaną stopą (THD ok. 14 %).

| Wielkość | Cel | Wynik |
|---|---|---|
| Stopa: wysokość startowa f0 | 289 Hz | 277 Hz |
| Stopa: stała czasowa τ | 33 ms | 35 ms |
| Stopa: wysokość ustalona fe | 46,7 Hz | 45,8 Hz |
| Stopa w 10/20/40/80/150 ms | 218/169/121/70/48 Hz | 217/170/120/71/49 Hz |
| Stopa: −3 dB / −6 dB | 170 / 205 ms | 193 / 201 ms |
| Klik | −30,5 dB | −41 dB |
| THD stopy (H2 / H3) | 14,5 % (−19 / −21 dB) | 14,0 % (−32 / −18 dB) |
| Bed: poziom względem stopy | −4,1 dB | −4,8 dB |
| Bed: powrót | 245 ms (0,58) | 225 ms (0,53) |
| Bed: szczyt widma | 45,8 Hz | 45,8 Hz |
| Bed: +50 / +100 ms po brakującej stopie | −6,4 / −16,4 dB | −22 / −45 dB |
| Hihat offbeat: centroid | 8,7 kHz | 8,7 kHz |
| Hihat offbeat: −10 dB / −20 dB | 44 / 107 ms | 40 / 105 ms |
| Swing parzystych 16-tek | 13,0 ms | 13,4 ms |
| Szerokość góry (side/mid, korelacja L/P) | −9,4 dB (0,80) | −9,5 dB (0,80) |
| Szum na 2 i 4 | −20,6 dB | −37 dB |
| Figura | A3 | A3 |

- **Drum 1 (stopa):** sinus G1 −0,2 półtonu (48,5 Hz: `coarse1` 31, `fine1` 43), `modamnt1` 50,
  `mod_eg1` 57, `pitch_slope1` 23; `velod1` 16, `vol_slope1` 108; mocny przester `drive1` 86.
- **Drum 2 (bed):** sinus F#1 −0,2 półtonu (45,8 Hz: `coarse2` 30, `fine2` 41), jak w PULS.
- **Drum 3 (figura):** jak w PULS, na A3 (`coarse3` 57).
- **Snare, Cymbal:** jak w PULS.
- **Hihat:** jak w PULS, HP 103, `velod6_open` 15, `vol_slope6` 12; losowa panorama płycej (LFO 6,
  głębokość 70): węższa góra.

### Co się nie zgadza i dlaczego

- **Klik (−41…−51 dB zamiast −27…−38) i poziom góry.** W albumie liczba kliku obejmuje hihat na
  ćwierćnucie, który siedzi w uśrednionej stopie. Tu hihaty to szum, który przy uśrednieniu ok. 120
  uderzeń prawie znika (15–20 dB), i są wobec stopy dużo ciszej niż na albumie: oktawa 8 kHz jest 42–43 dB
  pod oktawą 63 Hz, w albumie 14 dB. Szumowy hihat LXR na `vol6` 127 ma szczyt ok. −17 dBFS; przester
  hihatu dodaje głośności, ale działa za velocity, więc ćwierćnuty grają wtedy tak głośno jak offbeaty,
  a hihat ciemnieje i się wydłuża. Głośniejsza góra: hihaty i Cymbal na osobne wyjście i w górę na
  mikserze.
- **Środek (500 Hz–4 kHz) do 20 dB pod albumem.** Album to cały miks, z syntezatorami i wokalem; tu są
  tylko figura i szum na 2 i 4.
- **Szum na 2 i 4 (−34…−38 dB zamiast −21…−27).** Pada razem ze stopą; głośniejszy (przester werbla)
  dawał szczyt dema 0 dBFS.
- **PROSTO: powrót basu 220 ms zamiast 175–190 ms.** Krok offbeatu jest 211 ms po stopie; patrz
  przesunięcie −3 wyżej.
- **PRZESTER: H2 −32 dB zamiast −19 dB, −3 dB po 193 ms zamiast 170 ms.** `drive1` jest symetryczny,
  daje głównie nieparzyste harmoniczne (THD się zgadza), i trzyma ciało stopy.
- **fe 1–3 Hz niżej w PULS i PROSTO.** Bed miesza się w uśrednioną stopę i ściąga dopasowanie; sama
  stopa (bez reszty) ma fe 49,4 Hz w PULS, a wysokość w 150 ms (51 i 53 Hz) się zgadza.
- **Bed jest w pełni powtarzalny** (koherencja 1,0), w albumie 0,63–0,81: tu każde uderzenie bedu jest
  identyczne.

## Kity z laboratorium brzmień (sloty 52–55)

Cztery kity z przepisów laboratorium brzmień (2026-11-11), zaakceptowanych na ucho („A i B, wszystkie
przykłady OK”): zestaw A to ghost kicki na Drum 2 i bed, zestaw B rzadkie zdarzenia w tle, ok. 17 dB
pod groove, w pogłosie. Każdy kit to PROSTO, w którym głosy z tabeli zastąpiły edycje przepisu,
w kolejności przepisu; stopa i hihat są z PROSTO. Pliki zapisał firmware v0.7 w emulatorze (ten sam
renderer, którego używało laboratorium). Każdy plik jest, poza nazwą, bajt w bajt taki jak kit
zrobiony sposobem laboratorium: PROSTO jako kit 0, edycje przepisu z poziomami opisanymi niżej,
`save-kit`.

| Kit | Drum 2 | Drum 3 | Cymbal | FX | Demo A z przepisu |
|---|---|---|---|---|---|
| `ECHO` (52) | ghost kick A2 | figura PROSTO | ryk B1 | pogłos B1 | B1 |
| `FALA` (53) | ghost kick A2 | sub boom B4 | riser B2 | pogłos B2 (jak w B6) | B6 |
| `RYK` (54) | ghost kick A2 | figura PROSTO | ryk B7: tylko echa i pogłos | delay 1/16, 100 % wet, i pogłos B7 | B7 |
| `TLO` (55) | bed A9 | downlifter B3 | jąkanie B5 | pogłos A9 (rumble) | A9 |

- **Drum 1 (stopa):** z PROSTO (sinus G#1 +0,3 półtonu, spadek wysokości od ok. 5,2×, bez kliku),
  `vol1` 86 (w `TLO` 35). W `TLO` także do FX (`out` `FX`, jak w A9): karmi pogłos, który sama
  przycisza (rumble).
- **Drum 2, ghost kick (A2; `ECHO`, `FALA`, `RYK`):** miękkie echo stopy: sinus G1 (1,3 półtonu niżej
  od stopy), mniejszy i krótszy spadek wysokości (`modamnt2` 34, `mod_eg2` 44), krótsze ciało
  (`velod2` 10), LP 36 (bez kliku), `drive2` 18. Na krokach 4, 7, 12, 15: akcent (G, 127) na 7 i 15,
  ciche (g) ok. −8 dB na 4 i 12; generator `sub` sty 1 stawia to samo.
- **Drum 2, bed (A9; `TLO`):** krótki sinus F#1 (46 Hz) przez LP 16, `drive2` 8, na 4. szesnastce każdej
  ćwierćnuty (kroki 4, 8, 12, 16), tuż przed stopą; razem z rumble dół nie milknie.
- **Drum 3, figura (`ECHO`, `RYK`):** figura PROSTO (sinus B3 z FM, LFO 3 na `fmamnt3`), w proporcji
  PROSTO do stopy (`vol3` 29 = 34 × 0,86); w demie B gra ją generator `prc`.
- **Drum 3, sub boom (B4; `FALA`):** sinus opadający od ok. 245 Hz do E1, LP, długi; na pierwszej
  stopie taktów 1 i 9, do FX.
- **Drum 3, downlifter (B3; `TLO`):** szum, którego barwa opada od jasnej do ciemnej przez ok. takt;
  takty 1 i 9, do FX.
- **Snare (wszystkie cztery):** C2 z zestawu C: tyk szumu jak shaker (sam szum przez pasmo ok. 5 kHz,
  miękkie wejście 16 ms, 70 ms), na sucho, bez slotu FX. **Zestaw C (perkusja zamiast naiwnego rimu)
  czeka jeszcze na ocenę;** C2 jest tylko zaślepką do wymiany, gdy wybór zapadnie.
- **Cymbal, ryk (B1; `ECHO`):** szum przez wąskie pasmo ok. 500 Hz z mocnym rezonansem, wolne
  narastanie, drżenie 40 Hz (LFO 5 na `vol5`), daleko w pogłosie (`pre` 110); takty 1 i 9.
- **Cymbal, ryk tylko w pogłosie (B7; `RYK`):** ten sam ryk bez dźwięku bezpośredniego: slot to ciemny
  delay 1/16 (105 ms przy 143 BPM) na 100 % wet, a pogłos bierze jego echa.
- **Cymbal, riser (B2; `FALA`):** szum przez LP, który otwiera się przez ok. 3/4 taktu (LFO 5, piła
  w górę na `filter_freq_5`), głośność rośnie; takty 8 i 16, wpada w boom taktów 9 i 1.
- **Cymbal, jąkanie (B5; `TLO`):** bardzo krótki szum; generator `rar` sty 1 den 4 gra go jako uderzenie
  na 1 i cztery flamy po 4 na krokach 9–12 taktu 8 i 16.
- **Hihat:** z PROSTO (szum przez HP 99, losowa panorama na każdej 16-tce); z generatorem `hat` (sty 1
  den 7 lev −15) to warstwa ghostów z laboratorium.
- **FX:** jak w tabeli; pogłos zawsze przyciszany stopą (`dck` `dr1`, `ddp` 18, w `TLO` 44: niżej).
  W `TLO` downlifter i jąkanie grają w pogłosie A9, nie w swoim: predelay, wybrzmienie i barwa B3
  i B5 przesunęłyby bed, więc ich `vol` jest zmierzony od nowa, ok. 17 dB pod groove `TLO`.

**Poziomy.** Kity trzymają proporcję stopy do hihatu z PROSTO (hihat `vol6` 127 jak w PROSTO, w `TLO` 52), a resztę
głosów względem stopy tak jak przepisy. Laboratorium ściszyło swoją stopę (suwak 0,35, wzmocnienie =
suwak²) tylko po to, żeby było słychać resztę; ghost kick (suwak 0,4), bed A9 (0,6) i zdarzenia
(0,09–0,24) są ustawione względem tej cichej stopy. Kit suwaków nie zapisuje. Suwak i `vol` to to
samo wzmocnienie (za przesterem bębnów, przed przesterem talerza), więc `vol` kitu to `vol` przepisu
× suwak² / 0,35² × (`vol1` / 100), sprawdzony pomiarem. Stopa jest tak wysoko, jak pozwala Drum 2 przy
`vol2` 127: w `ECHO`, `FALA` i `RYK` ghost kick potrzebuje ok. 1,5 raza `vol` stopy, więc `vol1` 86
(PROSTO 100, 1,3 dB głośniej); w `TLO` nuta bedu A9 potrzebuje 3,6 raza, więc `vol1` 35, a hihat
i werbel, które PROSTO ustawia względem stopy, schodzą razem z nią: `vol6` 52 (127 × 35 / 86) i `vol4`
51. Hihat jest wtedy pod stopą tak samo jak w `ECHO`: 42,1 dB RMS wobec 42,0 dB (oba na
sucho, zmierzone). Szczyt na wyjściu 1: od −3,6 do −6,6 dBFS (PROSTO −2,7); −3,6 to dema
`demo/echo.wav`, `fala.wav` i `ryk.wav` z tykiem werbla na 2 i 4.

Dwa ustawienia idą za poziomem, a nie za `vol`. **Przyciszanie pogłosu** zależy od szczytu stopy
(głębokość × dwa razy szczyt, najwyżej całość): głośniejsza stopa przyciszałaby pogłos 7 razy
mocniej niż w przepisie (w `TLO` ok. 3 razy), więc `ddp` 18 (127 × 0,35² × 100 / 86; w `TLO` 44)
zostawia przyciszanie przepisu. Przy 127 pogłos chowałby się pod każdą stopą do zera, a ryk `ECHO`
byłby o 0,9 dB cichszy i pompowałby razem ze stopą (zmierzone). **Przester talerza** za `vol` też
zależy od poziomu, który dostaje: w przepisach suwak przed nim był tak nisko, że przester był tylko
wzmocnieniem 1 + k z najwyżej ok. 5 % ściśnięcia. Przy nowym poziomie ten sam `cymbal_distortion`
ściskałby 7 razy mocniej (w `TLO` 3 razy), więc w kitach jest niższy, tak by ściśnięcie i poziom były
jak w przepisie (k × poziom na wejściu jak w przepisie). Wycofane: `drive3` boomu w `FALA` wrócił do
40 z B4 (przester bębna jest przed `vol`, więc `vol3` 12 niesie cały poziom), a głębokość LFO ryku
(`amount_lfo5`) ma znowu proporcję B1 do `vol5` (0,69). Głośność ryku najlepiej zmieniać suwakiem:
sam `vol5` zmienia też proporcję drżenia (LFO dodaje do `vol` stałą głębokość).

| Kit | `vol1` | `vol2` | `vol3` | `vol4` | `vol5` | `vol6` | Poza `vol` (w przepisie) |
|---|---|---|---|---|---|---|---|
| `ECHO` | 86 | 127 | 29 | 124 | 31 | 127 | `amount_lfo5` 21 (44), `cymbal_distortion` 9 (100), `ddp` 18 (127) |
| `FALA` | 86 | 127 | 12 | 124 | 42 | 127 | `cymbal_distortion` 5 (40), `ddp` 18 (127) |
| `RYK` | 86 | 127 | 29 | 124 | 26 | 127 | `amount_lfo5` 18 (44), `cymbal_distortion` 9 (100), `ddp` 18 (127) |
| `TLO` | 35 | 127 | 14 | 51 | 38 | 52 | `cymbal_distortion` 11 (60), `ddp` 44 (127) |

Werbel (C2) zostaje przy `vol4` 124: tyle miał w C2 względem hihatu, obaj na suwaku 1; w `TLO`, gdzie
hihat ma 52, werbel ma 51 (124 × 52 / 127).

**Wynik** (emulator v0.7, 143 BPM, wyjście 1, wszystkie suwaki na 1). Demo A: przepis, z którego kit
powstał, z jego ścieżkami, zdarzeniami i długością, zagrany z pliku kitu, obok ten sam przepis
sposobem laboratorium; każdy głos sam na wyjściu 1, z FX (reszta na wyjściu 2, więc stopa dalej
przycisza pogłos), porównany **względem stopy**: przepis grał stopę ciszej, więc bezwzględne poziomy
muszą się różnić. Hihat jest tu z założenia dalej pod stopą niż w przepisie (proporcja PROSTO). Demo
B: 16 taktów z generatora: stopa 4/4, `hat`, `sub` sty 1 (w `TLO` bed A9 z ręki), na ścieżce 3 `prc`
(`ECHO`, `RYK`) albo `rar` sty 2 (`FALA`, `TLO`), na ścieżce 5 `rar` umieszczony jak w przepisie,
z jego zamrożeniem. G to akcent ghost kicka względem szczytu stopy w paśmie 30–150 Hz (cel 4–8 dB);
zdarzenie to jego najgłośniejsza ćwierćnuta (sucho + pogłos) względem RMS groove bez zdarzeń.
Wyzwolenia zgadzają się z zapisanymi krokami we wszystkich demach.

| Kit | Demo A: głos najdalej od przepisu (względem stopy) | Hihat względem przepisu | G pod stopą, A / B (przepis) | Zdarzenia pod groove, A (przepis) | Zdarzenia pod groove, B | Szczyt A / B |
|---|---|---|---|---|---|---|
| `ECHO` | ryk −0,09 dB | −16,8 dB | 4,9 / 4,9 dB (5,0) | 17,4, 17,1 dB (17,0, 17,1) | 17,5, 17,2 dB | −5,0 / −4,8 dBFS |
| `FALA` | riser +0,19 dB | −17,0 dB | 4,9 / 4,9 dB (5,0) | 17,0, 17,2, 16,7, 16,8 dB (17,1, 17,3, 16,9, 17,0) | te same | −5,0 / −5,0 dBFS |
| `RYK` | ryk +0,09 dB | −16,8 dB | 4,9 / 4,9 dB (5,0) | 17,3, 17,0 dB (17,4, 17,0) | 17,3, 17,1 dB | −5,1 / −4,8 dBFS |
| `TLO` | bed +0,02 dB | −16,7 dB | bez ghost kicka; bed 8,2 dB pod stopą (8,2) | A9 nie ma zdarzeń | 16,9, 17,1, 18,0, 17,1 dB | −6,6 / −5,5 dBFS |

W `TLO` downlifter taktu 9 wchodzi w pogłos zamrożony przez jąkanie (B5) i dlatego jest 18,0 dB pod
groove.

**Zamrożenie ręcznie.** B1, B5 i B7 zamrażają ogon pogłosu (`frz`) zdarzeniem renderu. Kit go nie
zapisuje (zapisany kit ma zawsze `frz` wyłączone), krok nie może go zablokować (p-lock), a LFO
i velocity nie mają go za cel. Na LXR zamrożenie włącza się ręcznie: SHIFT + krok 3 w trybie
performance albo `frz` na podstronie pogłosu (strona FX, SHIFT + krok 15). `ECHO` i `RYK`: na pierwszym
takcie po ryku, wyłączyć dwa takty później (w demach takty 2–3 i 10–11). `TLO`: na ostatniej
ćwierćnucie taktu jąkania, wyłączyć w połowie następnego taktu. Zamrożony pogłos trzyma też rumble:
przez te trzy ćwierćnuty nie bierze nowych stóp (zamrożony pogłos nie przyjmuje sygnału).

**Firmware.** Te kity potrzebują LICHOMAT v0.4 lub nowszego, v0.7 nie jest konieczne. Pogłos, jego
przyciszanie (rumble: `dck`, `ddp`), zamrożenie i delay zsynchronizowany z tempem (`syn`) weszły
w v0.4, a od v0.4 do v0.7 nie doszedł żaden parametr kitu: v0.5–v0.7 dodały tylko ustawienia
globalne, nagrywania, ścieżki i kroku (`trg`, `rqn`, `tpl`, `amt`, `vtp`, `vdp`, `mic`), a plik kitu ma
wciąż 268 bajtów. Zbudowane i zmierzone są na v0.7; na v0.4 nie były mierzone (v0.5 zaczyna głos co do
próbki, v0.4 co blok 1,45 ms). Wzorce dem z generatora (`sub`, `hat`, `prc`, `rar`) to prototyp v0.8;
kity ich nie potrzebują, kroki można wpisać ręcznie. Pogłos działa na razie z połową częstotliwości
próbkowania (H12): po zmianie trzeba te kity zmierzyć jeszcze raz, jak `HUSTAWKA`.

**Demo.** `demo/echo.wav`, `fala.wav` i `ryk.wav` grają wzorzec `ghost` (`GHOST_PATTERN` w
`tools/kit_build.py`, 8 taktów, 143 BPM): stopa 4/4, ghost kicki na 4, 7, 12, 15 (G 127 na 7 i 15,
g 52 na 4 i 12), tyk werbla na 2 i 4 i warstwa hihatu z generatora `hat` (takt 1, krok 8 `2:2`);
`demo/tlo.wav` gra wzorzec `bed`: to samo z bedem na 4, 8, 12, 16. Bez Drum 3 i Cymbala: zdarzenia są
rzadkie, a jednotaktowa pętla grałaby je w każdym takcie. Zdarzenia, figura i zamrożenie są
w demach A i B, nagranych do odsłuchu jako MP3 poza repozytorium.

## Instalacja

1. Zrób kopię całej karty SD na komputerze.
2. Wybierz folder projektu na karcie, `PROJ00`–`PROJ63`. Usuń z niego (albo przenieś) pliki, których
   nazwa zaczyna się od `52-` do `63-` i kończy na `.SND`, np. `52-INITK.SND` czy `60-INITK.SND`
   (albo tylko te numery, których kity mają trafić na kartę, np. `52-`–`55-`). Firmware
   szuka kitu po numerze: wczytuje pierwszy plik `NN-*.SND`, więc dwa pliki z tym samym numerem to
   loteria.
3. Skopiuj tam z `kits/card/PROJ00/` kity `52-ECHO.SND`, `53-FALA.SND`, `54-RYK.SND`, `55-TLO.SND`,
   `56-PULS.SND`, `57-HUSTA.SND`, `58-PROST.SND`, `59-PRZES.SND`, `60-WIR.SND`, `61-MGLA.SND`,
   `62-RDZA.SND` i `63-IGLA.SND`. Pliki 56–63 są te same co wcześniej, bajt w bajt: jeśli już są na
   karcie, wystarczą cztery nowe, 52–55.
4. Włóż kartę, włącz LXR i wczytaj ten projekt (projekt wczytuje kity z plików). Kity są w slotach
   52–63: LOAD, typ kit, numer.
5. Dla kitów 56–59 ustaw na wzorcu `Shuffle`: `PULS` 18, `HUSTAWKA` 45, `PROSTO` 0, `PRZESTER` 24,
   i zapisz wzorzec; tempo 142–143 BPM. Shuffle należy do wzorca, nie do kitu. Kity 52–55: `Shuffle`
   0, tempo 143 BPM, zamrożenie pogłosu ręcznie (sekcja „Kity z laboratorium brzmień”).

**Nazwy plików:** `NN-NAZWA.SND`. `NN` to slot 00–63, `NAZWA` to pierwsze 5 znaków nazwy kitu (całą,
do 8 znaków, firmware trzyma w pierwszych 8 bajtach pliku i pokazuje na ekranie). Żeby dać kit w inny
slot, zmień tylko numer, np. `60-WIR.SND` → `05-WIR.SND` (i usuń stary plik `05-*.SND`).

**Format:** każdy plik ma 268 bajtów: 8 bajtów nazwy, 247 bajtów parametrów i 13-bajtowy blok v0.4
(`LCHM`, wersja, długość, potem `siz ton pre mod dck ddp syn`).

**Firmware 1.7 i LICHOMAT v0.1–v0.3** czytają z pliku 255 bajtów i pomijają blok v0.4. Kit gra wtedy
bez pogłosu, bez rumble i bez filtra DJ. Delay MGLA i IGLA gra tam swój wolny czas (`tim` 58 i 59,
`rng` 127: ok. 349 ms i 176 ms, czyli `8d` i `16d` przy 128 BPM), bo `syn` tam nie ma. Głosy wysłane
do FX grają przez slot jak zwykle: stopa RDZA ze slotem `off` gra na sucho. MGLA i RDZA tracą przez
to najwięcej. Jeśli taki firmware zapisze kit, blok znika (268 → 255 B), a v0.4 wczyta go potem
z wyłączonym pogłosem.

`HUSTAWKA` bez bloku v0.4 traci pogłos, a z nim dół, który gra dalej bez stopy. Pozostałe kity 56–59
nie używają pogłosu. Kity 52–55 tracą bez bloku pogłos, a z nim odległość zdarzeń (ryk, riser, boom,
downlifter i jąkanie grają wtedy blisko i na sucho), `TLO` także rumble; delay `RYK` gra tam swój
wolny czas zamiast 1/16. Warunki kroków `2:2` i `4:4` we wzorcu demo `classic` wymagają v0.2+ (1.7 gra je w każdym takcie); same kity
ich nie mają.

## Przebudowa

Potrzebny jest renderer emulatora:

    source tools/env.sh
    cmake -S emu -B build/emu -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/emu --target lichomat-render

Potem:

    python3 tools/kit_build.py            # wszystkie kits/*.txt
    python3 tools/kit_build.py mgla       # jeden kit

Dla każdego kitu skrypt:

1. robi kartę `--seed-card` i wpisuje nazwę kitu w 8 bajtów nazwy pliku slotu (pusty slot ma tam
   zera), skąd firmware czyta ją przy starcie jak z każdej karty;
2. odpala render ze skryptem: linia `param` na każdy parametr, `save-kit` w slot, `save-project 0`,
   `--keep-card`; plik `NN-NAZWA.SND`, który zapisał firmware, kopiuje do `kits/card/PROJ00/`;
3. sprawdza odczyt: karta z wzorcem demo i nowym kitem jako kit 0; firmware go wczytuje i zapisuje
   projekt ponownie, a plik musi wyjść bajt w bajt taki sam; każda wartość ze specyfikacji musi
   stać na swoim miejscu w pliku (numery parametrów z podsumowania `--param`);
4. renderuje demo do `kits/demo/NAZWA.wav` i wypisuje pomiary (`--no-demo` to pomija).

Build jest powtarzalny: dwa przebiegi dają te same pliki `.SND` i WAV, bajt w bajt. Nowy kit to nowy
plik `kits/NAZWA.txt` z `name`, `slot` i parametrami; nazwa nie może zawierać nazw producenta ani
urządzenia. Trzy klucze dotyczą tylko dema, nie kitu: `bpm` (tempo, domyślnie 128), `shuffle`
(`PAR_SHUFFLE` 0–127, przez `--param`; domyślnie nieustawiony) i `pattern` (`classic`, domyślny,
`album`, `ghost` albo `bed`).

Kity 52–55 zbudował renderer z gałęzi v0.7 (zbudowany tak samo, w jej katalogu), ten sam, którego
używało laboratorium brzmień:

    python3 tools/kit_build.py --renderer ŚCIEŻKA/DO/v0.7/build/emu/lichomat-render echo fala ryk tlo

Renderer v0.7 nie przyjmuje `voice_lfoN` i `target_lfoN` przez `--param` (renderer tej gałęzi je
przyjmuje). Z takim rendererem skrypt wpisuje je w bajty kitu 0 na karcie przed startem (bajt 8 +
numer parametru), tak jak ustawia je wczytanie kitu, a sprawdzenie odczytu znajduje je w pliku na
tym samym miejscu.

## Pliki

- `wir.txt`, `mgla.txt`, `rdza.txt`, `igla.txt`, `puls.txt`, `hustawka.txt`, `prosto.txt`,
  `przester.txt`, `echo.txt`, `fala.txt`, `ryk.txt`, `tlo.txt`: ustawienia kitów z komentarzami.
- `card/PROJ00/52-ECHO.SND`, `53-FALA.SND`, `54-RYK.SND`, `55-TLO.SND`, `56-PULS.SND`,
  `57-HUSTA.SND`, `58-PROST.SND`, `59-PRZES.SND`, `60-WIR.SND`, `61-MGLA.SND`, `62-RDZA.SND`,
  `63-IGLA.SND`: kity na kartę.
- `demo/wir.wav`, `mgla.wav`, `rdza.wav`, `igla.wav`: dema, 8 taktów, 15 s, 2,6 MB każde;
  `demo/puls.wav`, `hustawka.wav`, `prosto.wav`, `przester.wav`: 8 taktów, 13,4–13,5 s, 2,4 MB;
  `demo/echo.wav`, `fala.wav`, `ryk.wav`, `tlo.wav`: 8 taktów, 13,4 s, 2,4 MB.
- `../tools/kit_build.py`: budowanie, sprawdzenie odczytu, dema, pomiary.
