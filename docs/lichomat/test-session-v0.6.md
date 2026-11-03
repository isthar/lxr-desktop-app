# LICHOMAT: sesja testowa na v0.3 i v0.6 (jeden wieczór, dwa pliki)

Ten plan zastępuje `test-session-v0.3.md`. Procedury są w `docs/lichomat/hardware-checklist.md` (po
angielsku, z krokami, **Expected** i **Record**); tu jest tylko kolejność, czas i to, co się różni na
0.6. Pliki, sumy SHA-256 i instalacja: `build/images/README.txt`.

- **Część 1, v0.3:** H12, czyli odczyt CPU. Musi być na buildzie bez pogłosu, a jego `pk` ustala
  częstotliwość pogłosu w 0.4 (plan v0.4 D41). Do tego kilka drobiazgów, które wychodzą przy okazji.
- **Część 2, v0.6:** wszystko inne. 0.6 zawiera 0.1–0.5, więc jedna instalacja wystarcza na sprawdzenia
  od H1 do U37. Wyjątkiem jest wszystko, co zależy od ostatecznej częstotliwości pogłosu: to osobna,
  późniejsza sesja (na końcu).

**Czas.** Cały plan to ok. 7 h, więcej niż jeden wieczór. **Na jeden wieczór (ok. 2,5 h) zrób
przygotowanie, część 1 i w części 2 tylko pozycje z ★, w tej kolejności.** Resztę zrobisz w kolejny
wieczór na tym samym 0.6, bez ponownej instalacji. Każdy blok części 2 jest zamknięty: możesz przerwać po
dowolnym.

## Przygotowanie (dzień wcześniej, ok. 20 min)

- [ ] `cd /Users/tk/Code/lxr/build/images && shasum -a 256 -c SHA256SUMS`: jedenaście razy `OK`.
- [ ] Zainstaluj SendMIDI (github.com/gbevin/SendMIDI) i MIDI Monitor (snoize.com/MIDIMonitor): na
      tym Macu ich nie ma. SendMIDI potrzebują H6, H7, U2, U3 i X4, a MIDI Monitora H5, H6, H8–H11,
      U10, U11, U18 i X4.
- [ ] DAW: projekt 48 kHz (tylko H7 krok 6 wymaga 44,1 kHz), ślad stereo na wyjście 1 LXR, ślad MIDI
      do interfejsu MIDI (DIN) i ślad MIDI do `Sonic Potions USB MIDI`. **Zegar MIDI w DAW domyślnie
      wyłączony na oba porty**; włączasz go tylko w bloku 2.5.
- [ ] Klipy MIDI w DAW, gotowe przed sesją:
  - U28: 16 nut na kanale 1 z nierównymi odstępami 30–200 ms;
  - U35: jeden takt szesnastek na kanale 3 przy 142 BPM, kroki 2, 6, 10, 14 o 12 ms później, a 4, 8,
    12, 16 o 6 ms wcześniej.
- [ ] Pliki SendMIDI do H7 kroku 6 są gotowe: `build/images/midi/pairs.txt` i `frames.txt`.
- [ ] Folder na nagrania, np. `~/LXR-testy-<data>/`.

**Okablowanie raz na cały wieczór:**
- wyjście 1 LXR do interfejsu audio;
- MIDI OUT LXR (DIN) do MIDI IN interfejsu;
- MIDI OUT interfejsu do MIDI IN LXR (DIN);
- USB LXR do komputera.

Klawiaturę do H10 podłącz przez DAW (MIDI thru na port interfejsu), żeby nie przekładać kabli. Syntezator
do H11 jest zbędny, wystarczy sam MIDI Monitor. Gniazda zegara (X1–X3) są opcjonalne.

## Część 1: v0.3 (ok. 30 min)

Plik: `build/images/ci-0.3/LXR_V2_LICHOMAT_0.3.img` (`d630eabc…`, plik z CI i z run sheet; firmware ten
sam co na końcu gałęzi `v0.3`).

- [ ] **Kopia całej karty SD na komputer** (10 min). Obowiązkowo, przed pierwszą instalacją.
- [ ] Instalacja 0.3 („Installing an update file” w liście sprawdzeń).
- [ ] **H1** i **U19 krok 1**: ekran `LICHOMAT` / `v0.3 unofficial`, nazwa pliku na karcie.
- [ ] **H2 krok 1** przy okazji: pierwszy start z Twoją kartą z 1.7 bez `glo.cfg error`, ustawienia
      globalne jak w 1.7. Zapisz tylko to; resztę H2 zrobisz na 0.6.
- [ ] Kopia robocza do H12: wczytaj projekt z kitem DLC, w którym wszystkie głosy idą na FX (Faders,
      Malware, Kronky albo Penalty), i od razu zapisz go w pustym slocie (SAVE, `Project`, slot
      `Empty`). Dalej pracujesz na tej kopii, oryginał zostaje.
- [ ] **H12** (5 min odczytu i ok. 10 min na ciężki wzorzec), kroki 1–5:
  - krok 3 to ciężki przypadek: wszystkie 7 ścieżek na szesnastkach, kilka `x8`, 140 BPM, delay
    w slocie FX;
  - szybciej niż przyciskami: generator Euklid (SHIFT + drugi przycisk trybu), dla każdego głosu
    `LEN` 16 i `stp` 16.
- [ ] Zapisz ciężki wzorzec i kit z kroku 3 w tej kopii (SAVE `Pattern`, SAVE `Kit`) i zanotuj numer
      projektu, wzorca i kitu. Potrzebują ich U20, U30 i U37.
- [ ] **Od razu wyślij mi `pk` z kroku 3** (to liczba P). Od niej zależy, który plik 0.4 wgrasz po
      H12. Oba są już gotowe, z poprawką offsetu DC pogłosu (E7.7):

| P (`pk`, H12 krok 3) | pogłos w 0.4 | plik |
|---|---|---|
| ≤ 63,28 | połowa (tak jak gra teraz) | `build/images/po-H12/polowa/` (`67ca9833…`) |
| więcej | połowa, wariant oszczędny | `build/images/po-H12/lean/` (`e44de0b6…`) |

Pełnej częstotliwości nie ma w tabeli: przy niej ogon basu w pogłosie przechyla się w lewo (decyzja
Q34, porównanie na stronie odsłuchu).

Więcej na 0.3 nie rób: wszystko inne z 0.1–0.3 jest w 0.6, więc na 0.3 robiłbyś to dwa razy.

## Część 2: v0.6

Plik: `build/images/LXR_V2_LICHOMAT_0.6.img` (`beefc513…`, lokalny build gałęzi `v0.6` @ `e971801`, z
poprawką ping-ponga E7.8; starszy plik `975abbd1…` zastąp tym).

**Różnice wobec tekstu listy sprawdzeń na 0.6:**
- Gdzie lista każe „install 0.1/0.2/0.3/0.5 again”, instalujesz 0.6.
- Z `trg smp` (domyślne) każde uderzenie jest stałe 2,90 ms po swoim czasie. Pomiary względne (od
  uderzenia odniesienia) się nie zmieniają; stałe ~3 ms wobec siatki DAW to właśnie to.
- `shf` jest zgrubnym widokiem `mic`: shift +3 z H4 pokazuje też `mic` +36. To normalne.
- U7 (powtórka H1 i H2) załatwiają H1 i H2 na 0.6.

### 2.1 Instalacja i Twoje projekty (15 min) ★

- [ ] ★ Zainstaluj 0.6: ekran `LICHOMAT` / `v0.6 unofficial`.
- [ ] ★ **H2** kroki 1–4 na Twoich projektach (0.6 czyta wzorce inaczej niż 0.3, więc tu to się liczy).
- [ ] ★ Projekt testowy **P**: Twój projekt zapisany w pustym slocie (SAVE, `Project`, `Empty`). W P
      robisz wszystkie wzorce poniżej, oprócz U2 i U3, które mają własne nowe projekty.

### 2.2 Rdzeń timingu (55 min)

Synchronizacja wewnętrzna, DAW nagrywa wyjście 1, MIDI Monitor na DIN out, DAW nie wysyła zegara.

- [ ] ★ **H3**, potem ★ **H4**, potem ★ **H5** (na wzorcu z H4, shuffle 127; krok 4 z piosenką A/B).
- [ ] ★ **H8** (scenariusze a i b), potem ★ **H9** (wzorce A i B, zapisz oba).
- [ ] **H11**: tylko MIDI Monitor, bez syntezatora.
- [ ] **H10**: z `rqn` `off` (domyślne). Od następnej pętli krok 5 gra z zagranym przesunięciem; to
      nowość 0.6, nie błąd.

### 2.3 Klik: uderzenia co do próbki (65 min)

Kit z klikiem z `docs/lichomat/kit-recipes-v0.5.md` (setup przed U27): zbuduj go raz i zapisz w slocie
kitu w P.

- [ ] ★ **U27**, potem ★ **U33** na tym samym wzorcu (klik na 16 krokach przy 120 BPM, dochodzą `mic`).
- [ ] **U28**: LXR zatrzymany, klip U28 z DAW przez DIN, potem przez USB.
- [ ] **U29**: włącz w DAW zegar MIDI **tylko do interfejsu (DIN)**, bufor 64 próbki; potem tylko do
      USB.
- [ ] ★ **U35**: zegar znowu tylko do DIN, klik na Drum 3, 142 BPM, klip U35.
      Zapisz dokładnie, co pokazuje `mic`. Oczekiwane +11 i −5 leżą blisko granicy zaokrąglenia
      (12 ms to 10,9 ticka, 6 ms to 5,45 ticka), a Note On przez DIN trwa ok. 1 ms. Wynik o ±1 tick
      zapisz tak, jak jest, i nie poprawiaj klipu; ocenię to.

### 2.4 Szablony i brzmienie na zwykłym kicie (20 min)

Zegar MIDI w DAW wyłączony, synchronizacja wewnętrzna. Kit ze stopą na głosie 1 i hihatem na głosie 6.

- [ ] ★ **U34** (142 BPM, `lay` na 100, 50 i 150 %, potem wszystkie szablony z Twoim groove).
- [ ] **U31** (`smp` i `blk` na sześciu dźwiękach).

### 2.5 LXR jako slave zegara z DAW (55 min)

- [ ] **H6**: zegar DAW tylko do DIN, bufor 64 próbki. Kroki 1–9; w kroku 8 SendMIDI do interfejsu.
- [ ] **H7**: zegar DAW tylko do USB. Kroki 1–7; krok 6 przy 44,1 kHz, z plikami
      `build/images/midi/pairs.txt` i `frames.txt` (`sendmidi dev "…" -- < pairs.txt`). Po H7 wróć
      do 48 kHz i wyłącz zegar w DAW.

### 2.6 Groove i występ z 0.2 i 0.3 (80 min)

Synchronizacja wewnętrzna, P, zwykły kit. Kolejność idzie od prostych wzorców do tych, które trzeba
zapisać na wycieczkę do 1.7 (2.9).

- [ ] **U1**, potem **U13**, potem **U12** kroki 1–4 (wzorzec zapisz: krok 5 jest w 2.9).
- [ ] **U15** (fill), **U10** (tempo ścieżki, wzorce A i B, piosenka), **U11** (humanize, opóźnienie).
- [ ] **U14** kroki 1–4 (wzorzec F w P); krok 5 w 2.9.
- [ ] **U16** kroki 1–7 (sceny w P); krok 8 w 2.9.
- [ ] **U17** (rolle), **U18** kroki 1–5 (slide; wzorzec i kit zapisz, krok 6 w 2.9).
- [ ] **U9** (generator wzorców).
- [ ] opcjonalnie **U19** krok 2 (`slt` Twoich kitów).

### 2.7 USB MIDI out (15 min)

- [ ] **X4** kroki 1–6 (w kroku 4 wyciągasz kabel USB).

### 2.8 Poprawki błędów fw 1.7 (40 min)

- [ ] **U2**: własne nowe projekty w pustym slocie; karta trzy razy do komputera.
- [ ] **U3**: nowy projekt. W kroku 6 ścisz.
- [ ] **U4**, **U6**. „As in v0.1”: nie słyszałeś v0.1 na LXR, więc porównaj z 1.7 i zapisz każdą
      różnicę.

### 2.9 Jedna wycieczka do 1.7 (45 min)

Wszystkie kroki „w 1.7” z 0.2–0.6 i H13 w jednej instalacji 1.7 w tę i z powrotem. Ważne: zapis
projektu w 1.7 zapisuje też `glo.cfg` tego projektu (bajt `trg` jako 0), a U16 krok 8 zapisuje P.
Dlatego U32 robisz w Twoim projekcie, którego 1.7 nie zapisuje, a nie w P.

Przed instalacją 1.7 (na 0.6):
- [ ] **U36** krok 1 w P (wzorzec z `mic`, szablonem i `amt` 70; wyłącz/włącz, projekt, `tmp.pat`).
- [ ] **U32** krok 1, jako ostatnie przed 1.7: wczytaj Twój projekt, `trg` `blk`, zapisz ustawienia
      globalne, wyłącz i włącz.
- [ ] Karta do komputera:
  - **U36** krok 4 (rozmiary `.PAT` w folderze P: 3 697 i 3 941 B);
  - skopiuj `fw-1.7/LXR_V2_V1.7.img` i usuń plik LICHOMAT.
- [ ] **U5** przy okazji: włącz LXR bez karty i pokręć gałką morph. Potem włóż kartę.

W 1.7:
- [ ] **H13** krok 1 (ekran `Sonic Potions` / `LXR2 Drums V 1.7`) i **U32** krok 2 (start bez
      `glo.cfg error`; **nie** zapisuj ustawień globalnych).
- [ ] Wczytaj P:
  - **U12** krok 5 (graj, nie ruszaj `prb`);
  - **U14** krok 5 (graj F, zapisz F);
  - **U18** krok 6 (graj, zapisz kit);
  - **U36** krok 2 (graj, zapisz wzorzec);
  - na końcu **U16** krok 8 (graj, zapisz projekt P).
- [ ] Wczytaj Twój projekt: **H13** krok 2 (gra jak zawsze). Ten projekt zostaje ostatnim użytym.

Z powrotem na 0.6 (zainstaluj `LXR_V2_LICHOMAT_0.6.img`):
- [ ] Start ładuje Twój projekt: **U32** krok 2 (`trg` pokazuje `blk`), potem `smp` i zapis ustawień
      globalnych.
- [ ] Wczytaj P: drugie połowy U16 krok 8, U14 krok 5, U12 krok 5, U18 krok 6 i U36 krok 2.

### 2.10 Opcjonalnie

- [ ] **X1–X3** (gniazda zegara), jeśli masz moduły i oscyloskop.
- [ ] **U8**: wymaga nagrań z v0.1, więc pomiń, chyba że chcesz zainstalować 0.1. B14 (PWM) jest
      poprawiony w 0.6 i zmienia tylko najwyższy wiersz tablicy (od 14 080 Hz); o B13 decydujesz na
      stronie odsłuchu, nie na LXR.

## Osobna, późniejsza sesja (po H12)

Te sprawdzenia zależą od ostatecznej wersji pogłosu, więc na dzisiejszym 0.6 nic nie znaczą: 0.6 nie ma
jeszcze poprawki offsetu DC pogłosu (E7.7). Pliki 0.4, 0.5 i 0.6 w `build/images/po-H12/` ją mają i są
gotowe już teraz, w folderze `polowa/` albo `lean/`, który wskazało P. Tę część możesz więc zrobić od
razu po H12, tego samego wieczoru, albo kiedy indziej. Nie czekasz na mnie ani na CI.

- **Na 0.4 z tego folderu (`build/images/README.txt`, punkt 5):**
  - U20: CPU z pogłosem, na ciężkim wzorcu i kicie zapisanym w części 1;
  - U21: pogłos i freeze;
  - U22: rumble;
  - U23 i U24: delay z tempem i filtr DJ. Pogłosu nie używają, ale lista każe testować 0.4 tylko na
    buildzie z ustaloną częstotliwością;
  - U25: zgodność kitu z 1.7, druga wycieczka do 1.7;
  - U26: przepisy kitów 0.4 (pogłos w trzech z czterech).
- **Na 0.5 z tego folderu:**
  - U30: CPU z pogłosem w ostatecznej częstotliwości;
  - U36 krok 3: wzorzec z 0.6 czytany przez 0.5. Przed tą sesją zapisz na 0.6 świeży wzorzec z `mic`,
    bo wzorce w P zapisał już 1.7.
- **Na 0.6 z tego folderu:** U37 (CPU z szablonami i `mic`, z pogłosem w ostatecznej częstotliwości).
- Do tego powtórka wszystkiego, co dziś nie przeszło.

## Co zapisać

- Wpisz każde sprawdzenie do run sheet na końcu listy (wystarczy też wiadomość albo zdjęcie): PASS, FAIL
  albo SKIPPED, data i wartości z **Record**. Przy każdym wyniku dopisz plik: 0.3 CI `d630eabc` albo 0.6
  lokalny `beefc513`, 0.4 `67ca9833` albo `e44de0b6`.
- H12: zdjęcie ekranu CPU w każdym kroku 2–5, z nazwą kitu, efektem i BPM; P wyślij od razu.
- **Nie musisz mierzyć w DAW.** Wyeksportuj nagranie każdego sprawdzenia z pomiarem czasu jako WAV
  (bez normalizacji, z częstotliwością nagrania) i nazwij je ID sprawdzenia (`H3.wav`, `U27-smp.wav`,
  `U27-blk.wav`, `U33-1.wav` …). Uderzenia co do próbki zmierzę sam. Okno MIDI Monitor zapisz (File >
  Save) albo zrób zrzut ekranu pod tą samą nazwą.
- Przy FAIL: co słychać albo widać i w którym kroku. Wyniki z 0.6 liczą się dla funkcji z 0.1–0.5. Gdy
  coś z 0.1–0.3 nie przejdzie, możesz to powtórzyć na 0.3, a timing z 0.5 z `trg blk`, żeby wiedzieć,
  gdzie się zaczęło. Nie jest to konieczne.

## Jeśli coś pójdzie nie tak

- **Najpierw kopia karty**, przed pierwszą instalacją (część 1). Bez niej nie zaczynaj.
- **Aktualizacja się nie udała** albo ekran stoi: wyłącz i zainstaluj jeszcze raz. Updater nigdy się
  nie zmienia.
- **LICHOMAT zachowuje się źle** (zawiesza się, szumi, nie zapisuje): wróć do 1.7 jak w H13, plikiem
  `build/images/fw-1.7/LXR_V2_V1.7.img` (`cdb1ca37…`; opis w `build/images/README.txt`), i napisz mi,
  co się stało i przy którym sprawdzeniu.
- **Pliki na karcie zepsute:** wyłącz LXR, w komputerze usuń zawartość karty i skopiuj z powrotem całą
  kopię.
- Przy U3 kroku 6 ścisz głośność. Kroku 5 z U3 nie rób na 1.7, bo może uszkodzić pliki na karcie.
- Sprawdzenie nie przechodzi: zapisz, co widzisz, i idź dalej; nie szukaj obejścia na siłę.
