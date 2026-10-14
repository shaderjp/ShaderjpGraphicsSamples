# Silver Robotの検証記録

2026-10-15、モデル・リグ・配布glTFの制作工程を検証しました。Blender正本は[SilverRobot.blend](../../SourceArt/Blender/SilverRobot/SilverRobot.blend)、制作・操作・モーション候補は[SilverRobot.md](SilverRobot.md)を参照してください。

| 使用ツール | 実測した版・設定 |
|---|---|
| Blender | 5.2.1 LTS、build hash `9e2066aef7ef`、2026-08-25 build |
| glTF Exporter | Khronos glTF Blender I/O v5.2.40 |
| Python | Blender付属Python 3.13.13 |
| NumPy | Blender付属2.3.4 |
| glTF-Validator | Khronos 2.0.0-dev.3.10 |
| レンダー | Cycles CPU、48 samples、denoise、seed 0、AgX、1280×720 |
| CPU | 13th Gen Intel Core i9-13900F |

今回はBlenderとCPUによるアセット検証です。Visual Studioのプロジェクト、C++ローダー、シェーダーを追加・変更していないため、MSBuildや描画アプリのGPU検証はこの制作工程の結果に含めていません。

`Tools/TestSilverRobotAssets.ps1`が成功しました。再出力は初期生成スクリプトを実行せず、独立した一時フォルダへコピーした保存済みblendだけから行いました。配布のglTF、BIN、GLB、Humanoid.jsonは再出力とバイト単位で一致し、正本のSHA-256も検証前後で一致しました。

- glTFとGLBの両方で、Khronos Validatorのerrors／warnings／infos／hintsはすべて0。
- 1メッシュ、5 Primitive、5材質、1スキン、53ジョイント。出力31,138頂点、44,584三角形。
- 全頂点に有効なウェイトがあり、各頂点の合計は1。30,290頂点は1ボーン、848頂点は2ボーン。JOINTS_0／WEIGHTS_0は4成分の枠で出力され、上限4を満たす。
- ジョイント参照、重複、階層、スキンのroot配置、アクセサ、法線の長さ、インデックス、非有限値、縮退三角形を確認。
- Aポーズで`jointWorld × inverseBind`と単位行列の最大成分差は`4.3164e-7`。
- 4 Actionすべてで始点と終点のジョイント行列が一致し、Walk_InPlaceの最大差は`2.1684e-19`。Idleは2秒、Walk_InPlaceは1秒。
- 各Actionの以下のフレームで、glTFのTRSキー・JOINTS_0・WEIGHTS_0・逆バインド行列だけからCPUで線形スキニングし、BlenderのArmature変形後の全頂点と比較した。

| Action | 比較したBlenderフレーム | 1フレームの比較頂点数 | 最大位置差 |
|---|---|---|---|
| Bind_Apose | 1、2 | 31,138 | `5.6848e-7 m` |
| Retarget_Tpose | 1、2 | 31,138 | `4.7407e-7 m` |
| Idle | 1、16、31、46、61 | 31,138 | `5.6532e-7 m` |
| Walk_InPlace | 1、8、16、23、31 | 31,138 | `7.8755e-7 m` |

保存済みモデルからHero、Front、Right、Back、Walkの実レンダー5点を作成し、白銀の装甲、関節、5指、初期Aポーズと歩行中の変形を目視確認しました。レンダー画像は[Images/SilverRobot](Images/SilverRobot)にあります。これはBlenderによる画像で、将来のD3D12／Vulkanサンプルのキャプチャではありません。

機械可読の検証結果は[SilverRobotValidation.json](SilverRobotValidation.json)へ保存しました。再実行時の詳細ログと結果は`.cache/silver-robot/verification.json`、`validate.log`、`reexport.log`に出力します。

```powershell
.\Tools\TestSilverRobotAssets.ps1
```

第三者のモーションは同梱していません。Actionを含む新規アセットは自作MITデータです。QuaterniusのCC0ライブラリは今後のリターゲット候補として調査しましたが、外部クリップでのリターゲット試験はまだ実施していません。
