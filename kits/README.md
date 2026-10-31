<!--
Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
-->

# Zestawy brzmień LICHOMAT

Cztery własne kity dla LICHOMAT v0.4 i nowszych, każdy pod inny styl: hipnotyczne groove techno,
dub techno, surowe/industrialne i minimal z FM. Pliki kitów zapisał sam firmware: emulator
(`lichomat-render`) ustawił parametry przez ścieżkę edycji menu i zapisał kit i projekt, więc format,
łącznie z 13-bajtowym blokiem v0.4, jest taki, jak na urządzeniu. Nic tu nie pochodzi z kitów
fabrycznych ani z DLC. Demo to rendery emulatora, nie nagrania z LXR; poziomy są zmierzone z plików
WAV, bez odsłuchu.

| Plik na karcie | Slot | Nazwa | Styl | Demo |
|---|---|---|---|---|
| `card/PROJ00/60-WIR.SND` | 60 | `WIR` | hipnotyczne groove techno | `demo/wir.wav` |
| `card/PROJ00/61-MGLA.SND` | 61 | `MGLA` | dub techno | `demo/mgla.wav` |
| `card/PROJ00/62-RDZA.SND` | 62 | `RDZA` | surowe / industrialne | `demo/rdza.wav` |
| `card/PROJ00/63-IGLA.SND` | 63 | `IGLA` | minimal / FM | `demo/igla.wav` |

Ustawienia każdego kitu, z komentarzami, są w `wir.txt`, `mgla.txt`, `rdza.txt` i `igla.txt`: z nich
`tools/kit_build.py` buduje pliki. Nazwy parametrów to nazwy z `emu/render/render_params.h`, wartości
takie, jakie trzyma menu (listy liczone od 0).

## Demo

Każde demo to ten sam wzorzec (16 kroków, 8 taktów, 128 BPM, `trg smp`, wyjście 1 stereo):

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

## Pomiary

Z plików demo (`tools/kit_build.py`, zwykły Python): szczyt i RMS całego dema (oba kanały), podstawowa
stopy z przejść przez zero między 40 a 160 ms po uderzeniu i wybrzmienie każdego głosu do −40 dB
od szczytu (głos sam, bez slotu i pogłosu; „>” to do następnego uderzenia tego głosu).

| Zestaw | Szczyt | RMS | Stopa | Drum 1 | Drum 2 | Drum 3 | Snare | Cymbal | Hihat zamkn. / otw. |
|---|---|---|---|---|---|---|---|---|---|
| WIR | −1,4 dBFS | −11,4 dBFS | 49,6 Hz | 450 ms | 98 ms | 102 ms | 174 ms | 90 ms | 62 / >348 ms |
| MGLA | −1,3 dBFS | −11,9 dBFS | 49,0 Hz | >458 ms | 44 ms | 372 ms | 218 ms | 64 ms | 34 / >344 ms |
| RDZA | −1,3 dBFS | −9,8 dBFS | 54,6 Hz | 450 ms | 40 ms | 98 ms | 198 ms | 12 ms | 74 / >348 ms |
| IGLA | −1,6 dBFS | −12,6 dBFS | 55,0 Hz | 272 ms | 72 ms | 56 ms | 92 ms | 16 ms | 32 / 132 ms |

Żadne demo nie przekracza −1 dBFS. RMS jest w granicach ±3 dB od poziomu odniesienia: mediana RMS
16 wzorców Twojej karty DLC w emulatorze to ok. −9,7 dBFS (tylko pomiar głośności; nic z tej karty
nie jest tu użyte). Wzorce DLC dochodzą do 0 dBFS, te kity mają ok. 1,3 dB zapasu.

## Instalacja

1. Zrób kopię całej karty SD na komputerze.
2. Wybierz folder projektu na karcie, `PROJ00`–`PROJ63`. Usuń z niego (albo przenieś) pliki, których
   nazwa zaczyna się od `60-`, `61-`, `62-` i `63-` i kończy na `.SND`, np. `60-INITK.SND`. Firmware
   szuka kitu po numerze: wczytuje pierwszy plik `NN-*.SND`, więc dwa pliki z tym samym numerem to
   loteria.
3. Skopiuj tam `60-WIR.SND`, `61-MGLA.SND`, `62-RDZA.SND` i `63-IGLA.SND` z `kits/card/PROJ00/`.
4. Włóż kartę, włącz LXR i wczytaj ten projekt (projekt wczytuje kity z plików). Kity są w slotach
   60–63: LOAD, typ kit, numer.

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

Warunki kroków `2:2` i `4:4` we wzorcu demo wymagają v0.2+ (1.7 gra je w każdym takcie); same kity
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
urządzenia.

## Pliki

- `wir.txt`, `mgla.txt`, `rdza.txt`, `igla.txt`: ustawienia kitów z komentarzami.
- `card/PROJ00/60-WIR.SND`, `61-MGLA.SND`, `62-RDZA.SND`, `63-IGLA.SND`: kity na kartę.
- `demo/wir.wav`, `mgla.wav`, `rdza.wav`, `igla.wav`: dema, 8 taktów, 15 s, 2,6 MB każde.
- `../tools/kit_build.py`: budowanie, sprawdzenie odczytu, dema, pomiary.
