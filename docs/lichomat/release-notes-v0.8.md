# LICHOMAT v0.8: notatki do wydania (szkic)

> **Szkic.** Nieopublikowany. Stanie się tekstem wydania GitHub `v0.8` w `isthar/lxr-desktop-app`, gdy wyjdzie wydanie 0.7, gdy przejdziesz na LXR sprawdzenia U42–U45 z listy `docs/lichomat/hardware-checklist.md` i zgodzisz się na wydanie. Gałąź `v0.8` jest na forku; nic nie jest otagowane ani wydane. Żadne sprawdzenie na urządzeniu nie jest jeszcze zrobione: wszystko poniżej opisuje, jak v0.8 ma działać według testów na hoście (138) i w emulatorze (360). Pogłos gra w tym buildzie w tymczasowej częstotliwości szkicu 0.4 (połowa), jak w 0.7. SHA-256 pliku aktualizacji dojdzie, gdy CI zbuduje commit wydania. Tekst wydania to część od „LICHOMAT to nieoficjalny firmware” do końca „Licencja”; dalsze sekcje są tylko dla Ciebie.

LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest robiony, zatwierdzony ani wspierany przez Erica Synths. Wersja 0.8 bazuje na v0.7, ta na v0.6, ta na v0.5, ta na v0.4, ta na v0.3, ta na v0.2, ta na v0.1, a v0.1 na oficjalnym firmware 1.7 (`erica-synths/lxr-desktop-app`, commit `a43cd4f`); wszystko, co dały v0.1–v0.7, zostaje. v0.8 robi z LXR groovebox do minimal groove techno: **generator wzorców** pisze w kilka sekund ghosty hihatów, ghost kicki, figury perkusji, rzadkie uderzenia na granicach fraz i przeniesienie rytmu z jednej ścieżki na drugą. Pisze zwykłe kroki, więc wynik poprawiasz, zapisujesz i grasz jak każdy inny wzorzec.

**Ważne:** ghosty grają ciszej tylko z włączonym `vol` głosu (velocity → głośność). Generator sam je włącza, gdy pisze ciche kroki, ale `vol` należy do kitu: zapisz też kit.

## Instalacja

Przed instalacją skopiuj całą kartę SD na komputer: to nieoficjalny firmware.

1. Skopiuj `LXR_V2_LICHOMAT_0.8.img` do głównego folderu karty bez zmiany nazwy (updater instaluje tylko pliki, których nazwa zaczyna się od `LXR` i kończy na `.img`) i usuń stamtąd każdy inny plik `LXR*.img`: updater instaluje pierwszy, który znajdzie.
2. Wyłącz LXR, włóż kartę, przytrzymaj przycisk enkodera i włącz LXR. Trzymaj, aż pojawi się `LXR Updater`.
3. Poczekaj na `Update OK!`; LXR uruchomi się sam. Ekran startowy pokazuje `LICHOMAT` i `v0.8 unofficial`.

## Powrót do v0.7, v0.6, v0.5, v0.4, v0.3, v0.2, v0.1 albo do oficjalnego firmware

Aktualizacja nigdy nie zmienia updatera, więc zawsze można wrócić. Usuń z karty `LXR_V2_LICHOMAT_0.8.img` i zainstaluj tak samo `LXR_V2_LICHOMAT_0.7.img` (ekran startowy `v0.7 unofficial`), starszą wersję LICHOMAT (pliki z run sheet listy sprawdzeń) albo oficjalny `LXR_V2_V1.7.img` od Erica Synths (ekran startowy `Sonic Potions` i `LXR2 Drums V 1.7`).

v0.8 nie zmienia żadnego formatu pliku. Wszystkie wersje wczytują projekty, wzorce, kity i `glo.cfg` zapisane przez v0.8 bez błędów. Wygenerowany wzorzec to zwykłe kroki: v0.2–v0.7 grają go tak samo jak v0.8. Firmware 1.7 gra kroki z warunkiem (`1:2`, `2:2`, `4:4`, `1:4`, których używają `var` i `rar`) w każdej pętli, jak kroki na 100 %.

## Nowe

### Generator wzorców: `typ sty den lev | var not sed und | src shf`

- **Gdzie:** strona ścieżki, dwa nowe ekrany za `vtp vdp sav`: enkoder dalej za `sav` albo SHIFT + drugi przycisk trybu jeszcze raz. Ekran `typ sty den lev`, potem `var not sed und`, potem `src shf` (gałki 1–4). Ustawienia dotyczą wybranego głosu.
- **Co robi:** wpisuje kroki do ścieżki wybranego głosu w pokazanym wzorcu. Zawsze 64 kroki (4 takty); na krokach, które pisze, czyści p-locki, flam, shift i fine, a pozostałe kroki ścieżki wyłącza.
- **Na żywo:** dopóki `typ` nie jest `---`, każda zmiana parametru od razu przelicza ścieżkę, także w trakcie grania, bez przerwy w dźwięku. Ręczne poprawki tej ścieżki znikają przy następnej zmianie, więc **po poprawkach ustaw `typ` na `---`**. Ustawienie parametrowi wartości, którą już ma (np. gałka oparta o koniec zakresu), nic nie robi.
- **`typ`:** `---` (wyłączony), `hat`, `sub`, `prc`, `rar`, `prp`; w trybie edycji długa nazwa (`hat ghosts`, `ghost kick`, `percussion`, `rare hits`, `propagate`). Wybór `typ` wczytuje ustawienia domyślne rodzaju do `sty den lev var not`.
- **`sty`** styl rodzaju, **`den`** gęstość (ghosty albo akcenty na takt, zdarzenia na 16 taktów; w `sub` tylko styl 8), **`lev`** −30…0 dB: poziom ghostów pod akcentem (−15 dB to velocity 23), **`var`** −4…+4: zmiana na końcu frazy (uderzenia, które grają tylko w taktach 5–8 albo tylko w takcie 8 frazy, albo milkną tam), **`not`** ±24 półtonów, **`sed`** 0–99: ziarno (to samo ziarno to ten sam wzorzec; kręcąc nim, przeglądasz warianty), **`src`** ścieżka źródłowa (stopa dla `sub`, `prc` i `rar`, źródło dla `prp`), **`shf`** ±8 kroków przesunięcia kopii.
- **`und`:** cofa ścieżkę do ostatniego zapisu albo wczytania wzorca: przekręć enkoder na `und`, naciśnij, przekręć (`push: undo`), naciśnij. Działa na wzorcu, który gra.
- **Ustawienia** należą do ścieżki, są tylko w pamięci i się nie zapisują; zapisują się kroki.

| `typ` | Style | Domyślnie (`sty den lev var not`) |
|---|---|---|
| `hat` ghosty hihatu | 0 Twoje akcenty, 1 klasyczny (akcenty na offbeatach, ghosty na parzystych szesnastkach), 2 gęsty, 3 ćwierćnuty cicho, 4 rozsypany | 1, 7, −15, +1, 0 |
| `sub` ghost kicki | 1 `...g..G....g..G.`, 2 `...........g..G.`, 3 `......g.......G.`, 4 `...g.......G....`, 5 `.......g.......G`, 6 `...g..gg...g..GG`, 7 dwutaktowy (w drugim takcie dochodzą 10 i 16), 8 z ziarna (`den` uderzeń z kroków 4, 7, 12, 15, 8, 16, 10, 14); g = ghost, G = akcent, nigdy na krokach stopy | 1, 3, −8, 0, 0 |
| `prc` figury perkusji | 0 Twoje akcenty, 1 synkopy, 2 offbeat, 3 3+3+2, 4 losowy; figura dwutaktowa z ghostami krok przed akcentem | 1, 3, −13, +1, 0 |
| `rar` rzadkie uderzenia | 1 koniec frazy (takty 8 i 16; od `den` 3 z jąkaniem), 2 początek frazy, 3 posyp z ziarna, 4 mieszany | 1, 2, 0, 0, 0 |
| `prp` przeniesienie rytmu | 1 kopia przesunięta o `shf`, 2 dopełnienie, 3 przedtakty, 4 echo jako ghosty | 1, 4, −15, 0, 0 |

- **Ghost kicki** gra osobna ścieżka z krótkim, stopopodobnym głosem (np. Drum 2), a stopa zostaje na swojej. Kity z laboratorium brzmień (sloty 52–55 na gałęzi `kits`) mają taki głos.
- **Fraza:** ścieżka 64-krokowa to jedno przejście warunku, więc krok w takcie 4 z `2:2` gra w takcie 8 i 16, a z `4:4` tylko w takcie 16. Licznik zeruje się przy START i zmianie wzorca.

## Zgodność

- Formaty wzorców, kitów, projektów, piosenek i `glo.cfg` się nie zmieniają.
- Generatora nie da się sterować przez MIDI ani NRPN, tylko z menu. Żaden istniejący numer parametru się nie zmienia, więc kity, NRPN i morph zostają jak były.
- Wygenerowany wzorzec grają v0.2–v0.7 tak samo; firmware 1.7 gra kroki z warunkiem w każdej pętli.

## CPU i pamięć

- Generator liczy tylko wtedy, gdy zmienia się jego parametr, w pętli głównej, nie w ścieżce dźwięku. Zapis 64 kroków idzie krok po kroku, każdy przy krótko zablokowanych przerwaniach, a długość ścieżki na końcu, więc sekwencer nigdy nie gra połowy starej i połowy nowej ścieżki. Koszt dźwięku i sekwencera w trakcie grania jest taki sam jak w 0.7.
- Pamięć: DTCM + RAM 470 268 z 471 032 B (+80 B wobec v0.7), DTCM 107 396 z 107 900 B, RAM 362 872 z 389 120 B, ITCM 13 744 z 16 384 B (bez zmian). Stos generatora: najwyżej 696 B (`seq_generateTrack`, w tym rdzeń 456 B), w pętli głównej.

## Zmienione zachowanie

- Strona ścieżki ma ekrany 5–7 generatora; enkoder idzie dalej za `sav`, a SHIFT + drugi przycisk trybu przechodzi przez siedem ekranów zamiast czterech.
- `sav` (zapis własnego szablonu z 0.7) działa tylko z trybu edycji: przekręć na `sav`, naciśnij, przekręć na slot, naciśnij. Wartość zostawiona na `sav` nie zapisze się sama.
- Ekran startowy pokazuje `LICHOMAT` i `v0.8 unofficial`.

## Bez zmian

- Wszystko z v0.1–v0.7. Bez generatora v0.8 gra bit w bit jak v0.7: w emulatorze wszystkie 41 złotych renderów v0.7 są bez zmian.

## Znane ograniczenia

- Generator pisze jedną ścieżkę naraz: nie ma jeszcze „całego groove'u jednym ruchem” ani wariantów w kolejnych wzorcach.
- Fraza ma najwyżej 16 taktów (warunki `x:4` na ścieżce 64-krokowej).
- Stopę generator czyta z pierwszego taktu ścieżki `src` i ten układ obowiązuje we wszystkich czterech taktach: przy stopie o długości niedzielącej 16 (np. 12 kroków) uderzenia mogą w taktach 2–4 trafić na stopę.
- Niezapisane kroki z generatora znikają przy zmianie wzorca, jak każda edycja w 1.7: zapisz wzorzec, zanim go zmienisz. Ścieżka 64-krokowa wydłuża wzorzec do czterech taktów, więc zmiana wzorca czeka do końca takiej pętli.
- Autoładowany kit może przywrócić `vol` wyłączone: wtedy ghosty grają pełną głośnością. Zapisz kit po generowaniu.
- Pogłos gra w połowie częstotliwości, jak 0.4–0.7 (decyzja Q34).
- Ograniczenia v0.1–v0.7 zostają.

## Pliki

- `LXR_V2_LICHOMAT_0.8.img`: plik aktualizacji na kartę SD, zbudowany przez CI z commitu otagowanego `v0.8`, SHA-256: (wpisany, gdy CI zbuduje commit wydania; ten sam co w run sheet listy sprawdzeń `docs/lichomat/hardware-checklist.md`).
- Kod źródłowy: tag `v0.8` w `isthar/lxr-desktop-app` (archiwa „Source code” wydania).

## Licencja

LICHOMAT to wolne oprogramowanie na licencji GNU General Public License w wersji 3 (GPL-3.0-only), z dodatkowym zezwoleniem i warunkami z NOTICE.md; pełny kod źródłowy jest w repozytorium. Nie ma żadnej gwarancji: instalacja jest na własne ryzyko, a Erica Synths go nie wspiera. „Erica Synths”, „LXR” i „LXR-02” to znaki towarowe Erica Synths. Są tu użyte tylko po to, by nazwać Erica Synths, urządzenie, dla którego jest ten firmware, jego updater, jego oficjalny firmware i repozytorium tego firmware (`erica-synths/lxr-desktop-app`, którego forkiem jest `isthar/lxr-desktop-app` i którego nazwę zachowuje), oraz w nazwach plików aktualizacji, które muszą zaczynać się od `LXR`, bo updater instaluje tylko pliki `LXR*.img`.

## Jak to sprawdzono w emulatorze (tylko dla Ciebie)

Gałąź `v0.8`, head `85cd7f0`, ostatnia zmiana firmware `3cebde7`; szczegóły w opisach commitów i w „Changes during execution” planu `docs/superpowers/plans/2026-11-11-v0.8-groove-generator.md`. Lokalny build daje `.bin` o SHA-256 `de3458eb…`, 359 020 B (v0.7: 343 996 B); plik z CI będzie inny (inna kolejność linkowania newlib), jak przy wcześniejszych wersjach.

- **Prototyp jako wzorzec:** generator najpierw powstał w Pythonie (`tools/gen/gen.py`, 74 testy), a firmware jest jego portem. 260 wektorów (każdy rodzaj, styl i koniec zakresu) zgadza się bajt w bajt, a losowe porównania prototypu z kodem C dały 0 różnic w 72 000 przypadkach (22 000 po rdzeniu, 50 000 po ghost kickach).
- **Testy:** 138 na hoście (135 w v0.7), wszystkie przechodzą pod UBSan i pod ASan + UBSan; 360 w emulatorze (353) pod UBSan i 331 (324) pod ASan + UBSan, bez aplikacji. Trzy testy aplikacji, które używają głośnika Maca, padały w jednym przebiegu, bo CoreAudio nie oddawało dźwięku (build v0.7 tak samo); w następnym przeszły.
- **Złote rendery:** wszystkie 41 z v0.7 bez zmian, żaden nie został zapisany od nowa.
- **Generator od początku do końca:** `V08_generator` (stopa, hihat i `rar` przez parametry, 16 taktów: rzadkie uderzenie tylko w taktach 8 i 16, hihat 11 uderzeń na takt w taktach 1–4 i 12 w taktach 5–8, ghosty na velocity 16–28), `V08_persist` (zapis i ponowne wczytanie grają te same nuty, ghost kicki na 51 i 127), `V08_ui`, `V08_armed`, `V08_vol`, `V08_menu`, `V08_screensaver` (ekrany, długie nazwy, `und`, `sav` tylko z trybu edycji, `vol` bez zapisu jako automatyka).
- **Losowe przebiegi `tools/emu_fuzz.py`,** teraz także z dziesięcioma parametrami generatora na wartościach 0–255: 1 500 renderów (sekwencer w obu ustawieniach `trg`, pod UBSan i pod ASan + UBSan, dźwięk, MIDI), bez żadnego znaleziska.
- **Stos:** zmierzony kompilatorem (`-fstack-usage`), wszystko statyczne: `patternGen_generate` 456 B, `seq_generateTrack` 696 B, `seq_genUndo` 608 B, limit planu 768 B.
- **Przeglądy:** zgodność z projektem i jakość po rdzeniu, po spięciu z sekwencerem i po menu, potem przegląd całości. Prawdziwe usterki, poprawione test najpierw: wyścig zapisu przy zmianie wzorca w trakcie generowania, `sav`/`und` uzbrojone poza trybem edycji, `vol` nagrywany jako automatyka, ta sama wartość na nowym wzorcu, która nic nie generowała.
- **CI na GitHubie:** bez przebiegu dla `v0.8`; GitHub nie uruchamia workflowów w tym repozytorium od 31.10 (tracker E8.5).

## Czego posłuchać (tylko dla Ciebie)

Strona do odsłuchu (wersja 17), sekcja „Generator”: przykłady każdego rodzaju i stylu, policzone prototypem i zagrane przez firmware w emulatorze, z siatką kroków pod każdym. Ghost kicki grają głosem A2 z laboratorium brzmień, a „Cały groove” to 32 takty, w których poza stopą wszystko jest z generatora. Pliki są też w `/Users/tk/Code/lxr/build/wt-v08/build/demo/gen/` (poza gitem, na Twoim Macu), robi je `tools/gen/examples.py`.

## Co sprawdzić na LXR (tylko dla Ciebie)

Procedury są w `docs/lichomat/hardware-checklist.md`, wyniki wpisz do run sheet na jej końcu. Sprawdzeń v0.1–v0.7 nie trzeba powtarzać; U42–U45 idą po sprawdzeniach 0.7 i potrzebują tylko LXR.

- **U42:** ghosty hihatu: ekrany, zmiany na żywo (`sed`, `den`, `lev`, `var`), ta sama wartość nic nie robi, `und`.
- **U43:** ghost kicki na głosie 2 (wszystkie 8 stylów, `var` ±), figury `prc`, przeniesienie `prp`.
- **U44:** rzadkie uderzenia przez 16 taktów: w których taktach grają.
- **U45:** zapis, wyłączenie, inne wersje firmware (0.7 i 1.7), CPU przy szybkim kręceniu `sed`.

## Publikacja (tylko właściciel)

1. Najpierw wydanie 0.7 (które idzie po wydaniach 0.4, 0.5 i 0.6). Build gałęzi `v0.8` przechodzi U42–U45; jeśli któreś nie przejdzie, najpierw poprawka kodu i to sprawdzenie jeszcze raz.
2. Zrób z tego pliku tekst wydania: usuń notkę o szkicu, ` (szkic)` w tytule i sekcje „tylko dla Ciebie”, a w „Znanych ograniczeniach” punkt o pogłosie, jeśli wydanie 0.4 ustaliło już częstotliwość. Ten plik leży na `timing-core`; skopiuj go na gałąź `v0.8` jako `docs/lichomat/release-notes-v0.8.md`, zrób commit i push `v0.8` na `isthar/lxr-desktop-app`. Ten commit wydajesz. Poczekaj, aż jego przebiegi CI (`.github/workflows/build.yml` i `emulator.yml`) przejdą.
3. Pobierz artefakt `lichomat-firmware-<commit>` tego przebiegu i rozpakuj go. Podsumowanie joba podaje SHA-256 pliku aktualizacji. Jeśli to SHA-256 z „Pliki” (plik z run sheet), to plik sprawdzony na LXR: idź dalej. Jeśli nie, to inny plik: gdy po commicie z run sheet zmieniły się źródła firmware, zainstaluj plik z CI, powtórz sprawdzenia, których te zmiany dotyczą, i wpisz je do run sheet; gdy zmieniły się tylko dokumenty, najpierw znajdź przyczynę. Potem wpisz nowy SHA-256 w „Pliki” i w run sheet, zrób commit i push.
4. Otaguj ten commit `v0.8`, wypchnij tag i utwórz z niego wydanie GitHub `v0.8` z tytułem `LICHOMAT v0.8`, z tekstem od „LICHOMAT to nieoficjalny firmware” do końca „Licencja” i z dołączonym `LXR_V2_LICHOMAT_0.8.img` z artefaktu.
