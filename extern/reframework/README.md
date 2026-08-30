# reframework SDK headers (vendored)

The REFramework plugin API, copied verbatim from upstream and compiled into
`RE4HeadTracking.dll`. These are headers only: `API.hpp` carries inline
implementations, so upstream code ends up inside our binary and its licence
notice has to travel with every ZIP we publish. It does, in
`THIRD-PARTY-NOTICES.md`, and the upstream licence sits beside these files as
`LICENSE`.

Distinct from `vendor/reframework/`, which holds the loader binary the
installer extracts into the game folder.

## Snapshot

- Upstream: https://github.com/praydog/REFramework, path `include/reframework/`
- Plugin API version: 1.15.0 (`REFRAMEWORK_PLUGIN_VERSION_*` in `API.h`)

| File | Upstream commit last touching it | SHA-256 |
|------|----------------------------------|---------|
| `API.h` | `c633636a66c4f7a91841d5b59bb67578614a1e4e` (2025-04-21) | `6417feddba2728e06bb04df94b20281713fd5aa328afa2bcb718a8b6dd357281` |
| `API.hpp` | `76520cacc29dabed331082e3009e5698ebfebedd` (2025-10-12) | `6f8a85a2a440c0d3c29d0829a52f85608f07478e8742804c7476ddc266bd6fe8` |

Both files were diffed against upstream `master` on 2026-08-23 and are
byte-identical to it. `LICENSE` is byte-identical to
https://raw.githubusercontent.com/praydog/REFramework/master/LICENSE.

We have made no modifications. Refresh by re-copying from upstream, then
re-run the diff and update the table above.
