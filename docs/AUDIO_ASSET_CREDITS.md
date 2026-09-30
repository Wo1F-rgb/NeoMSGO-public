# Audio Asset Sources

## Task #66: Kenney CC0 sound effects

Downloaded on 2026-07-16 from [Kenney](https://kenney.nl/assets). Each source pack is released under Creative Commons CC0 (public domain); attribution is not required. This file records provenance for project maintenance.

The distributed OGG files were converted to 48 kHz, 16-bit PCM WAV with FFmpeg before import as Unreal Engine `USoundWave` assets under `/Game/Audio/Imported`. No synthesis or third-party samples were added during conversion.

| Unreal asset | Gameplay use | Kenney pack | Original file |
|---|---|---|---|
| `SFX_Fire_Laser` | Weapon fire | [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) | `Audio/laserLarge_001.ogg` |
| `SFX_Projectile_Zap` | Projectile/beam flight | [Digital Audio](https://kenney.nl/assets/digital-audio) | `Audio/zap1.ogg` |
| `SFX_Impact_Metal` | Normal hit confirmation | [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) | `Audio/impactMetal_002.ogg` |
| `SFX_Headshot_Bell` | Headshot confirmation | [Impact Sounds](https://kenney.nl/assets/impact-sounds) | `Audio/impactBell_heavy_003.ogg` |
| `SFX_Explosion_Crunch` | Explosion | [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) | `Audio/explosionCrunch_002.ogg` |
| `SFX_KillConfirm` | Kill confirmation | [Interface Sounds](https://kenney.nl/assets/interface-sounds) | `Audio/confirmation_003.ogg` |
| `SFX_KillConfirm_Metal` | Player kill metallic confirmation | [RPG Audio](https://kenney.nl/assets/rpg-audio) | `Audio/metalLatch.ogg` |
| `SFX_Thruster_Loop` | Boost/thruster loop | [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) | `Audio/thrusterFire_003.ogg` |
| `SFX_Footstep_Metal` | Mech footstep | [Impact Sounds](https://kenney.nl/assets/impact-sounds) | `Audio/impactMetal_heavy_001.ogg` |
| `SFX_Reload_MetalLatch` | Reload mechanism | [RPG Audio](https://kenney.nl/assets/rpg-audio) | `Audio/metalLatch.ogg` |
| `SFX_Charge_Loop` | Weapon charge loop with runtime pitch rise | [Sci-fi Sounds](https://kenney.nl/assets/sci-fi-sounds) | `Audio/engineCircular_002.ogg` |
| `SFX_Charge_Complete` | Charge completion | [Digital Audio](https://kenney.nl/assets/digital-audio) | `Audio/threeTone1.ogg` |
| `SFX_LockOn_Confirm` | Lock-on completion | [Interface Sounds](https://kenney.nl/assets/interface-sounds) | `Audio/confirmation_001.ogg` |

Kenney's licensing FAQ confirms that assets on its asset pages are CC0 and may be used commercially without attribution: [Kenney Support](https://kenney.nl/support).

## Public-source replacement audio

82 BGM, 54 effects and 61 voice SoundWave packages use newly synthesized media.
The BGM consists of simple original tones; sound effects are synthesized tones/noise.
Voice packages contain silence. These are intentional public-source placeholders,
not recordings or extracted audio from another game. Original unverified audio is
not distributed. Gameplay package paths remain compatible with existing references.

See ASSET_LICENSES.md for licensing scope. Audible gameplay QA is pending.