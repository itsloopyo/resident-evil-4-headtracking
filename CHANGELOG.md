# Changelog

All notable changes to this project are documented here.

This project has not had a tagged release yet. Dev builds are published from
the latest commit on the `dev` pre-release; the first versioned release will
be cut from the Unreleased section below.

## [Unreleased]

### Logging

- The log now names the config file it actually read (`Config loaded from <path>`), so an edit made to the wrong `HeadTracking.ini` is visible in the log instead of costing a support round trip.
- A one-shot `First tracker pose received: yaw/pitch/roll (local|remote connection)` line the first time a tracker packet reaches the mod. It is emitted ahead of every enable/gameplay gate, so its absence means the packets never arrived rather than that tracking was off or the camera hook had not engaged.
- Corrected the log path in the docs. It is `<game>/re2_framework_log.txt`, not `reframework/reframework_log.txt`; REFramework uses that generic name for every RE Engine title.

### Changed
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
- Decoupled head tracking via OpenTrack (UDP 4242)
- 6DOF positional tracking with configurable sensitivity and limits
- Aim decoupling: head moves camera, mouse controls aim independently
- ImGui reticle overlay via REFramework
- Game state detection: tracking pauses in menus and loading screens
- Configurable hotkeys: toggle (End), position toggle (PgUp), reticle toggle (Insert)
- INI configuration file with sensitivity, position limits, smoothing, and hotkey settings
- Automated installer with REFramework auto-download
- Frame-rate independent smoothing and interpolation pipeline
