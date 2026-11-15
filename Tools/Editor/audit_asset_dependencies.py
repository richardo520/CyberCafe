"""Run inside UE Editor with Python Editor Script Plugin enabled. Never saves assets."""
import json
from pathlib import Path
import unreal


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=True,
        include_hard_management_references=True,
    )
    root = '/Game/VRTemplate/Maps/L_Cafe'
    if not registry.get_assets_by_package_name(root):
        raise RuntimeError('Main map is missing: ' + root)
    pending, visited, edges = [root], set(), {}
    while pending:
        package = pending.pop()
        if package in visited:
            continue
        visited.add(package)
        dependencies = sorted({str(p) for p in registry.get_dependencies(package, options)})
        edges[package] = dependencies
        pending.extend(p for p in dependencies if p.startswith('/Game/') and p not in visited)
    output = Path(unreal.Paths.project_saved_dir()) / 'ArchitectureAudit'
    output.mkdir(parents=True, exist_ok=True)
    report = {
        'root': root,
        'package_dependencies': edges,
        'note': 'Registry package references only; dynamic string loads and external media require separate checks.',
    }
    (output / 'main-map-dependencies.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Architecture audit: {} project packages; {}'.format(len(visited), output))


if __name__ == '__main__':
    main()
