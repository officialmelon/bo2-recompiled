#pragma once

#include <cstdint>
#include <filesystem>
#include <ostream>
#include <string>

namespace bo2::native {

struct XenosHlslTranslationRequest {
  uint32_t runtime_stage = 0;
  uint64_t runtime_hash = 0;
  std::filesystem::path capture_path;
  std::string stage_name;
  std::string disassembly;
};

const char* LimitedXenosHlslTranslatorVersion();

bool TryTranslateLimitedXenosHlsl(const XenosHlslTranslationRequest& request,
                                  std::ostream& out, std::string& error);

}  // namespace bo2::native
