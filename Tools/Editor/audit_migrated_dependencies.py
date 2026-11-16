"""Compare Game-package references with the pre-migration binary backup metadata."""
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
manifest = json.loads((project / 'Tools/AssetMigrationManifest.json').read_text(encoding='utf-8'))
mapping = {r['old']: r['new'] for r in manifest['assets']}
supplemental = project / 'Tools/MainMapMigrationManifest.json'
if supplemental.exists():
    mapping.update({r['old']: r['new'] for r in json.loads(supplemental.read_text(encoding='utf-8'))['assets']})
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
unreal.AssetOrganizationLibrary.mount_original_backup_for_audit()
registry.scan_paths_synchronous(['/AssetOrganizationBaseline'], force_rescan=True)
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False,
    include_hard_management_references=False)
cache = {}


def canonical(package, visiting=None):
    if package in mapping:
        return {mapping[package]}
    if package in cache:
        return cache[package]
    visiting = set() if visiting is None else visiting
    if package in visiting:
        return {package}
    visiting.add(package)
    assets = registry.get_assets_by_package_name(package)
    if assets and all(str(a.asset_class_path.asset_name) == 'ObjectRedirector' for a in assets):
        refs = [str(p) for p in registry.get_dependencies(package, options) if str(p).startswith('/Game/')]
        if refs:
            result = set().union(*(canonical(p, visiting) for p in refs))
            cache[package] = result
            return result
    cache[package] = {package}
    return {package}


rows = []
for original in manifest['affected_packages']:
    baseline = '/AssetOrganizationBaseline/' + original[len('/Game/'):]
    if not registry.get_assets_by_package_name(baseline):
        raise RuntimeError('Backup metadata was not indexed: ' + baseline)
    current = mapping.get(original, original)
    before, after = set(), set()
    for dependency in registry.get_dependencies(baseline, options):
        if str(dependency).startswith('/Game/'):
            before.update(canonical(str(dependency)))
    for dependency in registry.get_dependencies(current, options):
        if str(dependency).startswith('/Game/'):
            after.update(canonical(str(dependency)))
    before.discard(current)
    after.discard(current)
    rows.append({'original': original, 'current': current, 'before_count': len(before),
                 'after_count': len(after), 'missing_original_dependencies': sorted(before - after)})
missing = [r for r in rows if r['missing_original_dependencies']]
output = project / 'Saved/AssetOrganization/dependency-comparison.json'
output.write_text(json.dumps({'packages': len(rows), 'packages_with_missing_dependencies': len(missing), 'results': rows},
                             ensure_ascii=False, indent=2), encoding='utf-8')
if missing:
    raise RuntimeError('Original dependencies missing; see ' + str(output))
unreal.log('All {} packages retain their original Game-package dependencies.'.format(len(rows)))
