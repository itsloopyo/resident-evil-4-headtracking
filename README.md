# RE4 Head Tracking

![Resident Evil 4 running with this mod](https://raw.githubusercontent.com/itsloopyo/resident-evil-4-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Resident Evil 4 that moves the camera with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

> [!CAUTION]
> ## Experimental prototype - expect missing core features
>
> This is **not** a finished mod.
>
> Current builds may only test whether head tracking can drive the camera. Bug fixes and core features like decoupled look/aim, independent reticle behavior, correct shot direction, off-screen reticle support, movement handling, and comfort tuning may be missing at this early stage of development.

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

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle head tracking | `End` | `Ctrl+Shift+Y` |
| Toggle positional tracking | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world-space / camera-local) | `Page Down` | `Ctrl+Shift+H` |

Each action fires from either its nav-cluster key or its chord - they are
registered simultaneously, not configurable alternatives. The chord set exists
for tenkeyless / laptop keyboards without a nav cluster.

## Configuration

The mod creates a config file at `reframework/plugins/HeadTracking.ini` on first run. Edit it to customize:

A comment has to sit on its own line, above the key. The parser hands the whole
text after `=` to the value reader. For a `true`/`false` or text setting that
text is compared as a whole, so a trailing `; note` makes the comparison fail
and the setting silently keeps its default. Numeric settings survive a trailing
comment because the number is read off the front of the text, which is why some
lines below still carry one. Putting every comment on its own line always works.

```ini
[Network]
UDPPort=4242                    ; Must match OpenTrack output port

[Sensitivity]
YawMultiplier=1.0               ; Horizontal rotation (0.1-5.0)
PitchMultiplier=1.0             ; Vertical rotation (0.1-5.0)
RollMultiplier=1.0              ; Head tilt (0.0-2.0)

[Smoothing]
LocalSmoothing=0.0              ; Tracker on this machine, loopback (0.0-1.0)
RemoteSmoothing=0.15            ; Tracker is a remote network device (0.0-1.0)

[Position]
; Enable/disable 6DOF position tracking
Enabled=true
SensitivityX=2.0                ; Lateral sensitivity (0.0-5.0)
SensitivityY=2.0                ; Vertical sensitivity (0.0-5.0)
SensitivityZ=2.0                ; Depth sensitivity (0.0-5.0)
LimitX=0.30                     ; Max lateral offset in meters
LimitY=0.20                     ; Max vertical offset in meters
LimitZ=0.40                     ; Max forward offset in meters
LimitZBack=0.10                 ; Max backward offset (prevents camera clipping)
; Invert lateral axis
InvertX=false
; Invert vertical axis
InvertY=false
; Invert depth axis
InvertZ=false

[Hotkeys]
; Virtual key codes (hex)
ToggleKey=0x23                  ; End - Enable/disable
PositionToggleKey=0x21          ; Page Up - Toggle position
YawModeKey=0x22                 ; Page Down - Toggle world/camera-local yaw

[General]
; Auto-enable tracking on game start
AutoEnable=true
; true = horizon-locked; false = follows camera pitch
WorldSpaceYaw=true
```

Delete the file to reset to defaults.

## Troubleshooting

**Sending a log:**
- REFramework writes one log per game launch at `<game>/re2_framework_log.txt`. That generic name is used for every RE Engine title, so it is the right file for this game too. If the game folder is not writable it lands in `%APPDATA%\REFramework\<exe name>\` instead.
- The file is truncated on every launch, so it only ever holds the current session. Attach it as-is to a bug report.
- This mod's lines are prefixed `[RE4HT]`. The startup sequence to look for is: `Plugin loaded`, `Config loaded from ...`, `UDP receiver started on port ...`, `Initialization complete`, then `First tracker pose received: ...` once the tracker sends anything.

**Mod not loading:**
- Ensure REFramework is installed (`dinput8.dll` in game root)
- Check `reframework/` folder exists with `plugins/RE4HeadTracking.dll` inside
- Try running the game as administrator once

**No tracking response:**
- Verify OpenTrack is running and outputting data
- Check UDP port matches (default 4242)
- Press **End** to enable tracking
- Check firewall isn't blocking UDP port 4242

**View is off-centre:**
- Centre in your tracker app: OpenTrack's Center bind, the CENTER button in a phone app, or SteamVR's own centring. The mod has no centre of its own, so the tracker is the only place to set one.

**Jitter:**
- Increase `RemoteSmoothing` (phone or other network tracker) or `LocalSmoothing` (tracker on this PC) in the `[Smoothing]` section of HeadTracking.ini
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

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- [Capcom](https://www.capcom.com/) - Resident Evil 4 Remake
- [praydog](https://github.com/praydog/REFramework) - REFramework
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking software
- [CameraUnlock](https://github.com/itsloopyo/cameraunlock-core) - Shared head tracking library

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Capcom. Use at your own risk.
