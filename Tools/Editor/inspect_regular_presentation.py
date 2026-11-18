"""Read-only inspection of the saved regular-customer layout and presentation audio."""
import json
from pathlib import Path
import unreal

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/CyberCafe/Maps/L_RegularCustomer')
rows = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, unreal.RegularCustomerEncounter):
        panel = actor.get_editor_property('panel')
        rows.append(dict(label=actor.get_actor_label(), location=str(actor.get_actor_location()),
                         scale=str(actor.get_actor_scale3d()), panel_scale=str(panel.get_world_scale()),
                         panel_relative_scale=str(panel.get_editor_property('relative_scale3d'))))
sound = unreal.load_asset('/Game/CyberCafe/UI/Audio/S_NarrativeVoiceTick')
report = dict(encounters=rows, tick_duration=sound.get_editor_property('duration') if sound else None)
menu = unreal.load_asset('/Game/CyberCafe/Gameplay/Serving/Menu/DA_RecipeMenu')
report['menu'] = []
if menu:
    for entry in menu.get_editor_property('entries'):
        recipe = entry.get_editor_property('recipe')
        report['menu'].append(dict(name=str(entry.get_editor_property('display_name')),
                                  cup=str(entry.get_editor_property('cup_name')),
                                  recipe=recipe.get_path_name() if recipe else None,
                                  ingredients=[dict(id=str(i.get_editor_property('ingredient_id')), ml=i.get_editor_property('volume_ml')) for i in entry.get_editor_property('ingredients')]))
target = Path(unreal.Paths.project_saved_dir()) / 'RegularCustomer' / 'presentation-inspection.json'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
