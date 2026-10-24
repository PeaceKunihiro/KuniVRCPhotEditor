# KuniVRCPhotEditor 開発ガイド

[概要](../README.md) ｜ [利用ガイド](USER_GUIDE.md) ｜ [簡易手順](../Stable/readme.md)

アプリのC++ソース・ヘッダー・リソース定義は `Src` に格納しています。ソリューション／プロジェクトとビルド用スクリプトは直下、アイコンは `Assets`、外部ライブラリーは `third_party` にあります。

VS2022でリポジトリ直下の `KuniVRCPhotEditor.sln` を開き、Debug／x64を選びます。Ctrl＋Shift＋Bでビルド、Ctrl＋F5で起動します。

`build.cmd Release` でRelease／x64を生成し、出力は `x64\Release\KuniVRCPhotEditor_vN.M.X.exe`（現在は `KuniVRCPhotEditor_v0.6.0.exe`）です。Debugも `x64\Debug` 配下に同じ版付き名称で生成します。build.cmdはこの開発PCのVS2022 Community配置を使用します。`build.cmd Debug Test`／`build.cmd Release Test` は自己・UI・終了試験を実行します。試験用書込み先はEXE配下のself-test-dataで、本番設定・参照写真は変更しません。試験成功は全要件や実VRChat精度の保証ではありません。

- [更新履歴](UPDATELOG.md)
- [閲覧要件](VIEWER_REQUIREMENTS.md)
- [メタデータ編集・保持方針](STAGE2_IMPLEMENTATION.md)
- [人物記録・OCRの実装状況](STAGE3_IMPLEMENTATION.md)
- [Stage4：今後の機能・検証計画](KuniVRCPhotEditor_Stage4.md)
- [Stage5：MDB・写真カテゴライズ仕様](KuniVRCPhotEditor_Stage5.md)
- [v0.5.8：ID・表示名対応表／Import・Export仕様](V0.5.8_PLAN.md)
- [v0.5.10：選択画像から対応表への反映](V0.5.9_PLAN.md)
- [第三者ライセンス通知](THIRD_PARTY_NOTICES.md)
- [Stable向け簡易手順](../Stable/readme.md)
