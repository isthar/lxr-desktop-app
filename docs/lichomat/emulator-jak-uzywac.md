# Emulator LICHOMAT: jak samemu zrobić render swojego projektu

Emulator uruchamia cały firmware LICHOMAT na Macu, bez LXR-02, i zapisuje to, co LXR by zagrał, do pliku
WAV, razem z logiem MIDI. Gra tak samo jak firmware na LXR, poza tym, czego emulator nie modeluje
(analogowe wyjścia, czas procesora). To dobre narzędzie, żeby usłyszeć zmianę w firmware na własnych
kitach i wzorcach, zanim wgrasz ją na LXR.

## Raz: zbuduj emulator

W folderze repozytorium (albo w worktree gałęzi, której firmware chcesz usłyszeć, np. `build/wt-v03`
dla v0.3):

```sh
source tools/env.sh
cmake -S emu -B build/emu-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/emu-release
```

Renderer to `build/emu-release/lichomat-render`. Build Release jest kilka razy szybszy niż Debug
(ok. 90× czasu rzeczywistego).

## Render projektu z Twojej karty

Karta to folder z `PROJ00`, `PROJ01`… (na przykład kopia karty SD na dysku albo sama karta w czytniku).
Renderer pracuje na swojej kopii i nigdy nie zmienia oryginału.

```sh
R=build/emu-release/lichomat-render
$R --card /ścieżka/do/karty --project 3 --pattern 0 --bars 8 --stereo out1 --out wzorzec0.wav
$R --card /ścieżka/do/karty --project 3 --song 0 --bars 32 --stereo out1 --out song0.wav
```

- `--project N`: numer projektu (0–63), `--pattern P`: wzorzec, `--song S`: song.
- `--bars N`: długość w taktach; `--bpm B`: inne tempo niż zapisane w projekcie.
- `--stereo out1|out2|sum`: wyjście 1, wyjście 2 albo ich suma jako stereo. Bez tej opcji WAV ma
  4 kanały (oba wyjścia).
- `--midi-log plik.txt`: log wszystkiego, co LXR wysłałby przez MIDI.
- Wzorzec gra na swoim kicie, jeśli projekt ma włączone ładowanie kitu z wzorcem (tak jak na LXR).

## Zmiana parametrów i naciskanie przycisków

- `--param NAZWA=WARTOŚĆ` zmienia parametr tak, jak zrobiłby to enkoder w menu, np. `--param bpm=128`,
  `--param track_tempo=7` (×3/2 dla wybranej ścieżki), `--param roll=10` (roll 1/32). Nazwy są w
  `emu/render/render_params.h`. Parametrów celów LFO i velocity renderer nie zmienia, bo w menu mają
  osobną obsługę.
- `--events skrypt.txt` to lista zdarzeń w czasie, jedno w linii. Na przykład:

  ```
  100ms press 25          # drugi przycisk trybu: tryb wykonawczy
  +100ms press voice6     # wycisz głos 6
  +4000ms down step8      # trzymaj krok 8 (fill)
  +2000ms up step8
  +500ms din 90 30 7F     # nuta MIDI na wejściu DIN
  ```

  `+` liczy od poprzedniej linii. Z `--no-start` skrypt sam naciska START (`press start`). Gotowe
  przykłady są w `tools/demo_v02.sh` i `tools/demo_v03.sh`, a na Twoich kitach DLC w
  `build/dlc/demo_dlc.sh`.

## Porównanie dwóch wersji firmware

Zbuduj renderer w dwóch worktree (np. `build/wt-v02` i `build/wt-v03`) i zrób ten sam render obiema
wersjami. Pliki WAV możesz porównać w DAW (odwrócenie fazy jednego pokazuje różnicę).

## Prywatność

Rendery i kopie kart zostają na Twoim Macu. Jeśli firmware w emulatorze się zawiesi albo zgłosi błąd,
renderer zostawia kopię karty w folderze tymczasowym systemu (`lichomat-render-…`, ścieżka jest w
komunikacie), żeby dało się zbadać błąd; usuń ją, gdy nie jest potrzebna.
