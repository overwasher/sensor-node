##### [Home repo](https://github.com/overwasher/home/) | [Overwatcher code](https://github.com/overwasher/overwatcher) | [Sensor Node code](https://github.com/overwasher/esp-firmware) | [Telegram bot code](https://github.com/overwasher/telegram-bot) | [Task Tracker](https://taiga.dcnick3.me/project/overwasher/)

# Overwasher: sensor-node

Sensor-node component gathers accelerometer telemetry of the washing machine, detects changes in its activity, and sends notifications on status updates as well as raw telemetry data to the [Overwatcher](https://github.com/overwasher/overwatcher).

## Development Stack
Term 'sensor-node' encapsulates both software and hardware parts.

#### Hardware 
- ESP32 chip (with Wi-Fi module)
- MPU6050 accelerometer
- I2C wires

#### Software
- C code
- [esp-idf v4.3.1](https://github.com/espressif/esp-idf/releases/tag/v4.3.1)


Note that sensor-node component is **not** written in the object-oriented paradigm, because the latter is not meant for the embedded programming. Reason is that philosophy of OOP and practicalities of hardware are on distant, hence incompatible levels of abstraction. However, we are making use of other more suitable design patterns, that allow us to follow single responsibility principle and separate interfaces.


## Architecture
Sensor-node software comes as several modules with particular responsibilities:
- [`Accelerometer`](https://github.com/overwasher/sensor-node/blob/main/main/accelerometer.c): initializes I2C and MPU6050. Upon filling buffer of MPU6050, ESP32 receives interrupt and reads acceletation data
- [`Activity detection`](https://github.com/overwasher/sensor-node/blob/main/main/activity_detection.c): upon receiving buffer with telemetry, decides whether status of the washing machine has changed, and if it was the case, signals `Overwatcher Communicator` to send corresponding update to the server
- [`Overwatcher Communicator`](https://github.com/overwasher/sensor-node/blob/main/main/overwatcher_communicator.c) establishes an https connection with the server and provides functionality for sending status update and raw telemetry data
- [`Telemetry`](https://github.com/overwasher/sensor-node/blob/main/main/telemetry.c): upon receiving buffer (in parallel with `Activity detection`), stores buffer to the flash memory. When flash memory becomes almost full, it signals `Overwatcher Communicator` to send raw telemetry.
- [`Wi-Fi Manager`](https://github.com/overwasher/sensor-node/blob/main/main/wifi_manager.c) initializes wi-fi modules and provides implementation of connecting and disconnecting to the Access Point, which `Overwatcher Communicator` relies on. In general, we want to turn off wi-fi when it is not needed, in order to enter light sleep mode, optimizing power consumption.


## How to build & flash

VScode provides a [nice plugin](https://github.com/espressif/vscode-esp-idf-extension) to work with `esp-idf`: so, install it, clone repo, and build project with `Ctrl+E, B`.
If you have an ESP32 at your disposal, you can flash the project to it as well.

There is no list of hardware specifications yet. When we finalize hardware design files, we will publish them.

## How to contribute

Currently, the project is in heavy development and may change a lot in the nearest future. 

*We do not recommend contributing at this stage*. 

Later, when project becomes more stable, we may create some contribution guidelines for everyone to use. 
