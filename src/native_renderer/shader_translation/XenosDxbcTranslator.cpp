#include "XenosDxbcTranslator.h"

#include <bit>

#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/xenos.h>
#include <rex/string/buffer.h>
#include <rex/ui/graphics_provider.h>

namespace bo2::native {

const char* XenosDxbcTranslatorVersion() { return "xenia_dxbc_v1"; }

bool TranslateXenosPayloadToDxbc(const XenosDxbcTranslationInput& input,
                                 XenosDxbcTranslationResult& result,
                                 std::string& error) {
  result = {};

  if (input.payload_dwords == nullptr || input.payload_dword_count == 0) {
    error = "no shader payload dwords to translate";
    return false;
  }
  if (input.runtime_stage > 1) {
    error = "unknown runtime shader stage " +
            std::to_string(input.runtime_stage);
    return false;
  }

  const rex::graphics::xenos::ShaderType shader_type =
      input.runtime_stage == 0 ? rex::graphics::xenos::ShaderType::kVertex
                               : rex::graphics::xenos::ShaderType::kPixel;

  // DxbcShader (not the base Shader) so the translator records the bindful
  // texture SRV and sampler binding lists needed by the D3D12 backend.
  rex::graphics::DxbcShader shader(shader_type, input.runtime_hash,
                                   input.payload_dwords,
                                   input.payload_dword_count,
                                   std::endian::native);
  rex::string::StringBuffer disasm_buffer;
  shader.AnalyzeUcode(disasm_buffer);
  if (!shader.is_ucode_analyzed()) {
    error = "ReXGlue analyzer could not analyze the shader payload";
    return false;
  }

  rex::graphics::DxbcShaderTranslator translator(
      rex::ui::GraphicsProvider::GpuVendorID(0),
      /*bindless_resources_used=*/false,
      /*edram_rov_used=*/false);

  uint64_t modification = input.modification;
  if (!input.has_modification) {
    modification =
        input.runtime_stage == 0
            ? translator.GetDefaultVertexShaderModification(
                  input.dynamic_addressable_register_count,
                  input.rectangle_list_as_triangle_strip
                      ? rex::graphics::Shader::HostVertexShaderType::
                            kRectangleListAsTriangleStrip
                      : rex::graphics::Shader::HostVertexShaderType::kVertex)
            : translator.GetDefaultPixelShaderModification(
                  input.dynamic_addressable_register_count);
  }

  rex::graphics::Shader::Translation* translation =
      shader.GetOrCreateTranslation(modification);
  if (translation == nullptr) {
    error = "could not create a shader translation instance";
    return false;
  }

  if (!translator.TranslateAnalyzedShader(*translation) ||
      !translation->is_valid() || translation->translated_binary().empty()) {
    error = "DXBC translation failed for " +
            std::string(input.runtime_stage == 0 ? "VS" : "PS");
    if (!translation->errors().empty()) {
      error += ":";
      for (const auto& translation_error : translation->errors()) {
        error += " ";
        error += translation_error.message;
      }
    }
    return false;
  }

  result.dxbc = translation->translated_binary();
  result.modification = modification;
  result.uses_vertex_fetch = !shader.vertex_bindings().empty();
  result.uses_texture_fetch = !shader.texture_bindings().empty();
  result.uses_memexport = shader.memexport_eM_written() != 0;
  result.register_static_address_bound = shader.register_static_address_bound();
  const auto& constant_map = shader.constant_register_map();
  for (int i = 0; i < 4; ++i) {
    result.float_bitmap[i] = constant_map.float_bitmap[i];
  }
  result.float_count = constant_map.float_count;
  for (const auto& binding : shader.GetTextureBindingsAfterTranslation()) {
    XenosDxbcTextureBindingInfo info;
    info.fetch_constant = binding.fetch_constant;
    info.dimension = static_cast<uint32_t>(binding.dimension);
    info.is_signed = binding.is_signed;
    result.texture_bindings.push_back(info);
  }
  for (const auto& binding : shader.GetSamplerBindingsAfterTranslation()) {
    XenosDxbcSamplerBindingInfo info;
    info.fetch_constant = binding.fetch_constant;
    info.mag_filter = static_cast<uint32_t>(binding.mag_filter);
    info.min_filter = static_cast<uint32_t>(binding.min_filter);
    info.mip_filter = static_cast<uint32_t>(binding.mip_filter);
    info.aniso_filter = static_cast<uint32_t>(binding.aniso_filter);
    result.sampler_bindings.push_back(info);
  }
  result.ucode_disassembly = shader.ucode_disassembly();
  return true;
}

}  // namespace bo2::native
