# device_common

製品に依存しないESP-IDFコンポーネント。`firmware/components/device_common/` に配置する。
現在は各製品へ同一内容を同梱し、将来はこのディレクトリ全体をsubmoduleへ置き換える。

- `include/device_common/`, `src/`: ボタン、LED、NVS、ネットワーク、Matter共通操作、HTTPヘルパー、OTA。
- `web/`: 共通デザインとHTTP/DOMヘルパー。製品のHTMLから `@common/` で参照する。
- `cmake/web_assets.cmake`: 製品のWeb資産を結合・最小化・gzip圧縮してC++ヘッダを生成。
- `tools/ota/`: Python標準ライブラリだけで動くOTA CLI。
- `tests/`: コンポーネント単独で実行できるホストテスト。

製品側のCMakeで `PRIV_REQUIRES device_common` を指定し、ヘッダは
`device_common/network/network_health.h` のようにincludeする。
共通コンポーネントから製品の `main/` への依存を持たせない。

Web生成:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/../components/device_common/cmake/web_assets.cmake")
device_common_web_assets(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../web/index.html")
```

テスト（このディレクトリから）:

```sh
python -m unittest discover -s tests -v
```

ライセンスは [LICENSE.md](LICENSE.md)。既存ファイルの著作権表示を維持する。
