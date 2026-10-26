# M6: integracja z firmware (E1.15–E1.19, E2.1–E2.8)

Data: 2026-10-26. Gałąź `timing-core`, wypchnięta na fork (commit `2063c49`).

## Co powstało

**Firmware LXR-02 działa na nowym rdzeniu timingu `seq_core`.** Maszyny 24 impulsów z fw 1.7 już nie
ma.

| Zadanie | Wynik |
|---|---|
| E1.15 | Estymacja tempa jako slave dopasowuje prostą do zegara (regresja liniowa, N21), więc spakowany zegar USB z DAW daje tempo zegara, a nie częstotliwość pakietów. |
| E1.16, E1.18 | Kolejka zdarzeń jako bufor pierścieniowy z jednym scaleniem na granicę kroku. Najgorszy koszt wywołania spadł z 3861 do 330 zapisów (ok. 9% bloku audio, wcześniej 22–29%). |
| E1.17 | Porządki po wszystkich przeglądach E1: ok. 280 zaległych uwag i 7 poprawionych błędów rdzenia. `ASSERT_EQ` odrzuca liczby zmiennoprzecinkowe. |
| E1.19 | Estymator tempa przechodzi pojedynczy fałszywy takt MIDI, podwójny impuls na jacku (także dokładnie w połowie okresu) i odbicie styku. Prawdziwe skoki tempa restartują estymację jak wcześniej. |
| E2.1 | Podstawa czasu z licznika DWT procesora i kolejki synchronizacji (SPSC). |
| E2.2 | `sequencer.c` to klej wokół rdzenia: callbacki, skrzynka (mailbox), kopie stanu dla UI. |
| E2.3 | D4: jedno MIDI Note On na wyzwolenie, Note Off dopasowany do głosu, echo raz. |
| E2.4 | D7 i D8: przerwania pobierają tylko znacznik czasu, a całe MIDI jest parsowane w `seq_tick()`. |
| E2.5 | Jacki clock in i reset idą przez kolejkę synchronizacji. |
| E2.6 | Usunięty `clockSync` i martwy kod zegara; `sequencer.c` wyszedł z ITCM. |
| E2.7 | Błąd fw 1.7: przycisk kroku w menu globalnym ustawiał długość wszystkich torów. Naprawiony. |
| E2.8 | USB MIDI out: pakiety z poprawnym CIN, bufor czekający na endpoint. Koniec z wysyłaniem 128 bajtów spoza bufora. |

**Zasoby po integracji** (fw 1.7 w nawiasach):

| | Teraz | fw 1.7 |
|---|---|---|
| DTCM + RAM | 391 660 B (+13 240 B) | 378 420 B |
| ITCM | 13 128 B (−1 872 B) | 15 000 B |
| Obraz | 299 092 B (+24 404 B) | 274 688 B |
| Testy na hoście | 26 identyfikatorów ctest, wszystkie przechodzą, 0 ostrzeżeń | — |

## Co przeglądy znalazły

Prawdziwe błędy wyłapane i poprawione przed wypchnięciem:

- **Wyścig przy nagrywaniu automatyki (E2.2).** Funkcja czytała numer kroku dwa razy. Gdyby sekwencer
  zatrzymał się między odczytami, sięgnęłaby do kroku 255, poza tablicę.
- **Program Change wyprzedzany przez Start (E2.4).** DAW, który wysyła PC, a zaraz potem Start, ruszyłby
  ze starym patternem. Teraz PC idzie tą samą kolejką co Start, w kolejności znaczników czasu.
- **Przestarzały czas w kolejce (E2.4, E2.6).** Ładowanie kitu po zmianie patternu trwa długo, a
  zdarzenia synchronizacji z tego czasu dostawały stary znacznik. Teraz czas jest odczytywany przed
  każdym wpisem.
- **Zgubione zbocze bramki resetu (E2.5).** Przerwanie kasowało flagę drugi raz, już po odczycie pinu.
  To błąd jeszcze z fw 1.7.
- **Licznik choke'ów i takt zegara po SPP (E1.18).** Przy czyszczeniu kolejki usunięte zdarzenia nie
  były liczone. Po SPP i przełączeniu trybu sync krok grał bez swojego taktu MIDI clock.
- **Estymator tempa (E1.19).** Pierwsza wersja brała prawdziwy skok tempa o 2–2,5× za podwójny impuls,
  a odbicie styku w połowie przypadków wypychało prawdziwy impuls. Poprawione i sprawdzone na 36 360
  przebiegach.
- **Stary błąd parsera MIDI z fw 1.7.** Tune Request po Note On wychodził jako `F6 3C 64`. Poprawione
  w E2.8.

Błędy procesu, wyłapane i zamienione w reguły dla agentów:

- Fixery dwa razy zapisały cały `sequencer.c` z końcami linii LF zamiast CRLF.
- Jeden fixer nadpisał swój niewypchnięty commit przez `--amend`.

## Jak to robiliśmy

Od B obowiązywały dwie Twoje decyzje z 25.10:

- przegląd zgodności i przegląd jakości pracują równolegle, a jeden fixer bierze obie listy;
- rundy 2 i 3 sprawdzają tylko poprawki.

| | Workflow A2 (E1.15–E1.17) | Workflow B (E1.18, E2.1–E2.8, E1.19) |
|---|---|---|
| Zadań | 3 | 10 |
| Czas | ok. 9 h | 11 h |
| Na zadanie | 2,5–4 h | ok. 1 h (E2.1: 23 min, E1.19: 2 h 48 min) |
| Agenci | 24 | 70 |
| Tokeny agentów | 6,6 mln | 13,2 mln |

## Co zostało na przegląd E4.1

Te uwagi nie blokowały zadań. Zbiera je plan w „Execution amendments”, poz. 20–21:

- **Pozycje do rozstrzygnięcia: poprawić albo opisać jako ograniczenie.**
  - Nuta z jednorazowego rolla i echo nuty przychodzącej nie dostają Note Off po czasie.
  - Rzadki przypadek zgubionego impulsu tuż po skoku tempa na jacku.
  - Cofnięcie scalenia przywraca licznik sprzed `seqc_resetStats()`.
- **Luki w testach.** Część mutantów zabezpieczeń E1.19 przechodzi. Kontrole `fw_checks.sh` tylko
  przeszukują źródła.
- **Dokumentacja dla E2.9.** Zmiany z rund przeglądów E2 trzeba opisać w notatkach, a budżet RAM w R6.2
  ustawić na 391 824 B. To zadanie właśnie trwa.

## Co dalej

1. **Workflow C1:** E2.9, czyli notatki, oraz E3.1 (nazwa, wersja 0.1, ekran startowy), E3.3 (miernik
   CPU), E3.4 (CI) i E3.5 (README). Każde zadanie ma jeden łączny przegląd.
2. **E4.1:** przegląd całego etapu 1 we własnym workflowie. 8 grup recenzentów pracuje równolegle,
   sceptyk sprawdza każdą uwagę, a poprawki idą test-first.
3. **E4.2:** lista testów sprzętowych H1–H13 (plus U1 i X4) i dwa pliki aktualizacji. Testy na
   LXR-02 możesz zrobić tylko Ty.
4. **E4.3:** szkic notatek wydania v0.1. Nic nie jest publikowane bez Ciebie.
