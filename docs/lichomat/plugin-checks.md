<!--
  LICHOMAT: sprawdzenia prototypu wtyczki w Abletonie (tracker E9.21, P1–P8).

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
