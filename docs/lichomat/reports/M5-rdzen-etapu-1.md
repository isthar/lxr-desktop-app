# M5: rdzeń timingu `seq_core` (E1.1–E1.14)

Data: 2026-10-25. Gałąź `timing-core`, wypchnięta na fork.

## Co powstało

- **Rdzeń.** `Core/Src/Soundengine/Sequencer/seq_core.c` z nagłówkiem `seq_core.h` to czysty moduł C,
  który można testować na hoście. Zawiera:
  - zegar wewnętrzny;
  - granice kroków i zawinięcie patternu;
  - generowanie zdarzeń: swing całego kroku, przesunięcia, ratchety i prawdopodobieństwo, z częścią
    wczesną i późną;
  - choke per tor, mute i note-off;
  - lookahead przy zmianie patternu;
  - MIDI clock out i impulsy zegara;
  - roll, fazę nagrywania i skip-once;
  - pracę jako slave MIDI i jack (4 i 24 PPQ);
  - transport: Start, Continue, Stop, SPP i bramkę run;
  - statystyki.
- **Testy na hoście.** Ok. 25 plików testów i kilkaset testów T1–T20, z porównaniem do repliki timingu
  fw 1.7 (T20). `tools/check.sh` jest zielony i pokazuje 0 ostrzeżeń.
- **Firmware jeszcze nie używa rdzenia.** Klej w `sequencer.c` to etap E2, więc obraz firmware jest bez
  zmian.

## Jak to robiliśmy

Każde zadanie przeszło ten sam cykl:
1. implementacja według planu w TDD;
2. przegląd zgodności ze specyfikacją;
3. przegląd jakości kodu;
4. do 3 rund poprawek.

Recenzenci robili testy mutacyjne: celowo psuli kod i sprawdzali, czy testy to wyłapią.

| Licznik | Wartość |
|---|---|
| Agenci | 104 |
| Czas | ok. 14 godzin |
| Tokeny agentów | ok. 13 mln |
| Udział czasu przeglądów | ok. 92% |

## Co przeglądy znalazły

- **Kod z planu był w większości poprawny.** Zdecydowana większość poprawek dotyczyła testów: dodano
  i wzmocniono ok. 80 testów.
- **Prawdziwe problemy:**
  - Spakowany zegar USB z DAW. Estymacja mierzyła częstotliwość pakietów, nie zegara, np. 40 zamiast
    120 BPM albo stałe 107,67 BPM przy buforze 23,2 ms. Notatkę N21 i projekt nowego estymatora
    (regresja liniowa) opracował osobny workflow; wdrożenie to E1.15.
  - Przyklejone takty DIN, z odstępem ok. 0,32 ms, restartowały estymację tempa. Naprawione w E1.14.
  - Koszt kolejki zdarzeń na sprzęcie: `memmove` z newlib-nano kopiuje bajt po bajcie, co daje do
    0,8–1,2 ms na granicę kroku przy gęstym patternie i grozi przerwami dźwięku. Wdrożenie to E1.16.
  - Zaokrąglenie przy granicy kroku dokładnie w chwili wywołania; poprawione. Końce linii CRLF w
    `seq_core.h`; przywrócone.
- **Podejrzenia błędów i luki w testach** zebrane do porządków (E1.17), w tym systemowa słabość
  `ASSERT_EQ` przy liczbach zmiennoprzecinkowych.

## Co dalej

- E1.15 (nowy estymator tempa), E1.16 (kolejka jako bufor pierścieniowy) i E1.17 (porządki).
- Potem integracja z firmware, E2.1–E2.8: klej w `sequencer.c`, MIDI z przerwań, jacki, poprawki
  menu i USB MIDI out.
