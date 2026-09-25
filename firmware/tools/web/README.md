# Webプレビュー

`firmware/` から `python tools/web/preview_server.py` を実行し、http://localhost:8000 を開く。
ポートを変更する場合は `python tools/web/preview_server.py --port 8080` のように指定する。
製品の `web/` と共通コンポーネントの `web/` をビルド時と同じ方法で結合する。
最小化には `components/device_common/tools/web/requirements.txt` の依存が必要。
APIはモックで、実機は操作しない。
