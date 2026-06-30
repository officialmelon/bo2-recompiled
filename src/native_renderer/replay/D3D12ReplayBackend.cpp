#include "NativeRenderReplay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

namespace bo2::native::replay {

namespace {

#if defined(_WIN32)

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaxRealReplayTextureSlots = 4;

bool CompileShader(const char *source, const char *entry, const char *target,
                   const char *source_name, ComPtr<ID3DBlob> &blob,
                   std::string &error);
bool CompileShaderWithCache(const char *source, const char *entry,
                            const char *target, const char *source_name,
                            const std::filesystem::path &cache_path,
                            const std::filesystem::path &log_path,
                            ComPtr<ID3DBlob> &blob, std::string &error);

std::string HrError(const char *what, HRESULT hr) {
  return std::string(what) + " failed with HRESULT 0x" +
         FormatHex32(hr).substr(2);
}

bool CheckHr(HRESULT hr, const char *what, std::string &error) {
  if (SUCCEEDED(hr)) {
    return true;
  }
  error = HrError(what, hr);
  return false;
}

D3D12_RASTERIZER_DESC DefaultRasterizerDesc() {
  D3D12_RASTERIZER_DESC desc{};
  desc.FillMode = D3D12_FILL_MODE_SOLID;
  desc.CullMode = D3D12_CULL_MODE_NONE;
  desc.FrontCounterClockwise = FALSE;
  desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
  desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  desc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  desc.DepthClipEnable = TRUE;
  desc.MultisampleEnable = FALSE;
  desc.AntialiasedLineEnable = FALSE;
  desc.ForcedSampleCount = 0;
  desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
  return desc;
}

D3D12_BLEND_DESC DefaultBlendDesc() {
  D3D12_BLEND_DESC desc{};
  desc.AlphaToCoverageEnable = FALSE;
  desc.IndependentBlendEnable = FALSE;
  desc.RenderTarget[0].BlendEnable = FALSE;
  desc.RenderTarget[0].LogicOpEnable = FALSE;
  desc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  desc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
  desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
  desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
  desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  desc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
  desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  return desc;
}

D3D12_DEPTH_STENCIL_DESC DefaultDepthStencilDesc() {
  D3D12_DEPTH_STENCIL_DESC desc{};
  desc.DepthEnable = FALSE;
  desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  desc.StencilEnable = FALSE;
  desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
  return desc;
}

D3D12_CULL_MODE D3D12CullModeFromXenos(uint32_t cull_mode) {
  if ((cull_mode & 0x3) == 0) {
    return D3D12_CULL_MODE_NONE;
  }
  if ((cull_mode & 0x3) == 1) {
    return D3D12_CULL_MODE_FRONT;
  }
  if ((cull_mode & 0x3) == 2) {
    return D3D12_CULL_MODE_BACK;
  }
  return D3D12_CULL_MODE_NONE;
}

D3D12_COMPARISON_FUNC D3D12CompareFuncFromXenos(uint32_t func) {
  static constexpr D3D12_COMPARISON_FUNC kMap[8] = {
      D3D12_COMPARISON_FUNC_NEVER,
      D3D12_COMPARISON_FUNC_LESS,
      D3D12_COMPARISON_FUNC_EQUAL,
      D3D12_COMPARISON_FUNC_LESS_EQUAL,
      D3D12_COMPARISON_FUNC_GREATER,
      D3D12_COMPARISON_FUNC_NOT_EQUAL,
      D3D12_COMPARISON_FUNC_GREATER_EQUAL,
      D3D12_COMPARISON_FUNC_ALWAYS,
  };
  return kMap[func & 0x7];
}

D3D12_BLEND D3D12BlendFromXenos(uint32_t factor, bool alpha) {
  static constexpr D3D12_BLEND kColorMap[32] = {
      D3D12_BLEND_ZERO,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_SRC_COLOR,
      D3D12_BLEND_INV_SRC_COLOR,
      D3D12_BLEND_SRC_ALPHA,
      D3D12_BLEND_INV_SRC_ALPHA,
      D3D12_BLEND_DEST_COLOR,
      D3D12_BLEND_INV_DEST_COLOR,
      D3D12_BLEND_DEST_ALPHA,
      D3D12_BLEND_INV_DEST_ALPHA,
      D3D12_BLEND_BLEND_FACTOR,
      D3D12_BLEND_INV_BLEND_FACTOR,
      D3D12_BLEND_BLEND_FACTOR,
      D3D12_BLEND_INV_BLEND_FACTOR,
      D3D12_BLEND_SRC_ALPHA_SAT,
  };
  static constexpr D3D12_BLEND kAlphaMap[32] = {
      D3D12_BLEND_ZERO,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_SRC_ALPHA,
      D3D12_BLEND_INV_SRC_ALPHA,
      D3D12_BLEND_SRC_ALPHA,
      D3D12_BLEND_INV_SRC_ALPHA,
      D3D12_BLEND_DEST_ALPHA,
      D3D12_BLEND_INV_DEST_ALPHA,
      D3D12_BLEND_DEST_ALPHA,
      D3D12_BLEND_INV_DEST_ALPHA,
      D3D12_BLEND_BLEND_FACTOR,
      D3D12_BLEND_INV_BLEND_FACTOR,
      D3D12_BLEND_BLEND_FACTOR,
      D3D12_BLEND_INV_BLEND_FACTOR,
      D3D12_BLEND_SRC_ALPHA_SAT,
  };
  return alpha ? kAlphaMap[factor & 0x1F] : kColorMap[factor & 0x1F];
}

D3D12_BLEND_OP D3D12BlendOpFromXenos(uint32_t op) {
  static constexpr D3D12_BLEND_OP kMap[8] = {
      D3D12_BLEND_OP_ADD,
      D3D12_BLEND_OP_SUBTRACT,
      D3D12_BLEND_OP_MIN,
      D3D12_BLEND_OP_MAX,
      D3D12_BLEND_OP_REV_SUBTRACT,
      D3D12_BLEND_OP_ADD,
      D3D12_BLEND_OP_ADD,
      D3D12_BLEND_OP_ADD,
  };
  return kMap[op & 0x7];
}

D3D12_RASTERIZER_DESC RasterizerDescFromRenderState(
    const RenderStateRecord *state) {
  D3D12_RASTERIZER_DESC desc = DefaultRasterizerDesc();
  if (!state || !state->present) {
    return desc;
  }
  desc.CullMode = D3D12CullModeFromXenos(state->cull_mode);
  desc.FrontCounterClockwise = state->front_face ? FALSE : TRUE;
  desc.FillMode =
      state->fill_mode ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  desc.DepthClipEnable = (state->pa_cl_clip_cntl & 0x00010000u) ? FALSE : TRUE;
  return desc;
}

D3D12_BLEND_DESC BlendDescFromRenderState(const RenderStateRecord *state) {
  D3D12_BLEND_DESC desc = DefaultBlendDesc();
  if (!state || !state->present) {
    return desc;
  }

  const uint32_t write_mask = state->rb_color_mask & 0xF;
  const uint32_t blend_control =
      state->rb_blendcontrol.empty() ? 0 : state->rb_blendcontrol[0];
  const uint32_t src_blend = (blend_control >> 0) & 0x1F;
  const uint32_t blend_op = (blend_control >> 5) & 0x7;
  const uint32_t dest_blend = (blend_control >> 8) & 0x1F;
  const uint32_t alpha_src_blend = (blend_control >> 16) & 0x1F;
  const uint32_t alpha_blend_op = (blend_control >> 21) & 0x7;
  const uint32_t alpha_dest_blend = (blend_control >> 24) & 0x1F;
  const bool blend_enable =
      !(src_blend == 1 && dest_blend == 0 && blend_op == 0 &&
        alpha_src_blend == 1 && alpha_dest_blend == 0 &&
        alpha_blend_op == 0);

  D3D12_RENDER_TARGET_BLEND_DESC &rt = desc.RenderTarget[0];
  rt.BlendEnable = blend_enable ? TRUE : FALSE;
  rt.SrcBlend = D3D12BlendFromXenos(src_blend, false);
  rt.DestBlend = D3D12BlendFromXenos(dest_blend, false);
  rt.BlendOp = D3D12BlendOpFromXenos(blend_op);
  rt.SrcBlendAlpha = D3D12BlendFromXenos(alpha_src_blend, true);
  rt.DestBlendAlpha = D3D12BlendFromXenos(alpha_dest_blend, true);
  rt.BlendOpAlpha = D3D12BlendOpFromXenos(alpha_blend_op);
  rt.RenderTargetWriteMask =
      static_cast<UINT8>(write_mask ? write_mask : D3D12_COLOR_WRITE_ENABLE_ALL);
  return desc;
}

D3D12_DEPTH_STENCIL_DESC DepthStencilDescFromRenderState(
    const RenderStateRecord *state) {
  D3D12_DEPTH_STENCIL_DESC desc = DefaultDepthStencilDesc();
  if (!state || !state->present) {
    return desc;
  }
  if (!state->depth_test_enable && !state->depth_write_enable &&
      !state->stencil_enable) {
    return desc;
  }

  // The replay target does not have a captured depth resource yet. Keep the PSO
  // valid while preserving the disabled-depth case exactly for current draws.
  desc.DepthEnable = FALSE;
  desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = D3D12CompareFuncFromXenos(state->depth_func);
  desc.StencilEnable = FALSE;
  return desc;
}

D3D12_RECT ScissorRectFromRenderState(const RenderStateRecord *state,
                                      uint32_t width, uint32_t height) {
  D3D12_RECT rect{};
  rect.right = static_cast<LONG>(width);
  rect.bottom = static_cast<LONG>(height);
  if (!state || !state->present) {
    return rect;
  }

  const uint32_t tl = state->pa_sc_screen_scissor_tl;
  const uint32_t br = state->pa_sc_screen_scissor_br;
  if (tl == 0 && (br == 0 || br == 0x20002000u)) {
    return rect;
  }

  const int32_t left = static_cast<int32_t>(tl & 0x7FFF);
  const int32_t top = static_cast<int32_t>((tl >> 16) & 0x7FFF);
  const int32_t right = static_cast<int32_t>(br & 0x7FFF);
  const int32_t bottom = static_cast<int32_t>((br >> 16) & 0x7FFF);
  if (right <= left || bottom <= top) {
    return rect;
  }

  rect.left = std::clamp<LONG>(left, 0, static_cast<LONG>(width));
  rect.top = std::clamp<LONG>(top, 0, static_cast<LONG>(height));
  rect.right = std::clamp<LONG>(right, rect.left, static_cast<LONG>(width));
  rect.bottom = std::clamp<LONG>(bottom, rect.top, static_cast<LONG>(height));
  return rect;
}

struct ReplaySurfaceSize {
  uint32_t width = 1280;
  uint32_t height = 720;
};

ReplaySurfaceSize ChooseSurfaceSize(const ReplayCapture &capture) {
  for (const CaptureEvent &event : capture.events) {
    if (event.type == CaptureEventType::PresentSnapshot &&
        event.present.width && event.present.height) {
      return {event.present.width, event.present.height};
    }
    if (event.type == CaptureEventType::PM4Swap && event.swap.width &&
        event.swap.height) {
      return {event.swap.width, event.swap.height};
    }
  }
  return {};
}

std::array<float, 4> ColorForDraw(const ReplayDrawState &draw_state) {
  uint64_t value =
      draw_state.vertex_shader.hash ^
      ((draw_state.pixel_shader.hash << 17) |
       (draw_state.pixel_shader.hash >> 47)) ^
      (static_cast<uint64_t>(draw_state.draw.primitive_type) << 33) ^
      (static_cast<uint64_t>(draw_state.draw.source_select) << 41);
  const float r =
      0.20f + static_cast<float>((value >> 0) & 0xFF) / 255.0f * 0.80f;
  const float g =
      0.20f + static_cast<float>((value >> 16) & 0xFF) / 255.0f * 0.80f;
  const float b =
      0.20f + static_cast<float>((value >> 32) & 0xFF) / 255.0f * 0.80f;
  return {r, g, b, 1.0f};
}

uint16_t LoadLittleEndian16(const std::vector<uint8_t> &bytes,
                            std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    return 0;
  }
  return static_cast<uint16_t>(bytes[offset]) |
         (static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t LoadLittleEndian32(const std::vector<uint8_t> &bytes,
                            std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint32_t GpuSwap32(uint32_t value, uint32_t endian) {
  switch (endian) {
  case 1:
    return ((value << 8) & 0xFF00FF00) |
           ((value >> 8) & 0x00FF00FF);
  case 2:
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
  case 3:
    return ((value >> 16) & 0x0000FFFF) | (value << 16);
  default:
    return value;
  }
}

float FloatFromBits(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float DecodePacked8(uint32_t raw, const VertexAttributeRecord &attribute) {
  if (attribute.is_signed) {
    const int32_t value =
        (raw & 0x80) ? static_cast<int32_t>(raw | 0xFFFFFF00u)
                     : static_cast<int32_t>(raw);
    return attribute.is_integer ? static_cast<float>(value)
                                : static_cast<float>(value) / 127.0f;
  }
  return attribute.is_integer ? static_cast<float>(raw)
                              : static_cast<float>(raw) / 255.0f;
}

bool DecodeFloatAttribute(const VertexFetchRecord &fetch,
                          const VertexAttributeRecord &attribute,
                          uint32_t vertex_index,
                          std::vector<float> &components) {
  components.clear();
  uint32_t component_count = 0;
  uint32_t byte_size = 0;
  switch (attribute.data_format) {
  case 6:
    component_count = 4;
    byte_size = 4;
    break;
  case 36:
    component_count = 1;
    byte_size = 4;
    break;
  case 37:
    component_count = 2;
    byte_size = 8;
    break;
  case 57:
    component_count = 3;
    byte_size = 12;
    break;
  case 38:
    component_count = 4;
    byte_size = 16;
    break;
  default:
    return false;
  }

  const std::size_t base =
      static_cast<std::size_t>(vertex_index) * fetch.stride_bytes +
      attribute.offset_bytes;
  if (base + byte_size > fetch.payload_bytes.size()) {
    return false;
  }

  if (attribute.data_format == 6) {
    const uint32_t word =
        GpuSwap32(LoadLittleEndian32(fetch.payload_bytes, base), fetch.endian);
    for (uint32_t component = 0; component < 4; ++component) {
      components.push_back(
          DecodePacked8((word >> (component * 8)) & 0xFF, attribute));
    }
    return true;
  }

  for (uint32_t component = 0; component < component_count; ++component) {
    const uint32_t word = GpuSwap32(
        LoadLittleEndian32(fetch.payload_bytes, base + component * 4),
        fetch.endian);
    components.push_back(FloatFromBits(word));
  }
  return true;
}

std::vector<uint32_t> DecodeReplayIndices(const PM4DrawRecord &draw) {
  std::vector<uint32_t> indices;
  if (draw.index_payload_missing || draw.index_bytes.empty() ||
      draw.index_count == 0) {
    return indices;
  }

  const bool index32 = draw.index_format != 0;
  const std::size_t index_size = index32 ? 4 : 2;
  const std::size_t available = draw.index_bytes.size() / index_size;
  const std::size_t count =
      std::min<std::size_t>(draw.index_count, available);
  indices.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t offset = i * index_size;
    if (index32) {
      indices.push_back(GpuSwap32(LoadLittleEndian32(draw.index_bytes, offset),
                                  draw.index_endianness));
    } else {
      uint16_t value = LoadLittleEndian16(draw.index_bytes, offset);
      if (draw.index_endianness == 1) {
        value = static_cast<uint16_t>((value >> 8) | (value << 8));
      }
      indices.push_back(value);
    }
  }
  return indices;
}

struct RealReplayVertex {
  float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
  float uv[2] = {0.0f, 0.0f};
};

struct PreparedRealDraw {
  std::size_t draw_index = 0;
  std::vector<RealReplayVertex> vertices;
  std::vector<uint16_t> indices16;
  std::vector<uint32_t> indices32;
  bool uses_32bit_indices = false;
};

struct PreparedCapturedConstants {
  std::array<uint32_t, 32> dwords{};
  uint32_t count = 0;
};

struct UploadedRealDraw {
  PreparedRealDraw prepared;
  PreparedCapturedConstants constants;
  ComPtr<ID3D12Resource> vertex_buffer;
  ComPtr<ID3D12Resource> index_buffer;
  std::array<ComPtr<ID3D12Resource>, kMaxRealReplayTextureSlots> textures;
  std::array<ComPtr<ID3D12Resource>, kMaxRealReplayTextureSlots>
      texture_uploads;
  uint64_t vertex_bytes = 0;
  uint64_t index_bytes = 0;
  uint32_t texture_srv_base_index = 0;
  uint32_t sampler_descriptor_base_index = 0;
  std::array<uint32_t, kMaxRealReplayTextureSlots> texture_widths{};
  std::array<uint32_t, kMaxRealReplayTextureSlots> texture_heights{};
  std::array<uint32_t, kMaxRealReplayTextureSlots> texture_formats{};
  std::array<bool, kMaxRealReplayTextureSlots> texture_from_capture{};
  std::array<bool, kMaxRealReplayTextureSlots> sampler_from_capture{};
  std::array<std::string, kMaxRealReplayTextureSlots> texture_notes;
};

struct NativeShaderOverridePair {
  std::filesystem::path vertex_path;
  std::filesystem::path pixel_path;
  std::string vertex_entry = "VSMain";
  std::string pixel_entry = "PSMain";
  std::string vertex_profile = "vs_5_0";
  std::string pixel_profile = "ps_5_0";
  std::string vertex_source;
  std::string pixel_source;
  std::string vertex_cache_key;
  std::string pixel_cache_key;
  std::filesystem::path vertex_cache_path;
  std::filesystem::path pixel_cache_path;
  std::filesystem::path vertex_log_path;
  std::filesystem::path pixel_log_path;
};

bool BuildCanonicalVertices(const VertexFetchRecord &fetch,
                            std::vector<RealReplayVertex> &vertices) {
  if (fetch.payload_missing || fetch.payload_truncated ||
      fetch.payload_bytes.empty() || fetch.stride_bytes == 0 ||
      fetch.attributes.empty()) {
    return false;
  }

  const uint32_t vertex_count = static_cast<uint32_t>(
      fetch.payload_bytes.size() / fetch.stride_bytes);
  if (vertex_count == 0) {
    return false;
  }

  vertices.assign(vertex_count, {});
  bool has_position = false;
  for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
    RealReplayVertex &vertex = vertices[vertex_index];
    for (const VertexAttributeRecord &attribute : fetch.attributes) {
      std::vector<float> components;
      if (!DecodeFloatAttribute(fetch, attribute, vertex_index, components)) {
        continue;
      }

      if ((attribute.data_format == 38 || attribute.data_format == 57 ||
           attribute.data_format == 36) &&
          !components.empty()) {
        has_position = true;
        vertex.position[0] = components.size() > 0 ? components[0] : 0.0f;
        vertex.position[1] = components.size() > 1 ? components[1] : 0.0f;
        vertex.position[2] = components.size() > 2 ? components[2] : 0.0f;
        vertex.position[3] = components.size() > 3 ? components[3] : 1.0f;
      } else if (attribute.data_format == 6 && components.size() >= 4) {
        vertex.color[0] = components[0];
        vertex.color[1] = components[1];
        vertex.color[2] = components[2];
        vertex.color[3] = components[3];
      } else if (attribute.data_format == 37 && components.size() >= 2) {
        vertex.uv[0] = components[0];
        vertex.uv[1] = components[1];
      }
    }
  }

  return has_position;
}

D3D12_PRIMITIVE_TOPOLOGY TopologyForPrimitive(uint32_t primitive_type) {
  switch (primitive_type) {
  case 1:
    return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
  case 2:
    return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
  case 3:
    return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
  case 4:
  case 8:
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  case 5:
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
  default:
    return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  }
}

bool PrepareRealDrawAtIndex(const ReplayCapture &capture, std::size_t index,
                            PreparedRealDraw &prepared) {
  if (index >= capture.draws.size()) {
    return false;
  }
  const ReplayDrawState &state = capture.draws[index];
  const PM4DrawRecord &draw = state.draw;
  if (!draw.indexed || draw.index_payload_missing ||
      draw.index_payload_truncated || draw.vertex_fetches.empty() ||
      TopologyForPrimitive(draw.primitive_type) !=
          D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
    return false;
  }

  std::vector<uint32_t> decoded_indices = DecodeReplayIndices(draw);
  if (decoded_indices.empty()) {
    return false;
  }

  for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
    PreparedRealDraw candidate;
    candidate.draw_index = index;
    if (!BuildCanonicalVertices(fetch, candidate.vertices)) {
      continue;
    }

    const uint32_t max_index =
        *std::max_element(decoded_indices.begin(), decoded_indices.end());
    if (max_index >= candidate.vertices.size()) {
      continue;
    }

    candidate.uses_32bit_indices =
        draw.index_format != 0 || max_index > UINT16_MAX;
    if (candidate.uses_32bit_indices) {
      candidate.indices32 = decoded_indices;
    } else {
      candidate.indices16.reserve(decoded_indices.size());
      for (uint32_t decoded_index : decoded_indices) {
        candidate.indices16.push_back(static_cast<uint16_t>(decoded_index));
      }
    }
    prepared = std::move(candidate);
    return true;
  }
  return false;
}

bool PrepareFirstRealDraw(const ReplayCapture &capture,
                          const ReplayCliOptions &options,
                          PreparedRealDraw &prepared, std::string &error) {
  const std::size_t begin =
      options.draw_index ? std::min(*options.draw_index, capture.draws.size())
                         : 0;
  const std::size_t end =
      options.draw_index ? std::min(begin + 1, capture.draws.size())
                         : capture.draws.size();

  for (std::size_t i = begin; i < end; ++i) {
    if (PrepareRealDrawAtIndex(capture, i, prepared)) {
      return true;
    }
  }

  error =
      options.draw_index
          ? "selected draw has no complete indexed vertex/index snapshot that "
            "the current D3D12 real replay path can bind"
          : "capture has no complete indexed vertex/index snapshot that the "
            "current D3D12 real replay path can bind";
  return false;
}

std::vector<PreparedRealDraw> CollectSupportedRealDraws(
    const ReplayCapture &capture, const ReplayCliOptions &options,
    const PreparedRealDraw &first_draw) {
  std::vector<PreparedRealDraw> draws;
  draws.push_back(first_draw);
  if (options.draw_index || options.d3d12_draw_limit <= 1) {
    return draws;
  }

  const ReplayDrawState &first_state = capture.draws[first_draw.draw_index];
  for (std::size_t i = first_draw.draw_index + 1; i < capture.draws.size() &&
       draws.size() < options.d3d12_draw_limit;
       ++i) {
    const ReplayDrawState &state = capture.draws[i];
    if (state.vertex_shader.hash != first_state.vertex_shader.hash ||
        state.pixel_shader.hash != first_state.pixel_shader.hash) {
      continue;
    }

    PreparedRealDraw prepared;
    if (PrepareRealDrawAtIndex(capture, i, prepared)) {
      draws.push_back(std::move(prepared));
    }
  }
  return draws;
}

std::size_t CountDrawsForShaderPair(const ReplayCapture &capture,
                                    uint64_t vertex_shader_hash,
                                    uint64_t pixel_shader_hash) {
  std::size_t count = 0;
  for (const ReplayDrawState &state : capture.draws) {
    if (state.vertex_shader.hash == vertex_shader_hash &&
        state.pixel_shader.hash == pixel_shader_hash) {
      ++count;
    }
  }
  return count;
}

bool CreateUploadBuffer(ID3D12Device *device, const void *data,
                        uint64_t byte_size, ComPtr<ID3D12Resource> &resource,
                        std::string &error) {
  if (byte_size == 0) {
    error = "CreateUploadBuffer called with zero bytes";
    return false;
  }

  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heap.CreationNodeMask = 1;
  heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = byte_size;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  if (!CheckHr(device->CreateCommittedResource(
                   &heap, D3D12_HEAP_FLAG_NONE, &desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&resource)),
               "ID3D12Device::CreateCommittedResource(upload)", error)) {
    return false;
  }

  void *mapped = nullptr;
  D3D12_RANGE read_range{0, 0};
  if (!CheckHr(resource->Map(0, &read_range, &mapped), "ID3D12Resource::Map",
               error)) {
    return false;
  }
  std::memcpy(mapped, data, static_cast<std::size_t>(byte_size));
  D3D12_RANGE written_range{0, static_cast<SIZE_T>(byte_size)};
  resource->Unmap(0, &written_range);
  return true;
}

uint32_t AlignUpU32(uint32_t value, uint32_t alignment) {
  return alignment == 0 ? value
                        : ((value + alignment - 1) / alignment) * alignment;
}

uint32_t XenosTiledOffset2D(uint32_t x, uint32_t y, uint32_t pitch,
                            uint32_t bytes_per_block_log2) {
  pitch = AlignUpU32(pitch, 32);
  const uint32_t macro =
      ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bytes_per_block_log2 + 7);
  const uint32_t micro =
      ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const uint32_t offset =
      macro + ((micro & ~0xFu) << 1) + (micro & 0xFu) + ((y & 1) << 4);
  return ((offset & ~0x1FFu) << 3) + ((y & 16) << 7) +
         ((offset & 0x1C0u) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3Fu);
}

bool DecodeTextureRgba8(const TextureFetchRecord &fetch,
                        std::vector<uint8_t> &rgba, std::string &reason) {
  rgba.clear();
  if (fetch.payload_missing || fetch.payload_truncated ||
      fetch.payload_bytes.empty()) {
    reason = "texture payload is missing or truncated";
    return false;
  }
  if (fetch.format != 6) {
    reason = "unsupported texture format " + std::to_string(fetch.format);
    return false;
  }
  if (fetch.width == 0 || fetch.height == 0) {
    reason = "texture dimensions are zero";
    return false;
  }

  const std::size_t pixel_count =
      static_cast<std::size_t>(fetch.width) * fetch.height;
  const std::size_t output_bytes = pixel_count * 4;
  const uint32_t pitch_texels =
      fetch.pitch != 0 ? fetch.pitch << 5 : fetch.width;
  if (!fetch.tiled) {
    const std::size_t linear_footprint =
        (static_cast<std::size_t>(pitch_texels) * (fetch.height - 1) +
         fetch.width) *
        4;
    if (linear_footprint > fetch.payload_bytes.size()) {
      reason =
          "linear texture payload is smaller than the decoded 32bpp footprint";
      return false;
    }
  }

  rgba.resize(output_bytes);
  for (uint32_t y = 0; y < fetch.height; ++y) {
    for (uint32_t x = 0; x < fetch.width; ++x) {
      const std::size_t pixel =
          static_cast<std::size_t>(y) * fetch.width + x;
      const std::size_t source_offset =
          fetch.tiled
              ? XenosTiledOffset2D(x, y, pitch_texels, 2)
              : (static_cast<std::size_t>(y) * pitch_texels + x) * 4;
      if (source_offset + 4 > fetch.payload_bytes.size()) {
        reason =
            "texture payload is smaller than the tiled 32bpp footprint";
        return false;
      }
      const uint32_t word =
          GpuSwap32(LoadLittleEndian32(fetch.payload_bytes, source_offset),
                    fetch.endian);
      rgba[pixel * 4 + 0] = static_cast<uint8_t>(word & 0xFF);
      rgba[pixel * 4 + 1] = static_cast<uint8_t>((word >> 8) & 0xFF);
      rgba[pixel * 4 + 2] = static_cast<uint8_t>((word >> 16) & 0xFF);
      rgba[pixel * 4 + 3] = static_cast<uint8_t>((word >> 24) & 0xFF);
    }
  }
  if (rgba.empty()) {
    reason = "decoded texture is empty";
    return false;
  }
  reason.clear();
  return true;
}

bool CreateTexture2DRgba8(ID3D12Device *device, ID3D12GraphicsCommandList *list,
                          const uint8_t *rgba, uint32_t width,
                          uint32_t height,
                          ComPtr<ID3D12Resource> &texture,
                          ComPtr<ID3D12Resource> &upload,
                          std::string &error) {
  if (!rgba || width == 0 || height == 0) {
    error = "CreateTexture2DRgba8 called with empty texture data";
    return false;
  }

  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = width;
  texture_desc.Height = height;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  if (!CheckHr(device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&texture)),
               "ID3D12Device::CreateCommittedResource(texture)", error)) {
    return false;
  }

  const uint32_t row_bytes = width * 4;
  const uint32_t row_pitch =
      (row_bytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
      ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
  const uint64_t upload_size = static_cast<uint64_t>(row_pitch) * height;

  D3D12_HEAP_PROPERTIES upload_heap{};
  upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  upload_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  upload_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  upload_heap.CreationNodeMask = 1;
  upload_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC upload_desc{};
  upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  upload_desc.Width = upload_size;
  upload_desc.Height = 1;
  upload_desc.DepthOrArraySize = 1;
  upload_desc.MipLevels = 1;
  upload_desc.SampleDesc.Count = 1;
  upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  if (!CheckHr(device->CreateCommittedResource(
                   &upload_heap, D3D12_HEAP_FLAG_NONE, &upload_desc,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                   IID_PPV_ARGS(&upload)),
               "ID3D12Device::CreateCommittedResource(texture upload)",
               error)) {
    return false;
  }

  void *mapped = nullptr;
  D3D12_RANGE read_range{0, 0};
  if (!CheckHr(upload->Map(0, &read_range, &mapped),
               "ID3D12Resource::Map(texture upload)", error)) {
    return false;
  }
  uint8_t *mapped_bytes = static_cast<uint8_t *>(mapped);
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(mapped_bytes + static_cast<std::size_t>(row) * row_pitch,
                rgba + static_cast<std::size_t>(row) * row_bytes, row_bytes);
  }
  D3D12_RANGE written_range{0, static_cast<SIZE_T>(upload_size)};
  upload->Unmap(0, &written_range);

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  src.PlacedFootprint.Footprint.Width = width;
  src.PlacedFootprint.Footprint.Height = height;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = row_pitch;

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  list->ResourceBarrier(1, &barrier);
  return true;
}

void CreateTextureSrv(ID3D12Device *device, ID3D12Resource *texture,
                      D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv_desc.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(texture, &srv_desc, descriptor);
}

bool IsLinearTextureFilter(uint32_t filter) { return filter == 1; }

D3D12_TEXTURE_ADDRESS_MODE D3D12AddressModeFromXenosClamp(uint32_t clamp) {
  switch (clamp) {
  case 0:
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  case 1:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
  case 2:
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  case 3:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
  case 4:
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  case 5:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
  case 6:
    return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  case 7:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
  default:
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
}

D3D12_FILTER D3D12FilterFromTextureFetch(const TextureFetchRecord *fetch) {
  if (!fetch) {
    return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  }
  if (fetch->aniso_filter > 0 && fetch->aniso_filter <= 5) {
    return D3D12_FILTER_ANISOTROPIC;
  }

  const D3D12_FILTER_TYPE min_filter =
      IsLinearTextureFilter(fetch->min_filter) ? D3D12_FILTER_TYPE_LINEAR
                                               : D3D12_FILTER_TYPE_POINT;
  const D3D12_FILTER_TYPE mag_filter =
      IsLinearTextureFilter(fetch->mag_filter) ? D3D12_FILTER_TYPE_LINEAR
                                               : D3D12_FILTER_TYPE_POINT;
  const D3D12_FILTER_TYPE mip_filter =
      IsLinearTextureFilter(fetch->mip_filter) ? D3D12_FILTER_TYPE_LINEAR
                                               : D3D12_FILTER_TYPE_POINT;
  return D3D12_ENCODE_BASIC_FILTER(min_filter, mag_filter, mip_filter,
                                   D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

void CreateSampler(ID3D12Device *device, const TextureFetchRecord *fetch,
                   D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
  D3D12_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12FilterFromTextureFetch(fetch);
  if (fetch && fetch->clamp_modes_present) {
    sampler.AddressU = D3D12AddressModeFromXenosClamp(fetch->clamp_x);
    sampler.AddressV = D3D12AddressModeFromXenosClamp(fetch->clamp_y);
    sampler.AddressW = D3D12AddressModeFromXenosClamp(fetch->clamp_z);
  } else {
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  }
  sampler.MipLODBias = 0.0f;
  sampler.MaxAnisotropy =
      fetch && fetch->aniso_filter > 0 && fetch->aniso_filter <= 5
          ? (1u << (fetch->aniso_filter - 1))
          : 1u;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  switch (fetch ? fetch->border_color : 0) {
  case 1:
    sampler.BorderColor[0] = 1.0f;
    sampler.BorderColor[1] = 1.0f;
    sampler.BorderColor[2] = 1.0f;
    sampler.BorderColor[3] = 1.0f;
    break;
  case 2:
    sampler.BorderColor[0] = 0.5f;
    sampler.BorderColor[1] = 0.0f;
    sampler.BorderColor[2] = 0.5f;
    sampler.BorderColor[3] = 0.0f;
    break;
  case 3:
    sampler.BorderColor[0] = 0.0f;
    sampler.BorderColor[1] = 0.5f;
    sampler.BorderColor[2] = 0.5f;
    sampler.BorderColor[3] = 0.0f;
    break;
  default:
    sampler.BorderColor[0] = 0.0f;
    sampler.BorderColor[1] = 0.0f;
    sampler.BorderColor[2] = 0.0f;
    sampler.BorderColor[3] = 0.0f;
    break;
  }
  sampler.MinLOD = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  device->CreateSampler(&sampler, descriptor);
}

bool CreateRealGeometryPipeline(ID3D12Device *device,
                                ComPtr<ID3D12RootSignature> &root_signature,
                                ComPtr<ID3D12PipelineState> &pipeline_state,
                                DXGI_FORMAT format,
                                const RenderStateRecord *render_state,
                                const char *vertex_shader_source,
                                const char *vertex_shader_name,
                                const char *vertex_shader_entry,
                                const char *vertex_shader_profile,
                                const std::filesystem::path &vertex_cache_path,
                                const std::filesystem::path &vertex_log_path,
                                const char *pixel_shader_source,
                                const char *pixel_shader_name,
                                const char *pixel_shader_entry,
                                const char *pixel_shader_profile,
                                const std::filesystem::path &pixel_cache_path,
                                const std::filesystem::path &pixel_log_path,
                                std::string &error) {
  D3D12_DESCRIPTOR_RANGE texture_srv_range{};
  texture_srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  texture_srv_range.NumDescriptors =
      static_cast<UINT>(kMaxRealReplayTextureSlots);
  texture_srv_range.BaseShaderRegister = 0;
  texture_srv_range.RegisterSpace = 0;
  texture_srv_range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_DESCRIPTOR_RANGE sampler_range{};
  sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  sampler_range.NumDescriptors = static_cast<UINT>(kMaxRealReplayTextureSlots);
  sampler_range.BaseShaderRegister = 0;
  sampler_range.RegisterSpace = 0;
  sampler_range.OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER root_parameters[4]{};
  root_parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameters[0].Constants.ShaderRegister = 0;
  root_parameters[0].Constants.RegisterSpace = 0;
  root_parameters[0].Constants.Num32BitValues = 4;
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameters[1].Constants.ShaderRegister = 1;
  root_parameters[1].Constants.RegisterSpace = 0;
  root_parameters[1].Constants.Num32BitValues = 32;
  root_parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  root_parameters[2].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[2].DescriptorTable.pDescriptorRanges = &texture_srv_range;
  root_parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  root_parameters[3].DescriptorTable.NumDescriptorRanges = 1;
  root_parameters[3].DescriptorTable.pDescriptorRanges = &sampler_range;

  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters =
      static_cast<UINT>(sizeof(root_parameters) / sizeof(root_parameters[0]));
  root_desc.pParameters = root_parameters;
  root_desc.NumStaticSamplers = 0;
  root_desc.pStaticSamplers = nullptr;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature_blob;
  ComPtr<ID3DBlob> signature_errors;
  HRESULT hr =
      D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                  &signature_blob, &signature_errors);
  if (FAILED(hr)) {
    error = HrError("D3D12SerializeRootSignature", hr);
    if (signature_errors && signature_errors->GetBufferPointer()) {
      error += ": ";
      error.append(
          static_cast<const char *>(signature_errors->GetBufferPointer()),
          signature_errors->GetBufferSize());
    }
    return false;
  }

  if (!CheckHr(device->CreateRootSignature(0,
                                           signature_blob->GetBufferPointer(),
                                           signature_blob->GetBufferSize(),
                                           IID_PPV_ARGS(&root_signature)),
               "ID3D12Device::CreateRootSignature", error)) {
    return false;
  }

  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  if (!CompileShaderWithCache(vertex_shader_source, vertex_shader_entry,
                              vertex_shader_profile, vertex_shader_name,
                              vertex_cache_path, vertex_log_path,
                              vertex_shader, error) ||
      !CompileShaderWithCache(pixel_shader_source, pixel_shader_entry,
                              pixel_shader_profile, pixel_shader_name,
                              pixel_cache_path, pixel_log_path, pixel_shader,
                              error)) {
    return false;
  }

  D3D12_INPUT_ELEMENT_DESC input_elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(RealReplayVertex, position),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
       offsetof(RealReplayVertex, color),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
       offsetof(RealReplayVertex, uv),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature.Get();
  pso_desc.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pso_desc.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  pso_desc.BlendState = BlendDescFromRenderState(render_state);
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = RasterizerDescFromRenderState(render_state);
  pso_desc.DepthStencilState = DepthStencilDescFromRenderState(render_state);
  pso_desc.InputLayout = {
      input_elements,
      static_cast<UINT>(sizeof(input_elements) / sizeof(input_elements[0]))};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = format;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleDesc.Quality = 0;

  return CheckHr(device->CreateGraphicsPipelineState(
                     &pso_desc, IID_PPV_ARGS(&pipeline_state)),
                 "ID3D12Device::CreateGraphicsPipelineState(real geometry)",
                 error);
}

const char *DiagnosticRealGeometryShaderSource() {
  return R"(
cbuffer FrameConstants : register(b0)
{
  float2 surface_size;
  float2 _pad;
};

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[8];
};

struct VSIn
{
  float4 position : POSITION;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

struct VSOut
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
  VSOut output;
  float2 ndc;
  ndc.x = input.position.x / surface_size.x * 2.0f - 1.0f;
  ndc.y = 1.0f - input.position.y / surface_size.y * 2.0f;
  output.position = float4(ndc, input.position.z, 1.0f);
  output.color = input.color;
  output.uv = input.uv;
  return output;
}

Texture2D native_texture0 : register(t0);
SamplerState native_sampler0 : register(s0);

float4 PSMain(VSOut input) : SV_Target0
{
  float2 uv = saturate(input.uv);
  float3 tint = float3(0.25f + 0.75f * uv.x, 0.25f + 0.75f * uv.y, 1.0f);
  float constant_bias = saturate(abs(captured_constants[0].x) * 8.0f);
  float4 captured_texture = native_texture0.Sample(native_sampler0, uv);
  float3 base_color = saturate(input.color.rgb * tint +
                               float3(constant_bias, constant_bias * 0.25f,
                                      0.0f));
  return float4(saturate(lerp(base_color, captured_texture.rgb, 0.35f) +
                         captured_texture.aaa * 0.05f),
                1.0f);
}
)";
}

bool ReadTextFile(const std::filesystem::path &path, std::string &text,
                  std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "could not open shader override " + path.string();
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::ifstream::pos_type size = file.tellg();
  if (size == std::ifstream::pos_type(-1)) {
    error = "could not size shader override " + path.string();
    return false;
  }
  text.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!text.empty()) {
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
      error = "could not read shader override " + path.string();
      return false;
    }
  }
  return true;
}

std::string ShaderHashFileKey(uint64_t hash) {
  const std::string hex = FormatHex64(hash);
  return hex.size() > 2 && hex[0] == '0' && hex[1] == 'x' ? hex.substr(2)
                                                          : hex;
}

uint64_t Fnv1a64(std::string_view text) {
  uint64_t value = 14695981039346656037ull;
  for (const char ch : text) {
    value ^= static_cast<uint8_t>(ch);
    value *= 1099511628211ull;
  }
  return value;
}

std::string LowerAscii(std::string text) {
  for (char &ch : text) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  return text;
}

bool StageMatches(std::string stage, const char *short_stage,
                  const char *long_stage) {
  stage = LowerAscii(std::move(stage));
  return stage == short_stage || stage == long_stage;
}

std::filesystem::path ResolveOverridePath(const std::filesystem::path &root,
                                          const std::filesystem::path &path) {
  return path.is_absolute() ? path : root / path;
}

bool FindManifestOverridePath(const std::filesystem::path &root,
                              const char *short_stage,
                              const char *long_stage, uint64_t hash,
                              std::filesystem::path &path, std::string &entry,
                              std::string &profile, std::string &error) {
  const std::filesystem::path manifest = root / "overrides.json";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(manifest, ec) || ec) {
    return false;
  }

  std::vector<ShaderOverrideRecord> records;
  if (!LoadShaderOverrideManifest(manifest, records, error)) {
    return false;
  }

  for (const ShaderOverrideRecord &record : records) {
    if (LowerAscii(record.backend) != "d3d12" ||
        record.runtime_hash != hash ||
        !StageMatches(record.stage, short_stage, long_stage)) {
      continue;
    }
    path = ResolveOverridePath(root, record.path);
    if (!record.entry.empty()) {
      entry = record.entry;
    }
    if (!record.profile.empty()) {
      profile = record.profile;
    }
    return true;
  }
  return false;
}

bool FindCacheShaderPath(const std::filesystem::path &root,
                         const char *short_stage, const char *long_stage,
                         uint64_t hash, std::filesystem::path &path,
                         std::filesystem::path &source,
                         std::string &entry, std::string &profile,
                         std::string &cache_key, std::string &error) {
  const std::array<std::filesystem::path, 2> indexes = {
      root / "shader_cache_index.json",
      root / "shader_cache_index.jsonl",
  };
  bool found_index = false;
  std::error_code ec;
  for (const std::filesystem::path &index : indexes) {
    if (!std::filesystem::is_regular_file(index, ec) || ec) {
      continue;
    }
    found_index = true;

    std::vector<ShaderCacheRecord> records;
    if (!LoadShaderCacheIndex(index, records, error)) {
      return false;
    }

    for (const ShaderCacheRecord &record : records) {
      if (record.diagnostic || LowerAscii(record.backend) != "d3d12" ||
          record.runtime_hash != hash ||
          !StageMatches(record.stage, short_stage, long_stage)) {
        continue;
      }
      const std::string format = LowerAscii(record.format);
      if (!format.empty() && format != "dxbc" && format != "dxil") {
        continue;
      }
      path = record.path;
      source = record.source;
      if (!record.entry.empty()) {
        entry = record.entry;
      }
      profile = record.profile;
      cache_key = record.cache_key;
      return true;
    }
  }
  if (!found_index) {
    error.clear();
  }
  return false;
}

std::string MakeOverrideCacheKey(const char *short_stage, uint64_t hash,
                                 const std::string &profile,
                                 const std::string &source) {
  constexpr const char *kBindingLayoutVersion = "layout4";
  return std::string("manual_") + short_stage + "_" + ShaderHashFileKey(hash) +
         "_" + profile + "_" + kBindingLayoutVersion + "_src" +
         ShaderHashFileKey(Fnv1a64(source));
}

std::filesystem::path FindShaderOverridePath(
    const std::filesystem::path &root, const char *short_stage,
    const char *long_stage, uint64_t hash) {
  const std::string key = ShaderHashFileKey(hash);
  const std::filesystem::path stage_root = root / "d3d12";
  const std::array<std::filesystem::path, 4> candidates = {
      stage_root / (std::string(short_stage) + "_" + key + ".hlsl"),
      stage_root / (std::string(long_stage) + "_" + key + ".hlsl"),
      stage_root / (key + "." + short_stage + ".hlsl"),
      stage_root / (key + ".hlsl"),
  };

  for (const std::filesystem::path &candidate : candidates) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
      return candidate;
    }
  }
  return {};
}

bool LoadNativeShaderOverridePair(const ReplayDrawState &draw_state,
                                  const ReplayCliOptions &options,
                                  NativeShaderOverridePair &pair,
                                  std::string &error) {
  auto resolve_stage =
      [&](const char *short_stage, const char *long_stage, uint64_t hash,
          std::filesystem::path &source_path, std::string &source,
          std::string &entry, std::string &profile, std::string &cache_key,
          std::filesystem::path &cache_path, std::filesystem::path &log_path,
          std::string &stage_error) -> bool {
    std::string cache_error;
    if (FindCacheShaderPath(options.shader_cache_root, short_stage, long_stage,
                            hash, cache_path, source_path, entry, profile,
                            cache_key, cache_error)) {
      if (!cache_key.empty()) {
        log_path = options.shader_cache_root / "logs" / (cache_key + ".log");
      }
      if (source_path.empty()) {
        source_path = cache_path;
      } else {
        std::error_code ec;
        if (std::filesystem::is_regular_file(source_path, ec) && !ec &&
            !ReadTextFile(source_path, source, stage_error)) {
          return false;
        }
      }
      return true;
    }
    if (!cache_error.empty()) {
      stage_error = cache_error;
      return false;
    }

    std::string manifest_error;
    std::filesystem::path override_path;
    if (!FindManifestOverridePath(options.shader_override_root, short_stage,
                                  long_stage, hash, override_path, entry,
                                  profile, manifest_error)) {
      if (!manifest_error.empty()) {
        stage_error = manifest_error;
        return false;
      }
      override_path =
          FindShaderOverridePath(options.shader_override_root, short_stage,
                                 long_stage, hash);
    }

    if (override_path.empty()) {
      stage_error = "no translated, cached, or override shader is available "
                    "for " +
                    std::string(long_stage) + " shader " + FormatHex64(hash);
      return false;
    }

    if (!ReadTextFile(override_path, source, stage_error)) {
      return false;
    }

    source_path = override_path;
    cache_key = MakeOverrideCacheKey(short_stage, hash, profile, source);
    cache_path =
        options.shader_cache_root / "d3d12" / (cache_key + ".dxbc");
    log_path = options.shader_cache_root / "logs" / (cache_key + ".log");
    return true;
  };

  std::string vertex_error;
  if (!resolve_stage("vs", "vertex", draw_state.vertex_shader.hash,
                     pair.vertex_path, pair.vertex_source, pair.vertex_entry,
                     pair.vertex_profile, pair.vertex_cache_key,
                     pair.vertex_cache_path, pair.vertex_log_path,
                     vertex_error)) {
    error = "no translated, cached, or override shader pair is available for "
            "draw " +
            std::to_string(draw_state.draw_index) + " (VS=" +
            FormatHex64(draw_state.vertex_shader.hash) + ", PS=" +
            FormatHex64(draw_state.pixel_shader.hash) + ", override_root=" +
            options.shader_override_root.string() + "): " + vertex_error +
            ". Re-run with --allow-diagnostic-shader only for the temporary "
            "resource-backed geometry diagnostic path.";
    return false;
  }

  std::string pixel_error;
  if (!resolve_stage("ps", "pixel", draw_state.pixel_shader.hash,
                     pair.pixel_path, pair.pixel_source, pair.pixel_entry,
                     pair.pixel_profile, pair.pixel_cache_key,
                     pair.pixel_cache_path, pair.pixel_log_path,
                     pixel_error)) {
    error = "no translated, cached, or override shader pair is available for "
            "draw " +
            std::to_string(draw_state.draw_index) + " (VS=" +
            FormatHex64(draw_state.vertex_shader.hash) + ", PS=" +
            FormatHex64(draw_state.pixel_shader.hash) + ", override_root=" +
            options.shader_override_root.string() + "): " + pixel_error +
            ". Re-run with --allow-diagnostic-shader only for the temporary "
            "resource-backed geometry diagnostic path.";
    return false;
  }

  return true;
}

bool ReadCachedShaderBlob(const std::filesystem::path &path,
                          ComPtr<ID3DBlob> &blob, std::string &error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.seekg(0, std::ios::end);
  const std::ifstream::pos_type size = file.tellg();
  if (size <= std::ifstream::pos_type(0)) {
    error = "cached shader blob is empty: " + path.string();
    return false;
  }
  if (!CheckHr(D3DCreateBlob(static_cast<SIZE_T>(size), &blob),
               "D3DCreateBlob(cache)", error)) {
    return false;
  }
  file.seekg(0, std::ios::beg);
  file.read(static_cast<char *>(blob->GetBufferPointer()),
            static_cast<std::streamsize>(blob->GetBufferSize()));
  if (!file) {
    error = "could not read cached shader blob " + path.string();
    return false;
  }
  return true;
}

bool WriteBlobFile(const std::filesystem::path &path, ID3DBlob *blob,
                   std::string &error) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "could not create shader cache directory " +
              path.parent_path().string() + ": " + ec.message();
      return false;
    }
  }

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open shader cache blob for write " + path.string();
    return false;
  }
  file.write(static_cast<const char *>(blob->GetBufferPointer()),
             static_cast<std::streamsize>(blob->GetBufferSize()));
  if (!file) {
    error = "could not write shader cache blob " + path.string();
    return false;
  }
  return true;
}

bool WriteTextFile(const std::filesystem::path &path, const std::string &text,
                   std::string &error) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "could not create shader log directory " +
              path.parent_path().string() + ": " + ec.message();
      return false;
    }
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open shader log for write " + path.string();
    return false;
  }
  file << text;
  if (!file) {
    error = "could not write shader log " + path.string();
    return false;
  }
  return true;
}

bool CompileShader(const char *source, const char *entry, const char *target,
                   const char *source_name, ComPtr<ID3DBlob> &blob,
                   std::string &error) {
  return CompileShaderWithCache(source, entry, target, source_name, {}, {},
                                blob, error);
}

bool CompileShaderWithCache(const char *source, const char *entry,
                            const char *target, const char *source_name,
                            const std::filesystem::path &cache_path,
                            const std::filesystem::path &log_path,
                            ComPtr<ID3DBlob> &blob, std::string &error) {
  if (!cache_path.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(cache_path, ec) && !ec &&
        ReadCachedShaderBlob(cache_path, blob, error)) {
      return true;
    }
    if (!error.empty()) {
      return false;
    }
  }

  if (!source || source[0] == '\0') {
    error = "shader cache miss and no source is available for " +
            std::string(source_name ? source_name : "<unknown>");
    return false;
  }

  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
  ComPtr<ID3DBlob> errors;
  const HRESULT hr =
      D3DCompile(source, std::strlen(source), source_name, nullptr, nullptr,
                 entry, target, flags, 0, &blob, &errors);
  if (SUCCEEDED(hr)) {
    if (!cache_path.empty() && !WriteBlobFile(cache_path, blob.Get(), error)) {
      return false;
    }
    if (!log_path.empty()) {
      std::ostringstream log;
      log << "compiler=D3DCompile\n"
          << "source=" << source_name << "\n"
          << "entry=" << entry << "\n"
          << "target=" << target << "\n"
          << "cache=" << cache_path.string() << "\n";
      if (!WriteTextFile(log_path, log.str(), error)) {
        return false;
      }
    }
    return true;
  }

  error = HrError("D3DCompile", hr);
  if (errors && errors->GetBufferPointer()) {
    error += ": ";
    error.append(static_cast<const char *>(errors->GetBufferPointer()),
                 errors->GetBufferSize());
  }
  return false;
}

bool WriteOverrideCacheIndex(const ReplayDrawState &draw_state,
                             const ReplayCliOptions &options,
                             const NativeShaderOverridePair &pair,
                             std::string &error) {
  const std::filesystem::path index_path =
      options.shader_cache_root / "shader_cache_index.json";
  std::error_code ec;
  std::filesystem::create_directories(options.shader_cache_root, ec);
  if (ec) {
    error = "could not create shader cache root " +
            options.shader_cache_root.string() + ": " + ec.message();
    return false;
  }

  std::ofstream file(index_path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not write shader cache index " + index_path.string();
    return false;
  }

  file << "{\n"
       << "  \"version\": 1,\n"
       << "  \"records\": [\n"
       << "    {\n"
       << "      \"backend\": \"d3d12\",\n"
       << "      \"stage\": \"vertex\",\n"
       << "      \"runtime_hash\": \"" << FormatHex64(draw_state.vertex_shader.hash)
       << "\",\n"
       << "      \"profile\": \"" << pair.vertex_profile << "\",\n"
       << "      \"compiler\": \"D3DCompile\",\n"
       << "      \"cache_key\": \"" << pair.vertex_cache_key << "\",\n"
       << "      \"path\": \"" << pair.vertex_cache_path.generic_string()
       << "\",\n"
       << "      \"source\": \"" << pair.vertex_path.generic_string()
       << "\"\n"
       << "    },\n"
       << "    {\n"
       << "      \"backend\": \"d3d12\",\n"
       << "      \"stage\": \"pixel\",\n"
       << "      \"runtime_hash\": \"" << FormatHex64(draw_state.pixel_shader.hash)
       << "\",\n"
       << "      \"profile\": \"" << pair.pixel_profile << "\",\n"
       << "      \"compiler\": \"D3DCompile\",\n"
       << "      \"cache_key\": \"" << pair.pixel_cache_key << "\",\n"
       << "      \"path\": \"" << pair.pixel_cache_path.generic_string()
       << "\",\n"
       << "      \"source\": \"" << pair.pixel_path.generic_string() << "\"\n"
       << "    }\n"
       << "  ]\n"
       << "}\n";
  if (!file) {
    error = "could not finish shader cache index " + index_path.string();
    return false;
  }
  return true;
}

PreparedCapturedConstants BuildCapturedConstants(
    const ReplayDrawState &draw_state) {
  PreparedCapturedConstants prepared;
  std::vector<const PM4ConstantRecord *> records;
  records.reserve(draw_state.bound_constants.size());
  for (const PM4ConstantRecord &record : draw_state.bound_constants) {
    if (!record.payload_missing && !record.payload_truncated &&
        !record.dwords.empty()) {
      records.push_back(&record);
    }
  }
  std::sort(records.begin(), records.end(),
            [](const PM4ConstantRecord *lhs, const PM4ConstantRecord *rhs) {
              if (lhs->seq != rhs->seq) {
                return lhs->seq < rhs->seq;
              }
              return lhs->index < rhs->index;
            });

  for (const PM4ConstantRecord *record : records) {
    for (const uint32_t dword : record->dwords) {
      if (prepared.count >= prepared.dwords.size()) {
        return prepared;
      }
      prepared.dwords[prepared.count++] = dword;
    }
  }
  return prepared;
}

bool CreateDebugPipeline(ID3D12Device *device,
                         ComPtr<ID3D12RootSignature> &root_signature,
                         ComPtr<ID3D12PipelineState> &pipeline_state,
                         DXGI_FORMAT format, std::string &error) {
  D3D12_ROOT_PARAMETER root_parameter{};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameter.Constants.ShaderRegister = 0;
  root_parameter.Constants.RegisterSpace = 0;
  root_parameter.Constants.Num32BitValues = 8;

  D3D12_ROOT_SIGNATURE_DESC root_desc{};
  root_desc.NumParameters = 1;
  root_desc.pParameters = &root_parameter;
  root_desc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature_blob;
  ComPtr<ID3DBlob> signature_errors;
  HRESULT hr =
      D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                  &signature_blob, &signature_errors);
  if (FAILED(hr)) {
    error = HrError("D3D12SerializeRootSignature", hr);
    if (signature_errors && signature_errors->GetBufferPointer()) {
      error += ": ";
      error.append(
          static_cast<const char *>(signature_errors->GetBufferPointer()),
          signature_errors->GetBufferSize());
    }
    return false;
  }

  if (!CheckHr(device->CreateRootSignature(0,
                                           signature_blob->GetBufferPointer(),
                                           signature_blob->GetBufferSize(),
                                           IID_PPV_ARGS(&root_signature)),
               "ID3D12Device::CreateRootSignature", error)) {
    return false;
  }

  constexpr const char *kShaderSource = R"(
cbuffer DrawConstants : register(b0)
{
  float4 rect;
  float4 color;
};

struct VSOut
{
  float4 position : SV_Position;
  float4 color : COLOR0;
};

VSOut VSMain(uint vertex_id : SV_VertexID)
{
  float2 p0 = float2(rect.x, rect.y);
  float2 p1 = float2(rect.z, rect.y);
  float2 p2 = float2(rect.x, rect.w);
  float2 p3 = float2(rect.z, rect.w);
  float2 positions[6] = { p0, p1, p2, p2, p1, p3 };
  VSOut output;
  output.position = float4(positions[vertex_id], 0.0f, 1.0f);
  output.color = color;
  return output;
}

float4 PSMain(VSOut input) : SV_Target0
{
  return input.color;
}
)";

  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  if (!CompileShader(kShaderSource, "VSMain", "vs_5_0",
                     "NativeRenderReplayDebugVS.hlsl", vertex_shader, error) ||
      !CompileShader(kShaderSource, "PSMain", "ps_5_0",
                     "NativeRenderReplayDebugPS.hlsl", pixel_shader, error)) {
    return false;
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature.Get();
  pso_desc.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pso_desc.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  pso_desc.BlendState = DefaultBlendDesc();
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = DefaultRasterizerDesc();
  pso_desc.DepthStencilState = DefaultDepthStencilDesc();
  pso_desc.InputLayout = {nullptr, 0};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = format;
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleDesc.Quality = 0;

  return CheckHr(device->CreateGraphicsPipelineState(
                     &pso_desc, IID_PPV_ARGS(&pipeline_state)),
                 "ID3D12Device::CreateGraphicsPipelineState", error);
}

bool DrawSyntheticGeometry(ID3D12Device *device,
                           ID3D12GraphicsCommandList *list,
                           D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                           const ReplayCapture &capture,
                           const ReplayCliOptions &options, uint32_t width,
                           uint32_t height, DXGI_FORMAT format,
                           std::string &error) {
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline_state;
  if (!CreateDebugPipeline(device, root_signature, pipeline_state, format,
                           error)) {
    return false;
  }

  D3D12_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  D3D12_RECT scissor{};
  scissor.right = static_cast<LONG>(width);
  scissor.bottom = static_cast<LONG>(height);

  list->SetGraphicsRootSignature(root_signature.Get());
  list->SetPipelineState(pipeline_state.Get());
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  const uint32_t tile_width = 10;
  const uint32_t tile_height = 10;
  const uint32_t columns = std::max<uint32_t>(1, width / tile_width);
  const std::size_t draw_limit =
      std::min<std::size_t>(capture.draws.size(), options.d3d12_draw_limit);

  for (std::size_t i = 0; i < draw_limit; ++i) {
    const uint32_t x = static_cast<uint32_t>(i % columns) * tile_width;
    const uint32_t y = static_cast<uint32_t>(i / columns) * tile_height;
    if (y >= height) {
      break;
    }

    const std::array<float, 4> color = ColorForDraw(capture.draws[i]);
    const float left =
        static_cast<float>(x) / static_cast<float>(width) * 2.0f - 1.0f;
    const float right =
        static_cast<float>(std::min<uint32_t>(x + tile_width - 1, width)) /
            static_cast<float>(width) * 2.0f -
        1.0f;
    const float top =
        1.0f - static_cast<float>(y) / static_cast<float>(height) * 2.0f;
    const float bottom =
        1.0f -
        static_cast<float>(std::min<uint32_t>(y + tile_height - 1, height)) /
            static_cast<float>(height) * 2.0f;
    const float constants[8] = {
        left, top, right, bottom, color[0], color[1], color[2], color[3],
    };
    list->SetGraphicsRoot32BitConstants(0, 8, constants, 0);
    list->DrawInstanced(6, 1, 0, 0);
  }

  return true;
}

std::filesystem::path OutputPathFor(const ReplayCapture &capture,
                                    const ReplayCliOptions &options) {
  if (!options.d3d12_output_path.empty()) {
    return options.d3d12_output_path;
  }
  return capture.path.parent_path() / "native-renderer-d3d12-replay.bmp";
}

void WriteU16(std::ofstream &file, uint16_t value) {
  file.put(static_cast<char>(value & 0xFF));
  file.put(static_cast<char>((value >> 8) & 0xFF));
}

void WriteU32(std::ofstream &file, uint32_t value) {
  WriteU16(file, static_cast<uint16_t>(value & 0xFFFF));
  WriteU16(file, static_cast<uint16_t>((value >> 16) & 0xFFFF));
}

bool WriteBmp(const std::filesystem::path &path, const uint8_t *mapped,
              uint32_t row_pitch, uint32_t width, uint32_t height,
              std::string &error) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not open D3D12 BMP output: " + path.string();
    return false;
  }

  const uint32_t pixel_bytes = width * height * 4;
  const uint32_t file_header_size = 14;
  const uint32_t dib_header_size = 40;
  const uint32_t pixel_offset = file_header_size + dib_header_size;
  const uint32_t file_size = pixel_offset + pixel_bytes;

  file.put('B');
  file.put('M');
  WriteU32(file, file_size);
  WriteU16(file, 0);
  WriteU16(file, 0);
  WriteU32(file, pixel_offset);

  WriteU32(file, dib_header_size);
  WriteU32(file, width);
  WriteU32(file, static_cast<uint32_t>(-static_cast<int32_t>(height)));
  WriteU16(file, 1);
  WriteU16(file, 32);
  WriteU32(file, 0);
  WriteU32(file, pixel_bytes);
  WriteU32(file, 2835);
  WriteU32(file, 2835);
  WriteU32(file, 0);
  WriteU32(file, 0);

  std::array<uint8_t, 4> bgra{};
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t *src = mapped + static_cast<size_t>(row_pitch) * y;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *rgba = src + static_cast<size_t>(x) * 4;
      bgra[0] = rgba[2];
      bgra[1] = rgba[1];
      bgra[2] = rgba[0];
      bgra[3] = 0xFF;
      file.write(reinterpret_cast<const char *>(bgra.data()), bgra.size());
    }
  }

  return true;
}

bool WaitForGpu(ID3D12CommandQueue *queue, ID3D12Fence *fence, HANDLE event,
                uint64_t &fence_value, std::string &error) {
  ++fence_value;
  if (!CheckHr(queue->Signal(fence, fence_value), "ID3D12CommandQueue::Signal",
               error)) {
    return false;
  }
  if (fence->GetCompletedValue() >= fence_value) {
    return true;
  }
  if (!CheckHr(fence->SetEventOnCompletion(fence_value, event),
               "ID3D12Fence::SetEventOnCompletion", error)) {
    return false;
  }
  WaitForSingleObject(event, INFINITE);
  return true;
}

#endif

} // namespace

bool RunD3D12DiagnosticReplayBackend(const ReplayCapture &capture,
                                     const ReplayCliOptions &options,
                                     std::string &error) {
#if defined(_WIN32)
  const ReplaySurfaceSize size = ChooseSurfaceSize(capture);
  const uint32_t width = std::clamp<uint32_t>(size.width, 64, 3840);
  const uint32_t height = std::clamp<uint32_t>(size.height, 64, 2160);
  const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

  ComPtr<ID3D12Device> device;
  if (!CheckHr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device)),
               "D3D12CreateDevice", error)) {
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  if (!CheckHr(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
               "ID3D12Device::CreateCommandQueue", error)) {
    return false;
  }

  ComPtr<ID3D12CommandAllocator> allocator;
  if (!CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator)),
               "ID3D12Device::CreateCommandAllocator", error)) {
    return false;
  }

  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&list)),
               "ID3D12Device::CreateCommandList", error)) {
    return false;
  }

  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Alignment = 0;
  texture_desc.Width = width;
  texture_desc.Height = height;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = format;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.SampleDesc.Quality = 0;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear_value{};
  clear_value.Format = format;
  clear_value.Color[0] = 0.015f;
  clear_value.Color[1] = 0.018f;
  clear_value.Color[2] = 0.022f;
  clear_value.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;

  ComPtr<ID3D12Resource> target;
  if (!CheckHr(device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                   IID_PPV_ARGS(&target)),
               "ID3D12Device::CreateCommittedResource(render target)", error)) {
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!CheckHr(
          device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
          "ID3D12Device::CreateDescriptorHeap(RTV)", error)) {
    return false;
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target.Get(), nullptr, rtv);

  list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);

  const uint32_t tile_width = 8;
  const uint32_t tile_height = 8;
  const uint32_t columns = std::max<uint32_t>(1, width / tile_width);
  const std::size_t draw_limit =
      std::min<std::size_t>(capture.draws.size(), options.d3d12_draw_limit);

  for (std::size_t i = 0; i < draw_limit; ++i) {
    const uint32_t x = static_cast<uint32_t>(i % columns) * tile_width;
    const uint32_t y = static_cast<uint32_t>(i / columns) * tile_height;
    if (y >= height) {
      break;
    }

    const std::array<float, 4> color = ColorForDraw(capture.draws[i]);
    const D3D12_RECT rect{
        static_cast<LONG>(x),
        static_cast<LONG>(y),
        static_cast<LONG>(std::min<uint32_t>(x + tile_width, width)),
        static_cast<LONG>(std::min<uint32_t>(y + tile_height, height)),
    };
    list->ClearRenderTargetView(rtv, color.data(), 1, &rect);
  }

  if (!DrawSyntheticGeometry(device.Get(), list.Get(), rtv, capture, options,
                             width, height, format, error)) {
    return false;
  }

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = target.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count,
                                &row_size, &total_size);

  D3D12_HEAP_PROPERTIES readback_heap{};
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  readback_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC buffer_desc{};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = total_size;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (!CheckHr(device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&readback)),
               "ID3D12Device::CreateCommittedResource(readback)", error)) {
    return false;
  }

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = target.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  if (!CheckHr(list->Close(), "ID3D12GraphicsCommandList::Close", error)) {
    return false;
  }

  ID3D12CommandList *lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> fence;
  if (!CheckHr(
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "ID3D12Device::CreateFence", error)) {
    return false;
  }
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event) {
    error = "CreateEventW failed";
    return false;
  }
  uint64_t fence_value = 0;
  const bool waited =
      WaitForGpu(queue.Get(), fence.Get(), fence_event, fence_value, error);
  CloseHandle(fence_event);
  if (!waited) {
    return false;
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
  if (!CheckHr(
          readback->Map(0, &read_range, reinterpret_cast<void **>(&mapped)),
          "ID3D12Resource::Map", error)) {
    return false;
  }

  const std::filesystem::path output = OutputPathFor(capture, options);
  std::error_code ec;
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path(), ec);
  }
  const bool wrote =
      !ec && WriteBmp(output, mapped + footprint.Offset,
                      footprint.Footprint.RowPitch, width, height, error);
  D3D12_RANGE empty_range{0, 0};
  readback->Unmap(0, &empty_range);

  if (ec) {
    error = "could not create D3D12 output directory: " + ec.message();
    return false;
  }
  if (!wrote) {
    return false;
  }

  return true;
#else
  (void)capture;
  (void)options;
  error = "D3D12 replay backend is only available on Windows";
  return false;
#endif
}

bool RunD3D12RealReplayBackend(const ReplayCapture &capture,
                               const ReplayCliOptions &options,
                               std::string &error) {
#if defined(_WIN32)
  PreparedRealDraw prepared;
  if (!PrepareFirstRealDraw(capture, options, prepared, error)) {
    return false;
  }

  const ReplayDrawState &draw_state = capture.draws[prepared.draw_index];
  const std::vector<PreparedRealDraw> prepared_draws =
      CollectSupportedRealDraws(capture, options, prepared);
  NativeShaderOverridePair override_pair;
  const char *vertex_shader_source = nullptr;
  const char *pixel_shader_source = nullptr;
  const char *vertex_shader_entry = "VSMain";
  const char *pixel_shader_entry = "PSMain";
  const char *vertex_shader_profile = "vs_5_0";
  const char *pixel_shader_profile = "ps_5_0";
  std::string vertex_shader_name_storage = "NativeRenderReplayDiagnostic.hlsl";
  std::string pixel_shader_name_storage = "NativeRenderReplayDiagnostic.hlsl";
  bool using_shader_override = false;
  bool write_override_cache_index = false;
  if (LoadNativeShaderOverridePair(draw_state, options, override_pair, error)) {
    vertex_shader_source = override_pair.vertex_source.c_str();
    pixel_shader_source = override_pair.pixel_source.c_str();
    vertex_shader_entry = override_pair.vertex_entry.c_str();
    pixel_shader_entry = override_pair.pixel_entry.c_str();
    vertex_shader_profile = override_pair.vertex_profile.c_str();
    pixel_shader_profile = override_pair.pixel_profile.c_str();
    vertex_shader_name_storage = override_pair.vertex_path.string();
    pixel_shader_name_storage = override_pair.pixel_path.string();
    using_shader_override = true;
    write_override_cache_index = !override_pair.vertex_source.empty() &&
                                 !override_pair.pixel_source.empty();
  } else if (options.allow_diagnostic_shader) {
    vertex_shader_source = DiagnosticRealGeometryShaderSource();
    pixel_shader_source = DiagnosticRealGeometryShaderSource();
    vertex_shader_name_storage = "NativeRenderReplayDiagnosticVS.hlsl";
    pixel_shader_name_storage = "NativeRenderReplayDiagnosticPS.hlsl";
  } else {
    return false;
  }

  const ReplaySurfaceSize size = ChooseSurfaceSize(capture);
  const uint32_t width = std::clamp<uint32_t>(size.width, 64, 3840);
  const uint32_t height = std::clamp<uint32_t>(size.height, 64, 2160);
  const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

  ComPtr<ID3D12Device> device;
  if (!CheckHr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                 IID_PPV_ARGS(&device)),
               "D3D12CreateDevice", error)) {
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queue_desc{};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  ComPtr<ID3D12CommandQueue> queue;
  if (!CheckHr(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
               "ID3D12Device::CreateCommandQueue", error)) {
    return false;
  }

  ComPtr<ID3D12CommandAllocator> allocator;
  if (!CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator)),
               "ID3D12Device::CreateCommandAllocator", error)) {
    return false;
  }

  ComPtr<ID3D12GraphicsCommandList> list;
  if (!CheckHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&list)),
               "ID3D12Device::CreateCommandList", error)) {
    return false;
  }

  D3D12_RESOURCE_DESC texture_desc{};
  texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture_desc.Width = width;
  texture_desc.Height = height;
  texture_desc.DepthOrArraySize = 1;
  texture_desc.MipLevels = 1;
  texture_desc.Format = format;
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  D3D12_CLEAR_VALUE clear_value{};
  clear_value.Format = format;
  clear_value.Color[0] = 0.015f;
  clear_value.Color[1] = 0.018f;
  clear_value.Color[2] = 0.022f;
  clear_value.Color[3] = 1.0f;

  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;

  ComPtr<ID3D12Resource> target;
  if (!CheckHr(device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                   IID_PPV_ARGS(&target)),
               "ID3D12Device::CreateCommittedResource(real render target)",
               error)) {
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.NumDescriptors = 1;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  if (!CheckHr(
          device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
          "ID3D12Device::CreateDescriptorHeap(RTV)", error)) {
    return false;
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
      rtv_heap->GetCPUDescriptorHandleForHeapStart();
  device->CreateRenderTargetView(target.Get(), nullptr, rtv);

  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline_state;
  const RenderStateRecord *pipeline_render_state =
      draw_state.draw.render_state.present ? &draw_state.draw.render_state
                                           : nullptr;
  if (!CreateRealGeometryPipeline(device.Get(), root_signature, pipeline_state,
                                  format, pipeline_render_state,
                                  vertex_shader_source,
                                  vertex_shader_name_storage.c_str(),
                                  vertex_shader_entry, vertex_shader_profile,
                                  override_pair.vertex_cache_path,
                                  override_pair.vertex_log_path,
                                  pixel_shader_source,
                                  pixel_shader_name_storage.c_str(),
                                  pixel_shader_entry, pixel_shader_profile,
                                  override_pair.pixel_cache_path,
                                  override_pair.pixel_log_path, error)) {
    return false;
  }
  if (write_override_cache_index &&
      !WriteOverrideCacheIndex(draw_state, options, override_pair, error)) {
    return false;
  }

  std::vector<UploadedRealDraw> uploaded_draws;
  uploaded_draws.reserve(prepared_draws.size());
  for (const PreparedRealDraw &prepared_draw : prepared_draws) {
    UploadedRealDraw uploaded;
    uploaded.prepared = prepared_draw;
    uploaded.constants =
        BuildCapturedConstants(capture.draws[prepared_draw.draw_index]);
    uploaded.vertex_bytes =
        uploaded.prepared.vertices.size() * sizeof(RealReplayVertex);
    if (!CreateUploadBuffer(device.Get(), uploaded.prepared.vertices.data(),
                            uploaded.vertex_bytes, uploaded.vertex_buffer,
                            error)) {
      return false;
    }

    const void *index_data =
        uploaded.prepared.uses_32bit_indices
            ? static_cast<const void *>(uploaded.prepared.indices32.data())
            : static_cast<const void *>(uploaded.prepared.indices16.data());
    uploaded.index_bytes =
        uploaded.prepared.uses_32bit_indices
            ? uploaded.prepared.indices32.size() * sizeof(uint32_t)
            : uploaded.prepared.indices16.size() * sizeof(uint16_t);
    if (!CreateUploadBuffer(device.Get(), index_data, uploaded.index_bytes,
                            uploaded.index_buffer, error)) {
      return false;
    }
    uploaded_draws.push_back(std::move(uploaded));
  }

  D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
  srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  const std::size_t texture_descriptor_count =
      std::max<std::size_t>(1, uploaded_draws.size() *
                                   kMaxRealReplayTextureSlots);
  srv_heap_desc.NumDescriptors =
      static_cast<UINT>(texture_descriptor_count);
  srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> srv_heap;
  if (!CheckHr(
          device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)),
          "ID3D12Device::CreateDescriptorHeap(SRV)", error)) {
    return false;
  }
  const UINT srv_descriptor_size = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_start =
      srv_heap->GetCPUDescriptorHandleForHeapStart();

  D3D12_DESCRIPTOR_HEAP_DESC sampler_heap_desc{};
  sampler_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  sampler_heap_desc.NumDescriptors =
      static_cast<UINT>(texture_descriptor_count);
  sampler_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ComPtr<ID3D12DescriptorHeap> sampler_heap;
  if (!CheckHr(device->CreateDescriptorHeap(&sampler_heap_desc,
                                            IID_PPV_ARGS(&sampler_heap)),
               "ID3D12Device::CreateDescriptorHeap(Sampler)", error)) {
    return false;
  }
  const UINT sampler_descriptor_size =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  D3D12_CPU_DESCRIPTOR_HANDLE sampler_cpu_start =
      sampler_heap->GetCPUDescriptorHandleForHeapStart();

  uint32_t captured_texture_count = 0;
  uint32_t fallback_texture_count = 0;
  uint32_t unsupported_texture_count = 0;
  uint32_t captured_sampler_count = 0;
  uint32_t fallback_sampler_count = 0;
  uint32_t exact_sampler_clamp_count = 0;
  uint32_t fallback_sampler_clamp_count = 0;
  const uint8_t white_texel[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  for (std::size_t i = 0; i < uploaded_draws.size(); ++i) {
    UploadedRealDraw &uploaded = uploaded_draws[i];
    uploaded.texture_srv_base_index =
        static_cast<uint32_t>(i * kMaxRealReplayTextureSlots);
    uploaded.sampler_descriptor_base_index =
        static_cast<uint32_t>(i * kMaxRealReplayTextureSlots);

    const ReplayDrawState &uploaded_state =
        capture.draws[uploaded.prepared.draw_index];
    for (std::size_t slot = 0; slot < kMaxRealReplayTextureSlots; ++slot) {
      const std::size_t descriptor_index =
          i * kMaxRealReplayTextureSlots + slot;
      D3D12_CPU_DESCRIPTOR_HANDLE descriptor = srv_cpu_start;
      descriptor.ptr += static_cast<SIZE_T>(descriptor_index) *
                        srv_descriptor_size;
      D3D12_CPU_DESCRIPTOR_HANDLE sampler_descriptor = sampler_cpu_start;
      sampler_descriptor.ptr += static_cast<SIZE_T>(descriptor_index) *
                                sampler_descriptor_size;

      std::vector<uint8_t> texture_rgba;
      std::string texture_reason;
      const TextureFetchRecord *selected_fetch = nullptr;
      if (slot < uploaded_state.draw.texture_fetches.size()) {
        const TextureFetchRecord &fetch = uploaded_state.draw.texture_fetches[slot];
        if (DecodeTextureRgba8(fetch, texture_rgba, texture_reason)) {
          selected_fetch = &fetch;
        } else {
          ++unsupported_texture_count;
          if (texture_reason.empty()) {
            texture_reason = "texture decode failed";
          }
        }
      }

      if (selected_fetch) {
        uploaded.texture_widths[slot] = selected_fetch->width;
        uploaded.texture_heights[slot] = selected_fetch->height;
        uploaded.texture_formats[slot] = selected_fetch->format;
        uploaded.texture_from_capture[slot] = true;
        uploaded.sampler_from_capture[slot] = true;
        uploaded.texture_notes[slot] = "captured";
        if (!CreateTexture2DRgba8(
                device.Get(), list.Get(), texture_rgba.data(),
                uploaded.texture_widths[slot],
                uploaded.texture_heights[slot], uploaded.textures[slot],
                uploaded.texture_uploads[slot], error)) {
          return false;
        }
        ++captured_texture_count;
        ++captured_sampler_count;
        if (selected_fetch->clamp_modes_present) {
          ++exact_sampler_clamp_count;
        } else {
          ++fallback_sampler_clamp_count;
        }
      } else {
        uploaded.texture_widths[slot] = 1;
        uploaded.texture_heights[slot] = 1;
        uploaded.texture_formats[slot] = 6;
        uploaded.texture_from_capture[slot] = false;
        uploaded.sampler_from_capture[slot] = false;
        uploaded.texture_notes[slot] =
            texture_reason.empty() ? "no texture fetch" : texture_reason;
        if (!CreateTexture2DRgba8(device.Get(), list.Get(), white_texel, 1, 1,
                                  uploaded.textures[slot],
                                  uploaded.texture_uploads[slot], error)) {
          return false;
        }
        ++fallback_texture_count;
        ++fallback_sampler_count;
        ++fallback_sampler_clamp_count;
      }
      CreateTextureSrv(device.Get(), uploaded.textures[slot].Get(),
                       descriptor);
      CreateSampler(device.Get(), selected_fetch, sampler_descriptor);
    }
  }
  std::cout << "D3D12 real replay submitted " << uploaded_draws.size()
            << " supported draw(s) for shader pair VS="
            << FormatHex64(draw_state.vertex_shader.hash) << " PS="
            << FormatHex64(draw_state.pixel_shader.hash) << " out of "
            << CountDrawsForShaderPair(capture, draw_state.vertex_shader.hash,
                                       draw_state.pixel_shader.hash)
            << " captured draw(s) with that pair\n";
  std::cout << "D3D12 real replay bound " << captured_texture_count
            << " captured texture SRV(s), " << fallback_texture_count
            << " fallback texture SRV(s), unsupported_texture_attempts="
            << unsupported_texture_count << "\n";
  std::cout << "D3D12 real replay bound " << captured_sampler_count
            << " captured sampler descriptor(s), " << fallback_sampler_count
            << " fallback sampler descriptor(s), exact_clamp_modes="
            << exact_sampler_clamp_count
            << ", clamp_addressing_fallbacks="
            << fallback_sampler_clamp_count << "\n";
  if (pipeline_render_state) {
    std::cout << "D3D12 real replay applied render state from draw "
              << prepared.draw_index << ": color_mask="
              << FormatHex32(pipeline_render_state->rb_color_mask)
              << " cull=" << pipeline_render_state->cull_mode
              << " depth_test="
              << (pipeline_render_state->depth_test_enable ? "yes" : "no")
              << " depth_write="
              << (pipeline_render_state->depth_write_enable ? "yes" : "no")
              << " stencil="
              << (pipeline_render_state->stencil_enable ? "yes" : "no")
              << "\n";
  } else {
    std::cout << "D3D12 real replay render state unavailable; using default "
                 "D3D12 PSO state\n";
  }

  D3D12_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  D3D12_RECT scissor =
      ScissorRectFromRenderState(pipeline_render_state, width, height);

  list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  ID3D12DescriptorHeap *descriptor_heaps[] = {srv_heap.Get(),
                                              sampler_heap.Get()};
  list->SetDescriptorHeaps(2, descriptor_heaps);
  list->SetGraphicsRootSignature(root_signature.Get());
  list->SetPipelineState(pipeline_state.Get());
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  const float constants[4] = {static_cast<float>(width),
                              static_cast<float>(height), 0.0f, 0.0f};
  list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

  for (const UploadedRealDraw &uploaded : uploaded_draws) {
    const ReplayDrawState &uploaded_state =
        capture.draws[uploaded.prepared.draw_index];
    list->SetGraphicsRoot32BitConstants(
        1, static_cast<UINT>(uploaded.constants.dwords.size()),
        uploaded.constants.dwords.data(), 0);
    D3D12_GPU_DESCRIPTOR_HANDLE texture_srv =
        srv_heap->GetGPUDescriptorHandleForHeapStart();
    texture_srv.ptr += static_cast<UINT64>(uploaded.texture_srv_base_index) *
                       srv_descriptor_size;
    list->SetGraphicsRootDescriptorTable(2, texture_srv);
    D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle =
        sampler_heap->GetGPUDescriptorHandleForHeapStart();
    sampler_handle.ptr +=
        static_cast<UINT64>(uploaded.sampler_descriptor_base_index) *
        sampler_descriptor_size;
    list->SetGraphicsRootDescriptorTable(3, sampler_handle);

    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    vertex_view.BufferLocation =
        uploaded.vertex_buffer->GetGPUVirtualAddress();
    vertex_view.SizeInBytes = static_cast<UINT>(uploaded.vertex_bytes);
    vertex_view.StrideInBytes = sizeof(RealReplayVertex);

    D3D12_INDEX_BUFFER_VIEW index_view{};
    index_view.BufferLocation = uploaded.index_buffer->GetGPUVirtualAddress();
    index_view.SizeInBytes = static_cast<UINT>(uploaded.index_bytes);
    index_view.Format = uploaded.prepared.uses_32bit_indices
                            ? DXGI_FORMAT_R32_UINT
                            : DXGI_FORMAT_R16_UINT;

    list->IASetVertexBuffers(0, 1, &vertex_view);
    list->IASetIndexBuffer(&index_view);
    list->IASetPrimitiveTopology(
        TopologyForPrimitive(uploaded_state.draw.primitive_type));
    const UINT index_count =
        static_cast<UINT>(uploaded.prepared.uses_32bit_indices
                              ? uploaded.prepared.indices32.size()
                              : uploaded.prepared.indices16.size());
    list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
  }

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = target.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  list->ResourceBarrier(1, &barrier);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT row_count = 0;
  UINT64 row_size = 0;
  UINT64 total_size = 0;
  device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, &row_count,
                                &row_size, &total_size);

  D3D12_HEAP_PROPERTIES readback_heap{};
  readback_heap.Type = D3D12_HEAP_TYPE_READBACK;
  readback_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  readback_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  readback_heap.CreationNodeMask = 1;
  readback_heap.VisibleNodeMask = 1;

  D3D12_RESOURCE_DESC buffer_desc{};
  buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer_desc.Width = total_size;
  buffer_desc.Height = 1;
  buffer_desc.DepthOrArraySize = 1;
  buffer_desc.MipLevels = 1;
  buffer_desc.SampleDesc.Count = 1;
  buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (!CheckHr(device->CreateCommittedResource(
                   &readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                   IID_PPV_ARGS(&readback)),
               "ID3D12Device::CreateCommittedResource(real readback)", error)) {
    return false;
  }

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = readback.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dst.PlacedFootprint = footprint;

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = target.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  src.SubresourceIndex = 0;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  if (!CheckHr(list->Close(), "ID3D12GraphicsCommandList::Close", error)) {
    return false;
  }

  ID3D12CommandList *lists[] = {list.Get()};
  queue->ExecuteCommandLists(1, lists);

  ComPtr<ID3D12Fence> fence;
  if (!CheckHr(
          device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "ID3D12Device::CreateFence", error)) {
    return false;
  }
  HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event) {
    error = "CreateEventW failed";
    return false;
  }
  uint64_t fence_value = 0;
  const bool waited =
      WaitForGpu(queue.Get(), fence.Get(), fence_event, fence_value, error);
  CloseHandle(fence_event);
  if (!waited) {
    return false;
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE read_range{0, static_cast<SIZE_T>(total_size)};
  if (!CheckHr(
          readback->Map(0, &read_range, reinterpret_cast<void **>(&mapped)),
          "ID3D12Resource::Map", error)) {
    return false;
  }

  const std::filesystem::path output = OutputPathFor(capture, options);
  std::error_code ec;
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path(), ec);
  }
  const bool wrote =
      !ec && WriteBmp(output, mapped + footprint.Offset,
                      footprint.Footprint.RowPitch, width, height, error);
  D3D12_RANGE empty_range{0, 0};
  readback->Unmap(0, &empty_range);

  if (ec) {
    error = "could not create D3D12 output directory: " + ec.message();
    return false;
  }
  if (!wrote) {
    return false;
  }

  return true;
#else
  (void)capture;
  (void)options;
  error = "D3D12 replay backend is only available on Windows";
  return false;
#endif
}

} // namespace bo2::native::replay
