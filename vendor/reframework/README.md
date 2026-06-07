# reframework (vendored)

This directory contains a bundled copy of the upstream mod loader. It is the install-time
source of truth: install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Asset: `REFramework.zip`
- Tag: `nightly-01383-1f45c83a45a228e8e82d8bd151008a91966a8700`
- Commit: `0436e043af6f81a5d3fef49ae27d35e63431e566`
- Upstream URL: https://github.com/praydog/REFramework-nightly/releases/download/nightly-01383-1f45c83a45a228e8e82d8bd151008a91966a8700/REFramework.zip
- SHA-256: `e39a6405e7a1cf5c141bb87f068b607868a99daca2f1a835dcf7cd9533270a10`
- Fetched at: 2026-06-07T17:10:19.3524852+01:00
- Source: github

Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.
