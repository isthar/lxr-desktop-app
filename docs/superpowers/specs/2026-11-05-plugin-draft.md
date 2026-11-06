# LICHOMAT jako wtyczka do Abletona: szkic z rekomendacjami

| | |
|---|---|
| Data | 2026-11-05 |
| Status | Szkic do Twoich poprawek (tracker E9.1, decyzja Q38). Według reguły z 29.10 idę dalej z tymi rekomendacjami, dopóki ich nie zmienisz |
| Podstawa | Emulator z etapu 1b (`emu/`, gałąź `v0.7`), ten sam kod firmware co na LXR |
| Nie zmienia | Firmware na LXR, gałęzi 0.4–0.7 ani aplikacji emulatora |

## 1. Po co

Emulator gra kodem firmware na Macu, ale jako osobna aplikacja: ma własne wyjście audio, a z Abletonem łączy go tylko MIDI clock. Wtyczka instrumentu daje to samo wprost w secie:

- dźwięk idzie na ścieżkę Live'a, więc działa w miksie, w bounce i we freeze;
- nuty i automatyka płyną z klipów;
- sekwencer idzie za tempem i pozycją Live'a.

Mikrotiming, szablony i dynamika z v0.6–v0.7 słychać wtedy w aranżacji, nie tylko na żywo.

## 2. Co proponuję

1. **Formaty: VST3 i AU v2, tylko Mac z Apple Silicon.**
   - Live 12 na Macu ładuje AU, VST2 i VST3. CLAP-a nie ładuje, więc go pomijam.
   - SDK VST3 jest od 29.10.2025 na licencji MIT, AudioUnitSDK Apple'a na Apache-2.0. Obie dają się połączyć z GPL-3.0.
   - JUCE odpada: jest na AGPL albo licencji komercyjnej i wymaga Xcode.
   - Emulator buduje się tylko na arm64, więc wtyczka też. Intel nie wchodzi w grę.
2. **Silnik w osobnym procesie na każdą instancję wtyczki.** Wtyczka w Live'ie to cienka warstwa: przekazuje bloki audio, nuty i transport przez pamięć współdzieloną do procesu z silnikiem. Ten proces to emulator bez okna, jak `lichomat-render`. Powody, wszystkie z kodu:
   - Firmware trzyma cały stan w zmiennych globalnych (ok. 0,66 MB) i daje się uruchomić tylko raz na proces (`emu_powerOn()`). Restartu w procesie nie ma; aplikacja emulatora robi „power cycle” nowym procesem. Usunięcie i ponowne dodanie urządzenia w Live'ie wymaga więc nowego procesu. Kilka instancji naraz to kilka procesów, więc też działa.
   - Błąd w silniku woła `abort()`. W procesie Live'a zamknąłby cały Live razem z Twoim setem. W osobnym procesie wtyczka tylko milknie, pokazuje „silnik zatrzymany” i uruchamia go ponownie z ostatnim stanem.
   - Silnik czyta i pisze kartę, alokuje pamięć i potrzebuje 8 MB stosu, czyli robi rzeczy, których nie wolno robić w wątku audio hosta. We własnym procesie wolno.
   - Odrzucona alternatywa: kopia biblioteki z silnikiem ładowana osobno dla każdej instancji. Daje świeży stan, ale `abort()` i przechwytywanie sygnałów dalej dotyczą procesu Live'a.
3. **Opóźnienie zgłoszone hostowi.**
   - Silnik liczy z wyprzedzeniem, a wtyczka podaje Live'owi stałe opóźnienie, które Live wyrównuje przy odtwarzaniu (kompensacja opóźnień wtyczek).
   - Przy graniu klipów i aranżacji tego opóźnienia nie słychać. Przy graniu na żywo z klawiatury słychać.
   - Cel: ≤ 10 ms ponad stałe 2,9 ms silnika (128 próbek kolejki głosów, jak na LXR). Prototyp to zmierzy; aplikacja emulatora ma dziś 34–58 ms, bo jej bufor jest dobrany pod CoreAudio, nie pod hosta.
4. **Synchronizacja z Live'em w dwóch krokach.**
   - **Prototyp, bez zmiany firmware:** wtyczka zamienia transport Live'a na MIDI clock i Song Position, wpisane w dokładne chwile, a sekwencer gra jako slave, tak jak z zegarem z Abletona dziś. Ograniczenia obecnego slave'a:
     - po zmianie tempa dochodzi do nowego przez dopasowanie (najmniejsze kwadraty);
     - pozycję przyjmuje tylko w stanie zatrzymanym;
     - przeskok pętli Live'a to stop, pozycja i continue.
   - **Docelowo, tryb „host” w `seq_core`:** w każdym bloku wtyczka podaje pozycję w ćwierćnutach, tempo i stan transportu, a rdzeń ustawia na tym swój zegar muzyczny. Przeskoki pętli i zmiany tempa wypadają wtedy co do próbki. To zmiana w rdzeniu (nowe wejście w skrzynce i nowa funkcja), testowana na hoście jak reszta `seq_core`. LXR jej nie wywołuje, więc jego gra się nie zmienia; wszystkie nagrania wzorcowe zostają.
5. **Częstotliwość próbkowania.**
   - Silnik liczy w 44 140,625 Hz, tak jak LXR. Proces silnika przelicza to na częstotliwość Live'a (44,1, 48, 88,2 lub 96 kHz) konwerterem Apple'a (AudioConverter) w tej samej jakości, co aplikacja emulatora dziś.
   - W trybie testowym wtyczka może oddawać 44 140,625 Hz bez przeliczania, żeby porównać ją bit w bit z rendererem.
6. **Wyjścia.** Stereo sumy w prototypie. Potem dwie szyny stereo, out1 i out2, tak jak gniazda LXR:
   - głosy idą tam według `audio_out` kitu;
   - w Live'ie to wyjścia instrumentu na osobne ścieżki.

   Osobnego wyjścia na każdy z sześciu głosów LXR nie ma, a wtyczka tego nie zmienia.
7. **Wejścia.**
   - Nuty MIDI z klipów trafiają do silnika z dokładnym momentem w bloku. Silnik przyjmuje zdarzenia z czasem co do cyklu, a dźwięk wypada na siatce 2 próbek, jak na LXR.
   - Firmware już obsługuje Program Change (zmiana wzorca); plan sprawdzi, co dokładnie robi.
   - Parametry jako automatyka Live'a: wybrane parametry kitu idą do firmware tą samą drogą, co edycje w rendererze (CC). W prototypie kilka, potem lista do Twojego wyboru.
8. **Stan w secie.**
   - Każda instancja ma własną kartę: folder w `~/Library/Application Support/LICHOMAT Plugin/<id instancji>`.
   - Przy zapisie setu wtyczka zapisuje folder projektu z kartą (z `/tmp`, w kolejności plików, bo firmware czyta je według niej) jako jeden blok danych w stanie wtyczki.
   - Przy otwarciu setu rozpakowuje go do świeżego folderu i uruchamia silnik na nim.
   - Start trwa ok. 2 s czasu emulatora. Proces silnika liczy go bez czekania na zegar ścienny (renderer liczy ponad 10 razy szybciej), więc to ułamek sekundy.
9. **Okno.**
   - Prototyp: bez własnego okna, tylko ogólne okno parametrów Live'a.
   - Potem panel z emulatora w oknie wtyczki. Panel rozmawia tylko z interfejsem powłoki aplikacji, więc da się go podłączyć przez adapter. Backend Dear ImGui dla macOS zakłada jednak globalne haki zdarzeń (monitor NSEvent, aktywacja aplikacji); w cudzym oknie trzeba je zastąpić własnymi.
10. **Licencja i nazwa.**
    - Wtyczka na GPL-3.0, z tymi samymi warunkami NOTICE.md co reszta: bez nazw „Erica Synths”, „LXR”, „LXR-02” sugerujących związek z producentem, oznaczona jako zmodyfikowana.
    - Nazwa: „LICHOMAT” (VST3 i AU), producent we wtyczce: „LICHOMAT” albo Twoje imię, do Twojego wyboru.
    - Logo VST nie używam, bo wymaga osobnej umowy ze Steinbergiem.
    - Sprawdzenie licencji (test M21) obejmuje też pliki wtyczki.
    - Kodu z niekomercyjnych projektów (oryginalne wtyczki Sonic Potions, `lxr-vst3`, LXR02XXL i inne z listy emulatora) nie używam, tak jak w emulatorze.
11. **Dystrybucja.**
    - U Ciebie lokalnie wystarczy podpis ad hoc.
    - Do udostępnienia innym trzeba podpisu Developer ID i notaryzacji Apple'a. To Twoje konto, więc decyzja Twoja, po prototypie.

Poza zakresem tego etapu: MIDI out z wtyczki, Windows i Intel, wtyczka efektu (np. sam pogłos), AU v3.

## 3. Jak sprawdzę bez Ciebie

- **Walidatory:** walidator z SDK VST3 i `auval` Apple'a dla AU. Oba muszą przejść bez błędów.
- **Własny mały host testowy:**
  - ładuje wtyczkę, podaje transport i nuty, zapisuje WAV;
  - porównuje wynik z rendererem w trybie bez przeliczania próbkowania (bit w bit) i z przeliczaniem (różnica tylko z konwertera);
  - sprawdza moment uderzeń przez kilka pętli i zmian tempa (log uderzeń, jak w testach emulatora).
- **Odporność:**
  - zabicie procesu silnika w trakcie: wtyczka milknie i wraca;
  - dwie i cztery instancje naraz;
  - usunięcie i ponowne dodanie;
  - zapis i odtworzenie stanu.
- **Czas rzeczywisty:** wątek audio wtyczki niczego nie alokuje i na nic nie czeka (sprawdzane narzędziem do wykrywania blokad, jak bufor audio w aplikacji emulatora).

## 4. Kolejność i Twoje sprawdzenia

| Wiersz | Co | Twoje sprawdzenie w Abletonie |
|---|---|---|
| E9.2 | Prototyp: VST3, proces silnika, stereo, nuty, synchronizacja przez MIDI clock, karta na instancję, bez okna | Ok. 20 min: wtyczka się ładuje, gra z klipu, idzie za tempem, dwie instancje, zapis i otwarcie setu |
| E9.3 | Tryb „host” w `seq_core`: pętle i zmiany tempa co do próbki; pomiar i zmniejszenie opóźnienia | Pętla 4 takty z mikrotimingiem przez 5 min bez dryfu, zmiana tempa w aranżacji |
| E9.4 | AU, dwie szyny stereo, automatyka parametrów, stan w secie dopracowany | Wyjścia na osobne ścieżki, automatyka, freeze i bounce |
| E9.5 | Panel w oknie wtyczki | Obsługa panelu w oknie Live'a |

Każdy wiersz dostaje plan i przeglądy jak v0.6–v0.7. Prace nad wtyczką nie zmieniają gałęzi firmware z Twojej sesji testowej. Tryb „host” (E9.3) wejdzie do rdzenia na gałęzi po v0.7, z testami.

## 5. Otwarte do Twojej poprawki (z moimi domyślnymi)

| | Domyślnie |
|---|---|
| Kolejność wobec v0.7 | Równolegle (Q38) |
| Formaty | VST3 i AU v2, tylko Apple Silicon |
| Silnik | Osobny proces na instancję |
| Synchronizacja | Prototyp przez MIDI clock, potem tryb „host” w rdzeniu |
| Wyjścia | Stereo sumy, potem out1 i out2 |
| Okno | Prototyp bez okna, panel w E9.5 |
| Nazwa i producent | „LICHOMAT”, producent do Twojego wyboru |
| Udostępnianie innym | Po prototypie, Twoja decyzja o podpisie i notaryzacji |

## 7. Dopisek z 2026-11-06: E9.4 bez AU (moje decyzje, do Twoich poprawek)

AU wymaga AudioUnitSDK Apple'a, czyli kolejnego pobrania; zapytam Cię o nie osobno. Bez niego
E9.4 robi teraz dwie rzeczy, na gałęzi `plugin-v07` (wtyczka z firmware v0.7):

1. **Dwie szyny stereo: „Out 1” i „Out 2”**, jak dwa gniazda LXR.
   - Głos trafia tam według `audio_out` swojego kitu (np. stopa na Out 2).
   - Dopóki w Live nie włączysz drugiego wyjścia, „Out 1” niesie sumę obu, jak dziś.
   - Po włączeniu „Out 1” to samo wyjście 1, a „Out 2” to wyjście 2: dwie ścieżki do osobnej obróbki.
   - Silnik przelicza wtedy cztery kanały zamiast dwóch.
2. **Parametry do automatyki** (ok. 30): na każdy z 6 głosów głośność, panorama, filtr i decay; do
   tego filtr DJ, pogłos, delay, shuffle i **wybór wzorca** (0–63, wysyłany do firmware jak Program
   Change).
   - Zmiana z Live trafia do firmware co do próbki.
   - Silnik odsyła wtyczce aktualne wartości z firmware, więc po wczytaniu kitu Live pokazuje to, co
     gra.
3. **Bez zmian:** jedna instancja to jeden silnik, stan w secie, opóźnienie, testy z prototypu.
4. **Twoje sprawdzenie** dojdzie jako P9–P10 (dwa wyjścia, automatyka) w `plugin-checks.md`.

## 8. Dopisek z 2026-11-06: AU (E9.7), po Twojej zgodzie na pobranie (moje decyzje, do Twoich poprawek)

Zgoda na pobranie: 2026-11-06, 07:35. Pobrałem AudioUnitSDK Apple'a w wersji 1.4.0 (commit
`bd98b31`, 2025-12-05) do `build/AudioUnitSDK`:
- 50 plików, 668 KB;
- licencja Apache 2.0, zgodna z GPL-3.0.

Do repozytorium trafi przypięta kopia z sumami kontrolnymi, tak jak SDK VST3. Nic więcej nie pobieram.

1. **Co:** instrument AU v2 (typ `aumu`) „LICHOMAT”, dla Logic i innych hostów AU.
   - Live dalej gra VST3.
   - Ten sam silnik w osobnym procesie, te same dwa wyjścia, 36 parametrów i stan w secie co VST3 0.2.0 (E9.4).
2. **Jak:** najpierw sprawdzam, czy da się zbudować bez Xcode wrapper AU z SDK VST3 (`auwrapper`, ta sama wtyczka VST3 w środku komponentu AU).
   - Jego CMake wymaga Xcode, więc budowałbym go z własnym plikiem CMake, jak SDK VST3 w prototypie.
   - Jeśli to się nie uda, napiszę własną cienką warstwę AU na AudioUnitSDK (`MusicDeviceBase`) nad tym samym procesorem.
   - Wybór i powód będą w planie.
3. **Bez instalacji u mnie:**
   - testy ładują komponent w procesie hosta testowego (`AudioComponentRegister`), bez rejestracji w systemie;
   - walidator Apple'a (`auval`) uruchomi skrypt instalacyjny u Ciebie, tak jak walidator Steinberga dla VST3.
4. **Nazwy:** „LICHOMAT” od „LICHOMAT”, bez nazw producenta urządzenia; kody AU (producent, podtyp) własne.
5. **Twoje sprawdzenie:** P11 w `plugin-checks.md`, w Logic albo innym hoście AU. Jeśli nie masz takiego hosta, wystarczy wynik `auval` ze skryptu instalacyjnego.
6. **Kolejność:**
   - plan teraz, sprawdzany na kopii roboczej z gotowym E9.4;
   - wdrożenie po E9.4, na `plugin-v07`.
