#!/usr/bin/env python3
"""Build the run sheet page of the test session (tracker E8.11).

    python3 tools/runsheet/gen.py [out.html]     # default: build/runsheet/index.html

Takes each check's title, **Expected** and **Record** from docs/lichomat/hardware-checklist.md and
the order of docs/lichomat/test-session-v0.6.md (SECTIONS below, kept by hand), and fills them into
template.html. The page is published as a claude.ai artifact with the db capability; each result is
the document results/<item key>.

Copyright (C) 2026 Tomasz Krzywicki
SPDX-License-Identifier: GPL-3.0-only

Licensed under the GNU General Public License version 3, with the additional
permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
the repository.
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CHK = os.path.join(ROOT, 'docs', 'lichomat', 'hardware-checklist.md')
PLUG = os.path.join(ROOT, 'docs', 'lichomat', 'plugin-checks.md')	# E9.21/E8.12: the plugin's P1-P8 in Live


def squash(s):
    return re.sub(r'\s+', ' ', s).strip()


def parse_checks():
    src = open(CHK, encoding='utf-8').read()
    out = {}
    for s in re.split(r'^## ', src, flags=re.M)[1:]:
        head, _, body = s.partition('\n')
        m = re.match(r'([HUX]\d+(?:–[HUX]?\d+)?): (.*)', head)
        if head.startswith('Optional: clock jacks'):
            m = None
            rec = re.findall(r'\*\*Record[^*]*\*\*(.*?)(?=\n\n|\Z)', body, flags=re.S)
            exp = re.findall(r'\*\*Expected[^*]*\*\*(.*?)(?=\n\n|\Z)', body, flags=re.S)
            out['X1–X3'] = {'title': 'clock jacks (optional)', 'expected': [squash(e) for e in exp],
                            'record': [squash(r) for r in rec]}
            continue
        if not m:
            continue
        cid, title = m.group(1), m.group(2)
        exp = re.findall(r'\*\*Expected[^*]*\*\*(.*?)(?=\n\n|\Z)', body, flags=re.S)
        rec = re.findall(r'\*\*Record[^*]*\*\*(.*?)(?=\n\n|\Z)', body, flags=re.S)
        out[cid] = {'title': title, 'expected': [squash(e) for e in exp], 'record': [squash(r) for r in rec]}
        if cid == 'U4–U9':
            for row in re.findall(r'^\| (U\d) \| (.*?) \|$', body, flags=re.M):
                out[row[0]] = {'title': 'firmware changes F1–F12', 'expected': [row[1]],
                               'record': out[cid]['record']}
    return out


def parse_plugin_checks():
    """P1-P8 of plugin-checks.md: one table row each, | **Pn** (t) | do | expected | record |"""
    out = {}
    for m in re.finditer(r'^\| \*\*(P\d)\*\* \(([^)]*)\) \| (.*?) \| (.*?) \| (.*?) \|$',
                         open(PLUG, encoding='utf-8').read(), flags=re.M):
        cid, t, do, exp, rec = m.groups()
        out[cid] = {'title': squash(do), 'expected': [squash(exp)], 'record': [squash(rec)], 'time': t}
    assert sorted(out) == ['P%d' % i for i in range(1, 9)], sorted(out)
    return out


# The session, in the order of test-session-v0.6.md. kind "do": a step without a result (only done).
# file: a key of FILES, or "later" (chosen by P).
FILES = {
    '0.3': ['ci-0.3/LXR_V2_LICHOMAT_0.3.img', 'd630eabc'],
    '0.6': ['LXR_V2_LICHOMAT_0.6.img', '30a9730f'],
    '1.7': ['fw-1.7/LXR_V2_V1.7.img', 'cdb1ca37'],
    'half-0.4': ['po-H12/polowa/LXR_V2_LICHOMAT_0.4.img', '67ca9833'],
    'half-0.5': ['po-H12/polowa/LXR_V2_LICHOMAT_0.5.img', '0577e170'],
    'half-0.6': ['po-H12/polowa/LXR_V2_LICHOMAT_0.6.img', '79605882'],
    'lean-0.4': ['po-H12/lean/LXR_V2_LICHOMAT_0.4.img', 'e44de0b6'],
    'lean-0.5': ['po-H12/lean/LXR_V2_LICHOMAT_0.5.img', '1e396680'],
    'lean-0.6': ['po-H12/lean/LXR_V2_LICHOMAT_0.6.img', 'c7678823'],
}


def it(key, check, label, star=False, kind='check', note=None):
    d = {'key': key, 'check': check, 'label': label, 'star': star, 'kind': kind}
    if note:
        d['note'] = note
    return d


SECTIONS = [
    {'id': 'prep', 'title': 'Przygotowanie', 'time': 'dzień wcześniej, ok. 20 min', 'file': None, 'star': True,
     'intro': 'Okablowanie raz na cały wieczór: wyjście 1 LXR do interfejsu audio, MIDI OUT LXR do MIDI IN '
              'interfejsu, MIDI OUT interfejsu do MIDI IN LXR, USB LXR do komputera.',
     'items': [
         it('prep-sha', None, '`cd /Users/tk/Code/lxr/build/images && shasum -a 256 -c SHA256SUMS`: jedenaście razy `OK`', True, 'do'),
         it('prep-tools', None, 'SendMIDI i MIDI Monitor zainstalowane (na tym Macu ich nie ma)', True, 'do'),
         it('prep-daw', None, 'DAW: projekt 48 kHz, ślad stereo z wyjścia 1, ślady MIDI do DIN i do `Sonic Potions USB MIDI`; zegar MIDI w DAW wyłączony na oba porty', True, 'do'),
         it('prep-clips', None, 'Klipy MIDI gotowe: U28 (16 nut, odstępy 30–200 ms) i U35 (szesnastki na kanale 3, 142 BPM, kroki 2, 6, 10, 14 o 12 ms później, 4, 8, 12, 16 o 6 ms wcześniej)', False, 'do'),
         it('prep-folder', None, 'Folder na nagrania, np. `~/LXR-testy-<data>/`', False, 'do'),
         it('prep-proj62', None, 'Przy kopii karty: folder `PROJ62` z `build/images/h12/` na kartę (jeśli `PROJ62` jest zajęty, pod innym wolnym numerem)', True, 'do'),
     ]},
    {'id': 'p1', 'title': 'Część 1: v0.3', 'time': 'ok. 30 min', 'file': '0.3', 'star': True,
     'intro': 'Tylko to na 0.3: wszystko inne z 0.1–0.3 jest w 0.6.',
     'items': [
         it('p1-backup', None, '**Kopia całej karty SD na komputer**, obowiązkowo przed pierwszą instalacją', True, 'do'),
         it('p1-install', None, 'Instalacja 0.3 („Installing an update file” w liście sprawdzeń)', True, 'do'),
         it('p1-h1', 'H1', '**H1** i **U19 krok 1**: ekran `LICHOMAT` / `v0.3 unofficial`, nazwa pliku na karcie', True),
         it('p1-h2', 'H2', '**H2 krok 1**: pierwszy start z Twoją kartą z 1.7 bez `glo.cfg error`, ustawienia globalne jak w 1.7. Resztę H2 zrobisz na 0.6', True),
         it('p1-h12', 'H12', '**H12** kroki 1–5. Krok 3: LOAD, `Project`, `H12 CPU`, START (BPM 140 przychodzi z projektem). Krok 4: to samo z `TYP` `off` na stronie FX. Po H12 wczytaj swój projekt', True, 'h12'),
     ]},
    {'id': 's21', 'title': '2.1 Instalacja i Twoje projekty', 'time': '15 min', 'file': '0.6', 'star': True,
     'intro': 'Część 2: wszystko na 0.6. Z `trg smp` każde uderzenie jest stałe 2,90 ms po swoim czasie; `shf` +3 pokazuje też `mic` +36; przed wybraniem kroku strona kroku pokazuje `shf` −7 i `mic` −84 (to znaczy „żaden krok”).',
     'items': [
         it('s21-install', 'H1', 'Zainstaluj 0.6: ekran `LICHOMAT` / `v0.6 unofficial` (H1 i H2 na 0.6 załatwiają też U7)', True),
         it('s21-h2', 'H2', '**H2** kroki 1–4 na Twoich projektach', True),
         it('s21-p', None, 'Projekt testowy **P**: Twój projekt zapisany w pustym slocie (SAVE, `Project`, `Empty`). W P robisz wszystkie wzorce poniżej oprócz U2 i U3', True, 'do'),
     ]},
    {'id': 's22', 'title': '2.2 Rdzeń timingu', 'time': '55 min', 'file': '0.6',
     'intro': 'Synchronizacja wewnętrzna, DAW nagrywa wyjście 1, MIDI Monitor na DIN out, DAW nie wysyła zegara.',
     'items': [
         it('s22-h3', 'H3', '**H3**', True),
         it('s22-h4', 'H4', '**H4**', True),
         it('s22-h5', 'H5', '**H5** na wzorcu z H4, shuffle 127; krok 4 z piosenką A/B', True),
         it('s22-h8', 'H8', '**H8**, scenariusze a i b', True),
         it('s22-h9', 'H9', '**H9**, wzorce A i B (zapisz oba)', True),
         it('s22-h11', 'H11', '**H11**: tylko MIDI Monitor, bez syntezatora'),
         it('s22-h10', 'H10', '**H10** z `rqn` `off` (domyślne). Od następnej pętli krok 5 gra z zagranym przesunięciem: to nowość 0.6, nie błąd'),
     ]},
    {'id': 's23', 'title': '2.3 Klik: uderzenia co do próbki', 'time': '65 min', 'file': '0.6',
     'intro': 'Kit z klikiem z `docs/lichomat/kit-recipes-v0.5.md` (setup przed U27): zbuduj go raz i zapisz w slocie kitu w P.',
     'items': [
         it('s23-u27', 'U27', '**U27**: klik na 16 krokach przy 120 BPM', True),
         it('s23-u33', 'U33', '**U33** na tym samym wzorcu (dochodzą `mic`)', True),
         it('s23-u28', 'U28', '**U28**: LXR zatrzymany, klip U28 z DAW przez DIN, potem przez USB'),
         it('s23-u29', 'U29', '**U29**: zegar MIDI w DAW tylko do interfejsu (DIN), bufor 64 próbki; potem tylko do USB'),
         it('s23-u35', 'U35', '**U35**: zegar znowu tylko do DIN, klik na Drum 3, 142 BPM, klip U35', True,
            note='Zapisz dokładnie, co pokazuje `mic`. Oczekiwane +11 i −5 leżą blisko granicy zaokrąglenia; wynik o ±1 tick zapisz tak, jak jest, i nie poprawiaj klipu.'),
     ]},
    {'id': 's24', 'title': '2.4 Szablony i brzmienie na zwykłym kicie', 'time': '20 min', 'file': '0.6',
     'intro': 'Zegar MIDI w DAW wyłączony, synchronizacja wewnętrzna. Kit ze stopą na głosie 1 i hihatem na głosie 6.',
     'items': [
         it('s24-u34', 'U34', '**U34**: 142 BPM, `lay` na 100, 50 i 150 %, potem wszystkie szablony z Twoim groove', True),
         it('s24-u31', 'U31', '**U31**: `smp` i `blk` na sześciu dźwiękach'),
     ]},
    {'id': 's25', 'title': '2.5 LXR jako slave zegara z DAW', 'time': '55 min', 'file': '0.6',
     'items': [
         it('s25-h6', 'H6', '**H6**: zegar DAW tylko do DIN, bufor 64 próbki. Kroki 1–9; w kroku 8 SendMIDI do interfejsu'),
         it('s25-h7', 'H7', '**H7**: zegar DAW tylko do USB. Kroki 1–7; krok 6 przy 44,1 kHz z `build/images/midi/pairs.txt` i `frames.txt`. Potem wróć do 48 kHz i wyłącz zegar w DAW'),
     ]},
    {'id': 's26', 'title': '2.6 Groove i występ z 0.2 i 0.3', 'time': '80 min', 'file': '0.6',
     'intro': 'Synchronizacja wewnętrzna, P, zwykły kit. Wzorce, które trzeba zapisać na wycieczkę do 1.7 (2.9), są na końcu.',
     'items': [
         it('s26-u1', 'U1', '**U1**'),
         it('s26-u13', 'U13', '**U13**'),
         it('s26-u12', 'U12', '**U12** kroki 1–4 (wzorzec zapisz: krok 5 jest w 2.9)'),
         it('s26-u15', 'U15', '**U15** (fill)'),
         it('s26-u10', 'U10', '**U10** (tempo ścieżki, wzorce A i B, piosenka)'),
         it('s26-u11', 'U11', '**U11** (humanize, opóźnienie)'),
         it('s26-u14', 'U14', '**U14** kroki 1–4 (wzorzec F w P); krok 5 w 2.9'),
         it('s26-u16', 'U16', '**U16** kroki 1–7 (sceny w P); krok 8 w 2.9'),
         it('s26-u17', 'U17', '**U17** (rolle)'),
         it('s26-u18', 'U18', '**U18** kroki 1–5 (slide; wzorzec i kit zapisz, krok 6 w 2.9)'),
         it('s26-u9', 'U9', '**U9** (generator wzorców)'),
         it('s26-u19', 'U19', 'Opcjonalnie **U19** krok 2 (`slt` Twoich kitów)'),
     ]},
    {'id': 's27', 'title': '2.7 USB MIDI out', 'time': '15 min', 'file': '0.6',
     'items': [it('s27-x4', 'X4', '**X4** kroki 1–6 (w kroku 4 wyciągasz kabel USB)')]},
    {'id': 's28', 'title': '2.8 Poprawki błędów fw 1.7', 'time': '40 min', 'file': '0.6',
     'items': [
         it('s28-u2', 'U2', '**U2**: własne nowe projekty w pustym slocie; karta trzy razy do komputera'),
         it('s28-u3', 'U3', '**U3**: nowy projekt. W kroku 6 ścisz. Kroku 5 nie rób na 1.7'),
         it('s28-u4', 'U4', '**U4**: „as in v0.1” znaczy tu: porównaj z 1.7 i zapisz każdą różnicę'),
         it('s28-u6', 'U6', '**U6**, jak U4'),
     ]},
    {'id': 's29a', 'title': '2.9 Wycieczka do 1.7: przed instalacją 1.7', 'time': '45 min razem', 'file': '0.6',
     'intro': 'Zapis projektu w 1.7 zapisuje też jego `glo.cfg` (bajt `trg` jako 0), a U16 krok 8 zapisuje P. Dlatego U32 robisz w Twoim projekcie, nie w P.',
     'items': [
         it('s29-u36a', 'U36', '**U36** krok 1 w P (wzorzec z `mic`, szablonem i `amt` 70; wyłącz/włącz, projekt, `tmp.pat`)'),
         it('s29-u32a', 'U32', '**U32** krok 1, jako ostatnie przed 1.7: wczytaj Twój projekt, `trg` `blk`, zapisz ustawienia globalne, wyłącz i włącz'),
         it('s29-u36d', 'U36', 'Karta do komputera: **U36** krok 4 (rozmiary `.PAT` w folderze P: 3 697 i 3 941 B); skopiuj `fw-1.7/LXR_V2_V1.7.img` i usuń plik LICHOMAT'),
         it('s29-u5', 'U5', '**U5**: włącz LXR bez karty i pokręć gałką morph. Potem włóż kartę'),
     ]},
    {'id': 's29b', 'title': '2.9 W 1.7', 'time': '', 'file': '1.7',
     'items': [
         it('s29-h13a', 'H13', '**H13** krok 1 (ekran `Sonic Potions` / `LXR2 Drums V 1.7`) i **U32** krok 2 (start bez `glo.cfg error`; **nie** zapisuj ustawień globalnych)'),
         it('s29-u12b', 'U12', 'Wczytaj P: **U12** krok 5 (graj, nie ruszaj `prb`)'),
         it('s29-u14b', 'U14', '**U14** krok 5 (graj F, zapisz F)'),
         it('s29-u18b', 'U18', '**U18** krok 6 (graj, zapisz kit)'),
         it('s29-u36b', 'U36', '**U36** krok 2 (graj, zapisz wzorzec)'),
         it('s29-u16b', 'U16', 'Na końcu **U16** krok 8 (graj, zapisz projekt P)'),
         it('s29-h13b', 'H13', 'Wczytaj Twój projekt: **H13** krok 2 (gra jak zawsze). Ten projekt zostaje ostatnim użytym'),
     ]},
    {'id': 's29c', 'title': '2.9 Z powrotem na 0.6', 'time': '', 'file': '0.6',
     'items': [
         it('s29-u32c', 'U32', 'Start ładuje Twój projekt: **U32** krok 2 (`trg` pokazuje `blk`), potem `smp` i zapis ustawień globalnych'),
         it('s29-back', None, 'Wczytaj P: drugie połowy U16 krok 8, U14 krok 5, U12 krok 5, U18 krok 6 i U36 krok 2 (wyniki wpisz przy ich pozycjach w 1.7)', False, 'do'),
     ]},
    {'id': 's210', 'title': '2.10 Opcjonalnie', 'time': '', 'file': '0.6',
     'items': [
         it('s210-x', 'X1–X3', '**X1–X3** (gniazda zegara), jeśli masz moduły i oscyloskop'),
         it('s210-u8', 'U8', '**U8**: wymaga nagrań z v0.1, więc pomiń, chyba że chcesz zainstalować 0.1. O B13 decydujesz na stronie odsłuchu'),
     ]},
    {'id': 'later4', 'title': 'Po H12: 0.4', 'time': 'osobna sesja albo ten sam wieczór', 'file': 'later-0.4',
     'intro': 'Te sprawdzenia zależą od ostatecznej wersji pogłosu. Plik wskazuje P z H12 (powyżej). Nie czekasz na mnie ani na CI.',
     'items': [
         it('l4-u20', 'U20', '**U20**: CPU z pogłosem, na projekcie `H12 CPU`'),
         it('l4-u21', 'U21', '**U21**: pogłos i freeze'),
         it('l4-u22', 'U22', '**U22**: rumble'),
         it('l4-u23', 'U23', '**U23**: delay z tempem'),
         it('l4-u24', 'U24', '**U24**: filtr DJ'),
         it('l4-u25', 'U25', '**U25**: zgodność kitu z 1.7 (druga wycieczka do 1.7)'),
         it('l4-u26', 'U26', '**U26**: przepisy kitów 0.4'),
     ]},
    {'id': 'later5', 'title': 'Po H12: 0.5', 'time': '', 'file': 'later-0.5',
     'items': [
         it('l5-u30', 'U30', '**U30**: CPU z pogłosem w ostatecznej częstotliwości, na `H12 CPU`'),
         it('l5-u36', 'U36', '**U36** krok 3: wzorzec z 0.6 czytany przez 0.5 (przed tą sesją zapisz na 0.6 świeży wzorzec z `mic`)'),
     ]},
    {'id': 'later6', 'title': 'Po H12: 0.6', 'time': '', 'file': 'later-0.6',
     'items': [it('l6-u37', 'U37', '**U37**: CPU z szablonami i `mic`, z pogłosem w ostatecznej częstotliwości, na `H12 CPU`')]},
    # E8.12: the plugin prototype in Ableton Live; no LXR needed (docs/lichomat/plugin-checks.md)
    {'id': 'plugin', 'title': 'Wtyczka w Abletonie (bez LXR)', 'time': 'ok. 20 min', 'file': None, 'star': True,
     'intro': 'Prototyp LICHOMAT.vst3, gałąź `plugin`. Instalacja przy zamkniętym Live: '
              '`cd /Users/tk/Code/lxr/build/wt-plugin && plugin/tools/install_plugin.sh` (na końcu `47 tests passed, 0 tests failed`). '
              'Live 12, 48 kHz, bufor 128 albo 256, Options → Delay Compensation włączone. Opis kroków: `docs/lichomat/plugin-checks.md`; '
              'logi: `~/Library/Application Support/LICHOMAT Plugin/logs/`.',
     'items': [
         it('plug-install', None, 'Instalacja (`plugin/tools/install_plugin.sh`, Live zamknięty): `47 tests passed, 0 tests failed`', True, 'do'),
         it('plug-p1', 'P1', '**P1** Live widzi LICHOMAT (Plug-Ins → VST3)', True),
         it('plug-p2', 'P2', '**P2** urządzenie na ścieżce MIDI, proces `lichomat-engine`', True),
         it('plug-p3', 'P3', '**P3** klip z C2 na ósemki „i”: równo z metronomem', True),
         it('plug-p4', 'P4', '**P4** tempo 120 → 140, start od taktu 3, pętla 1 takt', True),
         it('plug-p5', 'P5', '**P5** dwie instancje', True),
         it('plug-p6', 'P6', '**P6** zapis i otwarcie setu', True),
         it('plug-p7', 'P7', '**P7** Resampling na siatce, Freeze', True),
         it('plug-p8', 'P8', '**P8** usunięcie i cofnięcie, nowe urządzenie, zabicie silnika', True),
     ]},
]


def main():
    checks = parse_checks()
    checks.update(parse_plugin_checks())
    used = {i['check'] for s in SECTIONS for i in s['items'] if i['check']}
    missing = [c for c in used if c not in checks]
    assert not missing, missing
    keys = [i['key'] for s in SECTIONS for i in s['items']]
    assert len(keys) == len(set(keys)), 'duplicate keys'
    data = {'sections': SECTIONS, 'files': FILES,
            'checks': {c: checks[c] for c in sorted(used)}, 'threshold': 63.28}
    tpl = open(os.path.join(HERE, 'template.html'), encoding='utf-8').read()
    blob = json.dumps(data, ensure_ascii=False).replace('</', '<\\/')
    html = tpl.replace('/*__DATA__*/null', blob)
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'runsheet', 'index.html')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    open(out, 'w', encoding='utf-8').write(html)
    n = len(keys)
    print(f'{n} items, {len(used)} checks, {len(html)} bytes')


if __name__ == '__main__':
    main()
