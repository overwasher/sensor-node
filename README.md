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

## Activity Detection algorithm explained

The presented Activity Detection algorithm is aware of both specifics of the accelerometer and parameters of the washing cycles of machines in dormitories. Yet it is simple and easily modifiable through parameters adjustment. Furthermore, it satisfies the functional requirement of providing accurate data within 3 minutes delay at most, as was verified through analysis of telemetry and reported statuses at the Overwatcher, and several 'field' tests.

In general, the algorithm possesses two mutually complementary properties: sensitivity and сonservatism.

### On buffer

But before sensitivity and сonservatism can be described, the notion of a buffer has to be introduced. Buffer is the atomic unit of processing in the activity detection algorithm. We chose the buffer-based approach due to the nature of communication between ESP32 and the accelerometer: the accelerometer has FIFO memory, and once in 10 ms, it records instantaneous acceleration in 3 components (x, y, z axes, predefined by the spacial position of the device). When FIFO memory fills up, the accelerometer sends interrupt to ESP32, and the latter starts reading FIFO, forming a buffer (which takes ~2ms). FIFO size is 1024 bytes, accelerations along each component are encoded as a 2 bytes number (and there are three components...); hence each buffer has 1024 // (2*3) = 170 instantaneous telemetry entries (thus buffers arrive with the periodicity of ~1.7 seconds). As was checked with the logic analyzer, only one measurement is lost because of interrupt and reading FIFO, and the presented algorithm tolerates such missing values.

### Sensitivity

The sensitivity, a marker of present acceleration, is a decision upon a buffer.

We noted that the vector sum of accelerations along each component is a sum of gravity of the Earth plus custom acceleration that the device is subject to — which is the one caused by approximately circular movement in the assumed use case. So, by assumption, if the washing machine is working, we expect a greater magnitude of acceleration when the washing machine drum is in the bottommost position (as gravity and centrifugal forces are in the same direction) and the lesser magnitude — in the uppermost position (where gravity and centrifugal forces are in the opposite direction).

So based on the buffer entries, we calculate 170 instantaneous magnitudes. With the described motivation, we would set the difference between maximum and minimum magnitudes as the decisive metric. If it is above a certain threshold, we conclude that there was movement. If it is below that threshold, we assume there was no movement.

To exclude the influence of the noise, instead of minimum and maximum, we choose 10th and 90th percentiles. 

The threshold of the difference is a direct parameter of sensitivity; currently, it is 20 milli-g (~0.2 m/s^2). Such choice of parameters yields highly accurate interpretation of whether the accelerometer experienced movement or not.


### Conservatism
The сonservatism is needed to account for false-negative (more important) and false-positive (less important) results:
- During the washing cycle, there are gaps when the mode is changed, and water is being poured in and out. So, while the washing machine is absolutely motionless, it is still active, which should be reflected in the reported status.
- We also need to exclude the occasional movement of the washing machine door (where the accelerometer is attached) when asserting that it is active.

So the decision is based on a history of decisions on buffers. If out of several most recent buffers (50, which is ~1.4 minutes), 20 or more are asserting that the washing machine was active, it is assumed to be indeed active. Note that we intend some form of bias towards indicating that the washing machine is active — for users, it's preferable not to come when there was a possibility rather than to go and find out that it was in vain.

### Reporting

Sensor-node reports to the Overwatcher upon a change of status (trespassing of the threshold of active buffers count) and upon timeout — every 2 minutes, to confirm both status of the washing machine and the operability of the sensor-node itself.

## How to build & flash

VScode provides a [nice plugin](https://github.com/espressif/vscode-esp-idf-extension) to work with `esp-idf`: so, install it, clone repo, and build project with `Ctrl+E, B`.
If you have an ESP32 at your disposal, you can flash the project to it as well.

There is no list of hardware specifications yet. When we finalize hardware design files, we will publish them.

## How to contribute

Currently, the project is in heavy development and may change a lot in the nearest future. 

*We do not recommend contributing at this stage*. 

Later, when project becomes more stable, we may create some contribution guidelines for everyone to use. 
