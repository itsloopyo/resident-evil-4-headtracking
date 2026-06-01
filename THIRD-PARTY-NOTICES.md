# Third-Party Notices

## REFramework

- **Version:** nightly-01366 (commit `0436e043af6f81a5d3fef49ae27d35e63431e566`)
- **License:** MIT
- **Upstream:** https://github.com/praydog/REFramework
- **Usage:** Plugin host and SDK for RE Engine games. Provides method hooking, type system access, ImGui overlay, and D3D rendering hooks.
- **Bundled:** yes. Shipped in the GitHub installer ZIP under `vendor/reframework`; install.cmd extracts it at install time.

---

## OpenTrack

- **Version:** N/A (UDP protocol only)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** Head tracking source. We receive tracking data via the OpenTrack UDP protocol on port 4242. No OpenTrack code is bundled.
- **Bundled:** no.

---

## CameraUnlock Core Library

- **Version:** commit `8ae3c98bd426eafe40a10303e8a394ad75c8a5c2`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared C++ library providing the UDP receiver, tracking processing pipeline, smoothing, interpolation, hotkey input, and math utilities. Compiled into the plugin DLL.
- **Bundled:** no. Linked at runtime by compiling its sources into the plugin DLL.

---
