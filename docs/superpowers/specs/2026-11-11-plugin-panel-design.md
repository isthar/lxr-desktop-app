# E9.5: panel w oknie wtyczki (szkic projektu)

Data: 2026-11-11. Status: szkic. Decyzje są moje (projekt mam w delegacji); każdą możesz poprawić, wtedy zmienię plan.
Wiersz trackera: E9.5. Dotyczy: VST3 i AU 0.2.0 z gałęzi `plugin-v07` (420d4a9). Wynik: wtyczka 0.3.0.

## 1. Cel

Okno wtyczki pokazuje to samo urządzenie, co okno aplikacji LICHOMAT Emu:
- wyświetlacz 16×2;
- enkoder „data” z wciśnięciem;
- cztery gałki;
- sześć suwaków głośności;
- 41 przycisków z diodami.

Klikasz myszą jak w aplikacji, a firmware w silniku wtyczki reaguje tak, jak na LXR. Dzięki temu da się w Live edytować kit i pattern bez osobnej aplikacji. Live przestaje też pokazywać swoje ogólne okno z suwakami parametrów.

## 2. Co jest w oknie, a czego nie ma

**Jest:** urządzenie z aplikacji, w tym samym układzie, kolorach i skali co w aplikacji. Pod spodem linia stanu silnika:
- „silnik startuje”;
- „gra” (wtedy nic nie piszę);
- „silnik padł, wraca”;
- „błąd silnika: zobacz log”.

**Nie ma** (w aplikacji zostaje jak jest):
- **pasek gniazd na górze** (wyjścia, karta, clock i reset): wyjścia wtyczki ustawia host (E9.4), a karta należy do wtyczki;
- **menu** (Card, Window, Help), **okna** Audio, MIDI i diagnostyki oraz **okienka** z pytaniami: dźwięk i MIDI prowadzi host;
- **klawiatura**: klawisze zostają dla hosta, bo spacja w Live to transport; panel obsługuję tylko myszą (klik, przeciąganie, prawy klik przytrzymuje przycisk, jak w aplikacji);
- **zmiana rozmiaru okna**: okno ma stały rozmiar, urządzenie w skali 1 (ok. 1020 × 470 punktów).

## 3. Jak to działa

Pięć części, każda z jednym zadaniem.

### 3.1. Twarz urządzenia w osobnym pliku (emu/app/gui)

Teraz `panel.cpp` rysuje w jednym miejscu urządzenie i okna aplikacji. Wydzielam z niego **twarz**: `panel_face.cpp` i `panel_face.h`.
- **Co rysuje:** wyświetlacz, enkoder, gałki, suwaki, przyciski z diodami i napisy.
- **Wejście:** stan wyświetlacza i diod (`emu_lcdSnapshot_t` i 41 bitów).
- **Wyjście:** zdarzenia panelu (`app_panelEvent_t`: BUTTON, ENCODER, PUSH, KNOB, FADER) przez funkcję zwrotną.
- **Stan:** własny stan elementów, czyli co trzyma mysz, co jest przytrzymane i reszty obrotu gałek.

Twarz nie zna powłoki aplikacji (`app_shell.h`). Aplikacja rysuje swoje okna i pasek gniazd jak dotąd, a w środku woła twarz.

**Aplikacja ma wyglądać i działać tak samo.** Sprawdza to test charakteryzacyjny:
- ImGui rysuje panel bez okna dla stałego stanu wyświetlacza;
- test liczy sumę kontrolną list rysowania (wierzchołki, kolory, indeksy) i porównuje z wzorcem zapisanym na kodzie sprzed podziału;
- symulowane kliknięcia dają te same zdarzenia.

### 3.2. Pamięć wspólna, wersja 3 (plugin/common/plug_shm)

Dochodzą trzy rzeczy. Każda strona nadal pisze tylko swoje pola.
- **Wyświetlacz:**
  - silnik zapisuje po każdym bloku stan LCD, diody, swój stan i klatkę hosta;
  - zapis idzie pod licznikiem sekwencji, jak wartości parametrów;
  - diody to OR z ostatnich 16 bloków (ok. 23 ms), więc żadne mignięcie dłuższe niż blok nie zginie między klatkami okna (30 na sekundę).
- **Pierścień zdarzeń panelu:**
  - jeden producent (wątek główny wtyczki) i jeden konsument (silnik);
  - 256 miejsc; pełny pierścień odrzuca zdarzenie i liczy je.
- **Poziomy suwaków na start:** wtyczka wpisuje je przed startem silnika, a silnik ustawia je w konfiguracji przed włączeniem zasilania.

Silnik innej wersji odmawia pamięci (wyjście 2), jak dotąd.

### 3.3. Silnik (plugin/engine)

- **Zdarzenia panelu:** hak, który już przekazuje nuty, w każdym wywołaniu przekazuje też zdarzenia panelu do urządzenia, w chwili „teraz” (`emu_inButton`, `emu_inEncoder`, `emu_inPush`, `emu_inKnob`, `emu_inFader`, tak jak `app_enginePanelEvent()` w aplikacji).
- **Stan wyświetlacza:** po każdym bloku silnik zapisuje go do pamięci wspólnej.
- **Bez dotykania panelu dźwięk jest ten sam bit w bit:** testy równości z rendererem przechodzą bez zmian, bo suwaki na start są pełne, jak w rendererze.

### 3.4. Most i okno (plugin/gui, nowy katalog)

**Most** należy do procesora i wszystko robi na wątku głównym:
- czyta stan wyświetlacza z pamięci;
- wpisuje zdarzenia z okna do pierścienia;
- zapamiętuje poziomy suwaków, które przez niego przeszły.

Każdy nowy silnik dostaje aktualne poziomy, także ten, który wraca po awarii.

**Okno** (`plug_view.mm`, Objective-C++) jest wspólne dla VST3 i AU:
- `NSView` z `MTKView` (Metal), własny kontekst ImGui na każde okno, twarz z 3.1 i linia stanu;
- mysz trafia do ImGui z metod widoku (`mouseDown`, `mouseDragged`, `rightMouseDown`, `scrollWheel`). Nie ma globalnych haków: zamiast `imgui_impl_osx`, który zakłada monitor zdarzeń całej aplikacji i podsłuchuje jej aktywację, jest własne przekazywanie;
- rysuje 30 klatek na sekundę i staje, gdy okno jest schowane;
- zamknięcie okna puszcza każdy przytrzymany przycisk (zdarzenia „w górę”), więc nic nie zostaje wciśnięte.

**Nazwy klas Objective-C** są w środowisku uruchomieniowym wspólne dla całego procesu. Jeśli inna wtyczka z ImGui albo nasz VST3 i AU w jednym hoście mają klasę o tej samej nazwie, wygrywa przypadkowa. Dlatego każda klasa dostaje przedrostek pakietu i wersji, na przykład `LichomatVST3_030_View`. Dotyczy to również klas backendu `imgui_impl_metal`, przemianowanych definicjami przy kompilacji. Test sprawdza, że każda klasa w pakiecie ma ten przedrostek.

### 3.5. VST3 i AU

**VST3:**
- `createView("editor")` zwraca `IPlugView` (klasa `CPluginView` z SDK) dla `NSView`;
- kontroler i procesor rozmawiają przez `IMessage`, czyli drogą, którą VST3 przewiduje, także dla hostów z procesorem w innym procesie:
  - „view” (otwarte lub zamknięte),
  - „panel” (zdarzenia),
  - „display” (stan wyświetlacza z poziomami suwaków, wysyłany przez timer procesora 30 razy na sekundę, tylko przy otwartym oknie i tylko po zmianie).

**AU:**
- `kAudioUnitProperty_CocoaUI` wskazuje klasę fabryki widoku w pakiecie;
- widok bierze most przez własną właściwość AU (wskaźnik w tym samym procesie, jak zwykle w AU v2).

## 4. Stan w secie

- **Suwaki:** poziomy sześciu suwaków są w stanie wtyczki, w nowej wersji formatu. Stan 0.2.0 wczytuje się z suwakami pełnymi. Na LXR suwak zostaje tam, gdzie go zostawisz, więc tu też.
- **Gałki:** obracają się bez końca (dwa ślizgacze, `emu_panel.c:187-218`), więc ich kąta nie zapisuję.
- **Edycje z panelu:**
  - zostają w pamięci firmware, jak na urządzeniu;
  - do setu trafiają dopiero po zapisie przez menu SAVE, bo karta z zapisem jest już w stanie wtyczki;
  - 36 parametrów automatyki idzie jak dotąd: firmware odsyła wartości, a host je pokazuje i zapisuje.
- **Automatyczny zapis zmian** przy zapisie setu jest poza zakresem. Firmware zapisuje przez menu, a zapis bez pytania nadpisywałby slot na karcie. Jeśli zechcesz, dostanie osobny wiersz.

Karta testu dla znajomego i README mówią o tym wprost: „zmiany zapisz przez SAVE jak na LXR”.

## 5. Przypadki brzegowe

- **Silnik padł:** okno pokazuje ostatni stan i „silnik padł, wraca”. Kliknięcia z tego czasu przepadają, bo nowy silnik czyści pierścień, tak jak nuty.
- **Host nie liczy bloków** (urządzenie wyłączone w Live): silnik stoi, więc wyświetlacz też. Kliknięcia czekają w pierścieniu (256) i działają po wznowieniu.
- **Dwa okna jednej instancji** (niektóre hosty AU): oba są na wątku głównym, więc pierścień nadal ma jednego producenta.
- **Kilka instancji:** każde okno ma własny kontekst ImGui, atlas czcionki i dane backendu Metal; kontekst ustawiam przed każdą klatką.
- **Retina:** skala z `backingScaleFactor` okna, jak w aplikacji.

## 6. Testy (bez Ciebie)

- **Twarz:**
  - suma kontrolna rysowania aplikacji przed podziałem i po nim jest ta sama;
  - kliknięcia w przycisk, przeciągnięcie gałki i suwaka oraz obrót enkodera dają oczekiwane zdarzenia.
- **Pamięć wspólna:**
  - zapis i odczyt wyświetlacza pod licznikiem;
  - OR diod z 16 bloków;
  - pierścień panelu: pełny, pusty i w kolejności.
- **Silnik przez sterownik testowy:**
  - wciśnięcie przycisku strony zmienia tekst LCD;
  - ruch suwaka zmienia głośność głosu w nagraniu;
  - bez zdarzeń panelu nagranie jest bit w bit jak renderer (testy, które już są).
- **Hosty testowe** (host SDK VST3 i host AU z `plugin/host`) w procesie z oknem poza ekranem:
  - otwarcie okna;
  - kliknięcie przez metody widoku;
  - zmiana LCD w silniku;
  - zamknięcie okna puszcza przytrzymany przycisk;
  - zapis i odczyt stanu z suwakiem.
- **Nazwy klas** Objective-C w obu pakietach mają swój przedrostek.
- **Bramki:**
  - walidator Steinberga 47/0;
  - sprawdzanie czasu rzeczywistego wątku audio (nic tam nie dochodzi);
  - bramka licencji obejmuje licencję Dear ImGui w pakietach;
  - PLUGIN-CHECK w obu buildach, EMU-CHECK i CHECK bez zmian.
- **`auval`** uruchamiasz Ty, przy instalacji, jak w P11 (a).

## 7. Sprawdzenie w hoście (P12)

Dla znajomego w Live (VST3), w karcie testu, ok. 10 min:
- okno się otwiera;
- wyświetlacz jest ten sam co w aplikacji;
- wciśnięcie głosu i strony działa;
- gałka zmienia brzmienie;
- suwak ścisza głos;
- zapis przez SAVE, zapis setu i ponowne otwarcie: zmiana jest;
- dwie instancje mają osobne okna.

To samo dla AU w Logic albo REAPER, gdy będzie gdzie, jako część P11.

## 8. Poza zakresem

- klawiatura;
- zmiana rozmiaru okna;
- pasek gniazd i operacje na karcie;
- automatyczny zapis zmian;
- VSTGUI (nie używam; twarz jest ta sama co w aplikacji);
- Windows i Intel;
- MIDI out.

## 9. Decyzje (do Twojej poprawki)

| | Domyślnie |
|---|---|
| Interfejs | Twarz z aplikacji (Dear ImGui), wydzielona z `panel.cpp`; aplikacja bez zmian |
| Co w oknie | Samo urządzenie i linia stanu; bez gniazd, menu i okien aplikacji |
| Wejście | Tylko mysz; klawisze dla hosta |
| Rozmiar | Stały, skala 1 |
| VST3: kontroler ↔ procesor | `IMessage`, 30 Hz, tylko przy otwartym oknie |
| AU: widok ↔ jednostka | Własna właściwość AU ze wskaźnikiem mostu |
| Suwaki | W stanie wtyczki; gałki nie (obrót bez końca) |
| Edycje z panelu | Jak na LXR: do setu po SAVE; bez zapisu automatycznego |
| Nazwy klas | Przedrostek pakietu i wersji |
| Gałąź | `plugin-panel` od `plugin-v07` 420d4a9; wydanie AU 0.2.0 zostaje na `plugin-v07` |
| Wersja | Wtyczka 0.3.0 (VST3 i AU); wydanie jako szkic po bramkach, publikujesz Ty |
