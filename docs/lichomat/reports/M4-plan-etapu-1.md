# M4: plan implementacji etapu 1

Data: 2026-10-24. Gałąź `timing-core`, commit planu `50d8889`. Nic nie jest wypchnięte.

## Co powstało

- **Plan:** `docs/superpowers/plans/2026-10-23-timing-core.md`, 19 265 linii.
  - 27 zadań w kolejności: E3.2, E1.1–E1.13, E2.1–E2.6, E3.1, E3.3–E3.5, E4.1–E4.3.
  - Każde zadanie ma kroki TDD, pełny kod, dokładne polecenia z oczekiwanym wynikiem i commit z listą
    plików.
- **Trzy zadania dopisane w trakcie**, pisane i sprawdzane osobno:
  - E1.14: slave po dużym skoku tempa, note-off przy zaniku zegara, skip-once per tor.
  - E2.7: błąd menu globalnego z fw 1.7.
  - E2.8: błędy USB MIDI out z fw 1.7.
  Właśnie trafiają do planu.
- **Notatki N12–N19** w `docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md`.
  Mają pierwszeństwo przed specyfikacją.

## Jak plan był sprawdzany

1. **Mapa kodu:** pięciu agentów przeczytało sekwencer, MIDI, zegar, UI i build. Każdy fakt w mapie ma
   plik i numer linii.
2. **API rdzenia:** sprawdzone na prototypie w 11 scenariuszach. Kompiluje się GCC 14.3 i clangiem bez
   ostrzeżeń.
3. **Sekcje planu:** trzech autorów odtworzyło każde zadanie od zera w kopii repo.
   - Nowy test najpierw nie przechodzi, potem przechodzi.
   - Po każdym zadaniu `tools/check.sh` jest zielony i pokazuje 0 ostrzeżeń.
4. **Krytycy:** trzech (pokrycie specyfikacji, poprawność względem kodu, wykonalność) zgłosiło razem
   30 uwag. 29 przyjęto i poprawiono. Jedną odrzucono z dowodem: szybkość rolla była dobrze.
5. **Ponowne odtworzenie:** poprawiony E1 i E2 odtworzono jeszcze raz, wszystko przechodzi.
6. **Sekcje E1.14, E2.7 i E2.8:** każdą odtworzył autor, potem niezależny recenzent, a uwagi
   poprawiono. W E1.14 i E2.8 sprawdzono też testy mutacyjne: celowo wprowadzone błędy łapią testy.

## Ważne odkrycia

- **Sekwencer w fw 1.7 chodzi z pętli głównej, nie z przerwania TIM2** (N12). TIM2 działa tylko przy
  zapisie i odczycie karty SD.
  - Start dźwięku jest więc kwantowany do bloku audio 1,45 ms, tak jak w fw 1.7.
  - Mikrotiming rdzenia jest drobniejszy niż blok. Wyzwalanie z dokładnością do próbki jest zadaniem
    E6.10.
- **Prawdziwe błędy znalezione przez krytyków i poprawione w planie, zanim powstał kod:**
  - Rdzeń: przy zmianie shuffle albo przesunięcia kroku między granicami kroków uderzenie mogło zagrać
    dwa razy albo wcale.
  - SPP w trybie song zawsze przeładowywał pattern i kasował niezapisane zmiany.
  - Wczytanie projektu w trakcie grania jako slave MIDI kasowało estymację tempa.
  - Wyjście clock out po Continue rozjeżdżało się z siatką kroków.
- **Błędy fw 1.7 spoza listy D1–D8, dopisane do etapu 1:**
  - **E2.8:** USB MIDI out przy każdym zawinięciu bufora wysyła 128 bajtów spoza bufora, z kolejki
    odbiorczej USB. Przy 120 BPM zdarza się to co ok. 0,67 s, od samego zegara. Do tego gubi komunikaty
    przy zajętym porcie i ma zły CIN dla F1, F2, F3 i F6.
  - **E2.7:** w menu globalnym naciśnięcie kroku bez SHIFT ustawia długość wszystkich torów.

## Pytania do Ciebie

Wszystkie są w trackerze, w tabeli „Pytania do Ciebie”. Nowe jest Q7: czy LXR jako slave ma wysyłać
dalej Start, Stop i Continue. Do czasu decyzji działa jak w fw 1.7, czyli nie wysyła.

## Koszt

| Część | Czas | Agenci | Tokeny agentów |
|---|---|---|---|
| Workflow planu | 5 h 40 min | 14 | 7,7 mln |
| Dodatkowe sekcje | ok. 1 h 45 min | 9 | — |

## Co dalej

1. Wstawienie E1.14, E2.7 i E2.8 do planu, a potem kontrola spójności. Plan będzie miał 30 zadań.
2. Implementacja zadań po kolei. Każde przechodzi przez implementację TDD z `tools/check.sh`, przegląd
   zgodności ze specyfikacją i przegląd jakości kodu. Tracker dostaje wpis po każdym zadaniu.
3. Testy na sprzęcie H1–H13 oraz U1 i X4 może zrobić tylko właściciel. Przygotuje je zadanie E4.2.
