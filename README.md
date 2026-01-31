# watch_nes

NES emulator for watchOS with a SwiftUI front end and a C core.

## Overview
This project runs classic NES ROMs on Apple Watch. The core is written in C for performance and portability, while the UI and system integration use SwiftUI. ROMs are bundled in the app and selected with the Digital Crown. Rendering is scanline-based and presented as a CGImage each frame.

## Features
- C core with CPU, PPU, APU, bus, cartridge, and mapper support.
- SwiftUI UI with crown-scrolled ROM menu and on-watch controls.
- Scanline rendering path for background + sprites.
- ROMs bundled inside the app target.

## Project layout
- `nes/nes Watch App/Core/include` and `nes/nes Watch App/Core/src`: C core (CPU/PPU/APU/bus/cartridge/mappers).
- `nes/nes Watch App/Core/NesCore.swift`: Swift wrapper over the C core.
- `nes/nes Watch App/Emulator/EmulatorViewModel.swift`: runtime loop, audio engine lifecycle.
- `nes/nes Watch App/CartridgeMenuView.swift`: ROM selection UI.
- `nes/nes Watch App/ContentView.swift`: emulator screen + controls.
- `nes/nes Watch App/Roms`: bundled `.nes` ROMs (for development/testing).

## Build and run
1. Open `nes/nes.xcodeproj` in Xcode.
2. Select the `nes Watch App` target.
3. Run on a watchOS simulator or device.

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
