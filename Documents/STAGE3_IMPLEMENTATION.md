# Stage3 開発状況と保存スキーマ

2026-10-10。v0.4.1でStage3Aの表示・記録操作を整理。Stage3全体の完了宣言ではない。
仕様の順序は3A（ログと写真の対応）→3B（正解付きOCR評価）→3C（候補保存と手動確定）。ユーザー指定の実ログ・写真・OCR参考画像を読取り専用で参照。10月10日14:20:44の撮影イベントとPNGの保存先・時刻が対応し、参加者62件を復元（partial）。OCRの正解一覧と精度検証は未完了。合成試験を実環境の互換性・OCR精度の検証と扱わない。

## 固定スキーマ（バージョン1）

既存の独自URI `urn:kunivrcphoteditor:metadata:1.0` を使用する。`kpe:CaptureRecord` は `rdf:parseType="Resource"` の構造体。次のプロパティはこの構造体内に置く。XML末尾追加ではなく既存XMPパケットへ統合する。

| プロパティ | 型・意味 |
| --- | --- |
| SchemaVersion | 整数文字列 `1` |
| ToolVersion | 生成版番号 |
| ReferenceTime / DetectedAt | UTCのISO 8601、ミリ秒・Z付き。撮影照合時刻と追加検知時刻を分離 |
| ReferenceTimeSource | xmp / filename / photo-log。追加検知しか根拠がない画像は自動保存を保留 |
| TimeStatus | estimated。写真保存ログをシャッター時刻の証明としない |
| SessionId | 選択ログのファイル識別子。ログ全文・接続文字列を含めない |
| WorldID | 対象時刻のログのワールドID |
| InstanceId | 接続文字列を直接保持しないインスタンス識別子 |
| MemberListStatus | reconstructed / partial / unknown。reconstructedは取得ログからの復元のみを意味する |
| CandidateStatus | not-observed / no-match / observed / ambiguous。3Aはnot-observed |
| CandidateWindowSeconds | 秒数。3Aの予約値3、OCR未実行 |
| InstanceMembers | rdf:Seq。各rdf:liは構造体でDisplayName・UserIdを持つ。未取得IDは空欄。同名を自動統合しない |
| ModelCandidates | rdf:Seqの構造体配列。3Aは空配列とnot-observedを併記し「候補なし」と区別 |

3B/3Cの候補構造体はDisplayName、UserId、RawText、ObservedAt、ObservationCount、MatchMethod、MatchScore、OcrConfidence、Bounds、Ambiguousを予約する。認識器が信頼度を提供しない場合は空欄とし、架空のスコアを作らない。手動確定Modelsは既存の`kpe:Subjects`へ対応し、自動配列とは別に保存する。

## Stage3Aの運用と保留

「人物記録設定 ▼」からログ・写真フォルダーと対象PID、処理状況・再試行を設定する。ログ初期値は `%AppData%\..\LocalLow\VRChat\VRChat`、写真初期値はPictures既知フォルダー配下のVRChat。既存設定を保持する。最新ログは名前の日時で自動選択し、`output_log_YYYY-MM-DD_hh-mm-ss.txt` と `output_log_YYYY_MM_DD-hh-mm-ss.txt` に対応する。同日時で曖昧な選択は保留する。

サイドビュー選択右の記録開始／停止は監視と自動記録をまとめて切り替える。アプリ起動直後はOFF。OFFは青ボタン・灰色丸、ONは赤ボタン・赤丸点滅。VRChat／ログ待ちはONのまま状態欄へ表示し、監視終了／開始失敗はOFFとなる。停止後は新しい保存を開始せず、開始済みのバックアップ・原本置換を安全に完了させる。

撮影画像への上書きを既定とし、AB_バックアップを必須で作成する。既存の命名・管理記録・上限設定を再利用し、バックアップ失敗時は置換しない。手動編集用の自動バックアップOFFでもStage3はバックアップを作成する。参加者情報が写真共有時に渡ることを設定欄に表示する。

シンプル表示の「同室者」「モデル候補」は名前を改行表示し、取得状態を併記する。手動モデルは別項目。not-observedは未観測、no-matchは候補なし、partialは部分的な記録として表示する。不正な配列は取得状況不明とし、別プレフィックスでも名前空間URIで解析する。

ログは読取り・書込み・削除共有で開き、追記の未完行を次回へ持ち越す。ファイル識別子・切詰めを監視し、切詰め・未対応イベントを不明／部分的として扱う。入退室はIDを優先し、IDのない退室で同名の人物を推測して消さない。

写真は定期的なサブフォルダー再走査で検出する。初回の既存写真は基準一覧へ登録する。安定確認・ログ猶予5秒・再試行期限60秒は検証用初期値で、実環境で調整が必要。同秒の人物／インスタンス境界、時刻矛盾、ワールド矛盾、撮影ログがない手動追加、不明なセッションは保留する。最新ログ切替前の待機写真は元ログ・セッションを保持して処理する。複数VRChat時や最新ログとプロセスの対応が不明な場合は新しい検出を保留する。PNG/JPEGへの自動記録に限定し、WebPは対象外と表示する。

設定と処理記録はEXE配下`stage3-settings.json`・`stage3-journal.json`へ集約する。記録が壊れている場合は保全し、監視を開始しない。保存予定を記録してから、既存ライターで一時保存・再読込み・画像と既存XMLの保持検証、AB_作成と管理記録保存、撮影画像置換を行う。既存CaptureRecordは重複記録しない。AB_画像は監視対象外とする。失敗・保留は写真単位で理由を表示する。

## 参照・検証対象

ログの入退室・インスタンス・写真イベント形式は[公開LogWatcher](https://github.com/vrcx-team/VRCX/blob/master/Dotnet/LogWatcher.cs)を参照して調査した。実装コードの移植ではない。現行ログ、写真保存ログがないカメラ設定、同秒境界・連写・外部編集・再起動復旧・CPU/I/O負荷は実環境確認が必要。

OCR・実時間ウィンドウ取得・候補の採用／除外UIは3A検証後の3B/3Cで接続する。これらを実装済みと表示しない。

2026-10-10のユーザー指示により、初期仕様の出力コピー方式をバックアップ必須の上書き方式へ変更。実データ参照先は引き続き読取り専用で検証し、書込み試験はself-test-data内の合成画像のみを使用する。

