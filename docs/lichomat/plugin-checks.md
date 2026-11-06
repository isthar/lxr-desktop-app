<!--
  LICHOMAT: sprawdzenia wtyczki w Abletonie (tracker E9.21, P1–P8; E9.41, P9–P10).

  Copyright (C) 2026 Tomasz Krzywicki
  SPDX-License-Identifier: GPL-3.0-only

  Licensed under the GNU General Public License version 3, with the additional
  permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
  the repository.
-->
# LICHOMAT: prototyp wtyczki w Abletonie (P1–P8)

| | |
|---|---|
| Wersja | gałąź `plugin`, `bfad91d` (2026-11-06), prototyp E9.2 |
| Co to jest | Nieoficjalna emulacja firmware LICHOMAT jako instrument VST3. To nie jest produkt Erica Synths i nie ma z nimi związku. Silnik działa w osobnym procesie `lichomat-engine` dla każdej instancji |
| Czego nie ma | Własnego okna (tylko ogólne parametry Live'a), AU, osobnych wyjść, mapowania CC i Program Change. To następne etapy (szkic `docs/superpowers/specs/2026-11-05-plugin-draft.md`, §4) |
| Wymagania | Mac z Apple Silicon, macOS 13 lub nowszy, Live 12 uruchomiony natywnie (nie przez Rosettę: wtyczka jest tylko na arm64) |
| Czas | ok. 20 min |

## Przed sprawdzeniami

1. **Instalacja.** W Terminalu:

   ```bash
   cd /Users/tk/Code/lxr/build/wt-plugin && plugin/tools/install_plugin.sh
   ```

   - Przed instalacją zamknij Live. Skrypt odmówi, jeśli działa jakiś `lichomat-engine`.
   - Skrypt buduje wersję Release i sprawdza ją w folderze tymczasowym: podpis (ad hoc) i walidator Steinberga. Na końcu ma być `47 tests passed, 0 tests failed`.
   - Dopiero potem kopiuje `LICHOMAT.vst3` do `~/Library/Audio/Plug-Ins/VST3`. Jeśli coś się nie uda, napisze `FAILED`, a poprzednia wersja zostanie nietknięta. Wtedy przyślij mi plik `build/plugin-release.install.log`.
   - Jeśli wtyczka nie ma jeszcze karty startowej, kopiuje kartę „init” do `~/Library/Application Support/LICHOMAT Plugin/Card`. Na pierwszym wzorcu (tym, który gra po starcie) wszystkie siedem głosów gra na krokach 1, 5, 9 i 13, czyli na każdą ćwierćnutę.
   - Odinstalowanie: `plugin/tools/install_plugin.sh --uninstall`. Usuwa tylko wtyczkę; folder `~/Library/Application Support/LICHOMAT Plugin` (karta startowa, logi) usuwasz ręcznie.
2. **Live:**
   - w Ustawieniach → Audio 48 kHz i bufor 128 albo 256 próbek;
   - w menu Options włączone „Delay Compensation”.
3. **Opóźnienie wtyczki przy 48 kHz:**
   - bufor 128: 450 ramek, 9,4 ms;
   - bufor 256: 578 ramek, 12,0 ms.

   Live je wyrównuje przy odtwarzaniu. Grając na żywo z klawiatury, je usłyszysz.

## Sprawdzenia

Każdy punkt: co zrobić, co powinno się stać, co zapisać.

| # | Co zrobić | Co powinno się stać | Zapisz |
|---|---|---|---|
| **P1** (2 min) | Ustawienia → Plug-Ins: włącz „Use VST3 Plug-In System Folders”, potem „Rescan”. W przeglądarce: Plug-Ins → VST3 → LICHOMAT. | Jest „LICHOMAT” od „LICHOMAT”. Live nie zgłasza błędu. | Czy jest; komunikat Live'a, jeśli był. |
| **P2** (2 min) | Przeciągnij LICHOMAT na nową ścieżkę MIDI. Otwórz Monitor aktywności i wpisz `lichomat`. | Urządzenie ładuje się bez własnego okna. W ciągu sekundy pojawia się proces `lichomat-engine`. | Czy się załadowało; czy proces jest. |
| **P3** (3 min) | Klip MIDI na 1 takt: nuta **C2** (MIDI 48) na każdą ósemkę „i” (2., 4., 6. i 8. ósemka); Live wysyła nuty klipu na kanale 1. Uruchom klip. | Między ćwierćnutami sekwencera karty słychać uderzenia z klipu: wszystkie głosy karty init naraz (każdy słucha nuty 48 na kanale 1). Równo z metronomem. | Czy gra; czy równo. |
| **P4** (4 min) | Transport od 1.1.1 przy 120 BPM. W trakcie zmień tempo na 140. Zatrzymaj i uruchom od taktu 3 (kliknięcie w linijkę). Na koniec pętla 1 takt w aranżacji, kilka przejść. | Sekwencer gra na ćwierćnuty razem z metronomem. Po zmianie tempa od razu gra w nowym tempie, dalej równo z metronomem. Od taktu 3 gra od razu we właściwym miejscu. W pętli każde przejście zaczyna się od „raz”. Na karcie init nic nie ginie. Na własnej karcie przy przeskoku pętli może zginąć uderzenie przesunięte przed „raz” (mikrotiming, swing): przeskok to stop, pozycja i continue (znane ograniczenie, poprawi je tryb „host”, E9.3). | Co słychać przy każdej z czterech rzeczy; czy coś się rozjeżdża w czasie. |
| **P5** (2 min) | Dodaj drugą ścieżkę MIDI z drugim LICHOMAT. | Dwa procesy `lichomat-engine`. Obie ścieżki grają, każda swoją kopią karty. | Czy obie grają. |
| **P6** (3 min) | Zapisz set, zamknij Live, otwórz set. | Obie ścieżki grają jak przed zapisem (karta jest zapisana w secie). | Czy grają; ile trwało otwarcie. |
| **P7** (3 min) | Na ścieżce audio z wejściem „Resampling” nagraj 2 takty. Potem zrób „Freeze” ścieżki LICHOMAT i odtwórz ją jak wcześniej. | Nagrane uderzenia leżą na siatce, bo opóźnienie wtyczki jest skompensowane. Zamrożona ścieżka gra to samo, bo przy zamrażaniu wtyczka czeka na silnik. | Czy na siatce (jeśli nie: o ile ms przesunięte); czy freeze gra. |
| **P8** (2 min) | Usuń urządzenie i cofnij (Cmd+Z). Potem dodaj nowe. Opcjonalnie, przy grającym transporcie: w Monitorze aktywności zaznacz `lichomat-engine`, kliknij ✕ na pasku i wybierz „Wymuś koniec”. | Po cofnięciu wraca i gra; nowe też gra. Po zabiciu silnika jest ok. 1 s ciszy, potem perkusja wraca w rytm. Live działa dalej. | Co się stało. |

## Jeśli coś nie gra albo trzeszczy

- **Logi.** Każda instancja zapisuje log w `~/Library/Application Support/LICHOMAT Plugin/logs/<id>.log` (w Finderze: Idź → Idź do folderu…, Cmd+Shift+G, i wklej ścieżkę):
  - start i koniec każdego silnika (kod wyjścia albo sygnał i powód);
  - przy końcu silnika: liczba niedoborów próbek i spóźnionych zdarzeń.

  Liczniki trafiają do logu dopiero, gdy silnik się kończy, więc przed wysłaniem zamknij set (albo usuń urządzenie). Log zostaje po zamknięciu setu; trzymane są 32 najnowsze. Prześlij mi najnowsze pliki, po jednym na instancję.
- **Brak dźwięku w P3–P4.** Sprawdź w Monitorze aktywności, czy działa `lichomat-engine`, i zajrzyj do logu.
- **Karta.** Nowa instancja bierze kartę z `~/Library/Application Support/LICHOMAT Plugin/Card` (kopiuje ją). Bez niej bierze kartę LICHOMAT Emu, a bez obu startuje na karcie init bez wzorców.
- **Silnik wciąż pada.** Po 5 awariach w ciągu minuty, albo 3 zawieszeniach w ciągu 10 minut, wtyczka się poddaje i milknie. Live działa dalej. Wtedy przyślij log.

## E9.4: dwa wyjścia i parametry (P9–P10)

| | |
|---|---|
| Wersja | gałąź `plugin-v07`, `d0534b1` (2026-11-06), wtyczka 0.2.0 z firmware LICHOMAT 0.7 |
| Co nowego | Dwa wyjścia stereo „Out 1” i „Out 2”, jak dwa gniazda urządzenia. 36 parametrów do automatyki: wzorzec; głośność, panorama, filtr, decay i wyjście każdego głosu (hi-hat osobno zamknięty i otwarty); filtr DJ; poziom i decay pogłosu; ilość FX. Wartości, które firmware zmienia samo (np. kit wczytany razem ze wzorcem), Live pokazuje sam |
| Czego nie ma | Shuffle i rozmiaru pogłosu (firmware zmienia je tylko z własnego menu), AU, własnego okna. CC i Program Change z klipów MIDI nadal nie trafiają do firmware; parametry tak |
| Czas | ok. 15 min |

### Przed sprawdzeniami

1. **Instalacja nowej wersji.** Zamknij Live. W Terminalu:

   ```bash
   cd /Users/tk/Code/lxr/build/wt-plugin-v07 && plugin/tools/install_plugin.sh
   ```

   - Na końcu ma być `47 tests passed, 0 tests failed`. Jeśli skrypt napisze `FAILED`, przyślij mi `build/plugin-release.install.log`.
   - Nowa wersja zastępuje poprzednią: w Live to ta sama wtyczka, sety z P1–P8 otwierają się z nią (z wartościami z karty).
   - Karta startowa zostaje: `~/Library/Application Support/LICHOMAT Plugin/Card`, karta „init”. Wszystkie głosy grają na wyjściu 1, a na pierwszym wzorcu na krokach 1, 5, 9 i 13.
2. **Live** jak w P1–P8: 48 kHz, bufor 128 albo 256 próbek, w menu Options włączone „Delay Compensation”.
3. **Parametry w Live.** Wtyczka nie ma okna, więc Live pokazuje jej parametry jako suwaki: rozwiń je trójkątem w nagłówku urządzenia LICHOMAT. Każdy parametr jest też na liście automatyki ścieżki (w aranżacji: przycisk „A”, potem wybór urządzenia LICHOMAT i parametru). Wartości są takie, jak na wyświetlaczu urządzenia: panorama −63..64, wzorzec 1–64, wyjście głosu St1, St2, L1, R1, L2, R2 albo FX.

### Sprawdzenia

| # | Co zrobić | Co powinno się stać | Zapisz |
|---|---|---|---|
| **P9** (6 min) | Ścieżka MIDI z LICHOMAT, transport gra (sekwencer karty, cztery ćwierćnuty). (a) Parametr **Drum 1 Output** ustaw na **St2**. (b) Dodaj ścieżkę audio; w jej „Audio From” wybierz ścieżkę LICHOMAT, a pod spodem **Out 2**; Monitor „In”. (c) Usuń tę ścieżkę audio. (d) Drum 1 Output z powrotem na **St1**. | (a) Nic się nie zmienia: dopóki Live nie bierze dźwięku z Out 2, ścieżka LICHOMAT gra oba wyjścia razem. (b) Drum 1 gra już tylko na nowej ścieżce, a ścieżka LICHOMAT bez niego (wskaźniki poziomu obu ścieżek). (c) Drum 1 wraca na ścieżkę LICHOMAT albo milknie (zależy od tego, czy Live wyłącza wtedy Out 2). (d) Drum 1 gra na ścieżce LICHOMAT, Out 2 nic. | Co słychać po (a), (b), (c) i (d). Najważniejsze (a): jeśli Drum 1 zniknął już wtedy, Live włącza Out 2 od razu i trzeba to zmienić we wtyczce. |
| **P10** (10 min) | W aranżacji na ścieżce LICHOMAT, transport od taktu 1: (a) automatyka **Drum 1 Volume** od 127 na początku taktu 1 do 0 na początku taktu 3, dalej 0; odtwórz. (b) Bez automatyki przesuń ręką **Reverb Level** na ok. 100 i **DJ Filter** w lewo, potem w prawo. (c) Automatyka **Pattern**: na trzeciej ćwierćnucie taktu 3 wartość 2, na trzeciej ćwierćnucie taktu 6 znów 1; odtwórz od taktu 1. (d) „Freeze” ścieżki i odsłuch. (e) Usuń automatykę Drum 1 Volume, ustaw ręką **Hi-hat Volume** na 40, zapisz set, zamknij Live, otwórz set. | (a) Drum 1 cichnie płynnie razem z linią automatyki. (b) Pogłos słychać; filtr DJ zamyka górę albo dół. (c) Od taktu 4 gra wzorzec 2 (na karcie init sam Drum 1 na ćwierćnuty), od taktu 7 znów wzorzec 1: firmware zmienia wzorzec na końcu grającego, jak z Program Change na urządzeniu. Razem ze wzorcem wczytuje jego kit, więc parametry wracają do wartości kitu i Live dostaje je od wtyczki: suwak Reverb Level wraca na 0, a Drum 1 Volume na 127, choć jego automatyka z (a) stoi na 0 (Drum 1 znów głośno). (d) Zamrożona ścieżka gra to samo co (c). (e) Hi-hat Volume pokazuje 40 i hi-hat gra ciszej, jak przed zapisem. | Czy (a)–(e) jak opisano. Przy (c): czy Drum 1 po zmianie wzorca gra głośno; czy przy Drum 1 Volume zapaliła się w Live czerwona lampka (automatyka nadpisana) albo przycisk „Re-Enable Automation”; i co wolisz po wczytaniu kitu: wartość z kitu (jak teraz, jak na urządzeniu) czy ostatnią z automatyki Live'a. |

Jeśli parametr nie działa: w logu instancji (`~/Library/Application Support/LICHOMAT Plugin/logs/<id>.log`, jak w P8) jest linia `parameters: MIDI channel …, receive filter: …`. Na karcie init: `MIDI channel 1, receive filter: CC on, Program Change on`. Przyślij mi tę linię z logiem.

Znane ograniczenie: przy eksporcie, bounce'ie albo freeze z nowo wstawionej wtyczki (nowy silnik offline) wartości z seta dochodzą do firmware w pierwszej ramce, razem ze startem transportu. Jeśli jakiś parametr mocno różni się od wartości z kitu (np. głos ściszony do 0), nuta dokładnie w tej ramce może przez 2–3 ms zagrać z wartością z kitu. Jeśli to usłyszysz, zapisz przy P10 (d).
