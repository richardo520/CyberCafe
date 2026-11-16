"""Read-only runtime entry/dependency audit; distinguish real template assets from redirects."""
import collections
import json
import os
from pathlib import Path
import unreal

root = os.environ.get('CYBERCAFE_ACTIVE_MAIN_MAP', '/Game/CyberCafe/Maps/L_Cafe')
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True, include_soft_package_references=True,
    include_searchable_names=False, include_hard_management_references=False,
    include_soft_management_references=False)
pending, visited, parent = collections.deque([root]), set(), {root: None}
real, redirects = [], []
while pending:
    package = pending.popleft()
    if package in visited:
        continue
    visited.add(package)
    assets = registry.get_assets_by_package_name(package)
    if package.startswith('/Game/VRTemplate/'):
        row = {'package': package, 'classes': sorted({str(a.asset_class_path.asset_name) for a in assets})}
        chain, item = [], package
        while item is not None:
            chain.append(item)
            item = parent[item]
        row['chain'] = list(reversed(chain))
        (redirects if assets and all(str(a.asset_class_path.asset_name) == 'ObjectRedirector' for a in assets) else real).append(row)
    for dependency in registry.get_dependencies(package, options):
        dependency = str(dependency)
        if dependency.startswith('/Game/') and dependency not in parent:
            parent[dependency] = package
            pending.append(dependency)
output = Path(unreal.Paths.project_saved_dir()) / 'MapOrganization'
output.mkdir(parents=True, exist_ok=True)
report = {'root': root, 'reachable_packages': len(visited), 'real_template_assets': real,
          'template_redirectors': redirects, 'note': 'Package metadata does not include arbitrary string loads.'}
(output / 'template-dependencies.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Main flow uses {} real template packages and {} template redirector packages.'.format(len(real), len(redirects)))
