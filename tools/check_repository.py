"""Check the public source tree without printing potentially private content."""
import ast
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = {'.blend', '.blend1', '.mod3', '.mrl3', '.tex', '.mhmpack', '.mhmprofile', '.dll', '.exe', '.zip', '.pem', '.key'}
PRIVATE = re.compile(r'(?i)[A-Z]:[\\/]+(?:Users|MHmod|SteamLibrary|Milice)[\\/]|gh[pousr]_[A-Za-z0-9]{20,}|AKIA[0-9A-Z]{16}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')

def main():
    paths = []
    ignored = {'.git', '.codegraph', '.superpowers', '.build', '.venv', '__pycache__', 'build', 'bin', 'obj', 'node_modules', 'target', 'gen'}
    for directory, folders, files in os.walk(ROOT, followlinks=False):
        folders[:] = [name for name in folders if name not in ignored and not (Path(directory) / name).is_symlink()]
        paths.extend(Path(directory) / name for name in files)
    python = 0
    for path in paths:
        if path.suffix.lower() in FORBIDDEN or path.name.startswith('.env'):
            raise ValueError('Generated/asset/credential file in public tree: ' + path.relative_to(ROOT).as_posix())
        text = path.read_text(encoding='utf-8-sig')
        if PRIVATE.search(text): raise ValueError('Private path or credential pattern in: ' + path.relative_to(ROOT).as_posix())
        if path.suffix == '.py': ast.parse(text, filename=str(path)); python += 1
        if path.suffix == '.md':
            for link in re.findall(r'\]\(([^)]+)\)', text):
                if '://' in link or link.startswith('#'): continue
                target = (path.parent / link.split('#')[0]).resolve()
                if not target.is_relative_to(ROOT) or not target.exists():
                    raise ValueError('Broken local documentation link: ' + path.relative_to(ROOT).as_posix())
    print(f'PASS: {len(paths)} public source files, {python} Python syntax checks, documentation links and private-content patterns')

if __name__ == '__main__': main()
