# 第28〜30回の学習順と制作工程

記事と配布サンプルは、基本編の第30回までを収録対象にします。各段階をDirect3D12／Vulkanの独立した実行プロジェクトとして保存しています。

| 回 | テーマ | 読む・操作する内容 |
|---|---|---|
| 28 | [CharacterController](CharacterController.md) | 入力、三人称カメラ、状態による姿勢ブレンド |
| 29 | [MotionMatching](MotionMatching.md) | 姿勢・希望軌跡からのフレーム検索、慣性化、足IK |
| 30 | [TraversalController](TraversalController.md) | カプセルの連続衝突判定、滑り、接地、段差、通常ジャンプ |

第29回で地上アニメーションの選択を切り替え、第30回でその配置を形状の制約へ従わせます。描画の初期化・リソース・同期・破棄は各APIの`Sample.cpp`に残し、共通のCPU処理と分けて解説します。

## モーションを編集する

`SourceArt/Blender/MotionMatching/SilverRobotMotionSet.blend`を正本にします。Quaternius Universal Animation Library 1の固定Standard版から7クリップを取り込み、Blenderで発進・停止・左右旋回の6クリップを補った、計13クリップです。地上検索には10クリップを使い、通常ジャンプの3クリップは第30回で直接再生します。

保存済みblendを編集して`export.py`を実行すると、glTF／BIN／GLB、Root軌跡、足接地、出典とハッシュを出力します。`MotionDatabaseBuilder`は同じC++の姿勢評価を使って検索データを生成し、各サンプルのビルド時にも実行します。モデル・制作コードはMIT、取り込んだQuaterniusの動作はCC0です。

初回の制作を再現するときだけ`Tools/AcquireMotionSources.py`と`generate.py`を使います。取得したZIPは`.cache/motion-matching`へ保存し、固定ハッシュを確認します。通常の編集で生成処理を実行しないでください。旧SilverRobotとSilverRobotAnimatedの正本・配布データは保持しています。

## レベルを編集する

`SourceArt/Blender/TraversalCourse/TraversalCourse.blend`のRender／Collision／Targetsを編集し、同じフォルダの`export.py`で出力します。実行時には出力済みのモデルとJSONを使うため、Blenderの導入は不要です。

## 記事とリポジトリを更新する順序

1. 共通章でCPUのアルゴリズムを説明する。
2. 各APIの記事で、配置・姿勢をGPUへ渡す場所と同期を説明する。
3. Debug／Releaseをビルドし、素材、CPU、GPU、ウィンドウ操作を確認する。
4. 検証済みの実行ファイルから1280×720画像を取得し、記事の画像と検証記録を更新する。
5. ソリューション、filters、記事リンク、アセットの出典を確認してリポジトリへ反映する。

具体的な実行コマンドと結果は[検証記録](MotionMatchingVerification.md)にまとめます。
