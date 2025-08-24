/**
 * SPDX-License-Identifier: LGPL-2.1
 * @copyright 2025 Ryotaro Onuki
 */
#include <Arduino.h>

#include "app_log.h"
#include "matter_switch.h"
#include "rgb_led.h"
#include "servo_motor.h"

#define CONFIG_APP_PIN_RGB_LED PIN_RGB_LED  //< 8 (defined in pins_arduino.h)
#define CONFIG_APP_PIN_SERVO_CTRL 20
#define CONFIG_APP_PIN_SERVO_POWER 19

MatterSwitch matter_;
RgbLed led_(CONFIG_APP_PIN_RGB_LED);
ServoMotor servo_;

void setup() {
  Serial.begin(CONFIG_MONITOR_BAUD);

  matter_.begin();
  matter_.printOnboarding();

  servo_.begin(CONFIG_APP_PIN_SERVO_CTRL, CONFIG_APP_PIN_SERVO_POWER);
}

void loop() {
  /* show status */
  if (!matter_.isCommissioned()) {
    led_.setBackground(RgbLed::Color::Magenta);
  } else if (!matter_.isConnected()) {
    led_.setBackground(RgbLed::Color::Red);
  } else {
    led_.setBackground(RgbLed::Color::White);
  }

  /* handle event */
  MatterSwitch::Event event;
  if (matter_.getEvent(event, 0)) {
    led_.blinkOnce(RgbLed::Color::Blue);
    switch (event.type) {
      case MatterSwitch::EventType::SwitchOn:
        LOGI("[Event] Switch ON");
        servo_.setTargetDegree(180, 90);
        break;
      case MatterSwitch::EventType::SwitchOff:
        LOGI("[Event] Switch OFF");
        servo_.setTargetDegree(0, 90);
        break;
    }
  }

  servo_.handle();
  led_.update();
  yield();
}
