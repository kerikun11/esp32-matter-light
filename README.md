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
|   赤   |    N/A     |     N/A      | ネットワークエラー               |
| ピンク |    N/A     |     N/A      | Matter Device Commissioning 状態 |

## 初回セットアップ

1. Matterデバイス登録  
   Amazon Alexa/Google HomeアプリからMatterデバイスの追加を行う。
   - QRコード: https://project-chip.github.io/connectedhomeip/qrcode.html?data=MT:Y.K9042C00KA0648G00
   - ペアリングコード: 34970112332
2. 赤外線登録  
   照明ON/OFFの赤外線リモコンデータの登録を行う。
   1. シリアルコンソールで下記コマンドを送信する  
      `record on`
   2. 照明のリモコンをMatterデバイスに向けて照明ONボタンを押す
   3. シリアルコンソールで下記コマンドを送信する  
      `record off`
   4. 照明のリモコンをMatterデバイスに向けて照明OFFボタンを押す

## 参考

- [espressif/arduino-esp32 - Example esp_matter_light | ESP Component Registry](https://components.espressif.com/components/espressif/arduino-esp32/versions/3.0.5/examples/esp_matter_light?language=en)
