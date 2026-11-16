"""Update authored text references from the reviewed UE migration manifest.

Dry-run by default. Binary UE packages are exclusively handled by AssetTools.
"""
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    manifest = json.loads((ROOT / 'Tools/AssetMigrationManifest.json').read_text(encoding='utf-8'))
    supplemental = ROOT / 'Tools/MainMapMigrationManifest.json'
    rows = manifest['assets'] + (json.loads(supplemental.read_text(encoding='utf-8'))['assets'] if supplemental.exists() else [])
    assets = sorted(rows, key=lambda r: -len(r['old']))
    rules = manifest['rules']
    prefixes = sorted(rules['prefix_rules'], key=lambda pair: -len(pair[0]))

    def remap(match):
        value = match.group()
        for row in assets:
            if value == row['old'] or value.startswith(row['old'] + '.'):
                return row['new'] + value[len(row['old']):]
        for excluded in rules['excluded_assets'] + rules['protected_roots']:
            if value == excluded or value.startswith(excluded + '.') or value.startswith(excluded + '/'):
                return value
        # Directory/name prefixes also cover runtime-generated asset paths.
        for old, new in prefixes:
            if value.startswith(old):
                return new + value[len(old):]
            if old.endswith('/') and value == old[:-1]:
                return new.rstrip('/')
        return value

    roots = {
        'Source': {'.h', '.cpp', '.cs'}, 'Config': {'.ini'},
        'Tools': {'.py', '.ps1'}, 'SourceArt': {'.json', '.md', '.txt'},
        'Content/Collections': {'.collection'},
    }
    changes = []
    for folder, extensions in roots.items():
        for path in sorted((ROOT / folder).rglob('*')):
            if not path.is_file() or path.suffix.lower() not in extensions:
                continue
            if path.relative_to(ROOT).as_posix() in {
                'Source/CyberCafe/Editor/AssetOrganizationLibrary.cpp',
                'Tools/Editor/organize_main_map.py',
            }:
                continue  # Migration source guards must retain their historical paths.
            original = path.read_bytes()
            encoding = 'utf-8-sig' if original.startswith(b'\xef\xbb\xbf') else 'utf-8'
            text = original.decode(encoding)
            updated = re.sub(r'/Game/[A-Za-z0-9_./:-]+', remap, text)
            if updated == text:
                continue
            relative = path.relative_to(ROOT)
            changes.append(relative.as_posix())
            if args.apply:
                backup = ROOT / ('Saved/MapOrganization/TextBackup' if supplemental.exists() else 'Saved/AssetOrganization/TextBackup') / relative
                backup.parent.mkdir(parents=True, exist_ok=True)
                if not backup.exists():
                    shutil.copy2(path, backup)
                path.write_bytes(updated.encode(encoding))
    report = ROOT / ('Saved/MapOrganization/text-reference-updates.json' if supplemental.exists() else 'Saved/AssetOrganization/text-reference-updates.json')
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps({'applied': args.apply, 'files': changes}, ensure_ascii=False, indent=2), encoding='utf-8')
    print(('Updated' if args.apply else 'Would update') + ' {} text files.'.format(len(changes)))
    for path in changes:
        print(path)


if __name__ == '__main__':
    main()
