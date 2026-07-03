#include "XenosDisassembly.h"

#include <charconv>
#include <sstream>

namespace bo2::native {
namespace {

std::string_view TrimView(std::string_view value) {
  while (!value.empty() &&
         (value.front() == ' ' || value.front() == '\t' ||
          value.front() == '\r' || value.front() == '\n')) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' ||
          value.back() == '\r' || value.back() == '\n')) {
    value.remove_suffix(1);
  }
  return value;
}

std::string ToString(std::string_view view) {
  return std::string(view.begin(), view.end());
}

std::string OperandRegisterText(std::string_view operand) {
  const std::size_t dot = operand.find('.');
  std::string_view base =
      dot == std::string_view::npos ? operand : operand.substr(0, dot);
  return ToString(TrimView(base));
}

std::string OperandMaskText(std::string_view operand) {
  const std::size_t dot = operand.find('.');
  if (dot == std::string_view::npos) {
    return {};
  }
  return ToString(TrimView(operand.substr(dot + 1)));
}

std::string OperandRegisterFile(std::string_view reg) {
  if (reg.empty()) {
    return {};
  }
  if (reg[0] == 'r') {
    return "gpr";
  }
  if (reg[0] == 'c') {
    return "float_constant";
  }
  if (reg[0] == 'b') {
    return "bool_constant";
  }
  if (reg.rfind("tf", 0) == 0) {
    return "texture_fetch";
  }
  if (reg.rfind("vf", 0) == 0) {
    return "vertex_fetch";
  }
  if (reg.rfind("oC", 0) == 0 || reg == "oDepth") {
    return "color_export";
  }
  if (reg == "oPos") {
    return "position_export";
  }
  if (reg.size() > 1 && reg[0] == 'o') {
    return "interpolator_export";
  }
  if (reg.rfind("eA", 0) == 0 || reg.rfind("eM", 0) == 0) {
    return "export_register";
  }
  return "unknown";
}

std::optional<int> OperandRegisterIndex(std::string_view reg) {
  std::size_t begin = 0;
  while (begin < reg.size() &&
         (reg[begin] < '0' || reg[begin] > '9')) {
    ++begin;
  }
  if (begin == reg.size()) {
    return std::nullopt;
  }
  std::size_t end = begin;
  while (end < reg.size() && reg[end] >= '0' && reg[end] <= '9') {
    ++end;
  }
  int value = 0;
  const std::string digits = ToString(reg.substr(begin, end - begin));
  const auto result =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}

std::vector<std::string> SplitOperands(std::string_view operands) {
  std::vector<std::string> result;
  std::string current;
  int bracket_depth = 0;
  for (const char ch : operands) {
    if (ch == '[' || ch == '(') {
      ++bracket_depth;
    } else if ((ch == ']' || ch == ')') && bracket_depth > 0) {
      --bracket_depth;
    }
    if (ch == ',' && bracket_depth == 0) {
      if (!TrimView(current).empty()) {
        result.push_back(ToString(TrimView(current)));
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  if (!TrimView(current).empty()) {
    result.push_back(ToString(TrimView(current)));
  }
  return result;
}

std::string OperationCategory(std::string_view opcode, bool has_export_dest) {
  if (opcode == "exec" || opcode == "exece" || opcode == "cnop" ||
      opcode == "alloc") {
    return "control_flow";
  }
  if (opcode.rfind("tfetch", 0) == 0) {
    return "texture_fetch";
  }
  if (opcode.rfind("vfetch", 0) == 0) {
    return "vertex_fetch";
  }
  if (has_export_dest) {
    return "export";
  }
  return "alu";
}

std::optional<int> FetchConstantIndex(
    std::string_view opcode, const std::vector<std::string>& parts) {
  if (opcode.rfind("tfetch", 0) != 0 && opcode.rfind("vfetch", 0) != 0) {
    return std::nullopt;
  }
  for (const std::string& part : parts) {
    std::string_view trimmed = TrimView(part);
    if (trimmed.rfind("tf", 0) == 0 || trimmed.rfind("vf", 0) == 0) {
      return OperandRegisterIndex(trimmed);
    }
  }
  return std::nullopt;
}

}  // namespace

ParsedShaderOperand ParseShaderOperand(std::string_view operand) {
  ParsedShaderOperand result;
  operand = TrimView(operand);
  result.text = ToString(operand);

  if (!operand.empty() && operand.front() == '-') {
    result.negate = true;
    operand.remove_prefix(1);
    operand = TrimView(operand);
  }

  if (operand.rfind("r_abs[", 0) == 0) {
    result.absolute = true;
    const std::size_t close = operand.find(']');
    if (close != std::string_view::npos) {
      const std::string inner = ToString(operand.substr(6, close - 6));
      result.register_text = "r" + inner;
      const std::size_t dot = operand.find('.', close);
      if (dot != std::string_view::npos) {
        result.mask = ToString(TrimView(operand.substr(dot + 1)));
      }
      result.register_file = OperandRegisterFile(result.register_text);
      result.register_index = OperandRegisterIndex(result.register_text);
      return result;
    }
  }

  result.relative = operand.find('[') != std::string_view::npos;
  result.register_text = OperandRegisterText(operand);
  result.register_file = OperandRegisterFile(result.register_text);
  result.register_index = OperandRegisterIndex(result.register_text);
  result.mask = OperandMaskText(operand);
  return result;
}

std::vector<ParsedShaderOperation>
ParseDisassemblyOperations(const std::string& disassembly) {
  std::vector<ParsedShaderOperation> operations;
  std::istringstream lines(disassembly);
  std::string line;
  while (std::getline(lines, line)) {
    std::string_view text = TrimView(line);
    if (text.empty()) {
      continue;
    }

    std::string_view address_text;
    if (text.rfind("/*", 0) == 0) {
      const std::size_t end = text.find("*/");
      if (end != std::string_view::npos) {
        address_text = TrimView(text.substr(2, end - 2));
        text = TrimView(text.substr(end + 2));
      }
    }
    if (text.empty()) {
      continue;
    }

    ParsedShaderOperation operation;
    operation.address = ToString(address_text);
    operation.text = ToString(text);

    std::string_view opcode = text;
    std::string_view operands;
    const std::size_t opcode_end = text.find_first_of(" \t");
    if (opcode_end != std::string_view::npos) {
      opcode = text.substr(0, opcode_end);
      operands = TrimView(text.substr(opcode_end + 1));
    }
    if (opcode == "+") {
      operation.coissued = true;
      const std::size_t coissue_opcode_end = operands.find_first_of(" \t");
      if (coissue_opcode_end == std::string_view::npos) {
        opcode = operands;
        operands = {};
      } else {
        const std::string_view coissue_text = operands;
        opcode = coissue_text.substr(0, coissue_opcode_end);
        operands = TrimView(coissue_text.substr(coissue_opcode_end + 1));
      }
    }

    operation.opcode = ToString(opcode);
    operation.operands = ToString(operands);
    operation.operand_parts = SplitOperands(operands);
    operation.parsed_operands.reserve(operation.operand_parts.size());
    for (const std::string& part : operation.operand_parts) {
      operation.parsed_operands.push_back(ParseShaderOperand(part));
    }
    operation.has_destination = !operation.operand_parts.empty() &&
                                opcode != "exec" && opcode != "exece" &&
                                opcode != "cnop" && opcode != "alloc";
    const std::string destination_register =
        operation.has_destination
            ? OperandRegisterText(operation.operand_parts.front())
            : "";
    const std::string destination_file =
        operation.has_destination ? OperandRegisterFile(destination_register)
                                  : "";
    const bool has_export_dest =
        destination_file == "color_export" ||
        destination_file == "position_export" ||
        destination_file == "interpolator_export";
    operation.category = OperationCategory(opcode, has_export_dest);
    operation.fetch_constant =
        FetchConstantIndex(opcode, operation.operand_parts);
    operations.push_back(std::move(operation));
  }
  return operations;
}

bool HasOperation(const std::vector<ParsedShaderOperation>& operations,
                  std::string_view opcode, std::string_view dest,
                  std::optional<int> fetch_constant) {
  for (const ParsedShaderOperation& operation : operations) {
    if (operation.opcode != opcode) {
      continue;
    }
    if (!dest.empty()) {
      if (!operation.has_destination || operation.operand_parts.empty() ||
          TrimView(operation.operand_parts.front()) != dest) {
        continue;
      }
    }
    if (fetch_constant && operation.fetch_constant != fetch_constant) {
      continue;
    }
    return true;
  }
  return false;
}

std::size_t CountOperations(
    const std::vector<ParsedShaderOperation>& operations,
    std::string_view opcode) {
  std::size_t count = 0;
  for (const ParsedShaderOperation& operation : operations) {
    if (operation.opcode == opcode) {
      ++count;
    }
  }
  return count;
}

bool DisassemblyContains(const std::string& disassembly,
                         std::string_view needle) {
  return disassembly.find(needle) != std::string::npos;
}

}  // namespace bo2::native
