"""UE5.6 editor package migration. Plan by default; apply/verify use an explicit environment mode.

No filesystem renames of UE packages, no asset deletion and no redirector cleanup.
"""
import collections
import hashlib
import json
import os
from pathlib import Path
import shutil
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
CONTENT = PROJECT / 'Content'
REPORTS = PROJECT / 'Saved' / 'AssetOrganization'
MANIFEST = PROJECT / 'Tools' / 'AssetMigrationManifest.json'
RULES_FILE = PROJECT / 'Tools' / 'AssetOrganizationRules.json'
REPORTS.mkdir(parents=True, exist_ok=True)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def registry_and_options():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True, include_hard_package_references=True,
        include_searchable_names=False, include_soft_management_references=True,
        include_hard_management_references=True)
    return registry, options


def destination(package, rules):
    if package in rules['excluded_assets']:
        return None
    if any(package == root or package.startswith(root + '/') for root in rules['protected_roots']):
        return None
    if package in rules['exact_rules']:
        return rules['exact_rules'][package]
    for old, new in sorted(rules['prefix_rules'], key=lambda pair: -len(pair[0])):
        if package.startswith(old):
            return new + package[len(old):]
    return None


def package_files(package):
    if not package.startswith('/Game/'):
        return []
    base = CONTENT / package[len('/Game/'):]
    if not base.resolve().is_relative_to(CONTENT.resolve()):
        raise RuntimeError('Package escaped project Content: ' + package)
    return [p for p in base.parent.glob(base.name + '.*')
            if p.suffix.lower() in {'.uasset', '.umap', '.uexp', '.ubulk', '.uptnl'}]


def plan():
    if MANIFEST.exists():
        raise RuntimeError('An existing manifest must be reviewed before generating another plan.')
    rules = json.loads(RULES_FILE.read_text(encoding='utf-8'))
    registry, options = registry_and_options()
    assets = registry.get_assets_by_path('/Game', recursive=True)
    known = {str(a.package_name) for a in assets}
    rows, targets = [], set()
    for asset in sorted(assets, key=lambda a: str(a.package_name)):
        old = str(asset.package_name)
        new = destination(old, rules)
        if not new or str(asset.asset_class_path.asset_name) in {'ObjectRedirector', 'World'}:
            continue
        if new in targets or new in known:
            raise RuntimeError('Duplicate or existing destination: ' + new)
        if not package_files(old):
            raise RuntimeError('Source package file missing: ' + old)
        targets.add(new)
        parts = new.split('/')
        group = '/'.join(parts[3:5]) if parts[3] == 'Gameplay' else parts[3]
        rows.append({'old': old, 'new': new, 'class': str(asset.asset_class_path.asset_name), 'group': group})
    # Back up transitive referencers as well as the selected assets, before any rename.
    affected, pending = set(), [r['old'] for r in rows]
    while pending:
        package = pending.pop()
        if package in affected:
            continue
        affected.add(package)
        pending.extend(str(p) for p in registry.get_referencers(package, options)
                       if str(p).startswith('/Game/') and str(p) not in affected)
    manifest = {'version': 1, 'main_map': rules['main_map'], 'rules': rules,
                'assets': rows, 'affected_packages': sorted(affected)}
    write_json(MANIFEST, manifest)
    unreal.log('Migration plan: {} assets; {} potentially affected packages; groups={}'.format(
        len(rows), len(affected), dict(collections.Counter(r['group'] for r in rows))))


def actor_snapshot(map_path):
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if not world:
        raise RuntimeError('Could not load main map: ' + map_path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    rows = []
    for actor in actors:
        location, rotation, scale = actor.get_actor_location(), actor.get_actor_rotation(), actor.get_actor_scale3d()
        rows.append({'name': actor.get_name(), 'class': actor.get_class().get_path_name(),
                     'label': actor.get_actor_label(),
                     'transform': [location.x, location.y, location.z, rotation.pitch, rotation.yaw, rotation.roll,
                                   scale.x, scale.y, scale.z]})
    if not rows:
        raise RuntimeError('No actors found; refuse an empty scene baseline.')
    return sorted(rows, key=lambda r: r['name'])


def backup(manifest):
    index_file = REPORTS / 'Backup' / 'index.json'
    if index_file.exists():
        return
    records = []
    for package in manifest['affected_packages']:
        for source in package_files(package):
            relative = source.relative_to(PROJECT)
            target = REPORTS / 'Backup' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            digest = hashlib.file_digest(source.open('rb'), 'sha256').hexdigest()
            records.append({'path': relative.as_posix(), 'bytes': source.stat().st_size, 'sha256': digest})
    write_json(index_file, records)


def save_dirty_game_packages():
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages += list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    packages = [p for p in packages if p.get_name().startswith('/Game/')]
    if packages and not unreal.EditorLoadingAndSavingUtils.save_packages(packages, only_dirty=True):
        raise RuntimeError('Could not save updated project packages.')
    return sorted({p.get_name() for p in packages})


def apply(manifest):
    if (REPORTS / 'applied.json').exists():
        raise RuntimeError('Migration was already applied; use verify mode.')
    backup(manifest)
    before_file = REPORTS / 'main-map-before.json'
    if not before_file.exists():
        write_json(before_file, actor_snapshot(manifest['main_map']))
    registry, _ = registry_and_options()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    journal_file = REPORTS / 'journal.json'
    journal = json.loads(journal_file.read_text(encoding='utf-8')) if journal_file.exists() else []
    completed = {row['old'] for batch in journal for row in batch['assets']}
    groups = collections.defaultdict(list)
    for row in manifest['assets']:
        if row['old'] not in completed:
            groups[row['group']].append(row)
    for group, rows in sorted(groups.items()):
        for offset in range(0, len(rows), 30):
            batch = rows[offset:offset + 30]
            imports, loaded = [], []
            for row in batch:
                # Never resolve an old redirector and accidentally rename a completed target twice.
                original = registry.get_assets_by_package_name(row['old'])
                if not original or str(original[0].asset_class_path.asset_name) == 'ObjectRedirector':
                    raise RuntimeError('Source is no longer an original asset; inspect journal: ' + row['old'])
                if registry.get_assets_by_package_name(row['new']):
                    raise RuntimeError('Destination unexpectedly exists: ' + row['new'])
                asset = original[0].get_asset()
                if not asset:
                    raise RuntimeError('Could not load source: ' + row['old'])
                loaded.append(asset)
                imports.extend(unreal.AssetOrganizationLibrary.capture_import_paths(asset))
            for row, asset in zip(batch, loaded):
                if not unreal.AssetOrganizationLibrary.move_reviewed_asset(asset, row['new']):
                    write_json(REPORTS / 'failed-batch.json', batch)
                    raise RuntimeError('Reviewed package move failed for {}; original backups are retained.'.format(row['old']))
            if not unreal.AssetOrganizationLibrary.restore_import_paths(imports):
                raise RuntimeError('Could not preserve reimport source paths.')
            # AssetTools only retains redirects it considers necessary. Keep every
            # reviewed old path valid before loading the next batch's referencers.
            for row, asset in zip(batch, loaded):
                if not unreal.AssetOrganizationLibrary.ensure_compatibility_redirector(row['old'], asset):
                    raise RuntimeError('Could not preserve original asset/class path: ' + row['old'])
            saved = save_dirty_game_packages()
            for row, asset in zip(batch, loaded):
                if asset.get_path_name() != row['new'] + '.' + row['new'].rsplit('/', 1)[1]:
                    raise RuntimeError('Unexpected object path after rename: ' + asset.get_path_name())
            journal.append({'group': group, 'assets': batch, 'saved_packages': saved})
            write_json(journal_file, journal)
            unreal.log('Migrated {}: {}/{}; redirectors retained.'.format(group, offset + len(batch), len(rows)))
    # Resolve and re-save hard references, and explicitly replace package/class
    # soft paths. Keep aliases for external text references and unopened tools.
    mapping = {r['old']: r['new'] for r in manifest['assets']}
    names = sorted({mapping.get(p, p) for p in manifest['affected_packages']})
    packages = unreal.AssetOrganizationLibrary.load_project_packages(names)
    if len(packages) != len(names):
        raise RuntimeError('Could not load every affected package for reference normalization.')
    redirects = {}
    for row in manifest['assets']:
        old_name, new_name = row['old'].rsplit('/', 1)[1], row['new'].rsplit('/', 1)[1]
        redirects[unreal.SoftObjectPath(row['old'] + '.' + old_name)] = unreal.SoftObjectPath(row['new'] + '.' + new_name)
        if 'Blueprint' in row['class']:
            redirects[unreal.SoftObjectPath(row['old'] + '.' + old_name + '_C')] = unreal.SoftObjectPath(row['new'] + '.' + new_name + '_C')
    tools.rename_referencing_soft_object_paths(packages, redirects)
    if not unreal.EditorLoadingAndSavingUtils.save_packages(packages, only_dirty=False):
        raise RuntimeError('Could not save normalized package references.')
    save_dirty_game_packages()
    write_json(REPORTS / 'applied.json', {'assets': len(manifest['assets']), 'batches': len(journal), 'redirectors_retained': True})


def remap_path(value, manifest):
    for row in sorted(manifest['assets'], key=lambda r: -len(r['old'])):
        if value == row['old'] or value.startswith(row['old'] + '.'):
            return row['new'] + value[len(row['old']):]
    return value


def verify(manifest):
    registry, _ = registry_and_options()
    missing, bad_redirectors = [], []
    for row in manifest['assets']:
        target = registry.get_assets_by_package_name(row['new'])
        if not target or str(target[0].asset_class_path.asset_name) != row['class']:
            missing.append(row['new'])
        original = registry.get_assets_by_package_name(row['old'])
        if not original or str(original[0].asset_class_path.asset_name) != 'ObjectRedirector':
            bad_redirectors.append(row['old'])
    before = json.loads((REPORTS / 'main-map-before.json').read_text(encoding='utf-8'))
    current_map = manifest['main_map']
    supplemental = PROJECT / 'Tools/MainMapMigrationManifest.json'
    if supplemental.exists():
        current_map = json.loads(supplemental.read_text(encoding='utf-8'))['main_map']
    after = actor_snapshot(current_map)
    scene_errors = []
    if [r['name'] for r in before] != [r['name'] for r in after]:
        scene_errors.append('Actor identities/count changed')
    for old, new in zip(before, after):
        if (remap_path(old['class'], manifest) != new['class'] or old['label'] != new['label'] or
                any(abs(a-b) > 1.e-4 for a, b in zip(old['transform'], new['transform']))):
            scene_errors.append(old['name'])
    write_json(REPORTS / 'main-map-after.json', after)
    report = {'migrated_assets': len(manifest['assets']), 'actor_count': len(after), 'missing_targets': missing,
              'bad_redirectors': bad_redirectors, 'scene_errors': scene_errors}
    write_json(REPORTS / 'verification.json', report)
    if missing or bad_redirectors or scene_errors:
        raise RuntimeError('Asset migration verification failed; see Saved/AssetOrganization/verification.json')
    unreal.log('Verified {} assets and {} unchanged main-map actors.'.format(len(manifest['assets']), len(after)))


mode = os.environ.get('CYBERCAFE_ASSET_MIGRATION_MODE', 'plan')
if mode == 'plan':
    plan()
elif mode in {'apply', 'verify'}:
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    (apply if mode == 'apply' else verify)(manifest)
elif mode == 'library':
    pass
else:
    raise RuntimeError('Unknown asset migration mode: ' + mode)

if os.environ.get('CYBERCAFE_ASSET_MIGRATION_AUTOQUIT') == '1':
    unreal.SystemLibrary.quit_editor()
