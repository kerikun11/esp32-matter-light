# Coding Rule

[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) に従い、以下の差分のみ独自に定める。

## 適用範囲

- 対象: `main/`, `config/` の C/C++ コード
- 対象外: `managed_components/`, `build/`, 外部から持ち込んだコード（`config/matter_closure_patch.h`）
- 外部 API が決めている名前（`app_main` など）はそちらに従う

## フォーマット

[.clang-format](.clang-format) で整形する。

```sh
clang-format -i main/*.h main/*.cpp config/*.h
```

- `ColumnLimit: 0`: 自動改行しない
- `AllowShortIfStatementsOnASingleLine: WithoutElse`: `if (!ok) return false;` を許可
- 整形したくない箇所は `// clang-format off` で囲む

## 命名規則

| 対象 | 形式 | 例 |
| --- | --- | --- |
| 型 | `UpperCamelCase` | `SmartLightController` |
| メソッド・関数 | `lowerCamelCase` | `isConnected()` |
| 変数・引数 | `snake_case` | `long_press_ms` |
| private メンバ変数 | `snake_case_` | `blink_start_` |
| 定数・enum の値 | `kUpperCamelCase` | `kRawDataBufferSize`, `Color::kRed` |
| マクロ | `UPPER_SNAKE_CASE` | `APP_LOG_BASE` |

Google との差分は、メソッド・関数を `lowerCamelCase` とすること（private メソッドにも末尾 `_` は付けない）。
