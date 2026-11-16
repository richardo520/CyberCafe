"""Prepare an exact allow-list for UE's CompileAllBlueprints commandlet. Never saves packages."""
import json
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=False,
    include_soft_management_references=True,
    include_hard_management_references=True,
)
root = '/Game/CyberCafe/Maps/L_Cafe'
if not registry.get_assets_by_package_name(root):
    raise RuntimeError('Main map not found: ' + root)
pending, visited, rows = [root], set(), []
while pending:
    package = pending.pop()
    if package in visited:
        continue
    visited.add(package)
    pending.extend(str(p) for p in registry.get_dependencies(package, options)
                   if str(p).startswith('/Game/') and str(p) not in visited)

for package in sorted(visited):
    for asset in registry.get_assets_by_package_name(package):
        if 'Blueprint' not in str(asset.asset_class_path.asset_name):
            continue
        rows.append({'package': package, 'object_path': package + '.' + str(asset.asset_name)})

output = Path(unreal.Paths.project_saved_dir()) / 'ArchitectureAudit' / 'main-map-blueprints.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps({'packages': len(visited), 'blueprints': rows, 'note': 'Allow-list only; compilation outcome is in blueprint-compile.log.'}, ensure_ascii=False, indent=2), encoding='utf-8')
if not rows:
    raise RuntimeError('No Blueprint dependencies found.')
output.with_suffix('.txt').write_text('\n'.join(r['object_path'] for r in rows) + '\n', encoding='utf-8')
unreal.log('Prepared {} Blueprint dependencies for native compilation.'.format(len(rows)))
