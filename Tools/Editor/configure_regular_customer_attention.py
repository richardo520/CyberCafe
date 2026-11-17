"""Install the instance-only head layer in the regular-customer map, preserving the original cafe."""
import hashlib
import json
from pathlib import Path
import shutil
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
report = project / 'Saved/RegularCustomer/Companionship'
report.mkdir(parents=True, exist_ok=True)
map_file = project / 'Content/CyberCafe/Maps/L_RegularCustomer.umap'
backup = report / 'L_RegularCustomer.before-attention.umap'
if not backup.exists():
    shutil.copy2(map_file, backup)
original = project / 'Content/CyberCafe/Maps/L_Cafe.umap'
before = hashlib.sha256(original.read_bytes()).hexdigest()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.load_level('/Game/CyberCafe/Maps/L_RegularCustomer'):
    raise RuntimeError('Could not load regular customer map.')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
encounters = [a for a in actors if isinstance(a, unreal.RegularCustomerEncounter)]
if len(encounters) != 1:
    raise RuntimeError('Expected exactly one encounter.')
encounter = encounters[0]
npc = encounter.get_editor_property('customer')
mesh = npc.get_component_by_class(unreal.SkeletalMeshComponent)
layer = unreal.RegularAttentionEditorLibrary.create_head_attention_layer(mesh)
if not layer:
    raise RuntimeError('Could not compile head-only post-process layer.')
asset = unreal.load_asset('/Game/CyberCafe/Characters/BarGirl/RegularCustomer/ABP_RegularCustomerAttention')
if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
    raise RuntimeError('Could not save attention animation asset.')
encounter.set_editor_property('attention_layer', layer)
if not levels.save_current_level():
    raise RuntimeError('Could not save encounter layer reference.')
if hashlib.sha256(original.read_bytes()).hexdigest() != before:
    raise RuntimeError('Original cafe map changed unexpectedly.')
(report / 'attention-setup.json').write_text(json.dumps({'layer': layer.get_path_name(), 'original_map_sha256': before,
    'encounter': encounter.get_path_name(), 'map_actors': len(actors)}, indent=2), encoding='utf-8')
