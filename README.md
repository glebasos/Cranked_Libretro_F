# Cranked Libretro

A libretro core port of [Cranked](https://github.com/TheLogicMaster/Cranked) - A Playdate emulator.

## About

This is a fork focused on improving the libretro port of Cranked for use with RetroArch and other libretro frontends.

## Improvements

### Core Functionality
- **Game Loading**: Fixed PDX/PDX.zip file handling and extraction
- **Display Output**: Corrected framebuffer format (XRGB8888) and memory layout
- **Crank Input**: Full implementation with multiple control methods
- **Core Options**: Configuration system for customizing emulator behavior

### Features Added
- **Crank Controls**:
  - L/R shoulder buttons for incremental rotation (5° per press)
  - Right analog stick for direct angle control
  - L3 button to toggle dock/undock state
  - Configurable initial state (docked/undocked)
  - Configurable initial angle (0-315°)

- **Display Palettes**:
  - Black & White (default)
  - White & Black (inverted)
  - Gray & Black
  - Green (Game Boy style)
  - Amber
  - Blue

### Core Options
Access via RetroArch: `Quick Menu → Options`

1. **Initial Crank State** - Whether crank starts docked or undocked
2. **Initial Crank Angle** - Starting angle in degrees (0-315°)
3. **Display Palette** - Choose color scheme for display

## Building

```bash
cd build
cmake ..
make cranked_libretro -j8
```

The compiled core will be at: `build/libretro/libcranked_libretro.dylib` (macOS) or `.so` (Linux)

## Installation

Copy the compiled core to your RetroArch cores directory:
- **macOS**: `~/Library/Application Support/RetroArch/cores/`
- **Linux**: `~/.config/retroarch/cores/`
- **Windows**: `%APPDATA%\RetroArch\cores\`

## Usage

1. Load the Cranked core in RetroArch
2. Load a `.pdx` or `.pdx.zip` file
3. Configure options in `Quick Menu → Options`
4. Play!

## Original Project

This is a fork of [Cranked](https://github.com/TheLogicMaster/Cranked) by TheLogicMaster.

For the full Cranked emulator with desktop UI and other features, please visit the original repository.

## License

This project inherits the license from the original Cranked project.

## Credits

- **Original Cranked Emulator**: TheLogicMaster
- **Libretro Port Improvements**: kurogedelic
- **Playdate SDK**: Panic Inc.

## Status

### Working
- Lua games
- Native (C) games
- Crank input
- Display output
- Core options

### Known Issues
- Some native games may have display corruption (diagonal artifacts)
- Audio support is incomplete
- No support for encrypted Catalog games

## Todo (Libretro-specific)

- Fix display corruption in some native games
- Implement audio support
- Add more display palette options
- Performance optimizations for bitmap operations
- Save state support

## Libraries and Credits

### Core Libraries
- [Unicorn](https://github.com/unicorn-engine/unicorn) - ARM CPU emulation
- [Lua 5.4 fork](https://github.com/scratchminer/lua54) - Lua runtime
- [libzippp](https://github.com/ctabin/libzippp) - PDX archive handling
- [Nlohmann Json](https://github.com/nlohmann/json) - JSON parsing
- [Capstone](https://github.com/capstone-engine/capstone) - Disassembly (optional)
- [Tracy](https://github.com/wolfpld/tracy) - Profiling (optional)

### Resources
- [Playdate SDK](https://play.date/dev/) - Official SDK and documentation
- [Libretro API](https://github.com/libretro/libretro-common) - RetroArch integration
- [Playdate Reverse Engineering](https://github.com/cranksters/playdate-reverse-engineering) - Community research

### Credits
- **Original Cranked Emulator**: [TheLogicMaster](https://github.com/TheLogicMaster)
- **Libretro Port Improvements**: kurogedelic
- **Playdate Console**: Panic Inc.

## Contributing

This is a focused fork for libretro improvements. For general Cranked development, please contribute to the [original project](https://github.com/TheLogicMaster/Cranked).
