> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

# RE4 Head Tracking

Head tracking for Resident Evil 4 Remake that moves your view with your head while your mouse or controller still controls aim, no VR headset required.

<!-- ![Mod GIF](https://raw.githubusercontent.com/itsloopyo/resident-evil-4-headtracking/main/assets/readme-clip.gif) -->

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Resident Evil 4 Remake](https://store.steampowered.com/app/2050650/Resident_Evil_4/) (Steam)
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking app (smartphone, webcam, or dedicated hardware)
- Windows 10/11 (64-bit)

## Installation

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/resident-evil-4-headtracking/releases)
2. Extract the ZIP anywhere
3. Double-click `install.cmd` (it auto-detects your game and installs REFramework if needed)
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`
5. Launch the game; head tracking is enabled automatically

The installer automatically finds your game via Steam registry lookup. If it can't find the game:
- Set the `RESIDENT_EVIL_4_PATH` environment variable to your game folder, or
- Run from command prompt: `install.cmd "D:\Games\RE4"`

### Manual Installation

For placing files by hand (or installing the Nexus ZIP, which contains only the plugin files):

1. Install [REFramework](https://github.com/praydog/REFramework-nightly/releases) for RE4 (extract to game root)
2. Copy `RE4HeadTracking.dll` to `<game>/reframework/plugins/` (the mod writes `HeadTracking.ini` there on first launch)

## Setting Up OpenTrack

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases)
2. Configure your tracker as input
3. Set output to **UDP over network**
4. Host: `127.0.0.1`, Port: `4242`
5. Start tracking before launching the game

### VR Headset Setup

A VR headset makes an excellent high-precision tracker.

1. Connect your headset to the PC with Air Link (Quest) or [Virtual Desktop](https://www.vrdesktop.net/)
2. Launch SteamVR so the headset is tracked
3. In OpenTrack, set the input to **SteamVR**
4. Set output to **UDP over network** (`127.0.0.1:4242`)
5. Start tracking before launching the game

### Webcam Setup

No special hardware needed; OpenTrack's built-in **neuralnet tracker** uses any webcam for 6DOF face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**
2. Select your webcam in the tracker settings
3. Set output to **UDP over network** (`127.0.0.1:4242`)
4. Start tracking before launching the game
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed

### Phone App Setup

This mod includes built-in smoothing for network jitter, so you can send directly from your phone on port 4242 without needing OpenTrack on PC.

1. Install an OpenTrack-compatible head tracking app
2. Configure it to send to your PC's IP on port 4242 (run `ipconfig` to find it)
3. Set the protocol to OpenTrack/UDP

**With OpenTrack (optional):** If you want curve mapping or visual preview, route through OpenTrack. Set OpenTrack's input to "UDP over network" on a different port (e.g. 5252), point your phone app at that port, and set OpenTrack's output to `127.0.0.1:4242`. Make sure your firewall allows incoming UDP on the input port.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Recenter view | `Home` | `Ctrl+Shift+T` |
| Toggle head tracking | `End` | `Ctrl+Shift+Y` |
| Toggle positional tracking | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world-space / camera-local) | `Page Down` | `Ctrl+Shift+H` |
| Toggle aim reticle | `Insert` | `Ctrl+Shift+U` |

Each action fires from either its nav-cluster key or its chord - they are
registered simultaneously, not configurable alternatives. The chord set exists
for tenkeyless / laptop keyboards without a nav cluster.

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize:

```ini
[Network]
Port=4242                       ; Must match OpenTrack output port

[Tracking]
Enabled=true                    ; Enable/disable head rotation tracking
YawSensitivity=1.0              ; Horizontal rotation (0.1-3.0)
PitchSensitivity=1.0            ; Vertical rotation (0.1-3.0)
RollSensitivity=1.0             ; Head tilt (0.1-3.0)
InvertYaw=false                 ; Invert horizontal rotation
InvertPitch=false               ; Invert vertical rotation
InvertRoll=false                ; Invert head tilt
Smoothing=0.0                   ; Rotation smoothing (0.0-1.0, baseline 0.15 floor applied)

[Position]
Enabled=true                    ; Enable/disable 6DOF position tracking
SensitivityX=2.0                ; Lateral sensitivity (0.0-5.0)
SensitivityY=2.0                ; Vertical sensitivity (0.0-5.0)
SensitivityZ=2.0                ; Depth sensitivity (0.0-5.0)
LimitX=0.30                     ; Max lateral offset in meters
LimitY=0.20                     ; Max vertical offset in meters
LimitZ=0.40                     ; Max forward offset in meters
LimitZBack=0.10                 ; Max backward offset (prevents camera clipping)
Smoothing=0.15                  ; Position smoothing (0.0-1.0)
InvertX=false                   ; Invert lateral axis
InvertY=false                   ; Invert vertical axis
InvertZ=false                   ; Invert depth axis

[Hotkeys]
; Virtual key codes (hex)
RecenterKey=0x24                ; Home - Recenter view
ToggleKey=0x23                  ; End - Enable/disable
PositionToggleKey=0x21          ; Page Up - Toggle position
YawModeKey=0x22                 ; Page Down - Toggle world/camera-local yaw
ReticleToggleKey=0x2D           ; Insert - Toggle reticle

[Reticle]
Enabled=true                    ; Show the head tracking reticle overlay

[General]
AutoEnable=true                 ; Auto-enable tracking on game start
WorldSpaceYaw=true              ; true = horizon-locked; false = follows camera pitch
```

Delete the file to reset to defaults.

## Troubleshooting

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in game root)
- Check `reframework/` folder exists with `plugins/RE4HeadTracking.dll` inside
- Try running the game as administrator once

**No tracking response:**
- Verify OpenTrack is running and outputting data
- Check UDP port matches (default 4242)
- Press **End** to enable tracking, **Home** to recenter
- Check firewall isn't blocking UDP port 4242

**Jitter:**
- Increase position smoothing in HeadTracking.ini
- If using a phone app over WiFi, some jitter is expected - the built-in interpolation helps

**Wrong rotation axis:**
- Adjust sensitivity multipliers or use the Invert settings in the Position section

**Yaw feels wrong when looking up or down at extreme angles:**
- Try toggling between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd` from the release folder. This removes the mod DLLs. REFramework is only removed if it was originally installed by this mod. To force-remove REFramework:

```
uninstall.cmd /force
```

## Building from Source

### Prerequisites

- [CMake](https://cmake.org/) 3.20+
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ desktop workload
- [pixi](https://pixi.sh) task runner
- Resident Evil 4 Remake installed (for deployment only)

### Build

```bash
git clone --recurse-submodules https://github.com/itsloopyo/resident-evil-4-headtracking.git
cd resident-evil-4-headtracking

# Build and deploy to game (release)
pixi run install

# Build only (debug)
pixi run build

# Package for release
pixi run package
```

### Available Tasks

| Task | Description |
|------|-------------|
| `pixi run build` | Build the mod (Debug configuration) |
| `pixi run build-release` | Build the mod (Release configuration) |
| `pixi run install` | Build release and deploy to game directory |
| `pixi run deploy` | Build debug and deploy to game directory |
| `pixi run detect-game` | Show detected game installation path |
| `pixi run uninstall` | Remove the mod from the game |
| `pixi run package` | Create release ZIPs |
| `pixi run clean` | Clean build artifacts |
| `pixi run clean-all` | Clean build artifacts and release output |
| `pixi run release` | Version bump, build, tag, and push |

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil 4 Remake
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking software
- [CameraUnlock](https://github.com/itsloopyo/cameraunlock-core) - Shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Capcom. Use at your own risk.
