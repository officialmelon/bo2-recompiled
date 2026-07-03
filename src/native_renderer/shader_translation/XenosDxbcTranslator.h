#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bo2::native {

// Generic Xenos -> DXBC translation for captured runtime PM4 shader payloads.
//
// This wraps the ReXGlue SDK's Xenia-derived DxbcShaderTranslator, which
// consumes raw analyzed microcode and emits complete SM 5.1 DXBC with the
// Xenia binding contract (system/float/bool-loop/fetch constant buffers in
// space0 plus shared-memory vertex pulling). Targets that compile this module
// must link the SDK shader translator sources; see the app CMakeLists.
struct XenosDxbcTranslationInput {
  // 0 = vertex, 1 = pixel (project runtime-stage convention).
  uint32_t runtime_stage = 0;
  uint64_t runtime_hash = 0;
  const uint32_t* payload_dwords = nullptr;
  std::size_t payload_dword_count = 0;
  // SQ_PROGRAM_CNTL dynamically addressable register count; 0 when unknown.
  uint32_t dynamic_addressable_register_count = 0;
  // Explicit translator modification bits; when unset the stage-default
  // modification (full interpolator mask, kNoModifiers depth mode) is used so
  // a VS/PS pair translated with defaults always has matching linkage.
  bool has_modification = false;
  uint64_t modification = 0;
  // Vertex shaders only: translate for rectangle-list draws expanded to
  // two-triangle strips in the vertex shader (host derives the fourth corner).
  bool rectangle_list_as_triangle_strip = false;
};

// Bindful texture SRV assigned by the translator, in bindful register order:
// SRV register = SRVMainRegister::kBindfulTexturesStart + list index.
struct XenosDxbcTextureBindingInfo {
  uint32_t fetch_constant = 0;
  // xenos::FetchOpDimension numeric value: 0=1D, 1=2D, 2=3D/stacked, 3=cube.
  uint32_t dimension = 0;
  bool is_signed = false;
};

// Sampler assigned by the translator, in register order starting at s0.
struct XenosDxbcSamplerBindingInfo {
  uint32_t fetch_constant = 0;
  uint32_t mag_filter = 0;
  uint32_t min_filter = 0;
  uint32_t mip_filter = 0;
  uint32_t aniso_filter = 0;
};

struct XenosDxbcTranslationResult {
  std::vector<uint8_t> dxbc;
  uint64_t modification = 0;
  bool uses_vertex_fetch = false;
  bool uses_texture_fetch = false;
  bool uses_memexport = false;
  uint32_t register_static_address_bound = 0;
  // Used float constant registers (c0-c255 in the stage's register window).
  // The translated cbuffer is tightly packed: used registers gathered in
  // ascending order, so the backend must gather with the same bitmap.
  uint64_t float_bitmap[4] = {0, 0, 0, 0};
  uint32_t float_count = 0;
  std::vector<XenosDxbcTextureBindingInfo> texture_bindings;
  std::vector<XenosDxbcSamplerBindingInfo> sampler_bindings;
  std::string ucode_disassembly;
};

bool TranslateXenosPayloadToDxbc(const XenosDxbcTranslationInput& input,
                                 XenosDxbcTranslationResult& result,
                                 std::string& error);

// Identifier written into shader cache records produced by this path.
const char* XenosDxbcTranslatorVersion();

}  // namespace bo2::native
