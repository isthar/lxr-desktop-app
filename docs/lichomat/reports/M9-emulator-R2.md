# M9: emulator, krok R2 – renderer WAV `lichomat-render` (E5.3) i poprawki F11, F12, F6b (E5.7)

Data: 2026-10-29. Gałąź `emulator`, wypchnięta na fork (Q19, od 29.10, 05:21), głowa `1099778`.
Plan: `docs/superpowers/plans/2026-10-29-emulator-r2-r3-renderer-goldens.md`, zadania R2.1–R2.6.

## Co powstało

**`lichomat-render`** uruchamia cały firmware LICHOMAT na wirtualnym urządzeniu i zapisuje dźwięk do
pliku WAV, bez czasu rzeczywistego. Na wejściu dostaje kopię Twojej karty (albo nową, z `--seed-card`)
i skrypt zdarzeń. Na wyjściu daje WAV, logi MIDI, zegara i panelu oraz podsumowanie w JSON.

| Część | Co robi |
|---|---|
| Opcje i karta (R2.1–R2.2) | `--help`, `--version`, sprawdzanie opcji, kopia karty, `--seed-card` (nowa karta jak po formatowaniu), kod 2 dla złej karty |
| Oś czasu (R2.3) | Reguła t0, WAV 4 kanały albo stereo, `--keep-card`, kod 5 |
| Skrypty zdarzeń (R2.4) | Przyciski, gałki, enkoder, MIDI DIN i USB, zegary (DIN, USB, jack), znaczniki; logi MIDI, zegara i panelu |
| Preludium (R2.5) | `--pattern`, `--song`, `--bpm`, `--param`, `--bars`; wybór wzorca i songu przez prawdziwy panel, edycja parametrów ścieżką menu |
| Zapisy i awarie (R2.6) | Zapis kitu, wzorca i projektu w skrypcie; awaria firmware daje kod 3 z historią ostatnich 32 zdarzeń, zawieszenie kod 4 (watchdog) |

**Poprawki błędów fw 1.7** (Twoja decyzja z 05:19: według moich rekomendacji). Każda jest osobnym
commitem z testem, który na kodzie bez poprawki padał:

| | Błąd | Na LXR-02 |
|---|---|---|
| F12 (Q23, `7f5732f`) | Wybór wzorca przyciskami kroków pytał o wartości euklid ścieżki 16 w tablicach 7-elementowych | Nic widocznego: strona Euclid i tak odświeża wartości. Test U9 w v0.2 |
| F11 (Q21, `7399c7d`) | Gałka nie na 0° przy włączeniu: odczyt przez NULL przy bootowaniu | Bez zmian w działaniu |
| F6b (Q22, `cf8fb4d`) | `freq2PhaseIncr1024/32767` bez nasycenia przy bardzo wysokim dźwięku | Obraz bajt w bajt ten sam |

**Liczby:**

| | Teraz |
|---|---|
| Testy emulatora | 133 identyfikatory ctest, wszystkie przechodzą w Debug (UBSan) i w Release, szeregowo i `-j8`, 0 ostrzeżeń |
| Testy firmware na hoście | 55, wszystkie przechodzą, 0 ostrzeżeń |
| DTCM + RAM | 391 796 B (bez zmian) |
| Obraz | 300 812 B (F11 skróciło `appMain_boot()` o 4 B) |
| CI na GitHubie | Zielone dla obu gałęzi (dotąd tylko zadania firmware; CI emulatora z ASan to R3.6) |

## Przegląd i poprawki

Jeden lekki przegląd R2.1–R2.4 i poprawek F11, F12, F6b. Wynik: 1 ważna uwaga i 8 drobnych; poprawki
F nie miały żadnej.
- **Ważna:** w skrypcie nuta MIDI wpisana po linii zegara trafiała do urządzenia dopiero po całym biegu
  zegara (nuta z 300 ms grała w 1180 ms). Teraz wszystkie wejścia idą w kolejności czasu (`e10cfb0`).
- **Drobne, wszystkie poprawione** (`14d43ae`, `1099778`):
  - ścieżka karty w ostrzeżeniach;
  - limit 4 GiB pliku WAV, sprawdzany już przy opcjach;
  - zegar zatrzymuje się na końcu renderu;
  - listy w podsumowaniu bez limitu;
  - ostrzeżenia przy każdym kodzie wyjścia;
  - commit w `--version` brany przy każdym buildzie;
  - dwie luki w testach.

## Co się okazało po drodze

- **Równoległy ctest:** 4 testy dzieliły jeden folder tymczasowy i liczyły cudze kopie kart. Każdy
  przypadek ma teraz własny folder (`c253669`).
- **Zapis projektu w fw 1.7** najpierw zapisuje edytowany kit do slotu wybranego na stronie zapisu
  (po bootowaniu kit 0; `presetManager.c:2319`). Specyfikacja §5.4 opisywała to inaczej i jest poprawiona.
- **`PAR_BPM` ma numer 272**, nie 250, jak mówi komentarz w `Parameters.h`.

## Co dalej

- **Ja, w następnej sesji:** R3.1–R3.6, czyli karty testowe, nagrania wzorcowe G1–G10 z `emu_golden.py`
  i CI emulatora z ASan (`emulator.yml`). Pierwszy przebieg z ASan może znaleźć kolejne błędy fw 1.7;
  zdecyduję o nich według Twojej reguły z 29.10.
- **Ty:** tylko testy v0.1 na LXR-02 (Q6). Pozostałe pytania są zamknięte moimi rekomendacjami.
