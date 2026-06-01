#pragma once

namespace RE4HT {

// Returns true if the player is in active gameplay (not paused, menu, loading, etc.).
// Refreshes the cached game state internally (rate-limited).
bool IsInGameplay();

// Returns true once after transitioning from non-gameplay to gameplay (for auto-recenter)
bool ShouldRecenter();

} // namespace RE4HT
