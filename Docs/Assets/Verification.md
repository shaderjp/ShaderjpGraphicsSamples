# Shader Ballの検証記録

検証日：2026-10-11。これはアセット制作の検証です。既存のDirect3D12／VulkanアプリケーションのビルドやGPU実行結果は[既存サンプルの検証記録](../Verification.md)を参照してください。

## 使用環境

| 項目 | 実際に使用したもの |
|---|---|
| OS | Windows x64 |
| Blender | 5.2.1 LTS、Release、Windows、build hash `9e2066aef7ef` |
| Blender build日時 | 2026-08-25 02:38:20、commit日時 2026-08-24 16:50 |
| glTFエクスポータ | Khronos glTF Blender I/O v5.2.40（Blender付属） |
| Bake | Cycles **CPU**、64 samples、seed 0、16 px margin |
| 参考レンダー | Cycles **CPU**、48 samples、seed 0、denoise有効、1200×1000 |
| PBRの表示変換 | AgX、Medium High Contrast、exposure 0 |
| Albedoの表示変換 | Emissionシェーダ、Standard、look None、exposure 0 |
| Khronos glTF-Validator | `2.0.0-dev.3.10`、公式Windows x64 CLI |
| Validator配布ZIPのSHA-256 | `c5068f51205deedc28acc3529ee7e11ee60e853454f673093398eba80142202c` |
| Python | Blender付属Python。構造・PNG検証は標準ライブラリ、制作はbpy／NumPy |

参考画像はBlenderでの描画です。この作業ではGPUレンダー、Direct3D12／VulkanでのglTF表示確認は行っていません。glTFローダーは後続作業のため、CPUで制作・検証しています。

## 配布データの結果

| 項目 | Albedo版 | PBR版 |
|---|---|---|
| Khronos Validator | Error 0、Warning 0、Info 0、Hint 0 | Error 0、Warning 0、Info 0、Hint 0 |
| Node / Mesh / Primitive / Material | 1 / 1 / 1 / 1 | 1 / 1 / 1 / 1 |
| 書出し後の頂点 | 4,696 | 4,696 |
| 三角形 / インデックス | 8,640 / 25,920 | 8,640 / 25,920 |
| インデックス型 | UNSIGNED_SHORT、5123 | UNSIGNED_SHORT、5123 |
| 外部BINのバイト数 | 202,112 | 277,248 |
| 頂点属性 | POSITION、NORMAL、TEXCOORD_0 | POSITION、NORMAL、TEXCOORD_0、TANGENT |
| 参照画像数 | 1（Albedo） | 4（Albedo、Normal、ORM、Emissive） |
| 拡張・アニメーション・スキン・モーフ・カメラ・ライト | なし | なし |

glTFのバウンドはX/Zがおよそ−0.6〜0.6 m、Yが0〜1.16 mです。数値はFLOAT書出しで約`2.4e-8 m`の丸めを含みます。UVは0〜1の範囲で1セット、法線は単位長、接線は単位長かつ法線に直交し、Wは±1です。Blenderの接線成分が小数4桁へ丸められることを考慮し、直交性は`2e-4`以内で検査しています。

7画像はすべて1024×1024の8-bit RGBA PNG、アルファ255です。Metallic・Roughness・AOのRGB一致、ORMのR/G/Bと対応単独画像の**全ピクセル一致**、Normalの非自明な凹凸・正の接線Z・量子化後の単位長、発光模様が小領域に収まることを自動検査しました。ファイル参照、アクセサ範囲、インデックス範囲、ManifestのSHA-256も一致しています。

## 保存と編集結果の再現

次の検証を実行し、すべて成功しました。

1. `Tools/TestShaderBallAssets.ps1`で両glTFをKhronos Validatorへ渡し、構造・画像の独自検査を実行。
2. 正本`.blend`だけを新しい一時フォルダへコピー。中間PNGの`.generated`を持たない状態で、梱包画像から両glTFを再書出し。
3. 再書出しした両glTFもValidatorでError 0・Warning 0。元の配布データと三角形ごとの位置・法線・UV、材質、画像、**JSON・BIN・PNGのバイト列がすべて一致**。
4. 書出し前後で正本`.blend`のSHA-256が不変。
5. 再ベイクスクリプトでNormal／AO／ORMを更新後、保存・再読込・再レンダー・再書出しが成功。
6. 空の一時フォルダへの初期生成、保存済みデータからの書出し、構造・画像検証が成功。
7. 編集保持の追加確認として一時コピーの球の頂点をX方向へ0.003 m動かし、Emission Strengthを0.5へ変更して保存。別のBlenderプロセスから書き出して、変更した位置と`emissiveFactor=(0.5,0.5,0.5)`を確認。正本にはこの確認用変更を適用していません。
8. 初期生成の既存ファイル保護を確認。`--overwrite-source`なしで既存保存先を指定すると終了コード1となり、`.blend`のハッシュは不変。

正本`.blend`のSHA-256は`12372b5ef2e8bd5db0dc7857a2d402d492edb5bf38b81f64163688e141024855`です。配布データの個別ハッシュは[Manifest.json](../../Assets/Models/ShaderBall/Manifest.json)に記録しています。機械可読な最新実行結果とログは`.cache/shader-ball/verification.json`などに生成し、Git管理には含めません。

`.gitattributes`で配布glTFとManifestをLFに固定し、BIN・PNG・BLENDをバイナリとして扱っています。Windowsの`core.autocrlf`による書出しバイト列とハッシュの変化を防ぎます。

Blender側のPBR書出しログには、ORMの複数チャンネルから同じImage Textureを辿る際に「More than one shader node tex image used for a texture」というSamplerの注意が出ます。各チャンネルは**同じORMノード・同じLINEAR／REPEAT設定**を使っています。実際のglTFはORM 1枚を共有し、Khronos Validatorでは警告0です。Blenderの`Material.use_nodes`にはBlender 6.0向けのDeprecationWarningも出ますが、今回の5.2 LTSでは正常動作しています。

## 目視確認と既存プロジェクト

BlenderのAlbedoのみの参考レンダーとPBR参考レンダー、Normal・AO・ORMの実画像を開いて確認しました。矢印の上向き、上下の帯、塗装と金属の境界、粗さと表面凹凸、溝と台座、発光位置を点検しました。画像は[アセット解説](ShaderBall.md)に添付しています。

`Tools/ValidateProjects.ps1`は既存10プロジェクトすべて成功しました。今回、`.sln`・`.vcxproj`・`.vcxproj.filters`・C++・HLSL・ビルド設定は変更していません。既存サンプルのビルド・GPUライフサイクルテストを今回のアセット検証として再実行したとは記録していません。

サイト記事の制作、JSONライブラリの導入、C++ glTFローダー、GitHubへのpush・公開は今回の変更に含めません。
