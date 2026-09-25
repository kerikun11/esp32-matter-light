# firmware/tests

## test_web.py

`../tools/web/`のビルドパイプライン（最小化・gzip圧縮・HTTPネゴシエーション・実際のC++
ヘッダとの整合性）を実機なしで検証する自動テスト。g++が必要です。

```sh
python -m unittest discover -s firmware/tests -v
```

## verify_device.py

実機に対する黒箱動作検証スクリプト。`/version`・`/`・`/state`・`/action`・
`/settings`のHTTP APIをWi-Fi経由で叩き、トグル操作の反映・発振しないこと・
不正な入力への耐性・設定の保存/復元などを確認します。Matterコントローラや
実機のIR受光部が必要な項目（本当に照明が反応するかなど）は対象外です。

```sh
python3 firmware/tests/verify_device.py --host xiao.local
python3 firmware/tests/verify_device.py --host 192.168.0.60 -v
```
