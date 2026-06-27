#pragma once

#include <filesystem>
#include <string_view>

#include <rex/filesystem.h>
#include <rex/rex_app.h>

namespace bo2 {

inline void ResolveProjectPaths(rex::PathConfig& paths, std::string_view xex_name) {
  if (paths.game_data_root.empty()) {
    auto directory = rex::filesystem::GetExecutableFolder();
    for (int depth = 0; depth != 8 && !directory.empty(); ++depth) {
      const auto assets = directory / "assets";
      if (std::filesystem::is_regular_file(assets / xex_name)) {
        paths.game_data_root = assets;
        break;
      }
      if (std::filesystem::is_regular_file(directory / xex_name)) {
        paths.game_data_root = directory;
        break;
      }
      const auto parent = directory.parent_path();
      if (parent == directory) {
        break;
      }
      directory = parent;
    }
  }

  if (!paths.game_data_root.empty()) {
    paths.update_data_root = paths.game_data_root;
  }
}

}  // namespace bo2
