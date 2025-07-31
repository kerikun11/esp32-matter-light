# ESP32 Matter Example

- Hardware
  - ESP32-S3
- Software
  - ESP-IDF v5.4.2

![状態遷移図](diagram.drawio.svg)

|  LED   | 人感センサ | 人感スイッチ | 意味                             |
| :----: | :--------: | :----------: | :------------------------------- |
|   白   |     ON     |      ON      | 人感センサ有効                   |
|   青   |     ON     |      ON      | 人感センサ反応中                 |
|   黄   |    OFF     |      ON      | 明るいため人感センサ無効         |
|   消   |    OFF     |     OFF      | 人感スイッチにより人感センサ無効 |
|   赤   |    OFF     |     N/A      | ネットワークエラー               |
| ピンク |    OFF     |     N/A      | Matter接続待機状態               |

## 参考

- [espressif/arduino-esp32 - Example esp_matter_light | ESP Component Registry](https://components.espressif.com/components/espressif/arduino-esp32/versions/3.0.5/examples/esp_matter_light?language=en)
