#include "rtx_fork_hooks.h"

#include "rtx_fork_game_state.h"

#include <cctype>
#include <string>

namespace dxvk::fork_hooks {

  // Keep this game-state-only hook separate from the API-entry implementation.
  // Unit tests link selected RTX objects from dxvk_lib, and pulling in the
  // API-entry object also pulls private D3D9DeviceEx references.
  bool isSkylessDimension() {
    std::string value;
    if (!fork_game_state::GameStateStore::get().tryGet("__atmosphere.skyless", value)) {
      return false;
    }

    for (char& character : value) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value == "1" || value == "true" || value == "yes" || value == "on";
  }

} // namespace dxvk::fork_hooks
