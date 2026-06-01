# Changelog

All notable changes to this project are documented here.

This project has not had a tagged release yet. Dev builds are published from
the latest commit on the `dev` pre-release; the first versioned release will
be cut from the Unreleased section below.

## [Unreleased]

### Added
- Decoupled head tracking via OpenTrack (UDP 4242)
- 6DOF positional tracking with configurable sensitivity and limits
- Aim decoupling: head moves camera, mouse controls aim independently
- ImGui reticle overlay via REFramework
- Game state detection: tracking pauses in menus and loading screens
- Auto-recenter on first tracking connection
- Configurable hotkeys: toggle (End), recenter (Home), position toggle (PgUp), reticle toggle (Insert)
- INI configuration file with sensitivity, position limits, smoothing, and hotkey settings
- Automated installer with REFramework auto-download
- Frame-rate independent smoothing and interpolation pipeline
