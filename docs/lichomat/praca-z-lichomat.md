# LICHOMAT 0.8: praca z firmware w praktyce

LICHOMAT 0.8 zamienia LXR-02 w groovebox do minimal groove techno: długie, hipnotyczne groove'y z ghostami,
cichą perkusją i rzadkimi zdarzeniami w tle. Generator wzorców pisze w kilka sekund ghosty hihatu, ghost
kicki, figury perkusji i rzadkie uderzenia na końcach fraz; do tego dochodzą groove na poziomie ścieżki,
mikrotiming, szablony głośności i pogłos z freeze i ducking. Wszystko robisz na samym LXR. Ten plik to
krótki przewodnik: pełny opis każdej funkcji jest w [instrukcji](instrukcja.md), a procedury sprawdzeń na
urządzeniu w [liście sprawdzeń](hardware-checklist.md) (U42–U45 dla generatora).

Skróty: „ekran 5” to `typ sty den lev`, „ekran 6” to `var not sed und`, „ekran 7” to `src shf` na stronie
ścieżki. Stronę ścieżki otwierasz tak: wybierz głos, przytrzymaj SHIFT, naciśnij drugi przycisk trybu
(`len stp rot mrp`); każde kolejne SHIFT + drugi przycisk trybu idzie o ekran dalej, piąte naciśnięcie to
ekran 5. Wartość zmieniasz gałką 1–4 danego ekranu albo enkoderem: przekręć na parametr, naciśnij (tryb
edycji), przekręć, naciśnij.

## Groove w 5 minut

Założenia: wzorzec pusty, tempo wewnętrzne (BPM 128), `shu` 0, `trg` `smp`. Wszystkie ścieżki mają 16 kroków,
dopóki generator ich nie wydłuży.

1. **Kit.** Wczytaj kit ECHO (slot 52). Pliki kitu leżą w `kits/card/PROJ00/` na gałęzi `kits`; skopiuj je
   na kartę SD na komputerze (to jedyny krok z komputerem), potem wczytaj kit na LXR: LOAD, kit, potwierdź.
   Obok są FALA (riser i boom na granicach fraz), RYK (ryk tylko w pogłosie) i TLO (bed z rumble), sloty 53–55.
   Może być też dowolny kit, w którym Drum 2 jest strojony jak miękka stopa: ghost kicki zagrają na głosie 2.
   Na głosie 5 przyda się długi dźwięk (szum, riser).
2. **Stopa ręcznie.** Wybierz głos 1. Pierwszy przycisk trybu (strona kroku), potem kroki 1, 5, 9 i 13: świecą
   cztery diody. Naciśnij START: cztery uderzenia na takt. Ta stopa jest też źródłem (`src` 1) dla generatora,
   który omija jej kroki.
3. **Hat ghosty, głos 6.** Wybierz głos 6, otwórz ekran 5 (`--- 0 0 0`). Na `typ` naciśnij, przekręć na
   `hat` (dolna linia `hat ghosts`), naciśnij. Ekran pokazuje `hat 1 7 -15`. Na stronie ścieżki, na pierwszym
   takcie, świeci 12 z 16 kroków: offbeaty 3, 7, 11 i 15 to głośne uderzenia, reszta to ciche ghosty
   (velocity 16–25). Generator sam włącza `vol` tego głosu (velocity →
   głośność), inaczej ghosty grałyby pełną głośnością.
4. **Ghost kicki, głos 2.** Wybierz głos 2, ekran 5, `typ` `sub` (`ghost kick`, `sub 1 3 -8`). Świecą kroki 4,
   7, 12 i 15 w każdym takcie, nigdy 1, 5, 9 ani 13. Kroki 4 i 12 to miękkie ghosty (velocity 51), 7 i 15
   pełne. Style 1–8 zmieniasz na `sty`; w stylu 8 działa też `den`.
5. **Figura perkusji, głos 3.** Głos 3, `typ` `prc` (`prc 1 3 -13`). Dostajesz dwutaktową figurę: akcenty
   na synkopach i ciche ghosty o krok przed nimi. Takty 3–4 powtarzają 1–2.
6. **Rzadkie uderzenie, głos 5.** Głos 5, `typ` `rar` (`rar 1 2 0`). Świeci jeden krok: krok 1 taktu 4
   z warunkiem, który gra w takcie 8 i 16 frazy. Odczekaj do taktu 8: długi dźwięk wchodzi raz i milknie
   do taktu 16. Przy `den` 3 w takcie 16 dochodzi jąkanie (ratchet ×4 na krokach 9–12 taktu 4); `den` 4
   i więcej daje jąkanie także w takcie 8. `sty` 2 przenosi uderzenie na początek frazy.
7. **Opcjonalnie `prp`.** Na wolnym głosie (np. 4): `typ` `prp` (`prp 1 4 -15`), na ekranie 7 `src` 6 i `shf`
   +5. Głos 4 gra wtedy rytm hihatu przesunięty o 5 kroków, z jego velocity. To najprostszy sposób, żeby
   przenieść figurę na inny dźwięk.
8. **Przeglądanie wariantów.** Gdy `typ` nie jest `---`, każda zmiana parametru przelicza ścieżkę od razu,
   w trakcie grania, bez przerwy w dźwięku.
   - `sed` (ekran 6, gałka 3): to samo ziarno daje ten sam wzorzec, kręć i słuchaj wariantów. Zostaw to,
     co Ci gra.
   - `den` (ekran 5, gałka 2) i `lev` (gałka 4): ile ghostów i jak cicho. Zacznij od domyślnych (hat 7 / −15
     dB, sub 3 / −8 dB) i ściszaj albo dokładaj po jednym.
   - `var` (ekran 6, gałka 1): zmiana na końcu frazy. `var` +1 dokłada ghost, który gra tylko w taktach 5–8;
     −1 zabiera ghosty z taktów 5–8. Słychać to dopiero po 4 albo 8 taktach.
9. **Własne akcenty, styl 0.** Jeśli chcesz sam zagrać głośne hity hihatu: ustaw `typ` na `---` (naciśnij,
   przekręć, naciśnij), na stronie kroku narysuj głos 6 na krokach 3, 7, 11 i 15 (velocity 127), wróć
   na ekran 5 i ustaw `typ` `hat`. Ekran pokaże `hat 0 7 -15`: styl 0, Twoje akcenty zostają, generator
   dokłada ghosty. `sed` dalej wraca do tego samego wzorca. To samo działa dla `prc`.
10. **Cofanie i ręczne poprawki.** `und` (ekran 6, enkoder na `und`): naciśnij, przekręć o jedno kliknięcie
    (dolna linia `push: undo`), naciśnij. Cofa ścieżkę do ostatniego zapisu wzorca i działa tylko na wzorcu,
    który gra. Gdy chcesz poprawić pojedyncze kroki ręką, najpierw ustaw `typ` na `---`: inaczej następna
    zmiana któregokolwiek parametru napisze ścieżkę od nowa i skasuje poprawki.
11. **Tło: pogłos.** Wyślij głos na szynę FX: strona głosu, przycisk wyboru 8 (MIX), drugi ekran, `out` `FX`.
    SHIFT + krok 15 jeszcze raz otwiera pogłos (`lvl siz dec ton`, drugi ekran `pre mod frz syn`); zacznij od
    `lvl` 100, `siz` 110, `dec` 105, `ton` 30, `pre` 10. Rzadkie uderzenia z głosu 5 najlepiej wchodzą
    w pogłos jako tło.
    - **Freeze:** tryb wykonawczy (drugi przycisk trybu), SHIFT + krok 3: ogon trzyma się bez końca, nowe
      uderzenia słychać na sucho. Ten sam SHIFT + krok 3 wyłącza.
    - **Ducking przez stopę:** SHIFT + krok 15 trzeci raz (`dck ddp djf djr`). `dck` `dr1` (stopa), `ddp` 110:
      pogłos pompuje się w rytm stopy, ogon puchnie między uderzeniami i chowa się pod nimi.
12. **Groove: szablony na hihatach.** Głos 6, strona ścieżki, ekran 3 (`tpl amt`): `tpl` `lay`, `amt` 100
    (przy `lay` za lekkim 110–130). Stopa zostaje `off`, `shu` 0, bo shuffle i szablon się sumują. Na głosie 7
    daj ten sam szablon, bo oba hihaty dzielą jeden głos. Ekran 4 (`vtp vdp`): generator już zniżył ghosty
    przez `lev`, a `vtp` ścisza je jeszcze raz, więc moja rada: zacznij od `vtp` `off` albo `vdp` 30–50 % i
    dostrój na słuch.
13. **Zapis.** Zapisz wzorzec: SAVE, na stronie zapisu typ pattern, wolny slot, potwierdź. Potem tak samo kit (typ kit): generator
    włączył `vol` głosów 2, 3, 4 i 6, a `vol` należy do kitu. Ustawienia generatora (`typ`, `sed` i reszta)
    nie zapisują się, zapisują się kroki. Po wyłączeniu i włączeniu LXR każde `typ` to `---`, a wzorzec gra
    tak samo.

## Co inaczej niż w fw 1.7

| Obszar | fw 1.7 | LICHOMAT | Od | Gdzie na LXR |
|---|---|---|---|---|
| Tempo i shuffle | większość temp gra za szybko, do 0,4 %; shuffle przesuwa tylko pierwsze uderzenie ratchetu | tempo dokładne do 0,01 %; shuffle przesuwa cały krok z ratchetami i `shf` | 0.1 | menu globalne (SHIFT + krok 12), `bpm shu` |
| Timing wyzwalania | głos startuje na początku następnego bloku audio | `smp`: start w próbce zdarzenia, zawsze 2,90 ms po nim; `blk` to timing 0.4 | 0.5 | menu globalne, podstrona 2, gałka 4, `trg` |
| Tempo ścieżki, humanize, opóźnienie | brak | `tmp` (11 temp), `hmt hmv`, `dly` −15…+30 ms na ścieżkę | 0.2 | strona ścieżki, ekran 2 |
| Mikrotiming kroku | tylko zgrubne `shf` (jednostka 1/8 kroku) | `mic` ±84 ticków (1/96 kroku); `shf` to zgrubny widok `mic` (12 ticków) | 0.6 | strona kroku, ekran 2, gałka 3 |
| Szablony groove | brak | `tpl amt`: `swg lay lyb rlx 2bt psh drg`, `us1`–`us4`, osobno dla głosu w wzorcu | 0.6 (`us1`–`us4` 0.7) | strona ścieżki, ekran 3 |
| Szablony głośności | brak | `vtp vdp`: `of8 ful dep qtr`, `us1`–`us4`; działają tylko z włączonym `vol` głosu | 0.7 | strona ścieżki, ekran 4 |
| Własne szablony | brak | `sav` zapisuje timing i głośność kroków 1–16 do `us1`–`us4` (z trybu edycji) | 0.7 | strona ścieżki, ekran 4, `sav` |
| Nagrywanie | nuta ląduje na siatce | `rqn` `off`: nuta na najbliższym kroku, zagrane przesunięcie w `mic` (błąd do 0,65 ms) | 0.6 | SHIFT + REC, `trk rqn` |
| Zerowanie mikrotimingu | brak | `clear [micro]?` zeruje `shf` i `mic` całej ścieżki | 0.7 | SHIFT + COPY, enkoder, `clear [micro]?` |
| Generator wzorców | brak | `typ sty den lev var not sed und src shf`: ghosty, ghost kicki, perkusja, rzadkie uderzenia, przeniesienie rytmu | 0.8 | strona ścieżki, ekrany 5–7 |
| Warunki trigów | `prb` do 100 | `prb` 101–115: `1:2 … 4:4 Fil !Fl 1st !1s Pre !Pr` | 0.2 | strona kroku, `prb` |
| Rotacja ścieżki | brak | kroki całej ścieżki o jeden wcześniej albo później | 0.2 | SHIFT + krok 1 albo 2 |
| Slide | brak | `sld` na Drum 1–3 jak w 303, czas `slt` | 0.3 | strona kroku `sld`; `slt` przycisk wyboru 1, drugi ekran |
| Rolle | stara lista temp | nowa lista 11 temp, także 1/32 i triole (`rol`); rolle idą po krokach ścieżki | 0.3 | tryb wykonawczy, trzymaj krok 1–7 |
| Fill | brak | trzymaj krok 8: kroki `Fil` grają, `!Fl` milkną | 0.3 | tryb wykonawczy, krok 8 |
| Sceny mute | brak | 8 scen: COPY + krok 9–16 zapisuje, krok 9–16 przywołuje; `scn` `bar` albo `stp` | 0.3 | tryb wykonawczy |
| Pogłos | brak | `lvl siz dec ton pre mod`; w połowie częstotliwości próbkowania | 0.4 | SHIFT + krok 15 drugi raz |
| Freeze | brak | ogon trzyma się bez końca; kontrolka na żywo, nie zapisuje się w kicie | 0.4 | tryb wykonawczy, SHIFT + krok 3; `frz` |
| Ducking pogłosu (rumble) | brak | `dck ddp`: wybrany głos przycisza pogłos | 0.4 | SHIFT + krok 15 trzeci raz |
| Delay | czas `tim`/`rng` | `syn`: delay za tempem, też za zewnętrznym zegarem; ping-pong `Pp` także dla głosów z centrum | 0.4 (ping-pong 0.6) | SHIFT + krok 15 drugi raz, `syn` |
| Filtr DJ | brak | `djf djr` na wyjściach stereo 1 i 2 | 0.4 | tryb wykonawczy, drugi ekran, gałki 3–4 |
| MIDI out | Note On i Note Off bez przesunięcia, nuty dwa razy | Note On z przesunięciem (`shf`, `mic`, `tpl`, `dly`), velocity z `vtp`, zawsze Note Off | 0.2 (`mic` `tpl` 0.6, `vtp` 0.7) | zawsze |
| Slave, zegar | tempo bez ułamków, brak SPP, gubiony reset jacka | tempo ułamkowe, Song Position Pointer, poprawki jacków `cki cko` | 0.2 | menu globalne, podstrona 2 |
| Strona CPU | brak | `CPU pk Qmax ovf`: obciążenie i kolejka sekwencera | 0.1 | SHIFT + krok 12 czwarty raz |
| Pliki i zgodność | – | żaden format się nie zmienia; wszystkie wersje wczytują pliki 0.8. fw 1.7 gra kroki z warunkiem w każdej pętli (jak `prb` 100) | 0.8 | – |

## Czego unikać

- **Zapisz wzorzec, zanim go zmienisz.** Niezapisane kroki z generatora znikają przy zmianie wzorca, jak
  każda edycja w 1.7. Ścieżka 64-krokowa wydłuża wzorzec do czterech taktów, więc zmiana wzorca czeka do końca
  takiej pętli.
- **Po ręcznych poprawkach ustaw `typ` na `---`.** Inaczej następna zmiana parametru przeliczy ścieżkę
  i skasuje poprawki.
- **Zapisz też kit.** `vol` (velocity → głośność) należy do kitu, a autoładowany kit może przywrócić `vol`
  wyłączone: wtedy ghosty grają pełną głośnością.
- **Generator pisze jedną ścieżkę naraz.** Nie ma jeszcze „całego groove'u jednym ruchem” ani wariantów
  w kolejnych wzorcach.
- **Stopa o długości niedzielącej 16 (np. 12 kroków).** Generator czyta stopę z pierwszego taktu ścieżki
  `src`, więc w taktach 2–4 uderzenia mogą trafić na stopę.
- **fw 1.7 gra kroki z warunkiem w każdej pętli.** Wzorzec z `var` i `rar` zagra na 1.7 inaczej; nie edytuj
  tam kroków z warunkiem, bo starszy edytor zamieni warunek na 100.
- **Freeze nie zapisuje się.** To kontrolka na żywo: kit zawsze zapisuje się z `frz` `off`, a wczytanie kitu
  i morph jej nie zmieniają.
- **Pogłos gra w połowie częstotliwości** (jak w 0.4–0.7), a shuffle z szablonami `tpl` się sumuje: z szablonami
  trzymaj `shu` na 0.
