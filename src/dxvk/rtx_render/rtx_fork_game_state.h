#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace dxvk {
  namespace fork_game_state {

    class GameStateStore {
    public:
      static GameStateStore& get() {
        static GameStateStore s_instance;
        return s_instance;
      }

      void set(const std::string& key, std::string value) {
        std::lock_guard<std::mutex> lock{ m_lock };
        m_values[key] = std::move(value);
      }

      bool tryGet(const std::string& key, std::string& out) const {
        std::lock_guard<std::mutex> lock{ m_lock };
        auto it = m_values.find(key);
        if (it == m_values.end()) {
          return false;
        }
        out = it->second;
        return true;
      }

    private:
      GameStateStore() = default;
      GameStateStore(const GameStateStore&) = delete;
      GameStateStore& operator=(const GameStateStore&) = delete;

      mutable std::mutex m_lock;
      std::unordered_map<std::string, std::string> m_values;
    };

  }
}
