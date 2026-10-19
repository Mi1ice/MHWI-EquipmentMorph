"""Collect dependency notices from the locked, locally installed source packages."""
import json
from pathlib import Path


def license_texts(root, extra=None):
    paths = {p for p in root.iterdir() if p.is_file() and p.name.lower().startswith(('license', 'licence', 'copying', 'notice'))}
    if extra:
        path = (root / extra).resolve()
        if path.is_relative_to(root.resolve()) and path.is_file():
            paths.add(path)
    result = []
    for path in sorted(paths):
        result.append(path.name + '\n' + path.read_text(encoding='utf-8-sig'))
    return result


def collect(root):
    metadata = json.loads((root / '.build/current-validation/desktop/cargo-metadata.json').read_text(encoding='utf-8-sig'))
    entries = []
    fallback = {'alloc-stdlib': 'alloc-stdlib-BSD.txt', 'selectors': 'selectors-MPL.txt',
                'tauri-plugin': 'tauri-plugin-MIT.txt', 'webview2-com': 'webview2-MIT.txt',
                'webview2-com-macros': 'webview2-MIT.txt', 'webview2-com-sys': 'webview2-MIT.txt'}
    for package in metadata['packages']:
        if not package['source']:
            continue
        directory = Path(package['manifest_path']).parent
        texts = license_texts(directory, package.get('license_file'))
        if not texts and package['name'] in fallback:
            texts = [(root / 'controls/MHWI-EquipmentMorph-Desktop/licenses' / fallback[package['name']]).read_text(encoding='utf-8')]
        if not texts:
            raise ValueError('Dependency license text missing: ' + package['name'])
        entries.append((package['name'] + ' ' + package['version'], package.get('license'),
                        package.get('authors', []), texts))
    desktop = root / 'controls/MHWI-EquipmentMorph-Desktop'
    lock = json.loads((desktop / 'package-lock.json').read_text(encoding='utf-8'))
    for name, package in lock['packages'].items():
        if not name or package.get('dev'):
            continue
        directory = desktop / name
        info = json.loads((directory / 'package.json').read_text(encoding='utf-8'))
        texts = license_texts(directory)
        if not texts:
            raise ValueError('Dependency license text missing: ' + info['name'])
        entries.append((info['name'] + ' ' + info['version'], info.get('license'), [], texts))
    lines = ['MHWI-EquipmentMorph ControlCenter dependency notices',
             'Includes locked build and platform dependencies; not all packages are linked into the Windows executable.',
             'WebView2, Windows fonts, Node.js, Rust and MSVC are separately installed prerequisites, not redistributed here.', '']
    for name, license_id, authors, texts in sorted(entries):
        lines.extend(['=' * 72, name, 'License: ' + str(license_id), 'Authors: ' + ', '.join(authors)])
        lines.extend(texts)
        lines.append('')
    return '\n'.join(lines) + '\n'
