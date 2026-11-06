<!--
  LICHOMAT: karta testu wtyczki VST3 0.2.0 w Abletonie dla testera spoza repozytorium
  (tracker E8.12–E8.13; te same sprawdzenia co docs/lichomat/plugin-checks.md, instalacja z wydania).

  Copyright (C) 2026 Tomasz Krzywicki
  SPDX-License-Identifier: GPL-3.0-only

  Licensed under the GNU General Public License version 3, with the additional
  permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
  the repository.
-->
# LICHOMAT VST3 0.2.0: test w Abletonie

Dzięki za pomoc. Ta karta prowadzi przez test krok po kroku. Na końcu jest wzór odpowiedzi do skopiowania i odesłania.

| | |
|---|---|
| Co to jest | LICHOMAT to nieoficjalny firmware dla Erica Synths LXR-02. Nie jest produktem Erica Synths, nie jest przez nich zatwierdzony i nie ma z nimi związku. Wtyczka uruchamia to firmware jako instrument VST3; każda instancja ma własny proces `lichomat-engine` |
| Wersja | wtyczka 0.2.0 (prototyp) z wydania [emu-0.7-vst3-0.2.0](https://github.com/isthar/lxr-desktop-app/releases/tag/emu-0.7-vst3-0.2.0) |
| Wymagania | Mac z Apple silicon (M1 lub nowszy), macOS 13 lub nowszy, Live 12 uruchomiony natywnie (nie przez Rosettę) |
| Czego nie ma | Własnego okna (Live pokazuje tylko suwaki parametrów), AU, mapowania CC i Program Change z klipów MIDI |
| Czas | ok. 45 min |

## 1. Instalacja (5 min)

1. Zamknij Live.
2. Z wydania pobierz `LICHOMAT-VST3-0.2.0-macOS-arm64.zip` i rozpakuj. `LICHOMAT.vst3` skopiuj do `~/Library/Audio/Plug-Ins/VST3`. W Finderze: Idź → Idź do folderu… (Cmd+Shift+G) i wklej tę ścieżkę.
3. Wtyczka jest podpisana ad hoc i nie jest notaryzowana. W Terminalu zdejmij jej kwarantannę:

   ```bash
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3
   ```

4. **Karta startowa.** Bez niej wtyczka startuje z pustymi wzorcami i sekwencer nic nie gra. Wklej do Terminala te trzy linie. Pobierają 5 małych plików karty „init” z repozytorium, z tego samego tagu, w kolejności, w jakiej czyta je firmware:

   ```bash
   B="$HOME/Library/Application Support/LICHOMAT Plugin"; U="https://raw.githubusercontent.com/isthar/lxr-desktop-app/emu-0.7-vst3-0.2.0/emu/tests/cards/init/PROJ00"; N="$B/.Card.new/PROJ00"
   [ ! -e "$B/Card" ] && curl -fsS --fail-early --create-dirs -o "$N/00-.PAT" "$U/00-.PAT" -o "$N/00-INITK.SND" "$U/00-INITK.SND" -o "$N/01-.PAT" "$U/01-.PAT" -o "$N/GLO.CFG" "$U/GLO.CFG" -o "$N/PRJ.NFO" "$U/PRJ.NFO" && mv "$B/.Card.new" "$B/Card" && echo "karta gotowa: $B/Card" || echo "BŁĄD albo karta już jest: nic nie zmieniono w $B/Card"
   ```

   Ma się wypisać `karta gotowa: …`. Co gra na tej karcie:
   - pierwszy wzorzec (ten, który gra po starcie): wszystkie siedem głosów na każdą ćwierćnutę (kroki 1, 5, 9 i 13);
   - drugi wzorzec: sam Drum 1 na ćwierćnuty;
   - każdy głos gra też na nutę C2 (MIDI 48) na kanale 1;
   - wszystkie głosy wychodzą na wyjście 1.
5. **Live.** W Ustawieniach → Audio wybierz 48 kHz i bufor 128 albo 256 próbek. W menu Options włącz „Delay Compensation”.

Opóźnienie wtyczki przy 48 kHz to 450 ramek (9,4 ms) przy buforze 128 i 578 ramek (12,0 ms) przy 256. Live je wyrównuje przy odtwarzaniu. Grając na żywo z klawiatury, je usłyszysz.

## 2. Podstawy: P1–P8 (ok. 20 min)

Każdy punkt to: co zrobić, co powinno się stać i co odpisać.

| # | Co zrobić | Co powinno się stać | Odpisz |
|---|---|---|---|
| **P1** (2 min) | Ustawienia → Plug-Ins: włącz „Use VST3 Plug-In System Folders”, potem „Rescan”. W przeglądarce: Plug-Ins → VST3 → LICHOMAT. | Jest „LICHOMAT” od „LICHOMAT”. Live nie zgłasza błędu. | Czy jest; komunikat Live'a, jeśli był. |
| **P2** (2 min) | Przeciągnij LICHOMAT na nową ścieżkę MIDI. Otwórz Monitor aktywności i wpisz `lichomat`. | Urządzenie ładuje się bez własnego okna. W ciągu sekundy pojawia się proces `lichomat-engine`. | Czy się załadowało; czy proces jest. |
| **P3** (3 min) | Klip MIDI na 1 takt: nuta **C2** (MIDI 48) na każdą ósemkę „i” (2., 4., 6. i 8. ósemka). Live wysyła nuty klipu na kanale 1. Uruchom klip. | Między ćwierćnutami sekwencera karty słychać uderzenia z klipu: wszystkie głosy naraz. Równo z metronomem. | Czy gra; czy równo. |
| **P4** (4 min) | Transport od 1.1.1 przy 120 BPM. W trakcie zmień tempo na 140. Zatrzymaj i uruchom od taktu 3 (kliknięcie w linijkę). Na koniec pętla 1 takt w aranżacji, kilka przejść. | Sekwencer gra na ćwierćnuty razem z metronomem. Po zmianie tempa od razu gra w nowym tempie, dalej równo. Od taktu 3 gra od razu we właściwym miejscu. W pętli każde przejście zaczyna się od „raz” i nic nie ginie. | Co słychać przy każdej z czterech rzeczy; czy coś się rozjeżdża w czasie. |
| **P5** (2 min) | Dodaj drugą ścieżkę MIDI z drugim LICHOMAT. | Dwa procesy `lichomat-engine`. Obie ścieżki grają, każda swoją kopią karty. | Czy obie grają. |
| **P6** (3 min) | Zapisz set, zamknij Live, otwórz set. | Obie ścieżki grają jak przed zapisem (karta jest zapisana w secie). | Czy grają; ile trwało otwarcie. |
| **P7** (3 min) | Na ścieżce audio z wejściem „Resampling” nagraj 2 takty. Potem zrób „Freeze” ścieżki LICHOMAT i odtwórz ją jak wcześniej. | Nagrane uderzenia leżą na siatce, bo opóźnienie wtyczki jest skompensowane. Zamrożona ścieżka gra to samo. | Czy na siatce (jeśli nie, o ile ms przesunięte); czy freeze gra. |
| **P8** (2 min) | Usuń urządzenie i cofnij (Cmd+Z). Potem dodaj nowe. Opcjonalnie, przy grającym transporcie: w Monitorze aktywności zaznacz `lichomat-engine`, kliknij ✕ na pasku i wybierz „Wymuś koniec”. | Po cofnięciu urządzenie wraca i gra; nowe też gra. Po zabiciu silnika jest ok. 1 s ciszy, potem perkusja wraca w rytm. Live działa dalej. | Co się stało. |

## 3. Dwa wyjścia i parametry: P9–P10 (ok. 15 min)

Wtyczka ma dwa wyjścia stereo, „Out 1” i „Out 2”, jak dwa gniazda urządzenia. Ma też 36 parametrów do automatyki:
- wzorzec;
- głośność, panorama, filtr, decay i wyjście każdego głosu (hi-hat zamknięty i otwarty osobno);
- filtr DJ;
- poziom i decay pogłosu;
- ilość FX.

Nie ma okna, więc Live pokazuje parametry jako suwaki: rozwiń je trójkątem w nagłówku urządzenia LICHOMAT. Każdy parametr jest też na liście automatyki ścieżki. W aranżacji kliknij „A”, potem wybierz urządzenie LICHOMAT i parametr. Wartości są takie jak na wyświetlaczu urządzenia:
- panorama −63..64;
- wzorzec 1–64;
- wyjście głosu St1, St2, L1, R1, L2, R2 albo FX.

Wartości, które firmware zmienia samo (np. kit wczytany razem ze wzorcem), Live pokazuje sam.

| # | Co zrobić | Co powinno się stać | Odpisz |
|---|---|---|---|
| **P9** (6 min) | Ścieżka MIDI z LICHOMAT, transport gra (sekwencer karty, cztery ćwierćnuty). (a) Parametr **Drum 1 Output** ustaw na **St2**. (b) Dodaj ścieżkę audio; w jej „Audio From” wybierz ścieżkę LICHOMAT, a pod spodem **Out 2**; Monitor „In”. (c) Usuń tę ścieżkę audio. (d) Drum 1 Output z powrotem na **St1**. | (a) Nic się nie zmienia: dopóki Live nie bierze dźwięku z Out 2, ścieżka LICHOMAT gra oba wyjścia razem. (b) Drum 1 gra już tylko na nowej ścieżce, a ścieżka LICHOMAT bez niego (patrz na wskaźniki poziomu obu ścieżek). (c) Drum 1 wraca na ścieżkę LICHOMAT albo milknie (zależy od tego, czy Live wyłącza wtedy Out 2). (d) Drum 1 gra na ścieżce LICHOMAT, Out 2 nic. | Co słychać po (a), (b), (c) i (d). Najważniejsze jest (a): czy Drum 1 zniknął już wtedy. |
| **P10** (10 min) | W aranżacji na ścieżce LICHOMAT, transport od taktu 1: (a) automatyka **Drum 1 Volume** od 127 na początku taktu 1 do 0 na początku taktu 3, dalej 0; odtwórz. (b) Bez automatyki przesuń ręką **Reverb Level** na ok. 100 i **DJ Filter** w lewo, potem w prawo. (c) Automatyka **Pattern**: na trzeciej ćwierćnucie taktu 3 wartość 2, na trzeciej ćwierćnucie taktu 6 znów 1; odtwórz od taktu 1. (d) „Freeze” ścieżki i odsłuch. (e) Usuń automatykę Drum 1 Volume, ustaw ręką **Hi-hat Volume** na 40, zapisz set, zamknij Live, otwórz set. | (a) Drum 1 cichnie płynnie razem z linią automatyki. (b) Pogłos słychać; filtr DJ zamyka górę albo dół. (c) Od taktu 4 gra wzorzec 2 (sam Drum 1 na ćwierćnuty), od taktu 7 znów wzorzec 1: firmware zmienia wzorzec na końcu grającego, jak z Program Change na urządzeniu. Razem ze wzorcem wczytuje jego kit, więc parametry wracają do wartości kitu i Live dostaje je od wtyczki: suwak Reverb Level wraca na 0, a Drum 1 Volume na 127, choć jego automatyka z (a) stoi na 0 (Drum 1 znów głośno). (d) Zamrożona ścieżka gra to samo co (c). (e) Hi-hat Volume pokazuje 40 i hi-hat gra ciszej, jak przed zapisem. | Czy (a)–(e) jak opisano. Przy (c): czy Drum 1 po zmianie wzorca gra głośno; czy przy Drum 1 Volume zapaliła się w Live czerwona lampka (automatyka nadpisana) albo przycisk „Re-Enable Automation”; i co wolisz po wczytaniu kitu: wartość z kitu (jak teraz, jak na urządzeniu) czy ostatnią z automatyki Live'a. |

Znane ograniczenie: przy eksporcie, bounce'ie albo freeze z nowo wstawionej wtyczki wartości z seta dochodzą do firmware w pierwszej ramce, razem ze startem transportu. Jeśli jakiś parametr mocno różni się od wartości z kitu (np. głos ściszony do 0), nuta dokładnie w tej ramce może przez 2–3 ms zagrać z wartością z kitu. Jeśli to usłyszysz, napisz przy P10 (d).

## Jeśli coś nie gra albo trzeszczy

- **Logi.** Każda instancja zapisuje log w `~/Library/Application Support/LICHOMAT Plugin/logs/<id>.log`. W logu jest:
  - start i koniec każdego silnika (kod wyjścia albo sygnał i powód);
  - przy końcu silnika: liczba niedoborów próbek i spóźnionych zdarzeń;
  - kanał MIDI parametrów (na tej karcie: `parameters: MIDI channel 1, receive filter: CC on, Program Change on`).

  Liczniki trafiają do logu dopiero, gdy silnik się kończy, więc przed wysłaniem zamknij set (albo usuń urządzenie). Prześlij najnowsze pliki, po jednym na instancję.
- **Brak dźwięku.** Sprawdź w Monitorze aktywności, czy działa `lichomat-engine`. Sprawdź też, czy istnieje folder `~/Library/Application Support/LICHOMAT Plugin/Card` (krok 4 instalacji). Potem zajrzyj do logu.
- **Silnik wciąż pada.** Po 5 awariach w ciągu minuty albo 3 zawieszeniach w ciągu 10 minut wtyczka się poddaje i milknie. Live działa dalej. Wtedy prześlij log.

## Odpowiedź

Skopiuj, uzupełnij i odeślij:

```text
Mac (procesor), macOS, Live (wersja i edycja), bufor:
P1:
P2:
P3:
P4 (tempo / start od taktu 3 / pętla):
P5:
P6 (i ile trwało otwarcie):
P7:
P8:
P9 (a):
P9 (b):
P9 (c):
P9 (d):
P10 (a)–(e):
P10 (c): czerwona lampka / Re-Enable Automation?
P10 (c): po wczytaniu kitu wolę wartość z kitu czy z automatyki Live'a?
Inne (trzaski, obciążenie CPU, co przeszkadzało):
```

## Odinstalowanie

Zamknij Live i usuń:
- `~/Library/Audio/Plug-Ins/VST3/LICHOMAT.vst3` (wtyczka);
- `~/Library/Application Support/LICHOMAT Plugin` (karta startowa i logi).
