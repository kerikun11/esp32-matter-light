# ESP32 Matter Servo Motor Controller - KERI's Lab

ESP32で作るMatter対応サーボモーター制御装置

## 作り方

### ビルド&書き込み方法

- [ESP-IDFのドキュメント](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/versions.html)に従って開発環境を構築する (Linux推奨)。
  - 環境構築のしやすさ、ビルド時間などからLinux推奨
  - 現時点ではPlatformIOには非対応(ESP32-Arduino Matterライブラリが[非対応](https://github.com/platformio/platform-espressif32/issues/854)なため)
- ESP32のUSBポート(非UARTポート)とPCを接続して下記コマンドを実行する。

```sh
# at ESP-IDF v5.4.2 Environment
source $IDF_PATH/export.sh

# go to firmware directory
cd firmware

# build
idf build

# flash and monitor
idf flash monitor
```

### 参考

- [espressif/arduino-esp32 - Example esp_matter_light | ESP Component Registry](https://components.espressif.com/components/espressif/arduino-esp32/versions/3.0.5/examples/esp_matter_light?language=en)

---

## ライセンス

- LGPL v2.1 (Arduinoライブラリと同じライセンス)
