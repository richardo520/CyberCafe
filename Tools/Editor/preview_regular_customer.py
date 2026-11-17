"""Render the actual native dialogue widget. Requires GPU rendering, not -nullrhi."""
import unreal

if not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Engine/Maps/Entry'):
    raise RuntimeError('Preview world did not load.')
if not unreal.RegularCustomerPreviewLibrary.render_choice_preview():
    raise RuntimeError('Could not render dialogue preview.')
