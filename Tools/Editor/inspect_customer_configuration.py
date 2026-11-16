"""Read-only diagnostics for the saved first-customer recipe."""
import json
from pathlib import Path
import unreal

recipe = unreal.load_asset('/Game/CyberCafe/Gameplay/Serving/Recipes/DA_Latte')
report = {'recipe': {}}
for name in ['recipe_id', 'expected_ice_count', 'accepted_cup_class', 'min_volume_ml', 'max_volume_ml']:
    report['recipe'][name] = str(recipe.get_editor_property(name))
report['note'] = 'Saved NPC instances are verified by the complete-map automation fixture, not unloaded-world actor iteration.'
output = Path(unreal.Paths.project_saved_dir()) / 'ArchitectureAudit' / 'customer-configuration.json'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
