"""KarmaTV PostFX - testi da tradurre.

Raccoglie i testi italiani del menu (tr("..."), catalogo effetti, nomi dei tasti) e controlla
che ogni file lang/<codice>.tsv li abbia tutti.

    python tools/i18n_check.py            elenco dei testi mancanti per lingua
    python tools/i18n_check.py --keys     tutti i testi (uno per riga, con \\n e \\0 come sequenze)
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LANGS = ['en', 'es', 'fr', 'de', 'pt']
LITERAL = r'"((?:[^"\\\n]|\\.)*)"'


def c_unescape(text):
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t', '0': '\0'}.get(m.group(1), m.group(1)), text)


def escape(text):
    return text.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\0', '\\0')


def read(rel):
    return io.open(os.path.join(ROOT, rel), encoding='utf-8-sig').read()


def keys():
    found = []

    def add(text):
        text = text.split('##')[0]
        if text and re.search(r'[A-Za-zÀ-ÿ]', text) and text not in found:
            found.append(text)

    for folder, _, files in os.walk(os.path.join(ROOT, 'src')):
        for name in files:
            if not name.endswith('.cpp'):
                continue
            src = read(os.path.relpath(os.path.join(folder, name), ROOT))
            # tr("a" "b") anche su piu' righe
            for m in re.finditer(r'\btr\(\s*((?:' + LITERAL + r'\s*)+)\)', src):
                add(''.join(c_unescape(x) for x in re.findall(LITERAL, m.group(1))))

    catalog = read('src/core/effects_catalog.cpp')
    # Parametri: {"KTV_x", "Etichetta", min, max, def, "voci\0" | nullptr, "Gruppo"}
    for m in re.finditer(r'\{"KTV_\w+",\s*' + LITERAL + r'([^}]*)\}', catalog):
        add(c_unescape(m.group(1)))
        rest = m.group(2)
        items = re.search(r',\s*' + LITERAL + r'\s*(?:,|$)', rest)
        for text in re.findall(LITERAL, rest):
            value = c_unescape(text)
            if '\0' in value:
                for item in value.split('\0'):
                    add(item)
            else:
                add(value)
    # Effetti: {"id", "Etichetta", C::...
    for m in re.finditer(r'\{"[a-z0-9_]+",\s*' + LITERAL + r',\s*C::', catalog):
        add(c_unescape(m.group(1)))
    for m in re.finditer(r'case C::\w+: return ' + LITERAL, catalog):
        add(c_unescape(m.group(1)))

    hotkeys = read('src/input/hotkeys.cpp')
    for m in re.finditer(r'\{"[a-z_]+",\s*' + LITERAL + r',\s*\d+\}', hotkeys):
        add(c_unescape(m.group(1)))

    updater = read('src/update/updater.cpp')
    for m in re.finditer(r'set_state\(State::Failed,\s*' + LITERAL + r'\)', updater):
        add(c_unescape(m.group(1)))

    for name in ('Originale rF2',):
        add(name)
    return found


def load(code):
    table = {}
    path = os.path.join(ROOT, 'lang', code + '.tsv')
    if not os.path.exists(path):
        return table
    for line in io.open(path, encoding='utf-8-sig').read().split('\n'):
        if not line or line.startswith('#') or '\t' not in line:
            continue
        key, value = line.split('\t', 1)
        table[c_unescape(key)] = value
    return table


def main():
    all_keys = keys()
    if '--keys' in sys.argv:
        sys.stdout.reconfigure(encoding='utf-8')
        for k in all_keys:
            print(escape(k))
        return 0
    missing_total = 0
    for code in LANGS:
        table = load(code)
        missing = [k for k in all_keys if k not in table]
        unused = [k for k in table if k not in all_keys]
        missing_total += len(missing)
        print(f'{code}: {len(all_keys) - len(missing)}/{len(all_keys)} tradotti, {len(missing)} mancanti, {len(unused)} non usati')
        for k in missing[:10]:
            print('   manca:', escape(k))
    return 1 if missing_total else 0


if __name__ == '__main__':
    sys.exit(main())
