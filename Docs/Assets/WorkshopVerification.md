# Workshopアセットの検証記録

2026-10-12。今回は保存済み正本、配布データ、Cycles参考画像、拡張ローダーの接続仕様を制作・検証しました。CPUローダーとGPU描画への接続は後続工程です。

## 使用環境

| 項目 | 実際に使用した版・条件 |
|---|---|
| Blender | 5.2.1 LTS、Windows、build hash `9e2066aef7ef` |
| glTFエクスポータ | Blender付属Khronos glTF Blender I/O v5.2.40 |
| Python | Blender付属3.13.13、MSC v.1944、AMD64 |
| NumPy | Blender付属2.3.4 |
| glTF-Validator | 固定版2.0.0-dev.3.10 |
| CPU | Intel Core i9-13900F、24 cores／32 logical processors |
| Cycles比較画像 | CPU、512 samples、seed 0、adaptive samplingなし、denoiseなし、最大バウンス1 |
| Cycles完成画像 | CPU、512 samples、seed 0、denoiseあり、最大バウンス8、AgX Medium High Contrast |
| 新規画像 | 1280×720、8 PNGと8線形float32 EXR |
| 外部環境 | 2048×1024、Cycles CPU、256 samples、seed 0、denoiseなし、線形float32 EXR |
| IBL | 既存CPU積分、Hammersley 1024 samples、RGBA16F／RG16F DDS |

## 構造・数値・再生成

- glTF-Validatorのエラー・警告・Info・Hintはすべて0件です。
- Standard／Stressの2 Scene、61共有Mesh、62 Primitive、16 Material、40 PNGを検証しました。Standardは251,200三角形／199 Mesh配置、Stressは2,260,800／1,791。各Sceneの固有Mesh・Material参照集合が一致し、三角形数と配置数は9倍です。
- 5個の固有Meshが1万三角形以上あります。閉じた形状はUV・法線の頂点分割を位置で溶接して検査し、全辺の面参照数が2でした。縮退三角形0、範囲外Index0、NaN／Inf0、ID重複0です。
- 単位法線、単位接線、直交性、W=±1と三角形内の符号、局所境界、World境界、正の一様スケール、階層循環を検証しました。
- PNGのCRC・RGBA8・寸法、Normalの単位長、ORMの共有とAO=1、MASKの0／255とcutoff 0.5、BLENDのAlpha 0.24を確認しました。全mipのRGBA8画像容量は184,549,664 bytesです。
- glTFと設定のカメラ位置・向き・垂直FOVを照合しました。20秒／60 Hz／1,201スナップショットの全経路を展開し、有限値とカメラの非縮退を確認しました。
- 点光源8／72、Sun各1本と、保存済みLight.energyからSPEC単位への変換を照合しました。Sunは1.2×683=819.6 luxです。
- DDSの面数、各mipの寸法・形式・バイト数、有限値、HDRの保持を確認しました。+Yの天頂が青いHDR、−Yが地面側となり、方位方向の連続性も確認しました。
- 一定色C=(0.13,0.37,1.7)でIrradiance=πCを確認しました。float64積分の最大絶対差は`9.33e-14`です。鏡面の粗さ別mipでも一定色を保持しました。
- 正本だけを隔離フォルダへコピーし、`.generated`を使わず梱包PNGから再書出ししました。glTF・BIN・40 PNG・シーン設定がバイト単位で一致し、元の2 blendのSHA-256は変化しませんでした。
- 初期生成を既存正本へ実行すると`Saved source exists`で終了コード1になり、正本を変更しません。
- 重複ID、Scene選択の不正、MASK cutoffの変更、縮退三角形、負のスケール、階層循環の6種類を隔離したデータで作り、すべて拒否しました。

接線にはBlenderの丸め差があったため、書出し済みの位置・法線・UV・Indexからfloat64で角度重み付きの接線を生成し、正規直交化後にfloat32へ保存しています。元の位置・法線・UV・Index順を維持し、再生成時のBIN一致を確認しています。

## 参考画像

全EXRのRGBは有限かつ非負です。比較用PNGは線形EXRに露出・Reinhard・sRGBを適用した独立計算と照合し、最大差は8-bitの1以内でした。

| 画像 | 線形RGB最大値 | 条件 |
|---|---:|---|
| Hero Direct | 57.6864 | 直接光、OpaqueMask |
| Hero Environment | 3.4215 | 環境光、OpaqueMask |
| Hero Combined | 47.7567 | 合成、OpaqueMask |
| Materials | 328.8707 | 材質接写、OpaqueMask |
| Occlusion | 119.1280 | 間仕切り・格子、OpaqueMask |
| Transparency | 32.4979 | 透明カバー、Full |
| Stress | 61.7324 | 9区画、OpaqueMask |
| Beauty | 45.8721 | 間接光・denoise・AgX、Full |

直接光と環境光の切替で画像が変化することも確認しました。8 PNGを開き、タービンの形状、材質、棚と間仕切り、格子の穴、カバー越しの内容、モニター、負荷比較版を目視確認しました。[画像と観察点](Workshop.md)を参照してください。

参考画像とリアルタイム描画には、材質モデル、接線、局所遮蔽、環境反射、Alphaと間接光による差が残ります。画像を画素一致の正解として扱いません。

## 実行した確認と後続工程

`Tools/TestWorkshopAssets.ps1`でValidator、再書出し、正本保持、数値・ハッシュ、6種類の不正データ、全参考画像、初期生成の上書き拒否を確認しました。Pythonファイルの構文検査と`git diff --check`、新規Markdownのローカルリンク・画像参照も確認しています。

`Tools/ValidateProjects.ps1`で既存33組のvcxproj／filtersを検証しました。今回はC++、HLSL、プロジェクト、SDK設定を変更していないため、MSBuild・SPIR-V・GPU描画の再試験は実施していません。拡張ローダーとDeferred接続時に両APIのDebug／Release試験へ引き継ぎます。

変更前のモデル・画像・DDS・EXR・Blender正本248ファイルはGitの元データと一致しています。既存の描画サンプル、第三者Submodule、記事リポジトリは変更していません。
