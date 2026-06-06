# Web UI Preview

実機なしでWeb UIのレイアウトを確認するための静的プレビューです。

リポジトリのルートで次を実行します。

```sh
python3 firmware/tools/web_preview/server.py
```

ブラウザで <http://localhost:8000> を開いてください。

プレビューはファームウェアと同じ
`firmware/main/smart_light_web_page.inc`を読み込みます。ブラウザの開発者
ツールで表示幅を変更すると、スマートフォン向けのレイアウトも確認できます。
プレビュー上の操作は実機へ送信されません。
