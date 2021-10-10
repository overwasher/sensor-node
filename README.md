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

## Hardware: the PCB

![pcb 3d view](https://raw.githubusercontent.com/overwasher/sensor-node-hardware/master/3DVIEW_RENDER.png)

We've created a PCB design so that you don't have to design your own. You get more details [here](https://github.com/overwasher/sensor-node-hardware)

## How to build & flash

VScode provides a [nice plugin](https://github.com/espressif/vscode-esp-idf-extension) to work with `esp-idf`: so, install it, clone repo, and build project with `Ctrl+E, B`.

You can flash it to the board built accoring to [these hardware design files](https://github.com/overwasher/sensor-node-hardware). Note though, that the software was not yet adapted for this hardware (for example the model of accelerometer used is different). The plan is to manufacture the first batch of the boards and start to adapt the software to them.

## Software Architecture

Sensor-node software comes as several interacting modules:
- [`Accelerometer`](https://github.com/overwasher/sensor-node/blob/main/main/accelerometer.c)
- [`Activity detection`](https://github.com/overwasher/sensor-node/blob/main/main/activity_detection.c)
- [`Telemetry`](https://github.com/overwasher/sensor-node/blob/main/main/telemetry.c)
- [`Overwatcher Communicator`](https://github.com/overwasher/sensor-node/blob/main/main/overwatcher_communicator.c)
- [`Wi-Fi Manager`](https://github.com/overwasher/sensor-node/blob/main/main/wifi_manager.c)

![sensor-node-architecture](https://github.com/overwasher/sensor-node/blob/main/sensor-node-view.jpg)

MPU6050 writes instantaneous accelerations to its FIFO memory, and upon FIFO overflow, it issues an interrupt to ESP32.

In ESP32,
- `Accelerometer` task, when `FIFO_INTERRUPT` arrives, reads MPU6050's FIFO, saves it, and posts `ON_BUFFER_EVENT` to `accel_event_loop`. Then it starts waiting for the next interrupt in the blocked state.
- `accel_event_loop`serves only one type of event: an `ON_BUFFER_EVENT` (which `Accelerometer` task posts). This event has two handlers: one performs `Activity Detection`, another one — saves buffer as `Telemetry` in flash or memory. Both handlers, from time to time, initiate sending data to the server by unblocking respective tasks:
- `ad_sending_task`/`tm_sending_task` wait to be unblocked by the respective handler and send status update/telemetry parcel to Overwatcher. Implementation of sending data is provided in the `Overwatcher Communicator` module.
- Communication with Overwatcher relies on `Wi-fi Manager` that provides means to initiate and terminate connection with Access Point (i.e. `start_communication()` and `stop_communication()`)

## Activity Detection algorithm explained

### Features

* domain-aware (knows about the washing mashine cycles)
* adjustable
* results propagate fast (< 3 minutes)

### Data flow

The acceleration data is read at rate of 100 Hz and is accumulated in internal accelerometer FIFO (1024 bytes in size).

The FIFO containts frames - triples of accelerations (x, y, z) encoded as 2-byte signed integers. Note that the size of FIFO (1024) is not divisible by size of frame (2 * 3 = 6). The FIFO can store 170 frames and 4 more bytes, which results in one truncated frame.

When the FIFO becomes full the accelerometer fires an interrupt at ESP32 which reads the FIFO contents into the buffer. Note that at 100 Hz FIFO becomes full aproximately every 1.7 seconds.

Due to weird architecture decisions on accelerometer side one measurement is lost, because interrupt is fired when the new frame cannot fit into the FIFO.

Because of this FIFO-centered hardware architecture, a similar, buffer-based approach is taken to process the data.

Buffer is an atomic unit of processing in the activity detection algorithm. It is similar to the contents of FIFO, but with a few catches:

- The accelerations are scaled to milli-g instead of device-specific scale (1 mg ~= 0.00981 m/s^2)
- The last truncated frame is not stored there; only the full 170 frames are

### On the vibration physics

We measure the vibration from the washing mashine, but where does it come from and what is it like? The picture below shows a model that can be useful to understand it.

![image](https://user-images.githubusercontent.com/10363282/136657192-1a45c128-8906-4a9e-99a8-c9f786d492b5.png)

The drum of the washing machine (`M` on the diagram) is suspended inside the washing machine to reduce outside vibrations. The suspension can be modelled as pairs of springs and dampers (`k_x`, `k_y`, `b_x`, `b_y`).

Note that in our case the vibrations are still present on the lid, as it linked with the drum.

The core source of the vibrations is an eccentric rotating mass - the clothes (`m`). By rotating they induce harmonic forces that cause the drum to vibrate (harmonically) along x and y.

Precisely these vibrations are measured by the accelerometer, although offset from zero by gravity.

![image](https://user-images.githubusercontent.com/10363282/136657175-e68f2823-3d76-43db-af84-219637be58e2.png)

(all measurements are in milli-g)

### Sensitivity stage

The sensitivity, a first stage of the activity detection, is a decision upon a buffer: was there an activity or not.

Based on the buffer frames, we calculate 170 instantaneous magnitudes. Then the buffer "amplitude" - the difference between max and min magnitude - is computed. If it is above a certain threshold, we conclude that there was movement. If it is below that threshold, we assume there was no movement.

To exclude the influence of noise (outliers), instead of minimum and maximum, we choose 10th and 90th percentiles of the magnitudes.

The threshold of the difference is a parameter of sensitivity stage; currently, it is 20 milli-g (~0.2 m/s^2). Such a choice of parameters yields a good interpretation of whether the accelerometer experienced movement or not.

### Conservatism stage

The сonservatism, a second stage of the activity detection, is needed to account for the following scenarios:

- During the washing cycle, there are pauses when the mode is changed, and water is being poured in and out. So, while the washing machine is absolutely motionless, it is still active, which should be reflected in the reported status.
- We also need to exclude the occasional movement of the washing machine door (to which the accelerometer is attached to). The users may do this, but it does not mean that the machine is active.

So the decision is based on a history of decisions on buffers. If out of several most recent buffers (50, which is ~1.4 minutes), 20 or more are asserting that the washing machine was active, it is assumed to be indeed active.

Note that this process may yield false-negative (which are bad) and false-positive (not that bad) results. For users, it's preferable not to come when there was a possibility rather than to go and find out that it was in vain, so the algorithm is biased towards positive decisions.

### Reporting

Sensor-node reports to the Overwatcher upon a change of status (trespassing of the threshold of active buffers count) and upon timeout — every 2 minutes, to confirm both status of the washing machine and the operability of the sensor-node itself.

## How to contribute

Currently, the project is in heavy development and may change a lot in the nearest future. 

*We do not recommend contributing at this stage*. 

Later, when project becomes more stable, we may create some contribution guidelines for everyone to use. 
