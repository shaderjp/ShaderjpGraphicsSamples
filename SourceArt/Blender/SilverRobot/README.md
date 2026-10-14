# Silver RobotのBlender正本

`SilverRobot.blend`をBlender 5.2.1 LTSで開いて編集します。添付の白銀ロボット三面図を視覚的な参考に、新規メッシュ・材質・リグを制作しました。参考画像はblendに埋め込まず、配布物にも含めていません。

初期表示はAポーズです。`EXPORT`には`SilverRobot`と`SilverRobotRig`、`PREVIEW_ONLY`には床、撮影用カメラ、ライトがあります。撮影用コレクションは通常のViewportでは非表示です。

- 約1.91 m、53ボーン。左右の5指に各3本のボーンを用意しています。
- 単一の三角形メッシュ、5材質。装甲は1ボーンへ固定し、胴体内部・首・肘・手首・膝の柔軟部には2ボーンのウェイトを設定しています。
- Armatureモディファイアは線形ブレンドスキニングです。Preserve VolumeはOFFです。
- 各パーツは`Part.*`頂点グループで選択できます。ウェイトの編集対象はボーン名のグループです。`Part.*`は選択用で、スキニングには使いません。
- 保存済みActionは`Bind_Apose`、`Retarget_Tpose`、`Idle`、`Walk_InPlace`です。NLAトラックはすべてミュートし、初期表示への重なりを防いでいます。

Pose Modeでボーンを操作できます。ボーンをメッシュ越しに表示したい場合はArmatureのIn Frontを有効にしてください。保存済みActionの確認ではDope SheetをAction Editorに切り替え、Armatureを選び、Actionを選択して再生します。フレーム範囲は歩行1〜31、待機1〜61、30 fpsです。Aポーズへ戻すときはActionを解除し、Pose Modeで全ボーンの位置・回転・スケールをクリアします。

`Humanoid.json`は人型の意味名と、このリグのボーン名・階層・レスト行列の対応です。別リグのActionは骨の向き、A/Tポーズ、体格、階層を補正するリターゲットが必要です。ボーン名だけの変更やActionの直接割当による汎用互換性は保証しません。`Retarget_Tpose`は対応合わせのためのポーズであり、バインド姿勢はAポーズのままです。

## 保存済みシーンから再出力

リポジトリルートで実行します。編集を先にblendへ保存してください。

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
& $blenderExe --background SourceArt/Blender/SilverRobot/SilverRobot.blend --python-exit-code 1 --python SourceArt/Blender/SilverRobot/export.py
.\Tools\TestSilverRobotAssets.ps1 -BlenderPath $blenderExe
```

`export.py`は開いたシーンのメッシュ・材質・ウェイト・ActionからglTF＋BINとGLBを出力します。初期生成は呼びません。blendも保存しません。編集後は、幾何モディファイアを適用して三角形化し、各頂点のボーンウェイトの合計を1に保ってください。ボーン以外のパーツ選択グループは合計に含めません。最大4インフルエンスの出力枠を使用します。

PBR材質はcore glTFのBase Color、Metallic、Roughness、Emissiveだけで表現します。テクスチャ・外部画像・外部モデルへの依存はありません。クリアコートなどglTF拡張を必要とする編集は、現行の検証プロファイルでは拒否されます。

## 参考レンダー

```powershell
& $blenderExe --background SourceArt/Blender/SilverRobot/SilverRobot.blend --python-exit-code 1 --python SourceArt/Blender/SilverRobot/render_references.py
```

保存済みの形状とActionから、Cycles CPU、48 samples、AgX、1280×720でHero／Front／Right／Back／Walkを撮影します。正本は変更しません。

## 初期生成を試す場合

```powershell
& $blenderExe --background --factory-startup --python-exit-code 1 --python SourceArt/Blender/SilverRobot/generate.py -- --source-dir .cache/silver-robot/NewSource
```

`generate.py`は新規生成専用です。指定先にblendがある場合は拒否します。既存モデルの編集結果を再出力するときには使用しないでください。

制作・配布仕様と外部モーション候補は[アセット解説](../../../Docs/Assets/SilverRobot.md)、実測値は[検証記録](../../../Docs/Assets/SilverRobotVerification.md)に記載しています。

## Universal Animation Library適用版

`SilverRobotAnimated.blend`は原本と別の編集正本です。UAL_Idle／Walk／Run／JumpStart／JumpLoop／JumpLand、A/T確認ポーズを保持します。Armatureを選んでAction EditorからActionを選び、30 fpsで再生します。NLAはミュートです。各Actionのフレーム範囲、骨対応、接地補正、ライセンスは[Animated制作記事](../../../Docs/Assets/SilverRobotAnimated.md)にあります。

```powershell
& $blenderExe --background SourceArt/Blender/SilverRobot/SilverRobotAnimated.blend --python-exit-code 1 --python SourceArt/Blender/SilverRobot/export_animated.py
.\Tools\TestSilverRobotAnimated.ps1
```

`export_animated.py`は保存済みシーンだけを出力し、再生成・リターゲットを呼びません。`-- --output-dir <path>`で別の出力先も選べます。リターゲットの再現は原本を開いて`retarget.py -- --output <新規blend>`を使います。既存Animatedの再生成には明示的な`--overwrite`が必要です。

Animated版のロボットはMIT、6つの外部モーションはQuaterniusのCC0-1.0です。固定素材、作者、SHA-256を`Retargeting.json`と配布`Animations.json`へ記録しています。

MIT License, Copyright (c) 2026 masafumi。原本のメッシュ、材質、リグ、確認Actionは自作です。Animated版のUALモーションには別途CC0-1.0が適用されます。
