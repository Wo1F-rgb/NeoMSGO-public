# VFX asset credits

VFX 素材の出典・改変内容です。第三者素材のライセンスと、自作 shader / effect のライセンスは分けて扱います。[ASSET_LICENSES.md](ASSET_LICENSES.md) も参照してください。

## Thruster smoke

- 原素材：**Smoke Particles**、White puff の frames 00 / 06 / 12 / 18。
- 作者：Kenney Vleugels / Kenney。
- 配布元：[Kenney — Smoke Particles](https://kenney.nl/assets/smoke-particles)。
- ライセンス：[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)。
- 使用先：`Content/Textures/VFX/T_ThrusterSmoke_00.uasset`〜`T_ThrusterSmoke_03.uasset`。
- プロジェクト材質：`Content/Textures/VFX/M_ThrusterSmoke.uasset`。
- 改変・利用方法：alpha 付き元画像を Unreal texture として import。自作の半透明 unlit 材質から camera-facing billboard に表示し、コードで色、透明度、移動、上昇、拡大を変化させます。

## Explosion sheet

- 原素材：**Explosion Sheet**、`boom3_0.png`。
- 作者：**StumpyStrust**。
- 配布元：[OpenGameArt — Explosion Sheet](https://opengameart.org/content/explosion-sheet)。
- 原ファイル：[boom3_0.png](https://opengameart.org/sites/default/files/boom3_0.png)。
- ライセンス：[CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)。
- 使用先：`Content/Textures/VFX/ExplosionSheet/T_ExplosionSheet.uasset`、`M_ExplosionSheet.uasset`。原 PNG は `Content/ArtSource/VFX/ExplosionSheet/boom3_0.png`。
- 改変・利用方法：原 PNG の画像内容は保持し、8 列 × 8 行の sprite sheet として Unreal に import。自作 flipbook 材質と実行時制御で frame、発光、透明度、サイズを変化させます。

## プロジェクト制作の effect

以下はプロジェクト制作の素材・グラフです。現時点の自作アセットの公開ライセンスは [ASSET_LICENSES.md](ASSET_LICENSES.md) に記載しています。

| 対象 | 制作内容 |
|---|---|
| `Content/Textures/VFX/Charge/M_ChargeGlow.uasset` | texture を使わない手続き的な additive glow 材質。 |
| `Content/WaterRealism0929/VFX/` | 水面、白波、spray の自作 shader。形状と透明度を材質内で計算。 |
| `Content/VFX/Water/` の Niagara / Material | Unreal の標準 Niagara module を組み合わせたプロジェクトの effect graph。標準 engine module は Unreal Engine 側の条件に従います。 |
| 公開版の `T_MS_WaterSplash` / `T_MS_WaterSplashV2` | 自作の透明 spray / foam 画像へ置き換える対象。公開版に含める画像は新たに生成したものとし、第三者の原波形・写真・水画像を流用しません。 |
| 公開版の `LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow` | 色 parameter と Custom Primitive Data に対応するプロジェクト制作の発光材質。 |

CC0 由来の元画像に、このプロジェクトのコード用 MIT を上書きしません。プロジェクト制作の追加部分についても、ライセンス未確定の段階で MIT または CC0 と断定しません。
