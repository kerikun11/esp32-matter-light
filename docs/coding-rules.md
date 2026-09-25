# コーディングルール

C++はGoogle C++ Styleを基に、関数・メソッド名をlowerCamelCaseとする。
型はUpperCamelCase、変数はsnake_case、privateメンバはsnake_case_、定数はkUpperCamelCase。
製品ルートの `.clang-format` を使用する。自動生成物と外部ライブラリは整形対象外。

- `.h` は公開インターフェースを中心とし、大きな実装は `.cpp` に置く。
- 共通ヘッダは `device_common/<責務>/<名前>.h`、製品ヘッダは `<責務>/<名前>.h` でincludeする。
- 共通コードの製品差は引数・設定・コールバックで渡す。製品名による分岐を作らない。
- SDK所有の状態へアクセスする際はSDKが要求するロックを取得する。
- Matter・HTTPタスクから製品の出力を直接駆動せず、アプリタスクへ操作要求を渡す。
- JSON入力を検証し、状態の確定後にHTTP応答用スナップショットを公開する。
- NVSキーと保存型の変更は保存済み設定への影響を確認する。
- Pythonツールは明確な終了コードを返し、製品名や開発者の絶対パスを埋め込まない。
- HTML・CSS・JavaScriptは分離し、ビルド時に結合・最小化・gzip圧縮する。

整形例（製品ルートから）:

```sh
find firmware/main firmware/components/device_common/include firmware/components/device_common/src -type f \( -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format -i
```
