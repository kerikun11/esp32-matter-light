# アーキテクチャ

各製品は、このディレクトリだけでビルド・Webプレビュー・OTA更新を実行できる。
`firmware/components/device_common/` は通常のESP-IDFコンポーネントとして同梱する。
将来はディレクトリ全体を独立リポジトリにして同じ場所へsubmoduleとして配置する。
現在は外部リポジトリへの依存やsubmoduleの初期化は不要。

## 責務

| 配置 | 責務 |
| --- | --- |
| `main/app/` | 製品の状態遷移・入出力の統合 |
| `main/board/` | ピン定義 |
| `main/drivers/` | 製品固有ハードウェア |
| `main/matter/` | 製品のエンドポイントと受信イベント |
| `main/settings/` | 設定スキーマと永続化 |
| `main/http/` | 製品の操作・設定API |
| `main/console/` | シリアル操作（搭載製品のみ） |
| `web/` | 製品固有のHTML・CSS・JavaScript |
| `components/device_common/` | NVS、ネットワーク、Matter共通操作、OTA、Web共通資産・生成ツール |
| `tests/` | 製品テストと実機検証 |

## 状態とタスク

Matterのコールバックは操作をキューへ渡す。HTTPタスクは設定のmutexと操作完了通知を使う。
アプリタスクが入力、製品ルール、出力、Web表示状態の公開を順に処理する。
ローカルのON/OFF変更は `attribute::report()` で通知し、自分の変更を受信イベントへ戻さない。
Fabric参照・操作とペアリング受付は共通MatterサービスでCHIPロックを取得する。
ネットワーク監視はIPv4喪失時のDHCP再試行とIPv4/IPv6のmDNS更新を担当する。

## 共通コンポーネントの同期

共通コンポーネントは両製品で同じ内容に保つ。修正したファイルをもう一方へ反映し、
共通テスト・各製品のテスト・各製品のビルドを確認する。
比較時は `__pycache__/` を除外する。製品側から共通コードへの依存のみを許可する。
共通コードが製品側の `main/` をincludeしたり、製品の設定ファイルを読み込むことは禁止する。

将来の切り出し単位は、公開ヘッダとC++ソースだけでなく、`web/`、`cmake/`、`tools/`、
`tests/`、ライセンスを含む `device_common/` 全体。
