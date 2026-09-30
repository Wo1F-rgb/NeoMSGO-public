"""Import the manifest-approved TypeB WAV set into /Game/Audio/Imported/TypeB.

The delivered files have an extra dot before ``.wav``. Destination names come
from this explicit ASCII table, so package names remain stable and portable.
"""

from pathlib import Path

import unreal


PROJECT_DIR = Path(unreal.Paths.project_dir())
SOURCE_ROOT = PROJECT_DIR.parent / "Audio" / "wav"
DESTINATION = "/Game/Audio/Imported/TypeB"

# Manifest order. These are asset identities, not gameplay mappings; gameplay
# usage mapping lives in MSAudioPreset.cpp.
ASSETS = {
    r"haidao_map\音效\Underwater_Audio..wav": "TypeB_Underwater_Audio",
    r"jijiawenjian\bgm\bgm\H0rizon..wav": "TypeB_H0rizon",
    r"jijiawenjian\bgm\boom\boom02..wav": "TypeB_Boom02",
    r"jijiawenjian\bgm\boom\boon_big..wav": "TypeB_BoomBig",
    r"jijiawenjian\bgm\chuji\chuchang..wav": "TypeB_Sortie",
    r"jijiawenjian\bgm\fly\fly_start01..wav": "TypeB_BoostStart",
    r"jijiawenjian\bgm\fly\fly_xunhuan..wav": "TypeB_BoostLoop",
    r"jijiawenjian\bgm\gedou\gedou_hit..wav": "TypeB_MeleeHit",
    r"jijiawenjian\bgm\gedou\gedou_run01..wav": "TypeB_MeleeDash",
    r"jijiawenjian\bgm\gedou\gedou01..wav": "TypeB_MeleeAttack",
    r"jijiawenjian\bgm\shoot\bazuka01_se..wav": "TypeB_FireBazooka",
    r"jijiawenjian\bgm\shoot\boon01_se..wav": "TypeB_FireCannon",
    r"jijiawenjian\bgm\shoot\danyaobuzu..wav": "TypeB_AmmoEmpty",
    r"jijiawenjian\bgm\shoot\dialliu..wav": "TypeB_Reload",
    r"jijiawenjian\bgm\shoot\hit01..wav": "TypeB_HitNormal",
    r"jijiawenjian\bgm\shoot\hit02..wav": "TypeB_HitHeadshot",
    r"jijiawenjian\bgm\shoot\se_kill..wav": "TypeB_KillConfirm",
    r"jijiawenjian\bgm\shoot\toupao01_se..wav": "TypeB_FireOrdnance",
    r"jijiawenjian\bgm\shoot\xuli01..wav": "TypeB_Charge",
    r"jijiawenjian\bgm\suoding\jingbao01..wav": "TypeB_LockWarning",
    r"jijiawenjian\bgm\suoding\suoding..wav": "TypeB_LockConfirm",
    r"jijiawenjian\bgm\UI\anniu_xuanfu..wav": "TypeB_UIHover",
    r"jijiawenjian\bgm\walk\walk02..wav": "TypeB_Footstep",
    r"jijiawenjian\image\原版\bgm\anniu01..wav": "TypeB_UIClick",
    r"jijiawenjian\image\原版\bgm\bgm——006..wav": "TypeB_BGM006",
    r"Sci-Fi_Shots_Pack2_Game_Of_Weapons\Wave\SciFi_Shot_P2__126_..wav": "TypeB_BeamShot126",
    r"Sci-Fi_Shots_Pack2_Game_Of_Weapons\Wave\SciFi_Shot_P2__132_..wav": "TypeB_BeamShot132",
    r"Sci-Fi_Shots_Pack2_Game_Of_Weapons\Wave\SciFi_Shot_P2__154_..wav": "TypeB_BeamShot154",
    r"Sci-Fi_Shots_Pack2_Game_Of_Weapons\Wave\SciFi_Shot_P2__181_..wav": "TypeB_BeamShot181",
    r"Sci-Fi_Shots_Pack2_Game_Of_Weapons\Wave\SciFi_Shot_P2__195_..wav": "TypeB_BeamShot195",
    r"StylescapeHD\Audio\Music\A_MusicScape_SSHD..wav": "TypeB_MusicScape",
    r"StylescapeHD\Audio\Nature\A_Desert_Ambience..wav": "TypeB_AmbienceDesert",
    r"StylescapeHD\Audio\Nature\A_Jungle_Forest_Ambience..wav": "TypeB_AmbienceJungle",
    r"StylescapeHD\Audio\Nature\A_MountainTop_Wind_Ambience..wav": "TypeB_AmbienceMountainWind",
    r"StylescapeHD\Audio\Nature\A_StrongWind_Forest_Ambience..wav": "TypeB_AmbienceStrongForestWind",
    r"StylescapeHD\Audio\Nature\A_Windy_Forest_Ambience..wav": "TypeB_AmbienceWindyForest",
}

LOOPING = {
    # TypeB_Charge: チャージ保持中に鳴り続ける溜め音(SpawnLoopingMetaSoundAttachedで再生)
    "TypeB_Charge",
    "TypeB_BoostLoop", "TypeB_H0rizon", "TypeB_BGM006", "TypeB_MusicScape",
    "TypeB_Underwater_Audio", "TypeB_AmbienceDesert", "TypeB_AmbienceJungle",
    "TypeB_AmbienceMountainWind", "TypeB_AmbienceStrongForestWind",
    "TypeB_AmbienceWindyForest",
}

tasks = []
for relative_path, asset_name in ASSETS.items():
    source = SOURCE_ROOT / Path(relative_path)
    if not source.is_file():
        raise RuntimeError(f"Manifest WAV is missing: {source}")
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DESTINATION
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.replace_existing_settings = True
    task.save = False
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

for asset_name in ASSETS.values():
    object_path = f"{DESTINATION}/{asset_name}.{asset_name}"
    sound_wave = unreal.EditorAssetLibrary.load_asset(object_path)
    if not sound_wave:
        raise RuntimeError(f"Audio import did not produce {object_path}")
    if asset_name in LOOPING:
        sound_wave.set_editor_property("looping", True)

unreal.EditorAssetLibrary.save_directory(DESTINATION, only_if_is_dirty=False, recursive=True)
unreal.log(f"TYPE_B_AUDIO_IMPORTED={len(ASSETS)}")
