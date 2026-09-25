# 開発手順

ESP-IDF 5.5.5、ESP-Matter 1.5.xを使用する。解決済みバージョンは
`firmware/dependencies.lock` に記録する。以下は製品ルートから実行する。

```sh
source "$IDF_PATH/export.sh"
cd firmware
python -m pip install -r components/device_common/tools/web/requirements.txt
# 初回のみ。lightはESP32-S3も対応する。
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

USB書き込みはブートローダー・パーティションを含む。OTAはアプリ領域のみを更新するため、
パーティション構成やブートローダーを変更した場合はUSBから書き込む。

## OTA

`firmware/` で実行する。接続先を対象機のホスト名またはIPに置き換える。

```sh
python components/device_common/tools/ota/ota.py info --host <hostname>.local
python components/device_common/tools/ota/ota.py update --host <hostname>.local
```

ビルド済みアプリは `build/project_description.json` から選択する。
`--file` で明示できる。製品名照合、転送進捗、再起動後のELF SHA-256照合を実行する。
詳しくは [OTAツール](../firmware/components/device_common/tools/ota/README.md) を参照。

## Webプレビュー

```sh
python tools/web/preview_server.py
```

http://localhost:8000 を開く。操作はモックに反映され、実機には送信されない。
`web/` と共通コンポーネントの `web/` の編集は次の読み込みで反映される。
Web生成物は `build/esp-idf/main/generated/` に置き、Git管理しない。

## 検証

```sh
python -m unittest discover -s components/device_common/tests -v
python -m unittest discover -s tests -v
idf.py build
```

ホストテストにはg++とWebビルド用Python依存が必要。
実機HTTP検証は `python tests/verify_device.py --host <hostname>.local`。
実機検証は照明・サーボを実際に操作し、設定を保存・復元する。
Matterコントローラとの相互接続、物理IR波形、実際のサーボ位置、実機OTAは別途確認する。
