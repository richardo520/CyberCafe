"""Create the isolated authored regular-customer slice through UE's template-level API.

Run once, after compiling the new native classes. Never overwrites an existing map.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
source = '/Game/CyberCafe/Maps/L_Cafe'
target = '/Game/CyberCafe/Maps/L_RegularCustomer'
source_file = project / 'Content/CyberCafe/Maps/L_Cafe.umap'
before_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(target):
    if os.environ.get('CYBERCAFE_RESUME_REGULAR_TEMPLATE') != '1' or (project / 'Saved/RegularCustomer/map-creation.json').exists():
        raise RuntimeError('Prototype map already exists; refusing to overwrite authored work.')
    if not levels.load_level(target):
        raise RuntimeError('Could not resume the incomplete template copy.')
else:
    if not levels.new_level_from_template(target, source):
        raise RuntimeError('UE template map duplication failed.')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
scene = actors.get_all_level_actors()
if any(isinstance(a, unreal.RegularCustomerEncounter) for a in scene):
    raise RuntimeError('Existing encounter found; refusing duplicate or overwrite.')
directors = [a for a in scene if isinstance(a, unreal.TutorialDirector)]
if len(directors) != 1:
    raise RuntimeError('Expected exactly one saved tutorial director.')
director = directors[0]
director.set_editor_property('bAutoStart', False)
npc = director.get_editor_property('npc_ref')
steps = director.get_editor_property('steps')
waits = [s for s in steps if isinstance(s, unreal.TutorialStep_WaitForServe)]
if len(waits) != 1 or not npc:
    raise RuntimeError('Saved NPC/order configuration is ambiguous.')
wait = waits[0]
spot = wait.get_editor_property('required_spot')
recipe = wait.get_editor_property('required_recipe')
display = wait.get_editor_property('order_hologram')
if not spot or not recipe or not display:
    raise RuntimeError('Missing existing order, recipe or menu display.')
spot.set_editor_property('bAcceptSubmissions', False)
starts = [a for a in scene if isinstance(a, unreal.PlayerStart)]
if len(starts) != 1:
    raise RuntimeError('Expected a single player entry point.')
player = starts[0].get_actor_location() + unreal.Vector(0, 0, 155)
head = npc.get_component_by_class(unreal.SkeletalMeshComponent).get_socket_location('head')
towards = player - head
towards.z = 0
length = math.hypot(towards.x, towards.y)
if length < 1:
    raise RuntimeError('Cannot derive safe panel facing.')
towards = towards / length
side = unreal.Vector(-towards.y, towards.x, 0)
position = head + towards * 45 + side * 48 + unreal.Vector(0, 0, -8)
facing = unreal.Rotator(0, math.degrees(math.atan2(player.y - position.y, player.x - position.x)), 0)
encounter = actors.spawn_actor_from_class(unreal.RegularCustomerEncounter, position, facing)
encounter.set_actor_label('RegularCustomer_NightShift')
encounter.set_editor_property('customer', npc)
encounter.set_editor_property('serving_spot', spot)
encounter.set_editor_property('recipe', recipe)
encounter.set_editor_property('order_display', display)
if not levels.save_current_level():
    raise RuntimeError('Could not save the isolated encounter map.')
after_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
if before_hash != after_hash:
    raise RuntimeError('Source cafe map changed unexpectedly.')
output = project / 'Saved/RegularCustomer'
output.mkdir(parents=True, exist_ok=True)
report = {'map': target, 'source_unchanged_sha256': after_hash, 'source_actors': len(scene),
          'encounter': encounter.get_path_name(), 'npc': npc.get_path_name(),
          'serving_spot': spot.get_path_name(), 'recipe': recipe.get_path_name(),
          'panel_position': str(position), 'player_head_estimate': str(player),
          'panel_distance_cm': (player - position).length(), 'tutorial_auto_start': False}
(output / 'map-creation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('Created regular customer prototype: ' + target)
