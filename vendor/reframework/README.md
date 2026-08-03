# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `REFramework.zip`
- Tag: `nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01394-ec6c81fd39831b328027ae00e102bc9c9c3f8aa5/REFramework.zip
- SHA-256: `a3d24f04e41933a7a3a6e1d6402b7de18ca677245d9ca0dda9f6a5ca20e9b94e`
- Fetched at: 2026-08-03T11:49:32.4281259+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
