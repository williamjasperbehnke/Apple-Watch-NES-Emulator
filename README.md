# Apple Watch NES Emulator

NES emulator for watchOS with a SwiftUI front end and a C++ core.

[![watch_nes demo](https://img.youtube.com/vi/lWY7RMkrnCU/hqdefault.jpg)](https://www.youtube.com/shorts/lWY7RMkrnCU)

## Overview
This project runs classic NES ROMs on Apple Watch. The core is written in C++ for performance and portability, while the UI and system integration use SwiftUI. ROMs are bundled in the app and selected with the Digital Crown. Rendering is scanline-based and presented as a CGImage each frame.

## Features
- C++ core with CPU, PPU, APU, bus, cartridge, and mapper support.
- SwiftUI UI with crown-scrolled ROM menu and on-watch controls.
- Scanline rendering path for background + sprites.
- ROMs bundled inside the app target.

## Project layout
- `nes/nes Watch App/Core/include` and `nes/nes Watch App/Core/src`: C++ core (CPU/PPU/APU/bus/cartridge/mappers).
- `nes/nes Watch App/Core/NesCore.swift`: Swift wrapper over the C++ core.
- `nes/nes Watch App/Emulator/EmulatorViewModel.swift`: runtime loop, audio engine lifecycle.
- `nes/nes Watch App/CartridgeMenuView.swift`: ROM selection UI.
- `nes/nes Watch App/ContentView.swift`: emulator screen + controls.
- `nes/nes Watch App/Roms`: bundled `.nes` ROMs (for development/testing).

## Full installation guide (Xcode)
This is a complete guide to install the app on a real Apple Watch using Xcode.

### Requirements
- A Mac with Xcode installed.
- An Apple Watch paired to your iPhone.
- An Apple ID signed into Xcode.
- The watch and Mac on the same Wi‑Fi (or the watch connected via USB/charger).

### 1) Get the project
1. Clone or download this repo.
2. Open `nes/nes.xcodeproj` in Xcode.

### 2) Set up signing
1. In Xcode, click the project in the left sidebar.
2. Select the `nes Watch App` target.
3. Go to **Signing & Capabilities**.
4. Check **Automatically manage signing**.
5. Choose your Apple ID team in the **Team** dropdown.

### 3) Connect your watch
1. Make sure your iPhone is unlocked.
2. Open the Watch app on iPhone.
3. Keep the watch unlocked and on the charger if possible.
4. In Xcode, click the run destination menu and select your Apple Watch.

### 4) Build and install
1. Select scheme **nes Watch App** (top bar).
2. Click **Run** (▶︎) or press `Cmd + R`.
3. Xcode will build and install the app on the watch.

### 5) Launch the app
- Open the app from the watch’s app grid or app list.

## ROMs
ROMs are loaded from the app bundle. Place `.nes` files under:
- `nes/nes Watch App/Roms`

The menu lists the ROM names without the `.nes` extension.

## Mapper support
- Mapper 0 (NROM)
- Mapper 1 (MMC1)
- Mapper 3 (CNROM)

## Audio
Audio uses a full APU implementation (pulse, triangle, noise, DMC) and is produced via `AVAudioEngine` using a source node. The audio producer runs on a dedicated queue, with a small ring buffer to smooth timing.

## Known issues
- Crackling can still occur in some games (notably Super Mario Bros) under load.

## Troubleshooting
- If a ROM doesn’t load, check the mapper ID and ensure it’s supported.
- If the menu doesn’t scroll on first launch, back out to the menu and try again.
- If audio is silent on device, verify watchOS audio routing and ensure the watch is not in silent mode.
- If your watch doesn’t appear as a run destination, keep the Watch app open on your iPhone and make sure Bluetooth/Wi‑Fi are on.
- If signing fails, confirm you’re signed into Xcode (Xcode → Settings → Accounts).
