"""Create a small source archive without SDKs, build outputs, or test media."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parent.parent
output = root / 'dist' / 'FluxPlayer-source.zip'
output.parent.mkdir(exist_ok=True)
files = [root / name for name in ['CMakeLists.txt', 'README.md', 'THIRD_PARTY.md', '.gitignore', 'Run-Player.cmd']]
for directory in ['src', 'cmake', 'scripts', 'tests', 'docs']:
    files.extend(path for path in (root / directory).rglob('*') if path.is_file() and '__pycache__' not in path.parts)
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for path in sorted(files):
        archive.write(path, Path('FluxPlayer') / path.relative_to(root))
print(output)
