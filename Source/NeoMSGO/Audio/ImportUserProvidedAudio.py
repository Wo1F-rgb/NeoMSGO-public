"""Imports the user-provided combat audio set into /Game/Audio/Imported.

Run through UnrealEditor-Cmd's PythonScript commandlet. The script only writes
the approved audio destination and is intentionally safe to rerun.
"""

from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
SOURCE_DIR = PROJECT_DIR / "Content" / "Audio" / "Source" / "UserProvided"
DESTINATION = "/Game/Audio/Imported"
LOOPING_ASSETS = {"SFX_ChargeLoop_Real", "SFX_ThrusterLoop_Real"}
COMPATIBILITY_ALIASES = {
    "SFX_ChargeLoop_Real": "SFX_Charge_Loop",
    "SFX_ThrusterLoop_Real": "SFX_Thruster_Loop",
    "SFX_Footstep_Real": "SFX_Footstep_Metal",
    "SFX_LockOn_Target": "SFX_LockOn_Confirm",
}
LOOPING_ASSETS.update({"SFX_Charge_Loop", "SFX_Thruster_Loop"})


wav_files = sorted(SOURCE_DIR.glob("SFX_*.wav"))
if not wav_files:
    raise RuntimeError(f"No user-provided WAV files found in {SOURCE_DIR}")

tasks = []
for wav_file in wav_files:
    task = unreal.AssetImportTask()
    task.filename = str(wav_file)
    task.destination_path = DESTINATION
    task.destination_name = wav_file.stem
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    tasks.append(task)

    alias_name = COMPATIBILITY_ALIASES.get(wav_file.stem)
    if alias_name:
        alias_task = unreal.AssetImportTask()
        alias_task.filename = str(wav_file)
        alias_task.destination_path = DESTINATION
        alias_task.destination_name = alias_name
        alias_task.automated = True
        alias_task.replace_existing = True
        alias_task.replace_existing_settings = True
        alias_task.save = False
        tasks.append(alias_task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

imported_paths = []
asset_names = [wav_file.stem for wav_file in wav_files]
asset_names.extend(
    COMPATIBILITY_ALIASES[name]
    for name in asset_names
    if name in COMPATIBILITY_ALIASES
)
for asset_name in asset_names:
    object_path = f"{DESTINATION}/{asset_name}.{asset_name}"
    sound_wave = unreal.EditorAssetLibrary.load_asset(object_path)
    if not sound_wave:
        raise RuntimeError(f"Audio import did not produce {object_path}")
    if asset_name in LOOPING_ASSETS:
        sound_wave.set_editor_property("looping", True)
    imported_paths.append(object_path)

unreal.EditorAssetLibrary.save_directory(
    DESTINATION, only_if_is_dirty=False, recursive=False
)
unreal.log("USER_AUDIO_IMPORT_OBJECTS=" + str(imported_paths))
