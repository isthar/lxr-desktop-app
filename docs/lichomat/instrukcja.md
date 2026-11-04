# LICHOMAT 0.6: instrukcja

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02, zbudowany na fw 1.7. Nie jest robiony, zatwierdzony
ani wspierany przez Erica Synths. Dodaje nowy rdzeń timingu z poprawkami MIDI i zegara, groove na poziomie
ścieżki, narzędzia do grania na żywo, pogłos z freeze i rumble, delay z tempem, filtr DJ, timing co do próbki
i mikrotiming kroku. Wersja 0.6 zawiera wszystko z 0.1–0.5, więc wystarczy jedna instalacja. Powrót do 1.7:
usuń z karty `LXR_V2_LICHOMAT_0.6.img` i zainstaluj `LXR_V2_V1.7.img` tak samo (updater się nie zmienia);
szczegóły w [notatkach 0.6](release-notes-v0.6.md), „Powrót do v0.5 … albo do oficjalnego firmware”. Opis
pochodzi z notatek do wydania i z emulatora; na LXR sprawdza go [sesja testowa](test-session-v0.6.md).

## Gdzie co jest

„Strona ścieżki” to strona Euklid: wybierz głos, przytrzymaj SHIFT i naciśnij drugi przycisk trybu. Enkoder w
prawo przechodzi na kolejne ekrany. „Tryb wykonawczy” to drugi przycisk trybu bez SHIFT. „Strona kroku”:
pierwszy przycisk trybu, potem krok.

| Funkcja | Gdzie | Na LCD | Od |
|---|---|---|---|
| Menu globalne, podstrona 1 | SHIFT + krok 12 | `bpm shu ch ssv` | 1.7 |
| Menu globalne, drugi ekran | SHIFT + krok 12 drugi raz | `mtx mrx mrt flw` | 1.7 |
| Menu globalne, podstrona 2 | SHIFT + krok 12 trzeci raz | `kcm cki cko trg` | 1.7 (`trg` 0.5) |
| Strona CPU | SHIFT + krok 12 czwarty raz | `CPU ..% pk ..%` / `Qmax ... ovf ...` | 0.1 |
| Shuffle (cały wzorzec) | menu globalne, podstrona 1; też tryb wykonawczy, drugi ekran | `shu` | 0.1 (nowy rdzeń) |
| Jacki zegara: PPQ in i out | menu globalne, podstrona 2 | `cki cko` | 1.7 (poprawki 0.1) |
| Timing wyzwalania | menu globalne, podstrona 2, gałka 4 | `trg` (`smp`, `blk`) | 0.5 |
| Euklid | strona ścieżki, ekran 1 | `len stp rot mrp` | 1.7 |
| Tempo ścieżki (polirytmia) | strona ścieżki, ekran 2 | `tmp` | 0.2 |
| Humanize czasu i velocity | strona ścieżki, ekran 2 | `hmt hmv` | 0.2 |
| Opóźnienie ścieżki | strona ścieżki, ekran 2 | `dly` | 0.2 |
| Szablony groove | strona ścieżki, ekran 3 (enkoder za `dly`) | `tpl amt` | 0.6 |
| Rotacja ścieżki | SHIFT + krok 1 (wcześniej), SHIFT + krok 2 (później) | – | 0.2 |
| Strona kroku | pierwszy przycisk trybu, potem krok | `vel nte prb sld` | 1.7 (`sld` 0.3) |
| Warunki trigów | strona kroku, `prb` za 100 (101–115) | `1:2` … `!Pr` | 0.2 |
| Slide Drum 1–3 | strona kroku `sld`; czas: przycisk wyboru 1, drugi ekran | `sld`, `slt` | 0.3 |
| Ratchet, shift, mikrotiming | strona kroku, ekran 2 | `flm shf mic` | `mic` 0.6 |
| Nagrywanie z mikrotimingiem | SHIFT + REC | `trk rqn` | 0.6 |
| Strona wykonawcza | drugi przycisk trybu | `bpm rol mrp srt` / `shu scn djf djr` | 1.7 (`scn` 0.3, `djf djr` 0.4) |
| Rolle, także 1/32 i triole | tryb wykonawczy, trzymaj krok 1–7; tempo `rol` | `rol` | 0.3 (nowa lista) |
| Tryb fill | tryb wykonawczy, trzymaj krok 8 | – | 0.3 |
| Sceny mute | tryb wykonawczy: krok 9–16 przywołuje, COPY + krok 9–16 zapisuje | `scn` | 0.3 |
| Freeze | tryb wykonawczy, SHIFT + krok 3; też `frz` na stronie FX | `frz` | 0.4 |
| Slot efektu | SHIFT + krok 15 | `typ out d/w` | 1.7 |
| Pogłos | SHIFT + krok 15 drugi raz | `lvl siz dec ton` / `pre mod frz syn` | 0.4 |
| Delay z tempem | podstrona pogłosu, drugi ekran, gałka 4 | `syn` | 0.4 |
| Rumble (przyciszanie pogłosu) | SHIFT + krok 15 trzeci raz | `dck ddp djf djr` | 0.4 |
| Filtr DJ | tryb wykonawczy, drugi ekran, gałki 3 i 4; też FX, trzecia podstrona | `djf djr` | 0.4 |
| NRPN efektów | NRPN 96–99 i 116 | – | 0.4 |
| MIDI out z przesunięciem | zawsze | – | 0.2, z `mic` i `tpl` 0.6 |

Na co uważać przy nawigacji:

- **Menu globalne:** każde kolejne SHIFT + krok 12 idzie dalej: podstrona 1, jej drugi ekran, podstrona 2,
  strona CPU i z powrotem. Gdy wchodzisz z drugiego ekranu innej strony, menu otwiera się na ekranie
  `mtx mrx mrt flw`, więc strona CPU jest o jedno naciśnięcie bliżej.
- **Strona FX:** każde SHIFT + krok 15 idzie dalej: slot, pogłos, `dck ddp djf djr`, znowu slot. Przycisk
  wyboru wraca do trybu głosu.
- **Strona kroku:** zanim naciśniesz krok, pokazuje `shf -7` i `mic -84`. To znaczy: żaden krok nie jest
  wybrany. Najpierw naciśnij krok.
- **Strona ścieżki:** ustawienia działają na wybrany głos w pokazanym wzorcu. Przycisk głosu na tej stronie
  wybiera inny głos.
- **Nie używaj, pozostałość z fw 1.x:** SHIFT + przycisk wyboru trzymane pół sekundy w trybie głosu i ruch
  gałki zapisują p-lock nie na wybranym kroku, tylko na innym kroku tej samej ścieżki (krok × 8 + numer
  przycisku). Po kroku 9–16 nie zapisują nic. Tak działa też 1.7; fw 1.7 pisał wtedy nawet w następnej ścieżce.

## Timing i groove

### Rdzeń timingu i shuffle (0.1)

- Tempo mastera jest dokładne, do 0,01 %. Fw 1.7 grał większość temp za szybko, do 0,4 %. Tempo ustawiasz
  dalej w całych BPM.
- Shuffle przesuwa cały krok, razem z ratchetami i `shf`. Wzorce z shuffle i ratchetami albo shiftem brzmią
  więc inaczej niż w 1.7. MIDI clock out i jack clock out zostają proste.
- Shuffle należy do wzorca. Wybór wzorca wczytuje jego zapisany shuffle, więc ustaw go po wyborze wzorca i
  zapisz wzorzec.
- Choke działa tylko w obrębie ścieżki. Nowszy krok z ujemnym przesunięciem ucina ratchet, który przelewa się
  z poprzedniego kroku tej samej ścieżki.

### Timing co do próbki: `trg` (0.5)

- `smp` (domyślne): każdy głos startuje w próbce swojego zdarzenia, zawsze 128 próbek (2,90 ms) po nim. Flam i
  drobne przesunięcia brzmią tak samo przy każdym uderzeniu.
- `blk`: timing 0.4. Głos startuje na początku następnego bloku audio (64 próbki, 1,45 ms), czyli 1,45–2,9 ms
  po zdarzeniu. Do porównania A/B i jako zapas, gdyby zabrakło CPU.
- Zapisuje się z ustawieniami globalnymi (strona zapisu, typ global). Zmiana działa od następnego uderzenia;
  z `smp` na `blk` w trakcie grania może raz dać podwójny start głosu. Opóźnienie wobec MIDI out i clock out
  jest stałe: ustaw offset w DAW raz.
- Uwaga: nuty MIDI przez USB mają ziarnistość 1 ms. LFO retriggerowane nutą (np. na cutoff Drum 1–3) może się
  przesunąć do 1,4 ms. Przy wczytaniu kitu i dostępie do karty pojedyncze uderzenie może zagrać jak w `blk`.

### Tempo ścieżki: `tmp` (0.2)

- Jedenaście temp względem mastera: `1/4 1/3 1/2 2/3 3/4 x1 4/3 3/2 x2 x3 x4`. Przy 120 BPM krok w `x1` trwa
  125 ms, w `3/4` 166,7 ms. `3/4` z nutą co 4 kroki to 3 na 4 przeciw stopie, `x2` na hihacie to 1/32, `3/2`
  triolowe szesnastki.
- Wszystkie ścieżki startują od kroku 1 przy START i przy zmianie wzorca, potem biegną swobodnie, każda w
  swojej długości (1–64) i swoim tempie. Koniec wzorca to dalej takt mastera: najdłuższa ścieżka liczona w
  krokach mastera.
- Swing, shift i ratchety skalują się z krokiem ścieżki. Zegary i dioda START/STOP zostają w tempie mastera.
  Diody kroków i nagrywanie na żywo idą po krokach ścieżki.
- Uwaga: zmiana `tmp` działa od następnego kroku ścieżki. Po Song Position Pointer ścieżka w innym tempie niż
  `x1` biegnie dalej na swojej siatce, a jej krok trwający w miejscu skoku nie gra.
- Uwaga: przy zmianie wzorca ścieżka w `3/4` lub wolniejsza gubi wczesne uderzenie pierwszego kroku, jeśli
  przesunięcie zabiera je o więcej niż krok mastera wcześniej (np. `3/4` z `shf` −7).

### Humanize i opóźnienie: `hmt hmv dly` (0.2)

- `hmt` 0–8 ms: każdy krok (z ratchetami) przesuwa się losowo o ± do tylu ms. `hmv` 0–25 %: velocity zmienia
  się losowo o ± do tylu procent, najwyżej do 127.
- `dly` −15 … +30 ms: cała ścieżka gra stale wcześniej albo później. Przesunięcia liczą się w tickach (1/96
  kroku mastera, 1,30 ms przy 120 BPM). Rolle nie dostają humanize ani opóźnienia.
- Granice: `dly` z humanize najwyżej jeden krok ścieżki później; cały offset (swing, shift, `dly`, humanize)
  najwyżej 7/8 kroku wcześniej. Uwaga: `dly` −15 przy `shf` −7 nic już nie dodaje, a w `x4` powyżej ok. 99 BPM
  górna granica obcina.

### Mikrotiming kroku: `mic` (0.6)

- Każdy krok ma własne przesunięcie od −84 do +84 ticków (1/96 kroku ścieżki, 7/8 kroku w każdą stronę), 0 na
  siatce. Tick to 1,30 ms przy 120 BPM i 1,10 ms przy 142.
- Gałka 3 na drugim ekranie strony kroku. Gałka i enkoder zmieniają `mic` o jeden tick. W trybie edycji widać
  `Step` i `Micro`, a obok ticków przesunięcie w ms (+19 przy 120 BPM: `24.7ms`). Enkoder za `mic` przechodzi
  na podstronę p-locków.
- `shf` to teraz zgrubny widok `mic`: jedna jednostka to 12 ticków. Edycja `shf` zachowuje część drobną,
  edycja `mic` ustawia `shf` na najbliższą jednostkę. Krok z 0.5 z `shf` +2 pokazuje `mic` +24.
- `mic` należy do kroku pokazanego wzorca. Zapisz wzorzec przed zmianą wzorca, inaczej edycja znika.
- Uwaga: przy +84 uderzenie leży 12 ticków przed następnym krokiem (flam 15,6 ms przy 120 BPM) i grają oba.
  Krok, który swing albo `dly` przesuną za pierwsze uderzenie następnego kroku, pomija choke. Nie ma funkcji,
  która zeruje mikrotiming całej ścieżki.

### Szablony groove: `tpl amt` (0.6)

- Szablon to jedno przesunięcie na każdą z 16 szesnastek taktu. Ćwierćnuty zostają na siatce we wszystkich
  szablonach. Ustawiasz go osobno dla każdego głosu w każdym wzorcu: stopa prosto, hihaty z groove.
- `swg` (`even swing`): +16 ticków na co drugiej szesnastce; na 200 % to feeling triolowy. `psh` (`push off`):
  −4 ticki na trzeciej szesnastce każdej ćwierćnuty; `drg` (`drag off`): +6.
- `lay` (`lay back`), `lyb` (`lay bar`), `rlx` (`relax bar`), `2bt` (`two beat`): zmierzone z utworów
  referencyjnych przy 142–143 BPM. `lay` przy 142 BPM to +19,8 ms na „e” i +5,5 ms na „a”.
- `amt` 0–200 %, domyślnie 100. Pełna tabela przesunięć: [notatki 0.6](release-notes-v0.6.md).
- `mic` i szablon się dodają i razem kończą na 7/8 kroku. Shuffle, `dly` i humanize dochodzą na końcu. Szablon
  i shuffle też się sumują, więc z szablonami trzymaj shuffle na 0.
- Pozycja w szablonie to krok ścieżki od startu albo od zmiany wzorca, modulo 16. Ścieżka w `x2` rozkłada 16
  pozycji na pół taktu mastera. Zmiana `tpl` albo `amt` działa od następnego kroku. Ratchety idą w całości z
  pierwszym uderzeniem. Rolle nie biorą ani szablonu, ani `mic`.
- Uwaga: we wzorcu o nieparzystej długości swing przełącza się co pętlę, więc szablon i swing mogą się
  rozjechać. Szablony są w tickach: przy 120 BPM ten sam szablon jest w ms o 18 % dłuższy niż przy 142.
- Uwaga: ścieżki 6 i 7 dzielą hihat. Wygrywa ten, który uderza później: zamknięty przesunięty za otwarty z
  tego samego kroku go ucina. Daj obu ten sam szablon. Nie ma własnych szablonów.

Który szablon na którą ścieżkę: [przepisy groove 0.6](groove-recipes-v0.6.md).

## Sekwencer i występ

### Warunki trigów (0.2)

- Na stronie kroku `prb` idzie za 100. Wartości 101–115: `1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4 Fil !Fl 1st !1s Pre !Pr`.
- `A:B`: krok gra w pętli A z każdych B pętli ścieżki (jej długość w jej tempie, od START i od zmiany wzorca).
  `1:2` gra w pętlach 1, 3, 5… `1st`: tylko pierwsza pętla po START i po zmianie wzorca; `!1s`: wszystkie poza
  nią. `Fil` gra tylko w trybie fill, `!Fl` wtedy milknie.
- `Pre`: gra, gdy zagrał ostatni krok z warunkiem na tej ścieżce; `!Pr`: gdy nie zagrał. Stan ustawiają kroki
  z `prb` poniżej 100 i z `A:B`, `Fil`, `!Fl`, `1st`, `!1s`. Po START: „nie zagrał”.

### Rotacja ścieżki (0.2)

- SHIFT + krok 1 przesuwa wszystkie kroki wybranego głosu w pokazanym wzorcu o jeden krok wcześniej, SHIFT +
  krok 2 o jeden później. Z krokiem idzie wszystko: nuta, velocity, `prb` albo warunek, p-locki, shift i
  ratchet. Rotacja działa w długości ścieżki: krok z końca wraca na początek. Kroki za długością zostają.

### Slide dla Drum 1–3 (0.3)

- `sld` (`off`/`on`) na stronie kroku, za `prb`. `slt` 0–127 × 4 ms (do 508 ms) na drugim ekranie strony
  oscylatora (przycisk wyboru 1). `slt` należy do kitu, 0 to brak slide.
- Krok ze `sld` wyzwala się normalnie (obwiednie od nowa), ale wysokość jedzie liniowo w półtonach od
  poprzedniej nuty głosu, jak w 303. MIDI out wysyła te same nuty co bez slide.
- Przy ratchecie glide trwa przez kolejne uderzenia kroku. Rolle nigdy nie mają slide. Na głosach 4–7 `sld`
  nic nie robi. Uwaga: kity sprzed 0.3 mogą mieć w `slt` przypadkowe wartości. Ustaw `slt` przed pierwszym
  slide.

### Nagrywanie z mikrotimingiem: `rqn` (0.6)

- SHIFT + REC otwiera stronę nagrywania, `trk rqn` (`trk` to ścieżka automatyki z 1.7). `rqn` jest globalne i
  zapisuje się z ustawieniami globalnymi.
- `rqn` `off` (domyślne): nuta ląduje na najbliższym kroku, a jej zagrane przesunięcie w `mic`, z błędem
  najwyżej 0,65 ms przy 120 BPM. Nagranie gra tak, jak było zagrane. `on`: na siatkę, jak 0.5.
- Przesunięcie liczy się od miejsca, w którym krok gra (siatka ze swingiem, szablonem i `dly`). Nuta zagrana
  dokładnie w groove daje `mic` 0. Zmiana szablonu albo `dly` po nagraniu przesuwa nagrane nuty.
- Krok to bliższy z bieżącego i następnego; przy remisie następny. Nuta zapisana na następnym kroku nie zagra
  drugi raz. Nuty MIDI z DIN i USB liczą się od chwili przyjścia; jednorazowy roll z panelu może być do 1,45
  ms obok.
- Nagrywanie zawsze nakłada: nowa nuta zastępuje nutę, velocity, `prb` i (z `off`) timing kroku; flam i
  p-locki zostają. Uwaga: tuż po powrocie ścieżki z własnego tempa do `x1` nuta w tym jednym kroku może trafić
  na następny. Gdy nagrywany krok ma w kolejce przesunięte uderzenie, brzmi raz i ono, i nuta na żywo.

### Tryb wykonawczy: fill, sceny, rolle (0.3)

- **Fill:** trzymaj krok 8, dioda świeci. Kroki z `Fil` grają, z `!Fl` milkną, od pierwszego kroku, który się
  jeszcze nie zaczął. Puszczenie kroku 8 kończy fill, nawet przy trzymanym SHIFT, LOAD albo SAVE. Poza trybem
  wykonawczym krok 8 edytuje krok jak dotąd.
- **Sceny mute:** wycisz głosy, przytrzymaj COPY i naciśnij krok 9–16: scena 1–8 zapamiętuje wyciszenia. Krok
  9–16 ją przywołuje. Dioda mruga, aż scena wejdzie. Pusta scena nic nie zmienia. `scn` (globalne): `bar` na
  następnym takcie mastera (domyślnie), `stp` na następnym kroku mastera, po zatrzymaniu od razu. Wygrywa
  ostatnie przywołanie.
- Scena czekająca w chwili STOP wchodzi na pierwszym kroku po następnym starcie; ręczne wyciszenia ustawione
  po zatrzymaniu jej ustępują. Zmiana projektu ją odrzuca.
- Uwaga: sceny zapisują się z projektem, nie w autosave. Bieżące wyciszenia nie zapisują się nigdzie.
  Uderzenie przesunięte przed granicę sceny gra jeszcze według starych wyciszeń.
- **Rolle:** trzymaj krok 1–7 (głos 1–7). Tempo `rol`, od wolnego: `One 1/1 1/2 1/3 1/4 1/6 1/8 12 16 24 32`.
  Triole to `1/3 1/6 12 24`; `32` daje dwa uderzenia na krok. Domyślnie `16`.
- Przy 120 BPM `32` co 62,5 ms, `24` co 83,3 ms. Roll idzie po krokach swojej ścieżki (z `tmp`) i dostaje
  swing kroku. Roll gra też na wyciszonych ścieżkach, jak w 1.7.
- Uwaga: przy shuffle od ok. 95 roll `32` gubi drugie uderzenie nieparzystych kroków. Fill, sceny i slide nie
  mają MIDI CC.

## Efekty

### Pogłos (0.4)

- Stoi na szynie FX za slotem efektu, więc echa delaya też się pogłosują. Wyjście stereo.
- `lvl` 0 wyłącza pogłos (wtedy nie zużywa CPU). Wyżej mokry sygnał rośnie jak (`lvl`/127)².
- `siz` wielkość sali. `dec` czas RT60 od 0,3 s (0) do 19,2 s (127), w sekundach przy każdym `siz`. `pre`
  predelay, 2–92 ms. `mod` wolny ruch ogona (0,2–1,5 Hz).
- `ton`: 0 ciemny dolnoprzepustowy ok. 1,5 kHz, 64 płasko, 127 łagodny górnoprzepustowy ok. 400 Hz. Zmiana
  działa od razu też na brzmiący ogon.
- Pogłos liczy się w połowie częstotliwości próbkowania. Wersja oszczędna (bez wczesnych odbić, mokry sygnał
  mono) tylko wtedy, gdy Twój odczyt CPU z H12 przekroczy 63,28.
- Kit z 1.7 wczytuje się z wyłączonym pogłosem (`lvl` 0), `syn` `off` i `dck` `off`.

### Freeze (0.4)

- SHIFT + krok 3 w trybie wykonawczym; przy trzymanym SHIFT dioda kroku 3 pokazuje stan. Także `frz` na
  podstronie pogłosu i NRPN 99 (0 wyłącza, 1–127 włącza).
- Ogon trzyma się bez końca, z wolnym ruchem, i traci ok. 0,3 dB na minutę. Nowe uderzenia nie wchodzą do
  zamrożonego ogona, słychać je na sucho. Wyłączenie wraca do `dec`, `ton` i `mod`.
- To kontrolka na żywo: wczytanie kitu i morph jej nie zmieniają, a kit zapisuje się z `frz` `off`.

### Rumble (0.4)

- `dck` wybiera głos, który przycisza pogłos: `off dr1 dr2 dr3 snr cym hat` (ścieżki 6 i 7 dzielą hihat).
  `ddp` 0–127 to głębokość. Zwolnienie ok. 80 ms, bez opóźnienia. Klasyka: stopa do szyny FX, ciemny pogłos,
  `dck` `dr1`. Ogon puchnie między uderzeniami i chowa się pod nimi.

### Delay: `syn` (0.4) i ping-pong (0.6)

- `syn`: `off 16t 16 8t 16d 8 4t 8d 4 4d` (t triola, d z kropką). Idzie za tempem, też za zewnętrznym zegarem
  MIDI. Zmiana czasu przechodzi przez 10 ms przenikania. Gdy `syn` nie jest `off`, `tim` i `rng` nie działają;
  ich wartości zostają w kicie.
- Uwaga: za długi czas dzieli się na pół, aż się zmieści (zwykły 741 ms, ping-pong 370 ms). Zwykły bierze `4`
  od 81 BPM i `4d` od 122 BPM. Ping-pong bierze `8d` od 122, `4t` od 109, `4` od 163 BPM. `4d` przy 120 BPM
  gra więc jak `8d` (375 ms).
- `Pp` przerzuca echa na przemian także dla głosów z centrum: pierwsze w prawo, drugie w lewo. Echa głosu
  spanoramowanego do końca są o 3 dB cichsze niż w 1.7, a skrajnie w prawo pierwsze echo jest po prawej.

### Filtr DJ: `djf djr` (0.4)

- Jeden filtr na wyjściach stereo 1 i 2, za szyną FX. `djf` 0–62 dolnoprzepustowy (19,5 Hz–17,9 kHz), 63–65
  płasko, 66–127 górnoprzepustowy (22,4 Hz–ok. 20 kHz). `djr` 0–127 to rezonans (Q 0,5–5). Przejścia idą bez
  kliku.
- `djf` to kontrolka na żywo: wczytanie kitu i morph jej nie ruszają, kit zapisuje się z `djf` 64. `djr`
  należy do kitu.
- Uwaga: `djf`, `djr`, `lvl`, `frz` i `dec` nie mają LFO, velocity ani p-locków, tylko menu i NRPN.

Przepisy na kity: [pogłos, freeze, rumble i delay](kit-recipes-v0.4.md),
[klik do sprawdzeń timingu](kit-recipes-v0.5.md).

## MIDI i zegar

### MIDI out

- Note On i Note Off wychodzą w czasie przesuniętego uderzenia: z `shf`, swingiem, `dly`, humanize, a od 0.6
  też z `mic` i szablonem. Syntezator na MIDI out gra z groove, DAW nagrywa groove. Zegar MIDI, jack clock out
  i diody zostają na siatce mastera. Nie ma kwantyzowanego MIDI out obok przesuniętego.
- Jedna Note On na uderzenie, z override nuty, i zawsze jej Note Off, też dla jednorazowego rolla. Note Off
  idzie per głos, więc głosy na jednym kanale nie zostawiają wiszących nut. Wyciszenie ścieżki gasi jej nutę.
  Nuta przychodząca, która gra głos, ma jedno echo na MIDI out.

### LXR jako slave

- Tryb synchronizacji to wartość BPM poniżej 2: `MID` (zegar MIDI, DIN i USB) albo `EXT` (jack clock in). Od 2
  w górę tempo wewnętrzne.
- Tempo mastera jest śledzone ułamkowo i mierzone per clock, też z USB. Song Position Pointer działa w trybie
  wzorców i piosenki, gdy LXR stoi; w piosence od razu wczytuje wzorzec z tej pozycji. Continue wznawia tam,
  gdzie odtwarzanie stanęło.
- Jako slave LXR nie wysyła własnego Start, Stop ani Continue, więc z MIDI thru sprzęt za nim dostaje
  komunikaty mastera raz. W `smp` gra stałe 2,90 ms za zegarem; DAW to skompensuje.
- Uwaga: z zegarem z bufora audio komputera zmierzone tempo (i LFO z tempem, zaokrąglone do całych BPM) może
  odejść do 2,8 % przy buforze 1024; mniejszy bufor pomaga. SPP jest pomijany przy zegarze wewnętrznym i w
  trakcie grania. Lokalny start w trybie slave czeka na następną szesnastkę mastera.

### Jacki zegara: `cki cko`

- Przy clock in na 4 PPQ spóźniony impuls nie opóźnia już kroku. Swingowany zegar 4 PPQ gra więc prosto: użyj
  shuffle LXR. Gdy zegar stanie bez resetu, gra jeden krok więcej niż impulsów.
- Reset działa jak bramka: wysoki stan zatrzymuje i wraca na start, niski startuje z następnym impulsem.
  Szybkie zbocze już nie ginie. W `EXT` MIDI Start startuje z następnym impulsem jacka. Clock out na 2 PPQ
  daje 2 PPQ też w trybach slave, a po Continue i SPP impulsy zostają na siatce kroków. Przy zegarze
  wewnętrznym MIDI clock out biegnie też, gdy LXR stoi.
- Uwaga: podwójne wyzwolenia albo zgubiony impuls przy skoku tempa mogą na kilka impulsów rozjechać tempo.
  Czyste impulsy (jedno zbocze, bez drgań styków) tego unikają.

### NRPN i CC

- Nowe NRPN (7-bitowe: CC 99 = 0, CC 98 = numer, CC 6 = wartość): 96 `djf`, 97 `djr`, 98 `lvl`, 99 `frz`, 116
  `dec`.
- Tylko w menu, bez CC i NRPN: `siz ton pre mod dck ddp syn`, `tmp hmt hmv dly`, `mic tpl amt`, fill, sceny,
  slide.
- NRPN tempa rolla wybiera z nowej, 11-pozycyjnej listy. Bank Select (CC 0) 64–127 jest ignorowany. LXR dalej
  przedstawia się przez USB jako `Sonic Potions USB MIDI`.

### Strona CPU (0.1)

- `CPU`: średnie obciążenie sekwencera i silnika dźwięku w ostatniej sekundzie; `pk`: najcięższy blok audio w
  tej sekundzie. Oba w procentach czasu bloku (64 próbki, 1,45 ms).
- `Qmax`: najwięcej zdarzeń w kolejce sekwencera od włączenia; `ovf`: zdarzenia odrzucone przy pełnej kolejce.
  Odświeża się dwa razy na sekundę, nic się nie zapisuje. Nie liczy interfejsu ani wczytania kitu przy zmianie
  wzorca.

## Pliki i zgodność z 1.7

### Instalacja

- Przed instalacją skopiuj całą kartę SD na komputer. Skopiuj `LXR_V2_LICHOMAT_0.6.img` do głównego folderu
  karty bez zmiany nazwy i usuń każdy inny `LXR*.img`: updater instaluje pierwszy, który znajdzie.
- Wyłącz LXR, włóż kartę, trzymaj przycisk enkodera i włącz. Trzymaj do `LXR Updater`, czekaj na `Update OK!`.
  Ekran startowy: `LICHOMAT`, `v0.6 unofficial`.

### Co się gdzie zapisuje

- **Wzorzec** (`.PAT` i autosave `tmp.pat`): blok z 0.2 (`tmp hmt hmv dly` siedmiu ścieżek), a od 0.6 drugi
  blok z częścią drobną `mic` i szablonami, tylko gdy ich używasz. Warunki trigów i `sld` siedzą w bajcie
  `prb` kroku.
- **Projekt** (`prj.nfo`): sceny mute. **Kit** (`.SND`, `tmp.snd`): pogłos, rumble, `syn`, `djr` i `slt`, w
  bloku z 0.4 i w bajtach, których 1.7 nie używa.
- **Ustawienia globalne** (`glo.cfg`, dalej 30 bajtów): `trg`, `rqn` i `scn`, w bajtach, których 1.7 nie
  czyta. Zapis przez stronę zapisu, typ global, jak w 1.7.
- **Nie zapisuje się:** bieżące wyciszenia, freeze (kit zawsze z `frz` `off`), `djf` (kit zawsze z 64), strona
  CPU. Ustawienia ścieżki, `mic` i szablony są edycją wzorca: zmiana wzorca bez zapisu je odrzuca.
- Pliki z 1.7 grają w 0.6 jak w 1.7: `x1`, humanize i `dly` 0, `mic` równe `shf` z pliku, `tpl` `off` z `amt`
  100, pogłos wyłączony, `trg` `smp`, `rqn` `off`. Pliki z v0.2–v0.5 grają jak tam.

## Poprawione błędy fw 1.7

W 0.1 i 0.2:

- Shuffle przesuwał tylko pierwsze uderzenie ratchetu, a MIDI clock out przy nim skakał. Tempo grało do 0,4 %
  za szybko.
- Nuty jednej ścieżki połykały ratchety innej. Przesunięta nuta brała p-locki i velocity sąsiedniego kroku.
- Każda nuta szła na MIDI dwa razy, część bez Note Off. Nuta zagrana tuż przed krokiem przy nagrywaniu grała
  dwa razy.
- Slave: tempo bez ułamków, ginące nuty na swingu, brak SPP. Psute wiadomości DIN, śmieci i zgubione
  wiadomości na USB MIDI out, gubiony reset jacka.
- Wczytanie kitu i Bank Select pisały bajt we wzorcu; Bank Select 64–127 mógł zawiesić LXR. Ponowny zapis
  projektu zmieniał nazwę kitu 0.
- Ping-pong włączony w trakcie grania pisał poza pamięć. Filtry hihatu, talerza i werbla potrafiły zamilknąć
  do wyłączenia. Pitch EG slope 127 zabierał ton.
- Drobne: strona piosenki po stopie na pozycji 255, p-locki i nuty zapisywane w następnej ścieżce (SHIFT +
  wybór, NRPN 121), krok bez SHIFT w menu globalnym zmieniał długość ścieżek, morph bez karty, śmieci na
  stronie Euklid.

W 0.6:

- Długie nazwy w trybie edycji pisały poza górną linię LCD; teraz najwyżej 8 znaków, np. `Slide tm`,
  `SceneChg`, `DJ filtr`, `Trig tmg` (E4.10).
- Oscylator PWM czytał tablicę piły o jeden za wierszem; zmienia się tylko najwyższy wiersz, od 14 080 Hz
  (E4.11).
- Ping-pong dla głosów z centrum nie przerzucał ech (E7.8).
- Po starcie z pustej karty `dst` LFO z `voi` 0 dawał tylko `off` i jeden cel innego głosu; teraz cele głosu 1
  (E4.13).
- Wartość parametru menu z NRPN spoza listy nazw pokazywała śmieci, np. roll z NRPN 119 = 14; teraz pierwszą
  nazwę listy (E4.13).
- Uszkodzona wiadomość USB MIDI z bajtem danych ≥ 128 pisała za tablicę CC; teraz jest odrzucana (E4.13).
- Offset DC pogłosu (wejściowy filtr z 1.7) zostawiał stałe przesunięcie po ogonie, rosnące we freeze;
  poprawka jest w buildach po H12 (E7.7).

## Zgodność plików

Co robią starsze firmware z plikami zapisanymi przez 0.6. Wszystkie wczytują je bez błędów.

| Plik z 0.6 | 1.7 i v0.1 | v0.2–v0.5 |
|---|---|---|
| Wzorzec, timing | tylko zgrubne `shf`; `tmp`, humanize, `dly`, `mic` i szablony giną | `tmp`, humanize i `dly` zostają; krok w zgrubnym `shf` (najbliższa 1/8, remis w górę); bez szablonów |
| Warunki trigów | grają w każdej pętli, jak `prb` 100 | v0.2–v0.5 je znają |
| Slide | krok gra bez slide, ze swoją nutą; z warunkiem jak `prb` 100 | v0.2 jak 1.7; v0.3–v0.5 grają slide |
| Projekt, sceny | sceny pomijane | v0.2 pomija; v0.3–v0.5 je znają |
| Kit, efekty 0.4 | slot efektu bez pogłosu i filtra DJ; `syn` gra wolny czas `tim`/`rng` | v0.2–v0.3 jak 1.7; v0.4–v0.5 je znają |
| `glo.cfg` | `trg`, `rqn` i `scn` pomijane | `rqn` pomijane; `scn` znają v0.3–v0.5, `trg` v0.5 |

- Nie ruszaj tam `prb` kroków z warunkiem albo `sld`: starszy edytor ustawi go na 100 lub mniej i zdejmie
  znacznik slide.
- Zapis w starszym firmware traci to, czego ono nie zna: wzorzec z 1.7 wraca z samym zgrubnym `shf`, z v0.5 ze
  zgrubnym `shf` i blokiem 0.2; projekt z 1.7 bez scen; kit z 1.7 i v0.1–v0.3 z wyłączonym pogłosem. `slt`
  przetrwa zapis w 1.7.
- Wzorzec bez `mic` i szablonów jest bajt w bajt taki jak z 0.5 (3 697 B); z nimi ma 3 941 B. Kity, piosenki i
  `glo.cfg` są w 0.6 takie jak w 0.5.

## Linki

- Notatki do wydania: [0.1](release-notes-v0.1.md) (po angielsku), [0.2](release-notes-v0.2.md),
  [0.3](release-notes-v0.3.md), [0.4](release-notes-v0.4.md), [0.5](release-notes-v0.5.md),
  [0.6](release-notes-v0.6.md).
- Przepisy: [groove 0.6](groove-recipes-v0.6.md), [kity z pogłosem 0.4](kit-recipes-v0.4.md),
  [klik 0.5](kit-recipes-v0.5.md).
- Plan sesji testowej: [test-session-v0.6.md](test-session-v0.6.md); procedury:
  [hardware-checklist.md](hardware-checklist.md).
