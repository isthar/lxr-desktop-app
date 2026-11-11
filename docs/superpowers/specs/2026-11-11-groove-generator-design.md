# E10.2: generator wzorców na stronie ścieżki (projekt)

Data: 2026-11-11. Status: szkic do Twojej oceny. Decyzje podjąłem sam (zgodnie z Twoim upoważnieniem); każdą możesz zmienić, a przykłady do odsłuchu rozstrzygną resztę.

## 1. Po co

Generator ma dawać w kilka sekund wzorce w stylu minimal groove techno: ghosty hihatów, suby, figury perkusji, rzadkie uderzenia na granicach fraz i przenoszenie rytmu z jednej ścieżki na drugą. Wpisuje **zwykłe kroki** do pokazanego patternu: możesz je potem poprawiać i zapisywać jak każde inne. Nie dokłada nowego silnika odtwarzania i nie zmienia formatu plików, więc wygenerowany pattern gra też na v0.7.

Liczby biorę z pomiarów referencji (E10.1, `build/reference/album/report-groove.md`, poza repo). Odwołuję się do nich jako „pomiar”.

## 2. Na czym się opieram (fakty z kodu v0.7, 95c2787)

- Pattern ma 7 ścieżek po 64 kroki (4 takty po 16). Każda ścieżka ma własną długość 1–64; pattern kończy się z najdłuższą ścieżką.
- Krok to 7 bajtów: głośność (velocity 0–127), `prob` (0–100 % albo warunek 101–115), nuta (60 = własna wysokość głosu), dwa p-locki. Do tego flam/shift i fine.
- Warunki A:B grają, gdy `loop % B == A−1`, gdzie `loop` liczy przejścia **tej ścieżki**. Licznik zeruje się przy starcie i zmianie patternu. Na ścieżce 64-krokowej jedno przejście to 4 takty, więc:
  - krok w takcie 4 z warunkiem 2:2 gra w takcie 8 każdej frazy 8-taktowej;
  - krok w takcie 1 z warunkiem 1:2 gra w takcie 1 każdej frazy;
  - 1:4…4:4 dają cykl 16 taktów.
  Tak wyrażam pozycje we frazie bez nowych kodów warunków.
- Velocity działa na głośność tylko przy włączonym `vol` głosu (velocity → volume), i to liniowo w amplitudzie: `velo = vel/127`. Więc −15 dB to vel 23, a nie 80.
- Edycje patternu, który gra, przepadają przy zmianie patternu, jeśli go nie zapiszesz (zachowanie fw 1.7). Zapisana kopia (`patternBackupBuffer`) to ostatni zapisany albo wczytany stan.
- Wolny RAM: około 844 B. Ustawienia generatora trzymam w RAM na ścieżkę, jak Euklid, i ich nie zapisuję: zapisują się kroki.
- Sekwencer czyta kroki w przerwaniu. Zapis kroku robię przy zablokowanych przerwaniach, krok po kroku, jak `seq_clearTrackMicro`.

## 3. Obsługa

Strona ścieżki (SHIFT + mode2), dwie nowe podstrony po `tpl amt` / `vtp vdp sav`:

| Podstrona | Ekran 1 | Ekran 2 |
|---|---|---|
| 3 | `typ sty den lev` | `var not sed und` |
| 4 | `src shf` | — |

| Parametr | Zakres (domyślnie) | Znaczenie |
|---|---|---|
| `typ` | `---` `hat` `sub` `prc` `rar` `prp` (`---`) | Rodzaj wzorca. `---` = generator wyłączony dla tej ścieżki. |
| `sty` | 0–8 | Styl w ramach rodzaju (tabele niżej). 0 = „moje akcenty” (tylko `hat` i `prc`). |
| `den` | 0–16 | Gęstość: ghostów na takt (`hat`), akcentów na takt (`prc`, `prp`), zdarzeń na 16 taktów (`rar`). |
| `lev` | −30…0 dB | Poziom ghostów względem akcentu (127). |
| `var` | −4…+4 | Zmiana na końcu frazy (`hat`, `prc`) albo interwał drugiej nuty (`sub`). |
| `not` | −24…+24 półtonów | Przesunięcie nuty wszystkich kroków (60 + `not`). |
| `sed` | 0–99 (0) | Ziarno. To samo ziarno daje ten sam wzorzec, w emulatorze i na urządzeniu. |
| `und` | `---` `und` | Push, obrót na `und`, push: przywraca ścieżkę z ostatniego zapisu patternu. |
| `src` | 1–7 (1) | Ścieżka źródłowa: stopa dla `sub` i `prc`, źródło dla `prp`. |
| `shf` | −8…+8 kroków (0) | Przesunięcie kopii (`prp`). |

**Wartości domyślne rodzaju:** wybranie `typ` wczytuje do `sty den lev var not` wartości z tabeli; `sed`, `src` i `shf` zostają.

| `typ` | `sty` | `den` | `lev` | `var` | `not` |
|---|---|---|---|---|---|
| `hat` | 1 | 7 | −15 | +1 | 0 |
| `sub` | 1 | 0 (bez limitu) | −3 | 0 | −2 |
| `prc` | 1 | 3 | −13 | +1 | 0 |
| `rar` | 1 | 2 | 0 | 0 | 0 |
| `prp` | 1 | 4 | −15 | 0 | 0 |

**Jak działa (jak Euklid):** gdy `typ` ≠ `---`, każda zmiana któregokolwiek parametru generatora od razu przelicza ścieżkę. Wybranie `typ` generuje pierwszy raz. Kręcąc `sed`, przeglądasz warianty, a `den` dokłada lub zdejmuje ghosty **bez przetasowania** pozostałych.

Ręczne poprawki znikną przy następnej zmianie parametru generatora tej ścieżki. Po poprawkach ustaw `typ` na `---`. `und` cofa do ostatniego zapisu i działa na patternie, który gra.

**Generator zawsze:**
- ustawia długość ścieżki na 64 (4 takty);
- czyści p-locki, flam, shift i fine na krokach, które pisze;
- dla `hat` i `prc` ze stylem 0 nie rusza Twoich akcentów (kroków z vel ≥ 64);
- gdy pisze vel < 127, a głos ma wyłączone `vol`, włącza je (inaczej ghosty grałyby pełną głośnością), tak jak ręczna zmiana tego parametru w kicie.

**Zapis:** jak każda edycja. Zapisz pattern (albo projekt), zanim przełączysz pattern.

## 4. Rodzaje wzorców

Kroki numeruję 1–16 w takcie; „ćwierćnuty” to 1, 5, 9, 13, „offbeaty” 3, 7, 11, 15, „parzyste szesnastki” 2, 4, 6, …, 16.

### 4.1 `hat`: ghosty hihatów (pomiar: 4–10 ghostów na takt, mediana 7,4; około −15 dB; miejsca stałe takt po takcie)

Akcenty (vel 127) według stylu, ghosty (vel z `lev`) na wolnych krokach. Ten sam układ we wszystkich 4 taktach.

| `sty` | Akcenty | Gdzie ghosty (waga) |
|---|---|---|
| 0 | Twoje kroki z vel ≥ 64 | parzyste 4, ćwierćnuty 2, reszta 1 |
| 1 klasyczny | offbeaty | parzyste 4, ćwierćnuty 2 |
| 2 gęsty | offbeaty i ćwierćnuty | parzyste 4 |
| 3 ćwierćnuty cicho | offbeaty | ćwierćnuty 8, parzyste 3 |
| 4 rozsypany | offbeaty | każdy wolny krok 1 |

- `den` ghostów na takt (obcięte do liczby wolnych kroków).
- Poziom ghosta: `lev` + stałe dla danego miejsca przesunięcie od −3 do +2 dB z ziarna. Z pomiaru: q25–q75 to −17…−12 dB.
- Ruch głośności takt po takcie (pomiar: ±1 dB) daje `hmv` (humanize velocity). Generator go nie ustawia.
- `var` +n: n dodatkowych ghostów na takt, grających tylko w taktach 5–8 frazy (warunek 2:2). To skok gęstości (pomiar: gęstość zmienia się na liniach fraz).
- `var` −n: n ghostów gra tylko w taktach 1–4 (1:2).

### 4.2 `sub`: suby (pomiar: warstwa wraca na 4. szesnastce każdej ćwierćnuty, często też na 3.; nuta 0–3 półtony pod stopą)

| `sty` | Takt (x = uderzenie) | Skąd |
|---|---|---|
| 1 | `...x...x...x...x` | wspólne wszystkim utworom: krok przed stopą |
| 2 | `..xx..xx..xx..xx` | klasyczny rolling |
| 3 | `.xxx.xxx.xxx.xxx` | galop |
| 4 | `..x...x...x...x.` | klasyczny offbeat |
| 5 | `..xx..x...xx..x.` | synkopa |
| 6 | `......xx......x.` | rzadki |
| 7 | takt A `..xx..xx..xx....`, takt B `..xx..xx..xx.xxx` | figura 2-taktowa |
| 8 | z ziarna: każda ćwierćnuta osobno z `...x` (waga 3), `..xx` (4), `.xxx` (1), `..x.` (2), `....` (1) | nieklasyczny |

- Uderzenia nigdy nie stoją na krokach stopy. Stopa to pierwszy takt ścieżki `src`; gdy jest pusta albo to ta sama ścieżka, przyjmuję 1, 5, 9, 13.
- Velocity: ostatnie uderzenie przed stopą 127, pozostałe `lev` (domyślnie dla `sub` −3 dB, vel 90). Z pomiaru: wejście na 3. szesnastce jest ciche.
- Nuta 60 + `not` (domyślnie −2). Strojenie suba pod stopę to sprawa kitu (E10.4).
- `var` ≠ 0: druga nuta, `var` półtonów od pierwszej, na ćwierćnutach 2 i 4 (pomiar: dwie nuty na zmianę w 3 z 8 utworów, pół tonu albo ton).
- `den` używa tylko styl 8, jako górną granicę uderzeń na takt (0 = bez limitu).

### 4.3 `prc`: figury perkusji (pomiar: figury 2-taktowe; takty parzyste różnią się od nieparzystych o ≥ 2 kroki; na 8 taktów 1–2 uderzenia dochodzą lub znikają; ghosty perkusji stoją krok przed akcentem)

- Takt 1: `den` akcentów (vel 127) wybranych z ziarna według wag stylu. Kroki stopy (`src`) pomijam.
- Takt 2: takt 1 z jednym uderzeniem przeniesionym w inne miejsce (różnica 2 kroków).
- Ghosty: `min(3, (den+1)/2)` na takt, na kroku przed akcentem, z poziomem `lev` (domyślnie dla `prc` −13 dB) ±2 dB.
- Takty 3–4 = takty 1–2. Ich zmiany grają tylko w taktach 7–8 frazy:
  - `var` +n: n uderzeń dochodzi w takcie 4 (warunek 2:2), z preferencją kroków 13–16. Z pomiaru: na końcu frazy więcej uderzeń na 13–16.
  - `var` −n: n akcentów z taktu 4 dostaje 1:2, więc znika w takcie 8.

| `sty` | Wagi miejsc na akcenty |
|---|---|
| 0 | Twoje akcenty z taktów 1–2; generator dokłada tylko ghosty i zmiany |
| 1 synkopy | 2. i 4. szesnastka ćwierćnuty 4, offbeat 2 |
| 2 offbeat | offbeat 6, 2. i 4. szesnastka 2 |
| 3 3+3+2 | kroki 4, 7, 12, 15 mają 8, reszta 1 |
| 4 losowy | wszystkie 1 |

### 4.4 `rar`: rzadkie uderzenia (pomiar: risery i jąkanie szumu w 8. takcie frazy, sub-boomy, downliftery i ryki na 1. takcie, reszta rozsypana z lekką przewagą taktów 1 i 8)

Jedna ścieżka na jeden dźwięk (riser, ryk, boom). Wszystko vel 127, nuta 60 + `not`.

| `sty` | Co pisze |
|---|---|
| 1 koniec frazy | Start na 1. kroku taktu 4 ścieżki. `den` 1: warunek 4:4 (takt 16); `den` 2+: 2:2 (takty 8, 16). `den` 3+: jąkanie na krokach 9–12 taktu 4 (flam 4 uderzenia), 4:4; `den` 4: jąkanie 2:2. |
| 2 początek frazy | 1. krok taktu 1. `den` 1: 1:4 (co 16 taktów); `den` 2–3: 1:2 (co 8); `den` 4+: bez warunku (co 4). |
| 3 posyp | `den` kroków (do 8) z ziarna, poza krokami stopy, każdy z prawdopodobieństwem 25 %: średnio `den` zdarzeń na 16 taktów. Takty 1 i 4 ścieżki mają wagę 2, pozostałe 1. |
| 4 mieszany | 1. krok taktu 1 z 1:2 (boom co 8 taktów), 1. krok taktu 4 z 4:4 (riser w takcie 16) i `den` − 2 kroków posypu. |

### 4.5 `prp`: przeniesienie rytmu ze ścieżki `src`

| `sty` | Co pisze |
|---|---|
| 1 kopia | kroki źródła przesunięte o `shf`, z velocity i warunkami źródła, nuta źródła + `not` (pomiar: figury wracają przesunięte o +5 i −4 kroki) |
| 2 dopełnienie | `den` akcentów na takt tam, gdzie źródło milczy (parzyste 4, offbeaty 3, reszta 1) |
| 3 przedtakty | ghost (`lev`) na kroku przed każdym akcentem źródła, jeśli tam pusto |
| 4 echo | akcenty źródła przesunięte o `shf`, jako ghosty (`lev`) |

Źródło krótsze niż 64 kroki powtarzam do 64. `src` równe ścieżce docelowej: nic się nie dzieje.

## 5. Losowanie (wspólne dla prototypu i firmware)

Musi dawać identyczny wynik w Pythonie i w C, więc opisuję je dokładnie.

- **Stan startowy:** zaczynam od x = 2166136261, potem dla bajtów (`sed`, `typ`, `sty`, numer ścieżki 0–6) liczę x = ((x XOR b) × 16777619) mod 2³². Jeśli x = 0, ustawiam x = 1.
- **Krok:** x ^= x << 13; x ^= x >> 17; x ^= x << 5 (32 bity). `rand(n)` = wynik % n.
- **Ranking miejsc:** dla każdego kandydata, w kolejności rosnących kroków, klucz = `rand(65536)` × waga. Biorę od największego klucza; przy remisie wygrywa niższy krok.
- **Kolejność losowań jest stała:**
  1. klucze dla wszystkich miejsc siatki (16, dla `prc` 32), także z wagą 0;
  2. przesunięcia poziomu dla wszystkich miejsc;
  3. losowania zależne od rodzaju (przeniesienie w takcie 2, zmiany `var`).

  Dlatego `den` i `lev` nie przestawiają reszty wzorca.
- **Velocity z dB:** vel = round(127 × 10^(dB/20)), co najmniej 1. W C to tabela 31 wartości dla 0…−30 dB, bez liczb zmiennoprzecinkowych.

## 6. Etapy

**E10.2 (ten szkic):**
- prototyp w Pythonie `tools/gen/gen.py` (gałąź `v0.8`), dokładnie według punktów 4–5;
- testy jego własności: liczba ghostów = `den`, poziomy w zakresie, akcenty na offbeatach, warunki fraz, `den` k ⊂ `den` k+1, determinizm;
- przykłady z emulatora v0.7 (zwykłe kroki, więc v0.7 je zagra) na stronie odsłuchu:
  - każdy styl `hat` i `sub` na 4–8 taktów ze stopą;
  - `prc` z `var` +1 i −1 na 8 taktów;
  - `rar` na 16 taktów;
  - `prp`;
  - cały groove na 32 takty.

**E10.3 (firmware, gałąź `v0.8` od v0.7 95c2787):**
- **Rdzeń:** `PatternGen.c/.h`, czysty C bez zależności od reszty firmware. Wejście: ustawienia, kroki źródła i bieżącej ścieżki. Wyjście: 64 kroki, timing, bity, długość, budowane na stosie (około 0,5 kB; plan sprawdza stos).
- **Testy rdzenia:** host testy porównujące bajt w bajt z wektorami z prototypu (około 200 kombinacji ustawień i ziaren).
- **Spięcie:**
  - menu: 10 parametrów według wzoru `tpl amt`;
  - zapis kroków przy zablokowanych przerwaniach;
  - `preset_touchTimer(PRESET_PAT_TIMER)`, jak przy przełączaniu kroku;
  - `und` z `patternBackupBuffer` (tylko gdy pokazany pattern gra);
  - włączanie `vol`.
- **Emulator:** kopie parametrów w rendererze, `drift.py record`, test z `--trig-log`: rzadkie zdarzenia w taktach 8 i 16, ghosty co takt.
- **Bramka RAM:** `tools/check.sh` (około +70 B).

**Później, po Twoim odsłuchu (moja rekomendacja: decyzja po przykładach):**
- **Warianty w kolejnych patternach** (aranżacja w blokach 8 taktów): z jednego groove'u generator pisze kilka patternów z drobnymi zmianami (ghosty ±1–2, figura ±1–2 uderzenia, zmiana suba).
- **Cały groove jednym ruchem:** potrzebuje stałych ról ścieżek w kitach z E10.4.
- **Warunki 1:8…8:8** (wolne kody 116–123) dla cykli 32 taktów, jeśli 16 nie wystarczy.

## 7. Czego nie robię

- Swingu i mikrotimingu (masz `tpl`, `hmt` i v0.7).
- Zapisywania ustawień generatora w patternie: brak RAM-u (jeden bajt na ścieżkę i pattern to 448 B), a wynik i tak jest w krokach.
- Generowania dźwięków. Kity i głosy to E10.4–E10.6.

## 8. Doprecyzowania po prototypie (11.11)

Prototyp (`tools/gen/gen.py` na v0.8) wymusił kilka rozstrzygnięć. Wiążą one także firmware.

- **Wejście:** każdą ścieżkę (źródło i bieżącą) czytam tak, jak gra. Ścieżkę krótszą niż 64 kroki powtarzam do 64. Bez tego styl 0 na ścieżce 16-krokowej zgubiłby akcenty w taktach 2–4.
- **Kroki bez uderzenia:** zapisuję je czyste i nieaktywne, z nutą 60 + `not`.
- **Zakresy:** `sty` spoza stylów danego rodzaju przycinam do najbliższego. Poziom (`lev` plus przesunięcie miejsca) przycinam do 0…−30 dB.
- **Losowanie:** przesunięcia poziomu losuję dla każdego rodzaju, także tam, gdzie nie są używane. Losowania zależne od rodzaju zależą tylko od `typ` i `sty`, nigdy od `den`, `lev` ani `var`.
- **`hat`:**
  - styl 0 szuka wolnych miejsc takt po takcie;
  - `var` +n bierze n kolejnych miejsc z rankingu z warunkiem 2:2;
  - `var` −n daje 1:2 ostatnim n z `den` ghostów.
- **`sub`:**
  - „ostatnie uderzenie przed stopą” to ostatnie uderzenie od poprzedniej stopy;
  - styl 8 to jeden takt z ziarna, powtórzony; `den` zostawia najlepsze w rankingu.
- **`prc`:**
  - w takcie 2 przenoszę akcent `sorted(takt1)[los % den]` na najlepsze miejsce rankingu kluczy 16–31;
  - ghosty stoją na kroku przed akcentem w tym samym takcie, **nigdy na kroku stopy**;
  - `var` + daje krokom 13–16 wagę ×4;
  - `var` − na stylu 0 nic nie robi (Twoich akcentów nie ruszam).
- **`rar`:**
  - `den` 0 nie pisze nic;
  - style 3–4 rankingują wszystkie 64 kroki kluczami z fazy 3;
  - styl 1 traktuje `den` ≥ 4 jak 4;
  - stopa dla posypu pochodzi z `src`.
- **`prp`:**
  - styl 1 nie kopiuje flagi slide;
  - style 3 i 4 **przejmują warunek kroku źródła**, więc echo akcentu 2:2 też gra tylko w taktach 5–8, a velocity biorą z `lev`;
  - nuta to 60 + `not`.

## 9. Poprawki po przeglądzie rdzenia (11.11)

Przegląd porównał rdzeń w C z prototypem (53 000 losowych przypadków, zero różnic) i wskazał miejsca, gdzie prototyp rozjeżdża się z punktami 3–5. Rozstrzygnięcia (zmieniają prototyp, wektory i rdzeń):

- **`prc`, takt 2:** przenoszę zawsze akcent taktu 1 z najwyższym kluczem rankingu, a nie `sorted(takt1)[los % den]`. Cel to najlepsze w rankingu kluczy 16–31 wolne miejsce poza krokami stopy. Losowanie „przeniesienia” znika z fazy 3. Dzięki temu zmiana `den` nie przetasowuje taktu 2, jak obiecują punkty 3 i 5.
- **Styl 0 bez akcentów** (`hat` i `prc`): gdy na ścieżce nie ma żadnego kroku z vel ≥ 64, styl 0 działa jak styl 1: akcenty i wagi stylu 1, ziarno dalej z `sty` = 0.
- **`prc`, styl 0:** akcenty każdego taktu biorę tak, jak grają, we wszystkich 4 taktach. Takty 3–4 zachowują swoje akcenty i dostają ghosty od nich. To zastępuje zdanie „takty 3–4 = takty 1–2” z punktu 4.3 dla stylu 0.
- **`prc`, ghost przed akcentem na 1. kroku taktu:** nie powstaje. Wcześniej trafiał na 16. krok tego samego taktu.
- **`rar`, styl 4:** posyp ma najwyżej 8 kroków, jak w stylu 3, więc `den` 10–16 daje ten sam wzorzec.
- **Ograniczenie:** stopę czytam z pierwszego taktu ścieżki `src`, tak jak gra (krótsza ścieżka się powtarza), i ten układ obowiązuje we wszystkich 4 taktach. Przy stopie o długości, która nie dzieli 16 (np. 12 kroków), sub i perkusja mogą w taktach 2–4 trafić na stopę. W tym stylu stopa gra 4/4, więc zostawiam to opisane, nie poprawione.
- **`typ` poza zakresem** (bajt 6–255, np. z NRPN): działa jak `---`, czyli nie rusza ścieżki. Wcześniej był przycinany do `prp`, który nadpisuje ścieżkę. (Przegląd jakości, 11.11.)

## 10. `sub` to ghost kicki (11.11, Twoje doprecyzowanie)

Twoje słowa: „chodziło mi z tym subem o coś takiego jak groove z ghost kickiem”, z przykładami `***g**g****g**g*` i `***********g**G*` pod stopą 4/4. Rodzaj `sub` (na LCD zostaje `sub`) zamiast szesnastkowego basiku pisze więc **figury ghost kicków**: ciche uderzenia stopopodobnego głosu na synkopowanych szesnastkach między stopami. Pomiar się z tym zgadza: dół wraca na krokach 4, 7, 12 i 15 w kilku utworach referencji. Brzmienie ghost kicka (głos podobny do stopy, krótszy, ciemniejszy) daje kit (E10.4).

Style (g = ghost, vel z `lev`; G = akcent, 127):

| `sty` | Takt | Skąd |
|---|---|---|
| 1 | `...g..G....g..G.` | klasyczny, Twój wzór z utworu 3 (4, 7, 12, 15) |
| 2 | `...........g..G.` | Twój drugi przykład (12, 15) |
| 3 | `......g.......G.` | „i” po 2. i 4. ćwierćnucie (7, 15) |
| 4 | `...g.......G....` | przed 2. i 4. stopą (4, 12) |
| 5 | `.......g.......G` | tuż przed 3. stopą i przed taktem (8, 16) |
| 6 | `...g..gg...g..GG` | gęsty (4, 7, 8, 12, 15, 16), jak jeden z utworów |
| 7 | takt A `...g..G....g..G.`, takt B `...g..G..g.g..GG` | figura 2-taktowa: w takcie B dochodzą 10 i 16 |
| 8 | z ziarna: `den` uderzeń na takt (domyślnie 3) z miejsc 4, 7, 12, 15 (waga 4), 8, 16 (waga 2), 10, 14 (waga 1); G na ostatnim w takcie; jeden takt powtórzony | nieklasyczny |

- **Kroki stopy:** uderzenia nigdy na nie nie trafiają, jak dotąd (stopa ze ścieżki `src`).
- **`var`** zmienia znaczenie i działa teraz jak w `prc`: +n to n ghostów dochodzących w 8. takcie frazy (2:2), z pierwszeństwem dla kroków 16, 14 i 10; −n to n ghostów z taktu 4 ścieżki, które dostają 1:2, czyli znikają w takcie 8. Druga nuta odpada.
- **Domyślnie:** `sty` 1, `den` 3, `lev` −8 dB (vel 51), `var` 0, `not` 0 (głos stroi kit).
- **Losowanie:** losowania dla stylu 8 i dla `var` idą w fazie 3, w tej kolejności.
- **Ścieżka:** ghost kicki gra osobna ścieżka (Drum 2 albo Drum 3) z głosem podobnym do stopy. Stopa zostaje na swojej ścieżce.

**Uzupełnienie do §3 (11.11, po przeglądzie menu):** ustawienie parametrowi generatora tej samej wartości (np. pokrętło lub enkoder dociśnięty do końca zakresu) nic nie robi: nie przelicza ścieżki, więc nie kasuje ręcznych poprawek. Generatora nie da się sterować przez MIDI ani NRPN.

**Szczegóły do implementacji (11.11, E10.16, moje decyzje):**

- **Stopa:** uderzenie (g albo G) na kroku stopy odpada; G nie przechodzi na inny krok. W stylu 8 G dostaje ostatnie uderzenie taktu, które zostało.
- **`den`** działa tylko w stylu 8: tyle uderzeń na takt, 1–8; 0 = wszystkie 8 miejsc; 9–16 = 8. Domyślnie 3 (dotąd 0). W stylach 1–7 `den` nic nie zmienia.
- **Losowanie, faza 3:** najpierw, tylko w stylu 8, 16 kluczy `rand(65536)` (klucz miejsca p to p-ta szesnastka taktu); potem, w każdym stylu, 16 kluczy `var`. Klucze `var` są losowane zawsze, więc zmiana `var` nie przestawia stylu 8. Losowania 1 i 2 idą jak dla każdego rodzaju, ale `sub` z nich nie korzysta (jak `rar`).
- **Styl 8:** ranking klucz × waga (4, 2, 1 jak w tabeli, inne miejsca 0), bez kroków stopy; `den` najlepszych; ten sam takt we wszystkich 4 taktach.
- **`var` +n:** kandydaci to wolne miejsca słownika ghost kicków (kroki 4, 7, 8, 10, 12, 14, 15, 16) w takcie 4, poza stopą. Najpierw 16, 14 i 10 w tej kolejności, potem reszta według kluczy `var` × wagi stylu 8. n pierwszych dostaje ghosta (`lev`) z warunkiem 2:2; gdy kandydatów jest mniej, dochodzi mniej.
- **`var` −n:** ghosty taktu 4 (bez G) według kluczy `var`; n pierwszych dostaje 1:2. G zostaje.
- **Velocity:** g = `lev` bez przesunięcia, G = 127. Domyślne −8 dB to z tablicy vel 51.
- **Nuta:** 60 + `not` na każdym uderzeniu. Druga nuta i reguła „ostatnie uderzenie przed stopą na 127” z §4.2 odpadają; §4.2 opisuje już tylko stary sub, zastąpiony tą sekcją.
- **`sty` 0:** jak dotąd, przycinany do 1.

## 11. Po przeglądzie całości (11.11, moje decyzje)

- **Styl 0 nie czyta własnych kroków jako Twoich akcentów.** Dotąd styl 0 (`hat` i `prc`) brał za akcent każdy krok z velocity ≥ 64. Po pierwszym przeliczeniu były wśród nich także kroki generatora: uderzenia `var` +n na 127 i ghosty przy `lev` ≥ −8 dB. Akcentów przybywało z każdą zmianą, a `sed` 0 nie wracał do pierwszego wzorca.
  - Teraz firmware pamięta dla każdej ścieżki, które kroki sam zapisał przy ostatnim przeliczeniu: maska 64 bitów, tylko w pamięci, ważna dla patternu, na którym ścieżka była ostatnio generowana.
  - Przy kolejnym przeliczeniu kroki z maski nie są akcentami, nawet z velocity ≥ 64. To samo dotyczy reguły „styl 0 bez akcentów gra jak styl 1” (§9).
  - Krok poprawiony ręcznie, który był w masce, nie liczy się jako akcent: następna zmiana i tak go zastępuje (§3). Akcent dopisany na pustym kroku się liczy.
- **Maska jest pusta:**
  - po ustawieniu `typ` na `---`, więc ręczne poprawki akcentów robi się przy `---`, a po powrocie do `hat` albo `prc` każdy krok z velocity ≥ 64 jest Twoim akcentem;
  - po `und`;
  - na patternie innym niż ten, na którym ścieżka była ostatnio generowana;
  - po włączeniu LXR.
- **Prototyp:** `generate()` dostaje maskę kroków poprzedniego przeliczenia (domyślnie pusta) i oddaje maskę kroków, które zapisał (`_Out.written`). Wektory dostają przypadki z maską. Bez maski wynik jest bajt w bajt taki jak dotąd.
- **Przeładowanie grającego patternu w trakcie zapisu** (przycisk patternu albo Program Change na ten sam pattern) przerywa zapis tak jak zmiana patternu: licznik zmian patternu zamiast samego numeru. Tak samo `und`.
- **„Ta sama wartość nic nie robi”** (§10, uzupełnienie do §3) dotyczy patternu, na którym ścieżka była ostatnio generowana. Na innym patternie pierwsze dotknięcie parametru, także tą samą wartością, pisze tam ścieżkę. Ustawienia należą do ścieżki, nie do patternu, a tylko tak `typ` ustawiony wcześniej zadziała na nowym patternie. Instrukcja to opisuje.
- **Bez zmian:**
  - menu odświeża wartości z przerwania przy zmianie taktu, jak dla `PAR_TRACK_*`; w najgorszym razie ekran na chwilę pokazuje starą wartość;
  - `vol` włącza się w kicie, który jest wczytany teraz. Przy autoload kitu inny pattern może wczytać kit z `vol` wyłączonym; notatki do wydania to opisują.
