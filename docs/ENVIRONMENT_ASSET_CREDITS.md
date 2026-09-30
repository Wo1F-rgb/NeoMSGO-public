# Environment asset credits

環境素材の出典と改変内容。以下の第三者素材には各行の条件が適用され、プロジェクトのコード用 MIT ライセンスへ変更されるものではありません。自作部分のライセンス状況は [ASSET_LICENSES.md](ASSET_LICENSES.md) を参照してください。

## CC0 の建物・工業設備

この節の素材はすべて [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/) です。出典表示は来歴を明確にするため残しています。

| 素材 / 作者・配布元 | 使用ファイル | プロジェクトでの改変 |
|---|---|---|
| [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) — Kenney Vleugels / Kenney | `Content/Environment/Buildings/SM_Env_Building_{FactoryBlock,ProcessHall,ServiceDepot}`、`Content/Environment/Debris/SM_Env_Debris_{CargoTank,FallenPipeBarrier,ConcreteStack}`、`Content/Environment/Textures/{M_Env_Industrial_Desaturated,T_Env_IndustrialAtlas_D}` | building-a/d/j、detail-tank、chimney-medium/basic を使用。接地中心へ pivot を変更し、建物高を 3,200 / 2,800 / 2,600 cm、設備の最大寸法を 800 / 1,000 / 650 cm へ調整。配管・瓦礫を回転。低彩度 Variation C atlas を使用。 |
| [City Kit (Industrial) 1.0](https://kenney.nl/assets/city-kit-industrial) — Kenney Vleugels / Kenney | `Content/Environment/Buildings/Industrial/SM_KenneyIndustrial_*` | building-b/c/e/f/g/h/i/k/l/m/n/o/p/q/r/s/t、chimney-large/small の 19 モジュールを 10 倍で import。共通 atlas 材質、Nanite、衝突形状を設定。 |
| [City Kit (Commercial) 2.1](https://kenney.nl/assets/city-kit-commercial) — Kenney Vleugels / Kenney | `Content/Environment/Buildings/Commercial/SM_KenneyCommercial_Skyscraper_*`、`Content/Environment/Textures/IndustrialP2/T_KenneyCommercial_Atlas`、`Content/Environment/Buildings/Materials/M_KenneyCommercial` | skyscraper-a〜e を 10 倍で import。variation-a atlas を共通 Unreal 材質へ統合し、Nanite と衝突形状を設定。 |
| [Ultimate Buildings Pack](https://quaternius.com/packs/ultimatetexturedbuildings.html) — Quaternius | `Content/Environment/Buildings/Quaternius/SM_Quaternius_*` | 1Story_RoundRoof、2Story_Columns / Double / Slim / Wide の 5 モジュールを 10 倍で import。元材質を下記 Concrete Panels に変更。Nanite と衝突形状を設定。 |
| [KayKit: City Builder Bits 1.0](https://kaylousberg.itch.io/city-builder-bits) — Kay Lousberg | `Content/Environment/Buildings/KayKit/SM_KayKit_Building_*`、`Content/Environment/Textures/IndustrialP2/T_KayKit_CityBits_Atlas`、`Content/Environment/Buildings/Materials/M_KayKit_CityBits` | building_A〜H と withoutBase 版を使用。OBJ の単位補正を含め実効 1,000 倍へ調整。citybits_texture atlas を共通材質へ統合し、Nanite と衝突形状を設定。入手元は [作者の公式配布リポジトリ](https://github.com/KayKit-Game-Assets/KayKit-City-Builder-Bits-1.0)。 |

## CC0 の岩・枯木・樹木

この節は [Poly Haven の CC0 素材](https://polyhaven.com/license)です。各ファイル名には `.uasset` が付きます。

| 素材 / 作者 | 使用ファイル | 改変 |
|---|---|---|
| [Boulder 01](https://polyhaven.com/a/boulder_01) — Rico Cilliers | `Environment/Rocks/SM_Env_Rock_Boulder01`、`Environment/Textures/{M,T}_Env_Rock_Boulder01*` | 階層 transform を焼き付け、最大寸法 500 cm、PBR texture 1K、Nanite。 |
| [Rock 07](https://polyhaven.com/a/rock_07) — Jenelle van Heerden | `Environment/Rocks/SM_Env_Rock_Rock07`、`Environment/Textures/{M,T}_Env_Rock_Rock07*` | 最大寸法 900 cm、PBR texture 1K、Nanite。 |
| [Namaqualand Boulder 06](https://polyhaven.com/a/namaqualand_boulder_06) — Greg Zaal、Jenelle van Heerden | `Environment/Rocks/SM_Env_Rock_Namaqualand06`、`Environment/Textures/{M,T}_Env_Rock_Namaqualand06*` | 最大寸法 1,400 cm、PBR texture 1K、Nanite。 |
| [Dead Tree Trunk](https://polyhaven.com/a/dead_tree_trunk) — Rob Tuytel | `Environment/Vegetation/SM_Env_Vegetation_DeadLog`、`Environment/Textures/M_Env_Vegetation_DeadLog`、`T_Env_DeadLog_*` | 長さ 600 cm に調整し接地 pivot 化。Nanite は無効。 |
| [Dead Tree Trunk 02](https://polyhaven.com/a/dead_tree_trunk_02) — Jenelle van Heerden、Rico Cilliers | `Environment/Vegetation/SM_Env_Vegetation_DeadSnag`、`Environment/Textures/M_Env_Vegetation_DeadSnag`、`T_Env_DeadSnag_*` | 約 82 度起こし、高さ 800 cm の枯損木へ加工。接地 pivot、Nanite 無効。 |
| [Jacaranda Tree](https://polyhaven.com/a/jacaranda_tree) — Rico Cilliers（制作）、Rob Tuytel（guidance） | `Environment/Daibao/Trees/SM_DaibaoTree_Jacaranda`、`jacaranda_tree_*`、`MI_DaibaoFoliage_Jacaranda_*` | メッシュ結合・FBX 変換、1K texture、Unreal foliage 材質、4 段階 LOD と配置時の高さ調整。 |

表中の相対パスは `Content/` 以下です。

## CC0 の PBR texture と道路設備

すべて [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) / [Poly Haven](https://polyhaven.com/license)。Diffuse、normal、roughness 等を Unreal 材質へ組み、normal/linear color 設定と UV 密度を調整しています。

| 素材 / 作者 | 使用ファイル・用途 | 改変 |
|---|---|---|
| [Rubble](https://polyhaven.com/a/rubble) — Amal Kumar | `Environment/Textures/T_Env_Ground_Rubble_*`、`M_Env_Ground_Rubble`、`Environment/Terrain/M_Env_Terrain`、`NewYorkTaigan/{Textures,Grid500m/Textures}/T_NYT_Rubble_*`、`WaterRealism0929/Environment/Textures/T_rubble_*` | Environment は 1K、NewYork / WaterRealism は 2K。斜面・地面・建物破損部へ適用。 |
| [Concrete Floor Damaged 01](https://polyhaven.com/a/concrete_floor_damaged_01) — Rob Tuytel | `Environment/Textures/T_Env_Ground_DamagedConcrete_*`、`M_Env_Ground_DamagedConcrete`、`WaterRealism0929/Environment/Textures/T_concrete_floor_damaged_01_*` | 1K / 2K PBR map を使用。材質と UV スケールを設定。 |
| [Dry Ground Rocks](https://polyhaven.com/a/dry_ground_rocks) — Rob Tuytel | `Environment/Textures/T_Env_Ground_DryRocks_*`、`M_Env_Ground_DryRocks`、`Environment/Terrain/M_Env_Terrain` | 1K PBR map。平地レイヤーと低周波の色変化へ使用。 |
| [Corrugated Iron](https://polyhaven.com/a/corrugated_iron) — Jenelle van Heerden（processing）、Dimitrios Savva（photography） | `Environment/Textures/IndustrialP2/T_PH_CorrugatedIron_*`、`Environment/Buildings/Materials/M_PH_CorrugatedIron` | 2K Diffuse / DirectX normal / roughness / metal map。Unreal 材質を構成。 |
| [Concrete Panels](https://polyhaven.com/a/concrete_panels) — Amal Kumar | `Environment/Textures/IndustrialP2/T_PH_ConcretePanels_*`、`Environment/Buildings/Materials/M_PH_ConcretePanels` | 2K Diffuse / DirectX normal / roughness。建物外壁材質として使用。 |
| [Asphalt 02](https://polyhaven.com/a/asphalt_02) — Rob Tuytel | `NewYorkTaigan/{Textures,Grid500m/Textures}/T_NYT_Asphalt_*` | PBR map の import、材質・道路の UV 密度調整。 |
| [Concrete Floor Worn 001](https://polyhaven.com/a/concrete_floor_worn_001) — Dimitrios Savva（photography）、Rico Cilliers（processing） | `NewYorkTaigan/{Textures,Grid500m/Textures}/T_NYT_Concrete_*` | 2K PBR map の import、建物・護岸等の材質・UV 密度調整。 |
| [Concrete Road Barrier](https://polyhaven.com/a/concrete_road_barrier) — Amal Kumar | `WaterRealism0929/Environment/Textures/T_Barrier_*`、`Materials/M_WR_Barrier`、同 Geometry の `SM_NYT_RealismBuildings_CentralNorth` / `CentralSouth` 内の設備形状 | glTF 形状を元の面数の約 1% に簡略化し、UV を保持して街路設備へ配置・結合。2K Diffuse / ARM / OpenGL normal map を使用。 |

パスは `Content/` 以下です。自作材質グラフの追加によって、元の CC0 素材の条件が変更されることはありません。

## CC BY 4.0 の大場樹木

以下の形状・texture は [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/) に基づき使用・改変しています。作者名、作品名、出典、ライセンス、改変表示を再配布時にも維持してください。作者が本ゲームを推奨しているという意味ではありません。

使用先は `Content/Environment/Daibao/Trees/`。各 mesh の texture、material、material instance も同じ出典を引き継ぎます。

| 原作品 / 作者 | 実アセット | 改変 |
|---|---|---|
| [Old Oak Tree](https://sketchfab.com/models/810208743d0e4cffadb15e77211b3a60) — BBARTS | `SM_DaibaoTree_OldOakEvergreen`、`Leaf__02*`、`Oak_Bark*`、`MI_DaibaoFoliage_OldOakEvergreen_*` | 約 315,542 → 110,435 triangles に簡略化。結合・FBX 変換、常緑樹 proxy として暗い葉色へ変更。4 LOD と配置時の高さ補正。 |
| [Oak Tree](https://sketchfab.com/models/caf081828df84d7cbb07b4107991facc) — BBARTS | `SM_DaibaoTree_Oak`、`Leaf*`（Leaf__02 を除く）、`Oak_Bark_001` 等、`MI_DaibaoFoliage_Oak_*` | 約 106,832 → 53,415 triangles に簡略化。結合・FBX 変換、材質・4 LOD・配置時の高さを調整。 |
| [Cherry Tree](https://sketchfab.com/models/539cc24793bb4cd6b35227cddd03d3a3) — Bl_at_ke | `SM_DaibaoTree_Cherry`、`cherry*`、`tree*`、`MI_DaibaoFoliage_Cherry_*` | 花の形状を保持して枝を簡略化。約 790,535 → 503,703 triangles で export（Unreal import 後 LOD0 は 503,574）。結合・FBX 変換、4 LOD、配置時の高さ調整。 |
| [Realistic HD Mexican fan palm (10/10)](https://sketchfab.com/models/1205a111ed9a4ba8a94077b58a39d10b) — PlantCatalog | `SM_DaibaoTree_Palm`、`Wash_*`、`Wahs_crownshaft*` | 約 185,966 → 65,077 triangles に簡略化。22 材質を保持して結合・FBX 変換。4 LOD と配置時の高さ調整。 |
| [Old olive tree](https://sketchfab.com/models/6328df8a0f214143a880a72b86db2ab4) — massive-graphisme | `SM_DaibaoTree_Olive`、`olive-*`、`MI_DaibaoFoliage_Olive_*` | 約 67,438 → 40,460 triangles に簡略化。結合・FBX 変換、材質・4 LOD・配置時の高さを調整。 |

## 地理データ

### OpenStreetMap

© [OpenStreetMap contributors](https://www.openstreetmap.org/copyright)。元データベースは [Open Database License 1.0（ODbL）](https://opendatacommons.org/licenses/odbl/1-0/) に基づき提供されています。

大場の建物 footprint、道路・橋の経路、植栽分類と配置の一部に OpenStreetMap のデータを使用しています。座標をローカル空間へ変換し、ゲーム向けの立体形状、高さ・材質、交差点・橋接続、衝突形状を作成・調整しています。対象は `Content/Environment/Daibao/{Buildings,Roads,Bridges,Landmarks,Repair20260924}` と `Content/Maps/Lvl_Biome_Daibao.umap` の配置情報です。

この出典表示は、派生データベースに必要な提供条件を免除するものではありません。再配布に際してデータベース部分には ODbL の条件を適用し、ゲームコード用 MIT と区別してください。

### 国土地理院

出典：[国土地理院の地理院タイル](https://maps.gsi.go.jp/development/ichiran.html)（標高タイル）を加工して作成。

`Content/Environment/Daibao/Terrain/` の地形は標高タイルを再標本化・メッシュ化したものです。ゲーム用に海面近傍・海底・接地高さと衝突形状を調整しており、測量や正確な地理情報の用途には適しません。

利用条件は [国土地理院コンテンツ利用規約](https://www.gsi.go.jp/kikakuchousei/kikakuchousei40182.html)と、同規約が示す公共データ利用規約第 1.0 版（PDL1.0）によります。出典・加工表示および対象データに適用される条件を維持してください。GSI 由来の標高データを CC0 または MIT と表示しません。

## プロジェクト制作部分

都市・荒地の手続き地形、トンネル、建物配置用形状、窓・屋根・砂岩の手続き texture、水 shader、道路標示、地下 tactical map はプロジェクトで制作しています。第三者データを使用する範囲は上記に記載しました。自作部分の公開ライセンスは [ASSET_LICENSES.md](ASSET_LICENSES.md) の状態に従います。
