# 開発指示

- 製品固有コードは `firmware/main/`、再利用可能な基盤は `firmware/components/device_common/` に置く。
- 共通コンポーネントから製品側のヘッダ・設定・ファイルパスを参照しない。
- `docs/coding-rules.md` と `docs/architecture.md` に従う。
- コードを変更したら関連するホストテストとESP-IDFビルドを実行する。
- 実機操作・OTA転送は、対象と操作がユーザーから指定された場合に実行する。
- `managed_components/`・`build/` は生成物。直接編集しない。
- 共通コンポーネントは将来submoduleへ切り出す。別製品が同じ作業範囲にある場合は、変更を両方へ反映して差分を確認する。
