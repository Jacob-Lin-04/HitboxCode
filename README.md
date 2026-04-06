# PrototypeHitBox

A custom 12-button hitbox-style fighting game controller built around a Raspberry Pi Pico, with c firmware,3DP enclosure, and future implementation for custom PCB.

## Overview

PrototypeHitBox is a custom-built hitbox-style controller for fighting games. The project focuses on designing and prototyping a 12-button all-button controller in all aspects.

Early mechanical iterations may use simple materials such as cardboard or basic 3D-printed parts to accelerate development and testing. Longer-term goals include a metal enclosure, a custom PCB, and a more polished firmware architecture suitable for low-latency gameplay.

## Scope

The scope includes:
- input layout definition
- electrical prototyping and wiring
- mechanical prototyping
- firmware development
- enclosure iteration
- documentation of the design process

This project does not initially prioritize:
- console compatibility
- polished industrial design
- mass manufacturability
- advanced configurability
- final custom PCB production

These may be addressed in later revisions.

## Project Goals

### Core Goals
- Build a functioning hitbox-style controller using a Raspberry Pi Pico
- Design all aspects of this controller (Hardware & Software)
- Document the electrical, mechanical, and firmware design process
- This project is functional and replicatable

### Stretch Goals
- Design and manufacture a custom PCB
- Improve enclosure design for durability and ergonomics
- Add support for additional buttons or configurable layouts
- Optimize firmware structure for extensibility and portability
- Win a tournament using this

## Requirements

### Functional Requirements
- The controller shall provide 12 total player inputs.
- The controller shall support the standard hitbox-style directional layout.
- The controller shall connect to a host device over USB.
- The controller shall be detected by a PC as a USB HID input device.
- The controller shall register button presses reliably and consistently.
- The controller shall support simultaneous button presses without input loss during normal gameplay.

## Current Status

### Completed
- Project scope defined
- Initial feature list drafted
- Several 3D-printed button housing components created

### In Progress
- Electrical prototyping and switch wiring
- Firmware setup
- Altium learning and schematic capture workflow

### Planned
- Full prototype assembly
- Button input testing
- USB HID validation on PC
- Mechanical iteration of enclosure
- Custom PCB design

## GPIO Switch Demo

The current firmware is a simple USB serial demo that prints a message whenever a switch connected to `GP14` is pressed.

### Wiring

- Connect one side of the switch to `GP14`
- Connect the other side of the switch to any `GND` pin

The firmware enables the Pico's internal pull-up resistor, so the pin reads HIGH when the switch is open and LOW when pressed.

### Build

From the project root:

```powershell
cmake -B build -S . -DPICO_BOARD=pico
cmake --build build
```

This produces `build/HitboxCode.uf2`.

### Flash To Pico

1. Unplug the Pico.
2. Hold the `BOOTSEL` button while plugging the Pico into USB.
3. The board will appear as a drive named `RPI-RP2`.
4. Copy `build/HitboxCode.uf2` onto that drive.
5. The Pico will reboot automatically and start the demo.

### View Terminal Output

With USB stdio enabled, the Pico appears as a USB serial device after it boots.

On Windows, open a serial terminal connected to the Pico COM port at any baud rate and watch for:

```text
Switch demo ready on GP14.
Switch pressed on GP14
```
