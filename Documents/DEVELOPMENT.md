# KuniVRCPhotEditor 開発ガイド

[概要](../README.md) ｜ [利用ガイド](USER_GUIDE.md) ｜ [簡易手順](../Stable/readme.md)

アプリのC++ソース・ヘッダー・リソース定義は `Src` に格納しています。ソリューション／プロジェクトとビルド用スクリプトは直下、アイコンは `Assets`、外部ライブラリーは `third_party` にあります。

VS2022でリポジトリ直下の `KuniVRCPhotEditor.sln` を開き、Debug／x64を選びます。Ctrl＋Shift＋Bでビルド、Ctrl＋F5で起動します。

`build.cmd Release` でRelease／x64を生成し、出力は `x64\Release\KuniVRCPhotEditor_vN.M.X.exe`（現在は `KuniVRCPhotEditor_v0.6.12.exe`）です。Debugも `x64\Debug` 配下に同じ版付き名称で生成します。build.cmdはこの開発PCのVS2022 Community配置を使用します。`build.cmd Debug Test`／`build.cmd Release Test` は自己・UI・終了試験を実行します。試験用書込み先はEXE配下のself-test-dataで、本番設定・参照写真は変更しません。試験成功は全要件や実VRChat精度の保証ではありません。

統合キャッシュ設定の画面仕様、容量単位、無効化・再有効化の動作は[統合仕様](CACHE_SETTINGS_PLAN.md)を参照してください。v0.6.12の既存記録ではDebug／Releaseビルド、観測・設定移行・v062統合設定・キャッシュ・MDB対象試験、各3回終了試験が成功しています。全体UI・実環境の大量データ移行は未検証です。詳細は[引継ぎ履歴](../work_logs/KuniVRCPhotEditor_history.md)を参照してください。一覧サムネイルはメモリ、ディスク、原画像の順に取得します。通常のディスク追加条件と明示的な［キャッシュする］の条件は[利用ガイド](USER_GUIDE.md#オプション注意点)に記載しています。

- [更新履歴](UPDATELOG.md)
- [閲覧要件](VIEWER_REQUIREMENTS.md)
- [メタデータ編集・保持方針](STAGE2_IMPLEMENTATION.md)
- [人物記録・OCRの実装状況](STAGE3_IMPLEMENTATION.md)
- [Stage4：今後の機能・検証計画](KuniVRCPhotEditor_Stage4.md)
- [Stage5：MDB・写真カテゴライズ仕様](KuniVRCPhotEditor_Stage5.md)
- [v0.5.8：ID・表示名対応表／Import・Export仕様](V0.5.8_PLAN.md)
- [v0.5.9：選択画像から対応表への反映](V0.5.9_PLAN.md)
- [第三者ライセンス通知](THIRD_PARTY_NOTICES.md)
- [Stable向け簡易手順](../Stable/readme.md)

## 主な保存先

以下はEXEの配置フォルダーを基準とする標準保存先です。

| データ | 保存先 |
| --- | --- |
| 閲覧・観測・OCR・記録設定 | Data/settings.json |
| メタデータキャッシュ | Data/MetadataDB |
| ディスクサムネイル | Data/ThumbnailDiskCache |
| 観測イベント | Data/observation-events.txt |
| ID・表示名対応表などの管理JSON | Data配下の用途別ファイル |
| 背景画像の管理コピー | EXE配置フォルダー直下（直近3件の保持ルール） |
| 写真のAB_バックアップ本体 | 元写真と同じフォルダー |

旧MetadataDB・ThumbnailDiskCacheとObservation/events.txtは移動前後の識別情報を照合して切り替えます。競合・使用中・アクセス不可時は旧保存先を固定して利用し、次回起動で再判定します。新旧を自動上書き・結合せず、キャッシュ設定のタイトルへ旧保存先の利用を通知します。設定の移行条件は次節を参照してください。独立した色設定ファイルは作成せず、settings.jsonのobservationColorsへ標準／ナイト各10行の#RRGGBB配列を保存します。写真バックアップや背景画像の管理コピーは、このキャッシュ保存先統合に伴って移動しません。

## 設定ファイルの配置（v0.6.12）

標準保存先はEXE配下のData/settings.json。旧EXE直下のsettings.jsonは起動時に移行し、閲覧・観測・OCR・記録設定を同じファイルで保持する。移行先が既に存在するなどの競合時は旧設定を保全して利用し、保存先統合の警告をキャッシュ設定のタイトルへ表示する。新旧を自動上書き・結合しない。Dataが重複して入れ子にならないよう、他のデータ保存先はアプリ基準フォルダーから求める。
