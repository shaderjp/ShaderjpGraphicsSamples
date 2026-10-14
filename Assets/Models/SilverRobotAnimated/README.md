# Silver Robot Animated

`SilverRobot.gltf`＋`SilverRobot.bin`はGPUスキニングの基本22／23／発展10用です。`SilverRobot.glb`は同じ内容を1ファイルへまとめた他ツール向けの版です。53ジョイント、31,138頂点、44,584三角形、5つの定数PBR材質、4インフルエンス枠を使います。

保存済み正本は[SilverRobotAnimated.blend](../../../SourceArt/Blender/SilverRobot/SilverRobotAnimated.blend)。Aポーズ原本を別に保持しています。[Blender制作・操作・出力](../../../Docs/Assets/SilverRobotAnimated.md)、[CPUローダー](../../../Docs/GltfSkinningLoading.md)、[GPU検証](../../../Docs/SkinningVerification.md)を参照してください。

30 fpsで焼き込んだUAL_Idle／Walk／Run（Jog）／JumpStart／JumpLoop／JumpLandと、Bind_Apose／Retarget_Tposeを含みます。JumpStartとJumpLandは終端で保持、その他のUALクリップはループが初期値です。Rootの水平移動を除き、腰の揺れと上下動を保持しています。

- ロボットのメッシュ・材質・リグはMIT、Copyright (c) 2026 masafumi。再配布ではリポジトリの[LICENSE](../../../LICENSE)を保持してください。
- UALモーションはQuaternius、CC0-1.0。[Quaternius-CC0.txt](Quaternius-CC0.txt)を同梱しています。モデルのMITとは別の素材です。
- `Animations.json`に公式URL、素材ミラーの固定コミット、取得版、元Action名、骨対応、元の秒数／焼込み秒数、SHA-256を保存しています。
- `Humanoid.json`に人型の意味名、実ボーン名、親子関係、Blenderレスト行列を保存しています。

実行ローダーはJSON＋外部BINを使用します。UV／Textureなし、LINEARキー、Morphなしというこのアセットのプロファイルに合わせています。アプリ起動にBlenderや外部モーションライブラリの再読込は必要ありません。
