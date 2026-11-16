"""Reviewed follow-up migration: cafe map plus its real VRTemplate dependencies."""
import importlib.util
import json
import os
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
SPEC = importlib.util.spec_from_file_location('asset_organization_helpers', Path(__file__).with_name('organize_project_assets.py'))
base = importlib.util.module_from_spec(SPEC)
previous = os.environ.get('CYBERCAFE_ASSET_MIGRATION_MODE')
os.environ['CYBERCAFE_ASSET_MIGRATION_MODE'] = 'library'
try:
    SPEC.loader.exec_module(base)
finally:
    if previous is None:
        os.environ.pop('CYBERCAFE_ASSET_MIGRATION_MODE', None)
    else:
        os.environ['CYBERCAFE_ASSET_MIGRATION_MODE'] = previous
base.REPORTS = PROJECT / 'Saved/MapOrganization'
base.REPORTS.mkdir(parents=True, exist_ok=True)
manifest_file = PROJECT / 'Tools/MainMapMigrationManifest.json'
OLD_MAP = '/Game/VRTemplate/Maps/L_Cafe'
NEW_MAP = '/Game/CyberCafe/Maps/L_Cafe'


def plan():
    if manifest_file.exists():
        raise RuntimeError('Reviewed map migration manifest already exists.')
    report = json.loads((base.REPORTS / 'template-dependencies.json').read_text(encoding='utf-8'))
    registry, options = base.registry_and_options()
    rows = []
    for item in report['real_template_assets']:
        old = item['package']
        name = old.rsplit('/', 1)[1]
        if old == OLD_MAP:
            new = NEW_MAP
        elif '/Materials/Liquids/' in old:
            new = '/Game/CyberCafe/Gameplay/Drinks/Materials/Liquids/' + name
        elif '/Materials/Hologram/' in old:
            new = '/Game/CyberCafe/Gameplay/Serving/Materials/Hologram/' + name
        elif '/VFX/' in old:
            new = '/Game/CyberCafe/Player/VFX/' + name
        elif '/Materials/Functions/' in old:
            new = '/Game/CyberCafe/Shared/Materials/Functions/' + name
        elif name in {'M_Outline', 'M_Outline_Inst', 'M_TeleportNoise', 'M_TeleportCylinder'}:
            new = '/Game/CyberCafe/Player/Materials/' + name
        elif name == 'DefaultRibbonMaterial_NoGradient':
            new = '/Game/CyberCafe/Gameplay/Drinks/Materials/Pouring/' + name
        else:
            raise RuntimeError('Unreviewed template asset: ' + old)
        if registry.get_assets_by_package_name(new):
            raise RuntimeError('Destination exists: ' + new)
        rows.append({'old': old, 'new': new, 'class': item['classes'][0]})
    affected, pending = set(), [r['old'] for r in rows]
    while pending:
        package = pending.pop()
        if package in affected:
            continue
        affected.add(package)
        pending.extend(str(p) for p in registry.get_referencers(package, options)
                       if str(p).startswith('/Game/') and str(p) not in affected)
    base.write_json(manifest_file, {'version': 1, 'main_map': NEW_MAP, 'old_main_map': OLD_MAP,
                                  'assets': rows, 'affected_packages': sorted(affected)})


def apply(manifest):
    if (base.REPORTS / 'applied.json').exists():
        raise RuntimeError('Map migration already applied.')
    base.backup(manifest)
    base.write_json(base.REPORTS / 'main-map-before.json', base.actor_snapshot(OLD_MAP))
    world = unreal.load_asset(OLD_MAP)
    registry, _ = base.registry_and_options()
    imports = []
    for row in sorted(manifest['assets'], key=lambda r: r['class'] == 'World'):
        asset = world if row['class'] == 'World' else registry.get_assets_by_package_name(row['old'])[0].get_asset()
        if not asset:
            raise RuntimeError('Source failed to load: ' + row['old'])
        imports.extend(unreal.AssetOrganizationLibrary.capture_import_paths(asset))
        ok = (unreal.AssetOrganizationLibrary.move_main_cafe_map(world) if row['class'] == 'World'
              else unreal.AssetOrganizationLibrary.move_reviewed_asset(asset, row['new']))
        if not ok:
            raise RuntimeError('Move failed: ' + row['old'])
        base.save_dirty_game_packages()
    if not unreal.AssetOrganizationLibrary.restore_import_paths(imports):
        raise RuntimeError('Could not retain reimport sources.')
    mapping = {r['old']: r['new'] for r in manifest['assets']}
    names = sorted({mapping.get(p, p) for p in manifest['affected_packages']})
    packages = unreal.AssetOrganizationLibrary.load_project_packages(names)
    if len(packages) != len(names):
        raise RuntimeError('Affected package did not load.')
    paths = {unreal.SoftObjectPath(old + '.' + old.rsplit('/', 1)[1]):
             unreal.SoftObjectPath(new + '.' + new.rsplit('/', 1)[1]) for old, new in mapping.items()}
    unreal.AssetToolsHelpers.get_asset_tools().rename_referencing_soft_object_paths(packages, paths)
    if not unreal.EditorLoadingAndSavingUtils.save_packages(packages, only_dirty=False):
        raise RuntimeError('Reference normalization did not save.')
    base.save_dirty_game_packages()
    base.write_json(base.REPORTS / 'applied.json', {'main_map': NEW_MAP, 'moved_assets': len(manifest['assets'])})


mode = os.environ.get('CYBERCAFE_MAP_MIGRATION_MODE', 'plan')
if mode == 'plan':
    plan()
elif mode in {'apply', 'verify'}:
    manifest = json.loads(manifest_file.read_text(encoding='utf-8'))
    (apply if mode == 'apply' else base.verify)(manifest)
else:
    raise RuntimeError('Unknown map migration mode.')
