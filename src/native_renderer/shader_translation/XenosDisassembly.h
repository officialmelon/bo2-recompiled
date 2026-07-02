#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bo2::native {

struct ParsedShaderOperation {
  std::string address;
  std::string opcode;
  std::string operands;
  std::string text;
  bool coissued = false;
  std::string category;
  std::vector<std::string> operand_parts;
  bool has_destination = false;
  std::optional<int> fetch_constant;
};

std::vector<ParsedShaderOperation>
ParseDisassemblyOperations(const std::string& disassembly);

bool HasOperation(const std::vector<ParsedShaderOperation>& operations,
                  std::string_view opcode, std::string_view dest = {},
                  std::optional<int> fetch_constant = std::nullopt);

std::size_t CountOperations(
    const std::vector<ParsedShaderOperation>& operations,
    std::string_view opcode);

bool DisassemblyContains(const std::string& disassembly,
                         std::string_view needle);

}  // namespace bo2::native
