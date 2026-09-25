# ファームウェア開発ガイド

ESP32 Matter Smart Light Controllerのビルド・書き込み・更新・検証手順。
デバイスの使い方や回路は[プロジェクトREADME](../README.md)を参照してください。

## 開発環境

- ESP-IDF: 5.5.5（[dependencies.lock](dependencies.lock)に記録されたバージョン）
- ESP-Matter: 1.5.1（同ロックファイルに記録されたバージョン）
- 対応ターゲット: ESP32-C6 / ESP32-S3
- WebUIビルド用のPythonパッケージ: [requirements.txt](tools/web/requirements.txt)

依存コンポーネントは[main/idf_component.yml](main/idf_component.yml)で指定し、
ESP-IDFのComponent Managerで取得します。

## ビルド・USB書き込み

[ESP-IDFのドキュメント](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32/get-started/index.html)に従って
開発環境を用意し、リポジトリのルートから以下を実行します。
以降、このREADMEのコマンドは `firmware/` ディレクトリで実行します。

```sh
# ESP-IDFの環境を有効化
source "$IDF_PATH/export.sh"
cd firmware

# 初回またはターゲット変更時に、使用するマイコンを1つ選択
idf.py set-target esp32c6
# ESP32-S3の場合は、上のコマンドの代わりに以下を実行
# idf.py set-target esp32s3

# 初回のみ、ESP-IDFのPython環境にWebUIビルドの依存を追加
python -m pip install -r tools/web/requirements.txt

# HTML/CSS/JSの最小化・gzip圧縮も自動実行
idf.py build
```

生成されるファームウェアは `build/esp32-matter-light.bin` です。
WebUIの生成ファイルは `build/esp-idf/main/generated/` に出力され、Gitには含めません。

ESP32のUSBポート（非UARTポート）をPCに接続して書き込みます。
`/dev/ttyACM0` は接続先に合わせて変更してください。

```sh
idf.py -p /dev/ttyACM0 flash monitor
```

シリアルモニタは `Ctrl+]` で終了します。書き込みせずログだけを確認する場合:

```sh
idf.py -p /dev/ttyACM0 monitor
```

## OTA（ファームウェア更新）

curlコマンドでファームウェアを書き換えられる。書き込み後は自動で再起動する。

```sh
TARGET=xxx
curl --fail-with-body \
  --progress-bar \
  --output /dev/stdout \
  --write-out $'\nHTTP %{http_code}\n' \
  --header "Content-Type: application/octet-stream" \
  --data-binary @build/esp32-matter-light.bin \
  "http://${TARGET}.local/update"
```

`xxx`にはWeb画面で設定したホスト名を指定する。進捗バー、デバイスからのレスポンス、HTTPステータスはすべて標準出力へ表示される。

デバイスに書き込まれているファームウェアのプロジェクト名（CMakeの`PROJECT_NAME`）とアップロードするイメージのプロジェクト名が一致しない場合は拒否される（LAN内に別機種のESP32デバイスが混在していても誤って別機種用のファームウェアを書き込まないようにするためのガード）。プロジェクト名を変更した直後など、意図的に上書きしたい場合はクエリパラメータでチェックを無効化できる。

```sh
curl --fail-with-body \
  --data-binary @build/esp32-matter-light.bin \
  "http://${TARGET}.local/update?skip_check=1"
```

現在のバージョン情報は `GET /version` で確認できる。

```sh
curl "http://${TARGET}.local/version"
```

新しいファームウェアの初回起動がクラッシュループした場合は、ブートローダーが自動的に直前のファームウェアにロールバックする。

## WebUIの開発・動作確認

WebUIのソースは [main/web/index.html](main/web/index.html) です。
実機なしのプレビューを起動するには、以下を実行します。

```sh
python tools/web/preview_server.py
```

<http://localhost:8000> を開いて確認します。プレビューの操作は実機に送信されません。
ビルド処理とプレビューの詳細は[WebUIツール](tools/web/README.md)を参照してください。

### 自動テスト

WebUIの圧縮・HTTPネゴシエーション・JSON APIを検証します。g++が必要です。

```sh
python -m unittest discover -s tools/tests -v
```

### 実機検証

Wi-Fiに接続した実機のHTTP APIを検証します。
この検証は照明やセンサのトグル操作、設定の保存・復元を行います。

```sh
python tools/tests/verify_device.py --host xiao.local
# IPアドレス指定・詳細ログの場合
python tools/tests/verify_device.py --host 192.168.0.60 -v
```

検証範囲は[テストツール](tools/tests/README.md)を参照してください。
設定ペインの「デバイス情報」ではIPアドレスや登録済みFabric一覧も確認できます。

## 関連ドキュメント

- [開発ツール一覧](tools/README.md)
- [ピンアサイン](main/app_config.h)
- [OTA実装](main/ota_service.cpp)
- [ライセンス](LICENSE.md)

## 参考

- [espressif/arduino-esp32 - Example esp_matter_light | ESP Component Registry](https://components.espressif.com/components/espressif/arduino-esp32/versions/3.0.5/examples/esp_matter_light?language=en)
