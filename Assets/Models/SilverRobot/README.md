# Silver Robot — GPUスキニング向けアセット

- `SilverRobot.gltf`＋`SilverRobot.bin`：構造を確認しやすいglTF 2.0。
- `SilverRobot.glb`：同じ内容を単一ファイルに梱包した版。
- `Humanoid.json`：人型の意味名とボーン名・階層・Blenderのレスト行列の対応。
- [Blender正本](../../../SourceArt/Blender/SilverRobot/README.md)、[制作仕様とモーション候補](../../../Docs/Assets/SilverRobot.md)、[検証結果](../../../Docs/Assets/SilverRobotVerification.md)。

初期状態はAポーズ。1スキン、53ジョイント、5マテリアル、4アクションを含みます。`JOINTS_0`、`WEIGHTS_0`、逆バインド行列を出力しています。装甲は剛体ウェイト、柔軟部は2ボーンのブレンドです。GPU側で扱う属性枠は4インフルエンスです。

`Bind_Apose`、`Retarget_Tpose`、`Idle`、`Walk_InPlace`は自作の確認用アクションです。歩行はその場で1秒ループし、Root Motionはありません。他リグのモーションは人型対応とレスト姿勢を用いてリターゲットします。

このフォルダは原本のアセットです。[追加したスキニングローダー](../../../Docs/GltfSkinningLoading.md)で読めます。基本22／23と発展10では、原本を保持した[UAL適用版](../SilverRobotAnimated/README.md)を使用します。

MIT License, Copyright (c) 2026 masafumi。リポジトリルートの[LICENSE](../../../LICENSE)を保持して再配布できます。第三者のモーションや参考画像は含めていません。
