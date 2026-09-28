# Changelog

All notable changes to this project are documented here.

This project has not had a tagged release yet. Dev builds are published from
the latest commit on the `dev` pre-release; the first versioned release will
be cut from the Unreleased section below.

## [Unreleased]

### Logging

- The log now names the config file it actually read (`Config Canonical: <path>`, or `Created` or `Migrated` on a first start), so an edit made to the wrong file is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Corrected the log path in the docs. It is `<game>/re2_framework_log.txt`, not `reframework/reframework_log.txt`; REFramework uses that generic name for every RE Engine title.

### Changed
- Settings move to `reframework\plugins\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default of the earlier version that wrote `HeadTracking.ini`, as far as the file shows which version that was, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- A number in `HeadTracking.ini` that is not a number the mod can use (`nan`, `inf`) is written as `default` where the defaults the README shows set that setting to `default`, and as the built-in value elsewhere.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Turning positional tracking off or on with `Page Up` / `Ctrl+Shift+G`, and switching the yaw mode with `Page Down` / `Ctrl+Shift+H`, is saved to `CameraUnlock.ini` straight away, so the next start keeps your choice. Turning head tracking on or off with `End` still lasts for the session only.
- The keys are renamed to the names every head tracking mod on this format uses: `[Network] UDPPort` is `UdpPort`, `[General] AutoEnable` is `EnableOnStartup`, `[Position] Enabled` is `PositionEnabled`, `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are `PositionLimitX` and so on, and `[Hotkeys] PositionToggleKey` is `CycleTrackingModeKey`. The import carries each value across.
- Since the dev build of 2026-08-20, and not caused by the move to `CameraUnlock.ini`, reading the settings changed in two commits:
  - d9ed7ce: a number that is not finite (`nan`, `inf`, `1e400`) keeps the setting's default. The dev build kept `nan` as the value and clamped an infinity to the end of the setting's range.
  - 0ed6a02, which moved the mod onto the shared plugin code: a number followed by anything but an inline comment (`0,15` or `0.5abc`), or written in hex (`0x1`), keeps the setting's default. The dev build read the number at the front of the text, so `LocalSmoothing=0,15` gave 0.
  - 0ed6a02: a position limit below 0.01 is kept as written, down to 0. The dev build raised it to 0.01.
  - 0ed6a02: `LimitY` sets how far the view moves down as well as up. The dev build held downward travel at 0.20 m whatever `LimitY` said.
  - 0ed6a02: a hotkey code the mod no longer accepts as a hotkey (0, a negative code, one above `0xFE`, or Shift, Ctrl or Alt, which the chords are made of) keeps that hotkey's default key. The dev build registered the code as written.
  - 0ed6a02: a sensitivity outside the dev build's range is read as written up to 5 (rotation) or 10 (position), where the dev build clamped it. Either way it is not carried over, as above.
- `Page Up` / `Ctrl+Shift+G` turns positional tracking off and on again instead
  of cycling three modes. The third mode disabled head rotation, and it sat
  directly after the mode a `[Position] Enabled=false` config starts in, so one
  press of a key labelled "toggle position" switched head rotation off.
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, and the mod's own centre. Your tracker owns the
  centre now. Set it there, with OpenTrack's Center bind, the CENTER button in
  a phone app, or SteamVR, and the mod applies what the tracker sends.
  Two centres in series was the problem: when the view was off you could not
  tell which side was wrong, and switching trackers meant centring in both.
- Smoothing is now two user-configurable parameters in a new `[Smoothing]` section of `HeadTracking.ini`: `LocalSmoothing` (default 0.0) for a tracker running on this machine, and `RemoteSmoothing` (default 0.15) for a tracker on a remote network device. The value is picked per connection from the packet source address and is re-evaluated while the game runs, so switching between a local OpenTrack instance and a phone on WiFi takes effect without a restart.
- Removed the `[Position] Smoothing` key. Both new parameters cover rotation and position, so there is no separate position smoothing setting.
- Removed the hidden 0.15 baseline smoothing floor that silently overrode the configured value. Local users now get zero-latency tracking by default.

### Added
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- Decoupled head tracking via OpenTrack (UDP 4242)
- 6DOF positional tracking with configurable limits
- Aim decoupling: head moves camera, mouse controls aim independently
- Game state detection: tracking pauses in menus and loading screens
- Configurable hotkeys: toggle (End), position toggle (PgUp), yaw mode (PgDn)
- INI configuration file with position limits, smoothing, and hotkey settings
- Automated installer with vendored REFramework
- Frame-rate independent smoothing and interpolation pipeline

### Removed
- The key that toggled the reticle (`[Hotkeys] ReticleToggleKey`, `Insert`, and `Ctrl+Shift+U`), and the reticle setting (`[Reticle] Enabled`). The flag they set was never read, so neither changed anything on screen (d9ed7ce).
- The sensitivity, scale, deadzone, response curve and axis inversion settings (`[Sensitivity] YawMultiplier`, `PitchMultiplier` and `RollMultiplier`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`). Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before: every copy of the config the mod shipped (the installer ZIP, the launcher seed and the file it wrote at first launch) held the multipliers at 1.0, the position sensitivities at 2.0 and the inversions off. The 2.0 is the scale of the lean at the camera, which the mod still applies.
- The installer and the Nexus ZIP no longer carry a config file, and the launcher manifest no longer seeds one: the mod creates `CameraUnlock.ini` when it first starts.
