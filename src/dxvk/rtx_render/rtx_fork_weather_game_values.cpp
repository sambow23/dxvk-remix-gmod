// rtx_fork_weather_game_values.cpp — publishes WeatherBlender state as
// __weather.* GameValues for the GameValueRead* graph components.
//
// Not in rtx_weather.cpp: that file is cherry-picked upstream, and
// GameStateStore / SetGameValue are Remix Plus additions with no upstream
// equivalent. Not in rtx_fork_api_entry.cpp: that TU references D3D9DeviceEx,
// so calling into it from rtx_context.cpp breaks the unit-test link.

#include "rtx_fork_hooks.h"

#include "rtx_fork_game_state.h"
#include "rtx_weather.h"

#include <cstdio>

namespace dxvk {
namespace fork_hooks {

  void publishWeatherGameValues(const WeatherBlender& blender) {
    // Dormant publishes nothing, so the last transition survives a lull —
    // plugins polling across one depend on that.
    if (blender.getBlendedSnapshot() == nullptr) {
      return;
    }

    auto& store = fork_game_state::GameStateStore::get();
    store.set("__weather.current", blender.getCurrentPreset());
    store.set("__weather.previous", blender.getPreviousPreset());

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", blender.getBlendProgress());
    store.set("__weather.blend_progress", buf);
  }

} // namespace fork_hooks
} // namespace dxvk
