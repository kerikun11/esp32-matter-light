# OTA CLI

Python 3.10以降の標準ライブラリを使用する。追加パッケージは不要。
製品の `firmware/` から実行する。

```sh
python components/device_common/tools/ota/ota.py info --host device.local
python components/device_common/tools/ota/ota.py update --host device.local
python components/device_common/tools/ota/ota.py update --host 192.168.0.60 --file build/app.bin
```

`--file` 未指定時は `--build-dir`（既定: `build`）の `project_description.json` から
アプリ用binを選ぶ。ブートローダー・結合イメージは使用しない。
転送前にイメージ記述子と接続先の製品名を照合し、接続先がtargetを返す場合はチップも照合する。
デバイス側もイメージを検証する。製品名チェックの回避オプションは提供しない。

転送後は `--wait` 秒（既定60）待ち、`/version` のELF SHA-256と起動確認状態を照合する。
`--timeout` はHTTP通信のタイムアウト秒（既定20）。同じイメージが実行済みなら転送を省略する。
転送を自動で再試行しない。通信切断で結果が不明な場合は `info` で状態を確認する。

終了コード: 0=情報取得成功・更新確認済み・同一イメージ実行済み、
2=入力/通信/転送エラー、3=転送受付後の起動確認タイムアウト。
従来のSHA未対応ファームウェアから更新可能だが、更新先にはSHA対応の `/version` が必要。

これはLAN上のHTTP OTAであり、Matter OTAプロトコルや署名配信サーバーではない。
