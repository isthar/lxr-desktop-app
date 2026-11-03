# LICHOMAT 0.6: przepisy groove, czyli który szablon na którą ścieżkę

Przepisy dla szablonów groove LICHOMAT 0.6 (specyfikacja
`docs/superpowers/specs/2026-11-02-v0.6-microtiming-groove.md` §4.2, plan
`docs/superpowers/plans/2026-11-02-v0.6-microtiming-groove.md`), do grania na LXR groove techno w stylu
albumu referencyjnego. Nic tu nie jest jeszcze sprawdzone na urządzeniu: pierwszym sprawdzeniem jest
U34 z `docs/lichomat/hardware-checklist.md`, a jego wyniki (które szablony i ile `amt` pasują do
Twoich utworów) poprawią ten plik. Liczby w ms są przy 142 BPM, tempie pomiaru; tick to tam 1,10 ms.

## Gdzie są ustawienia

- **`tpl amt`:** wybierz głos, SHIFT + drugi przycisk trybu (strona Euklid), enkoder w prawo za `dly`:
  trzeci ekran strony ścieżki, `tpl` na gałce 1, `amt` na gałce 2. Należą do wybranego głosu w
  pokazanym wzorcu i zapisują się z wzorcem; przed zmianą wzorca zapisz wzorzec.
- **Szablony:** `off swg lay lyb rlx 2bt psh drg`; tabela przesunięć jest w notatkach wydania
  (`docs/lichomat/release-notes-v0.6.md`, „Szablony groove”). Każdy szablon przesuwa 16 szesnastek
  taktu, a ćwierćnuty (kroki 1, 5, 9, 13) zostawia na siatce.
- **`amt`:** 0–200 %, domyślnie 100.
- **Shuffle na 0.** Szablon i shuffle się sumują, a shuffle działa na cały wzorzec, także na stopę.
  Groove robi się szablonami na ścieżkach, które mają się ruszać.

## Podstawa

| Głos | Rola | `tpl` | `amt` | Dlaczego |
|---|---|---|---|---|
| Drum 1 | stopa | `off` | | Stopa to siatka. Na ćwierćnutach i tak żaden szablon jej nie rusza, ale przednutki albo stopa na offbeacie by się przesunęły |
| Hihat zamknięty (ścieżka 6) | szesnastki | `lay` | 100 | „e” każdej ćwierćnuty 18 ticków później (19,8 ms), „a” 5 ticków (5,5 ms), offbeat na siatce: tak siedzą hihaty w utworze 5 albumu referencyjnego |
| Hihat otwarty (ścieżka 7) | offbeaty | `lay` | 100 | Na offbeatach `lay` ma 0, więc otwarty zostaje prosto. Ten sam szablon co zamknięty, bo oba dzielą jeden głos (patrz „Na co uważać”) |
| Drum 2, Drum 3 | perkusja | `rlx` | 80 | Luźny takt z utworu 7, przy 80 % trochę spokojniejszy od hihatów, żeby perkusja nie ciągnęła mocniej niż one |
| Snare | clap na 2 i 4 | `off` | | Na krokach 5 i 13 każdy szablon ma 0. Ciche przednutki można dać na `lay`, jeśli mają iść z hihatami |
| Cymbal | ride na offbeatach | `off` | | Offbeat jest w `lay` na siatce. Do spróbowania `drg` albo `psh` (niżej) |

`rlx` na 80 % przy 142 BPM (ticki, w nawiasie ms):

| Krok | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| ticki | 0 | 12 | 1 | 2 | 0 | 13 | 2 | 18 | 0 | 11 | 2 | 13 | 0 | 7 | 2 | 14 |
| ms | 0 | 13,2 | 1,1 | 2,2 | 0 | 14,3 | 2,2 | 19,8 | 0 | 12,1 | 2,2 | 14,3 | 0 | 7,7 | 2,2 | 15,4 |

## Gdy `lay` brzmi za lekko: `amt` 110–130 %

| `amt` | „e” (kroki 2, 6, 10, 14) | „a” (kroki 4, 8, 12, 16) |
|---|---|---|
| 50 % | 9 ticków, 9,9 ms | 3 ticki, 3,3 ms |
| 100 % | 18 ticków, 19,8 ms | 5 ticków, 5,5 ms |
| 110 % | 20 ticków, 22,0 ms | 6 ticków, 6,6 ms |
| 120 % | 22 ticki, 24,2 ms | 6 ticków, 6,6 ms |
| 130 % | 23 ticki, 25,3 ms | 7 ticków, 7,7 ms |
| 150 % | 27 ticków, 29,7 ms | 8 ticków, 8,8 ms |

**Zastrzeżenie (specyfikacja §4.2).** Szablony pochodzą z uśrednionych obwiedni całych utworów, a
uśrednianie rozmywa słabsze kroki. Mediany z pojedynczych taktów są w raporcie pomiaru większe na
kilku pozycjach „e” (16–28 ms w utworze 5), a na większości pozycji „a” są oznaczone jako niepewne.
`lay` to średnia: jego „e” to cztery pozycje utworu 5 (14,6, 23,9, 16,8 i 16,3 ticku, średnio 17,9 →
18), „a” trzy (11,3, 1,4 i 3,0, średnio 5,2 → 5; krok 8 z −12,5 ticku pominięty, bo zasłania go
następne uderzenie). Jeśli `lay` brzmi za lekko, poprawką jest `amt` 110–130 %: 130 % daje „e” na
25,3 ms, w górnej części tych median. Napisz, co wybierzesz (U34); tabela szablonów może się potem
zmienić bez zmiany formatu plików.

## Inne szablony do spróbowania

- **`swg` z `amt` zamiast shuffle:** co druga szesnastka później, ale tylko na wybranych ścieżkach.
  Mediana swingu albumu referencyjnego, 10 ms przy 142 BPM, to `swg` na 56 % (9 ticków). Utwory z
  8–13 ms to 45–75 %, a te z 23–26 ms (5 i 7, mediany taktów) to 135–150 %. 200 % to triola.
- **`lyb`:** to samo co `lay`, ale każda pozycja całego taktu osobno (z utworu 5: np. „e” drugiej
  ćwierćnuty 24 ticki, ostatnie „a” 3). Bardziej „zagrane”, mniej regularne.
- **`2bt`:** bity 1 i 3 swingowane, 2 i 4 prawie prosto, offbeaty +2–3 ticki (utwór 6). Groove, który
  oddycha co pół taktu.
- **`drg`:** offbeatowe ósemki 6 ticków później (6,6 ms): ride albo otwarty hihat, który się spóźnia.
- **`psh`:** offbeatowe ósemki 4 ticki wcześniej (−4,4 ms): hat, który ciągnie do przodu.

## Na co uważać

- **Wspólny hihat.** Ścieżki 6 i 7 grają jednym głosem: wygrywa ta, która uderza później, a zamknięty
  przesunięty za otwarty z tego samego kroku go ucina. Daj obu ten sam szablon.
- **Tempo.** Szablony są w tickach, więc idą z tempem: przy 130 BPM tick to 1,20 ms i `lay` na 100 %
  daje 21,6 ms i 6,0 ms; przy 120 BPM ten sam szablon jest w ms o 18 % dłuższy niż przy 142.
- **Własne tempo ścieżki.** Szablon liczy się w krokach ścieżki: na ×2 jego 16 pozycji wypada na pół
  taktu mastera.
- **Ścieżki o innej długości.** Pozycja w szablonie to liczba kroków ścieżki od startu modulo 16, więc
  ścieżka z 12 albo 7 krokami dalej dostaje groove według taktu. We wzorcu o nieparzystej długości
  szablon zaczyna od nowa ze wzorcem, a swing przełącza się co pętlę: tam mogą się rozjechać.
- **Rolle** nie biorą szablonu: trzymany roll gra na prostej siatce ze swingiem.
- **Nagrywanie z szablonem.** Z `rqn` `off` nagrany `mic` to odchyłka od groove, nie od siatki: nuta
  zagrana w `lay` zapisuje 0. Zmiana szablonu po nagraniu przesuwa całe nagranie razem z nim.
- **`mic` na kroku dodaje się do szablonu**, a suma kończy się na 7/8 kroku. Pojedynczy krok możesz
  więc dalej przesunąć ręcznie ponad szablon.

## Czego posłuchać

Rendery z emulatora, nie nagrania z LXR: `/Users/tk/Code/lxr/build/wt-v06/build/demo/` (poza gitem, na
Twoim Macu), opis w `README-v0.6.txt` tamże (`tools/demo_v06.sh` z gałęzi `v0.6`). Karta init przy
142 BPM, 8 taktów: zamknięty hihat (głos 6) na wszystkich szesnastkach z danym szablonem na 100 %,
reszta karty prosto.

| Szablon | Plik |
|---|---|
| `off` (odniesienie, hihat na siatce) | `v06-hats-off.wav` |
| `swg` | `v06-hats-swg.wav` |
| `lay` | `v06-hats-lay.wav`; na 50 i 150 %: `v06-hats-lay50.wav`, `v06-hats-lay150.wav` |
| `lyb` | `v06-hats-lyb.wav` |
| `rlx` | `v06-hats-rlx.wav` (na 100 %; 80 % to ok. 1/5 mniej) |
| `2bt` | `v06-hats-2bt.wav` |
| `psh` | `v06-hats-psh.wav` |
| `drg` | `v06-hats-drg.wav` |

Nie ma renderu `amt` 110–130 %: leży między `v06-hats-lay.wav` a `v06-hats-lay150.wav`. Nie ma też
renderu całego przepisu z „Podstawy”; najprościej złożyć go na LXR. `v06-record-take.wav` to
nagrywanie z mikrotimingiem (karta `groove`, klik, „e” +12,1 ms, „a” −5,5 ms), nie szablon.
