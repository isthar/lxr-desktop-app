# LICHOMAT: jedna sesja testowa na v0.3 (propozycja)

Zamiast trzech osobnych sesji (v0.1, potem v0.2, potem v0.3) możesz zrobić jedną, na v0.3: ta wersja
zawiera wszystko z v0.1 i v0.2. Procedury są w `docs/lichomat/hardware-checklist.md` (po angielsku,
z krokami, **Expected** i **Record**); tu jest tylko kolejność i to, co jest najważniejsze. Jeśli
któreś sprawdzenie na v0.3 nie przejdzie, powtórz je na wersji, z której pochodzi funkcja (H na v0.1,
U4–U14 na v0.2), i napisz mi, gdzie się zaczęło.

## Przygotowanie

- Skopiuj całą kartę SD na komputer.
- Plik aktualizacji z CI (nie lokalny build): `LXR_V2_LICHOMAT_0.3.img`, SHA-256 `d630eabc…`
  (pełny w run sheet listy sprawdzeń), pobierany poleceniem
  `gh run download 41126736959 --repo isthar/lxr-desktop-app -n lichomat-firmware-813aaf665353f9c7c2004f803cf7e08a4273049e`.
  Sprawdź go `shasum -a 256`. Ten plik ma już poprawki F13–F18 tam, gdzie zmieniają firmware (F13), a
  F14–F18 nie zmieniają w nim ani bajtu.
- Do sprawdzeń z powrotem do 1.7 (H13, U12, U14, U16, U18): `LXR_V2_V1.7.img`.
- Instalacja jak każda aktualizacja (lista sprawdzeń, „Installing an update file”). Ekran startowy:
  `LICHOMAT` i `v0.3 unofficial`.

## Kolejność

1. **Najpierw H1 i H12 (strona CPU).** Odczyt CPU odblokowuje v0.4 (pogłos, freeze, rumble, delay
   z tempem, filtr DJ). Jeśli masz czas tylko na to, wyślij mi same liczby z H12: `CPU`, `pk`, `Qmax`,
   `ovf` dla kroków 2–5, z nazwą kitu, efektem i BPM. Na ciężki przypadek (krok 3) dobrze nadaje się
   kit, w którym wszystkie głosy idą na FX, np. z Twojej paczki DLC: Faders, Malware, Kronky albo
   Penalty.
2. **Rdzeń timingu (v0.1):** H3, H4, H5, H8, H9. To serce całego projektu: równy timing, shuffle z
   ratchetami, MIDI clock out, choke na ścieżce, zmiana wzorca.
3. **Pliki i zgodność:** H2 (pliki z 1.7), U14 (blok ścieżek we wzorcach), U16 kroki 7–8 (sceny w
   projekcie i w 1.7), U18 krok 6 (slide i kit w 1.7), U19 (wartości `slt` Twoich kitów; w paczce
   DLC wszystkie są 0).
4. **Groove (v0.2):** U10 (tempo ścieżki), U11 (humanize i opóźnienie), U12 (warunki trigów; na v0.3
   `Fil` gra przy trzymanym kroku 8, patrz U15), U13 (rotacja).
5. **Występ i acid (v0.3):** U15 (fill), U16 kroki 1–6 (sceny), U17 (rolle), U18 kroki 1–5 (slide).
6. **Reszta:** H6, H7 (slave MIDI DIN i USB), H10 (nagrywanie), H11 (nadpisanie nuty MIDI), X4 (USB
   MIDI out), U1–U9 (poprawki fw 1.7), opcjonalne X1–X3 (jacki zegara). U8 porównuje dźwięk z v0.1:
   nagraj te dźwięki na v0.1 przed instalacją v0.3 albo pomiń U8.
7. **Na koniec H13:** powrót do 1.7, żeby wiedzieć, że droga z powrotem działa.

## Co mi wysłać

Najprościej: dla każdego sprawdzenia PASS albo FAIL i wartości z **Record** (wystarczy wiadomość, zdjęcie
ekranu albo wpis do run sheet na końcu listy sprawdzeń). Przy FAIL: co słychać albo widać i na której
wersji się zaczyna. Wpiszę to do trackera i zdecyduję o poprawkach.
