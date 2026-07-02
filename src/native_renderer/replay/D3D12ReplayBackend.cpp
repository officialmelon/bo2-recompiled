#include "NativeRenderReplay.h"

#include "D3D12LiveReplaySubmit.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
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

constexpr std::size_t kMaxRealReplayTextureSlots = 8;
constexpr uint32_t kCapturedFloat4ConstantCount = 512;
constexpr uint32_t kCapturedConstantDwordCount =
    kCapturedFloat4ConstantCount * 4;
constexpr uint32_t kCapturedConstantBufferBytes =
    kCapturedConstantDwordCount * sizeof(uint32_t);
constexpr uint32_t kInputLayoutPosition = 1u << 0;
constexpr uint32_t kInputLayoutColor0 = 1u << 1;
constexpr uint32_t kInputLayoutTexcoord0 = 1u << 2;
constexpr uint32_t kInputLayoutNormal0 = 1u << 3;
constexpr uint32_t kInputLayoutTexcoord1 = 1u << 4;
constexpr DXGI_FORMAT kReplayDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

bool CompileShader(const char *source, const char *entry, const char *target,
                   const char *source_name, ComPtr<ID3DBlob> &blob,
                   std::string &error);
bool CompileShaderWithCache(const char *source, const char *entry,
                            const char *target, const char *source_name,
                            const std::filesystem::path &cache_path,
                            const std::filesystem::path &log_path,
                            ComPtr<ID3DBlob> &blob, std::string &error);
bool DecodeTextureRgba8(const TextureFetchRecord &fetch,
                        std::vector<uint8_t> &rgba, std::string &reason);

uint64_t HashCombine64(uint64_t hash, uint64_t value) {
  hash ^= value + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
  return hash;
}

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

void AppendDeviceRemovedReason(ID3D12Device *device, std::string &error) {
  if (!device) {
    return;
  }
  const HRESULT reason = device->GetDeviceRemovedReason();
  if (reason == S_OK) {
    return;
  }
  error += " device_removed_reason=0x" + FormatHex32(reason).substr(2);
}

void AppendD3D12InfoQueueMessages(ID3D12Device *device, std::string &error) {
  if (!device) {
    return;
  }
  ComPtr<ID3D12InfoQueue> info_queue;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
    return;
  }

  const UINT64 message_count =
      info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
  const UINT64 first_message = message_count > 8 ? message_count - 8 : 0;
  for (UINT64 i = first_message; i < message_count; ++i) {
    SIZE_T message_size = 0;
    if (FAILED(info_queue->GetMessage(i, nullptr, &message_size)) ||
        message_size == 0) {
      continue;
    }
    std::vector<uint8_t> storage(message_size);
    auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
    if (FAILED(info_queue->GetMessage(i, message, &message_size)) ||
        !message->pDescription) {
      continue;
    }
    error += "\nD3D12: ";
    error.append(message->pDescription, message->DescriptionByteLength);
  }
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

D3D12_DEPTH_STENCILOP_DESC DefaultKeepStencilOp(D3D12_COMPARISON_FUNC func) {
  D3D12_DEPTH_STENCILOP_DESC op{};
  op.StencilFailOp = D3D12_STENCIL_OP_KEEP;
  op.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
  op.StencilPassOp = D3D12_STENCIL_OP_KEEP;
  op.StencilFunc = func;
  return op;
}

D3D12_STENCIL_OP D3D12StencilOpFromXenos(uint32_t op) {
  static constexpr D3D12_STENCIL_OP kMap[8] = {
      D3D12_STENCIL_OP_KEEP,
      D3D12_STENCIL_OP_ZERO,
      D3D12_STENCIL_OP_REPLACE,
      D3D12_STENCIL_OP_INCR_SAT,
      D3D12_STENCIL_OP_DECR_SAT,
      D3D12_STENCIL_OP_INVERT,
      D3D12_STENCIL_OP_INCR,
      D3D12_STENCIL_OP_DECR,
  };
  return kMap[op & 0x7];
}

D3D12_DEPTH_STENCILOP_DESC StencilOpDescFromDepthControl(
    uint32_t rb_depthcontrol, bool back_face) {
  const uint32_t func_shift = back_face ? 20u : 8u;
  const uint32_t fail_shift = back_face ? 23u : 11u;
  const uint32_t pass_shift = back_face ? 26u : 14u;
  const uint32_t depth_fail_shift = back_face ? 29u : 17u;
  D3D12_DEPTH_STENCILOP_DESC op{};
  op.StencilFailOp =
      D3D12StencilOpFromXenos((rb_depthcontrol >> fail_shift) & 0x7);
  op.StencilDepthFailOp =
      D3D12StencilOpFromXenos((rb_depthcontrol >> depth_fail_shift) & 0x7);
  op.StencilPassOp =
      D3D12StencilOpFromXenos((rb_depthcontrol >> pass_shift) & 0x7);
  op.StencilFunc =
      D3D12CompareFuncFromXenos((rb_depthcontrol >> func_shift) & 0x7);
  return op;
}

uint8_t StencilReadMaskFromRenderState(const RenderStateRecord &state) {
  const uint32_t mask = (state.rb_stencilrefmask >> 8) & 0xFF;
  return static_cast<uint8_t>(mask ? mask : D3D12_DEFAULT_STENCIL_READ_MASK);
}

uint8_t StencilWriteMaskFromRenderState(const RenderStateRecord &state) {
  const uint32_t mask = (state.rb_stencilrefmask >> 16) & 0xFF;
  return static_cast<uint8_t>(mask ? mask : D3D12_DEFAULT_STENCIL_WRITE_MASK);
}

uint32_t StencilRefFromRenderState(const RenderStateRecord *state) {
  if (!state || !state->present) {
    return 0;
  }
  return state->rb_stencilrefmask & 0xFF;
}

bool RenderStateUsesDepthTarget(const RenderStateRecord *state) {
  if (!state || !state->present) {
    return false;
  }
  return state->depth_test_enable || state->depth_write_enable ||
         state->stencil_enable;
}

DXGI_FORMAT DsvFormatForRenderState(const RenderStateRecord *state) {
  return RenderStateUsesDepthTarget(state) ? kReplayDepthStencilFormat
                                           : DXGI_FORMAT_UNKNOWN;
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

  desc.DepthEnable =
      (state->depth_test_enable || state->depth_write_enable) ? TRUE : FALSE;
  desc.DepthWriteMask = state->depth_write_enable
                            ? D3D12_DEPTH_WRITE_MASK_ALL
                            : D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = D3D12CompareFuncFromXenos(state->depth_func);
  desc.StencilEnable = state->stencil_enable ? TRUE : FALSE;
  if (state->stencil_enable) {
    desc.StencilReadMask = StencilReadMaskFromRenderState(*state);
    desc.StencilWriteMask = StencilWriteMaskFromRenderState(*state);
    desc.FrontFace = StencilOpDescFromDepthControl(state->rb_depthcontrol,
                                                   false);
    desc.BackFace = (state->rb_depthcontrol & (1u << 7))
                        ? StencilOpDescFromDepthControl(
                              state->rb_depthcontrol, true)
                        : desc.FrontFace;
  }
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

uint16_t GpuSwap16(uint16_t value, uint32_t endian) {
  switch (endian) {
  case 1:
  case 2:
    return static_cast<uint16_t>((value >> 8) | (value << 8));
  default:
    return value;
  }
}

float FloatFromBits(uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float HalfToFloat(uint16_t value) {
  const uint32_t sign = (value & 0x8000u) << 16;
  const uint32_t exponent = (value >> 10) & 0x1Fu;
  const uint32_t mantissa = value & 0x3FFu;
  uint32_t out = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      out = sign;
    } else {
      uint32_t mant = mantissa;
      uint32_t exp = 127 - 15 + 1;
      while ((mant & 0x0400u) == 0) {
        mant <<= 1;
        --exp;
      }
      mant &= 0x03FFu;
      out = sign | (exp << 23) | (mant << 13);
    }
  } else if (exponent == 0x1Fu) {
    out = sign | 0x7F800000u | (mantissa << 13);
  } else {
    out = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
  }
  return FloatFromBits(out);
}

int32_t SignExtend(uint32_t value, uint32_t bit_count) {
  const uint32_t shift = 32 - bit_count;
  return static_cast<int32_t>(value << shift) >> shift;
}

float ConvertPackedComponent(uint32_t raw, uint32_t width,
                             const VertexAttributeRecord &attribute) {
  if (attribute.is_signed) {
    const int32_t signed_value = SignExtend(raw, width);
    if (attribute.is_integer) {
      return static_cast<float>(signed_value);
    }
    const float scale =
        attribute.signed_rf_mode == 1
            ? (1.0f / (float((uint64_t{1} << (width - 1)) - 1) + 0.5f))
            : (1.0f / float((uint64_t{1} << (width - 1)) - 1));
    return static_cast<float>(signed_value) * scale;
  }
  if (attribute.is_integer) {
    return static_cast<float>(raw);
  }
  return static_cast<float>(raw) / float((uint64_t{1} << width) - 1);
}

uint32_t VertexFormatComponentCount(uint32_t format) {
  switch (format) {
  case 33:
  case 36:
    return 1;
  case 25:
  case 31:
  case 34:
  case 37:
    return 2;
  case 16:
  case 17:
  case 57:
    return 3;
  case 6:
  case 7:
  case 26:
  case 32:
  case 35:
  case 38:
    return 4;
  default:
    return 0;
  }
}

uint32_t VertexFormatByteSize(uint32_t format) {
  switch (format) {
  case 6:
  case 7:
  case 16:
  case 17:
  case 25:
  case 31:
  case 33:
  case 36:
    return 4;
  case 26:
  case 32:
  case 34:
  case 37:
    return 8;
  case 57:
    return 12;
  case 35:
  case 38:
    return 16;
  default:
    return 0;
  }
}


bool DecodeFloatAttribute(const VertexFetchRecord &fetch,
                          const VertexAttributeRecord &attribute,
                          uint32_t vertex_index,
                          std::vector<float> &components) {
  components.clear();
  const uint32_t component_count =
      VertexFormatComponentCount(attribute.data_format);
  const uint32_t byte_size = VertexFormatByteSize(attribute.data_format);
  if (component_count == 0 || byte_size == 0) {
    return false;
  }

  const std::size_t base =
      static_cast<std::size_t>(vertex_index) * fetch.stride_bytes +
      attribute.offset_bytes;
  if (base + byte_size > fetch.payload_bytes.size()) {
    return false;
  }

  auto load_word = [&](uint32_t word_index) {
    return GpuSwap32(LoadLittleEndian32(fetch.payload_bytes,
                                        base + word_index * 4),
                     fetch.endian);
  };

  switch (attribute.data_format) {
  case 6: {
    if (fetch.endian == 2) {
      const uint8_t *bytes = fetch.payload_bytes.data() + base;
      components.push_back(ConvertPackedComponent(bytes[1], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[2], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[3], 8, attribute));
      components.push_back(ConvertPackedComponent(bytes[0], 8, attribute));
      return true;
    }
    const uint32_t word = load_word(0);
    for (uint32_t component = 0; component < 4; ++component) {
      components.push_back(
          ConvertPackedComponent((word >> (component * 8)) & 0xFF, 8,
                                 attribute));
    }
    return true;
  }
  case 7: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 10) & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 20) & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 30) & 0x003, 2, attribute));
    return true;
  }
  case 16: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 11) & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 22) & 0x3FF, 10, attribute));
    return true;
  }
  case 17: {
    const uint32_t word = load_word(0);
    components.push_back(ConvertPackedComponent(word & 0x3FF, 10, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 10) & 0x7FF, 11, attribute));
    components.push_back(
        ConvertPackedComponent((word >> 21) & 0x7FF, 11, attribute));
    return true;
  }
  case 25:
  case 26: {
    for (uint32_t component = 0; component < component_count; ++component) {
      const uint32_t word = load_word(component / 2);
      const uint32_t raw = (word >> ((component & 1) * 16)) & 0xFFFF;
      components.push_back(ConvertPackedComponent(raw, 16, attribute));
    }
    return true;
  }
  case 31:
  case 32: {
    for (uint32_t component = 0; component < component_count; ++component) {
      const uint32_t word = load_word(component / 2);
      const uint32_t raw = (word >> ((component & 1) * 16)) & 0xFFFF;
      components.push_back(HalfToFloat(static_cast<uint16_t>(raw)));
    }
    return true;
  }
  case 33:
  case 34:
  case 35: {
    for (uint32_t component = 0; component < component_count; ++component) {
      const uint32_t word = load_word(component);
      if (attribute.is_signed) {
        const int32_t value = static_cast<int32_t>(word);
        components.push_back(attribute.is_integer
                                 ? static_cast<float>(value)
                                 : static_cast<float>(value) *
                                       (1.0f / 2147483647.0f));
      } else {
        components.push_back(attribute.is_integer
                                 ? static_cast<float>(word)
                                 : static_cast<float>(word) *
                                       (1.0f / 4294967295.0f));
      }
    }
    return true;
  }
  case 36:
  case 37:
  case 38:
  case 57:
    for (uint32_t component = 0; component < component_count; ++component) {
      components.push_back(FloatFromBits(load_word(component)));
    }
    return true;
  default:
    return false;
  }
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
  float normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float uv1[2] = {0.0f, 0.0f};
};

struct PreparedRealDraw {
  std::size_t draw_index = 0;
  std::vector<RealReplayVertex> vertices;
  std::vector<uint16_t> indices16;
  std::vector<uint32_t> indices32;
  uint32_t vertex_count = 0;
  uint32_t input_layout_mask = 0;
  uint64_t input_layout_signature = 0;
  bool indexed = false;
  bool vertexless = false;
  bool uses_32bit_indices = false;
  bool force_depth_only_color_mask = false;
  D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

struct PreparedCapturedConstants {
  std::array<uint32_t, kCapturedConstantDwordCount> dwords{};
  uint32_t count = 0;
};

struct UploadedRealDraw {
  PreparedRealDraw prepared;
  PreparedCapturedConstants constants;
  ComPtr<ID3D12Resource> constant_buffer;
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

struct D3D12ReplayPipeline {
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
  ComPtr<ID3D12RootSignature> root_signature;
  ComPtr<ID3D12PipelineState> pipeline_state;
  const RenderStateRecord *render_state = nullptr;
  bool used_diagnostic_shader = false;
  bool forced_depth_only_color_mask = false;
};

struct D3D12ReplayRenderTarget {
  uint32_t guest_base = 0;
  ComPtr<ID3D12Resource> resource;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  bool initialized = false;
};

struct D3D12ReplayRenderTargetCache {
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  std::map<uint32_t, D3D12ReplayRenderTarget> targets;
  uint32_t next_descriptor = 0;
};

struct CapturedColorTargetSeed {
  const RenderTargetPayloadRecord *payload = nullptr;
  uint32_t draw_index = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

uint32_t GuestColorBaseForDraw(const ReplayDrawState &state) {
  if (!state.draw.render_state.present ||
      state.draw.render_state.color_base.empty()) {
    return 0;
  }
  return state.draw.render_state.color_base[0];
}

uint32_t ChoosePresentedGuestColorBase(
    const std::vector<PreparedRealDraw> &draws, const ReplayCapture &capture) {
  struct BaseScore {
    std::size_t scene_draws = 0;
    std::size_t total_draws = 0;
    std::size_t depth_base_matches = 0;
    std::size_t last_draw_index = 0;
  };

  std::map<uint32_t, BaseScore> scores;
  std::set<uint32_t> non_depth_bases;
  for (auto it = draws.rbegin(); it != draws.rend(); ++it) {
    if (it->draw_index >= capture.draws.size()) {
      continue;
    }
    const ReplayDrawState &state = capture.draws[it->draw_index];
    const uint32_t base = GuestColorBaseForDraw(state);
    if (base == 0) {
      continue;
    }
    BaseScore &score = scores[base];
    ++score.total_draws;
    score.last_draw_index = std::max<std::size_t>(score.last_draw_index,
                                                  it->draw_index);
    if (state.draw.render_state.present &&
        state.draw.render_state.depth_base == base) {
      ++score.depth_base_matches;
    } else {
      non_depth_bases.insert(base);
    }
    if (!it->force_depth_only_color_mask) {
      ++score.scene_draws;
    }
  }

  uint32_t best_base = 0;
  BaseScore best_score{};
  for (const auto &[base, score] : scores) {
    if (!non_depth_bases.empty() && !non_depth_bases.contains(base)) {
      continue;
    }
    if (best_base == 0 || score.scene_draws > best_score.scene_draws ||
        (score.scene_draws == best_score.scene_draws &&
         score.total_draws > best_score.total_draws) ||
        (score.scene_draws == best_score.scene_draws &&
         score.total_draws == best_score.total_draws &&
         score.last_draw_index > best_score.last_draw_index)) {
      best_base = base;
      best_score = score;
    }
  }
  if (best_base != 0) {
    return best_base;
  }

  for (auto it = draws.rbegin(); it != draws.rend(); ++it) {
    if (it->draw_index >= capture.draws.size()) {
      continue;
    }
    const uint32_t base = GuestColorBaseForDraw(capture.draws[it->draw_index]);
    if (base != 0) {
      return base;
    }
  }
  return 0;
}

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

void AssignTexcoordComponents(RealReplayVertex &vertex,
                              uint32_t &input_layout_mask,
                              uint32_t &vertex_texcoord_count, float u,
                              float v);

bool FindCapturedColorTargetSeed(const ReplayCapture &capture,
                                 const std::vector<UploadedRealDraw> &draws,
                                 uint32_t replay_width,
                                 uint32_t replay_height,
                                 CapturedColorTargetSeed &seed,
                                 std::string &reason) {
  std::size_t candidates = 0;
  std::string first_reject;
  for (const UploadedRealDraw &uploaded : draws) {
    if (uploaded.prepared.draw_index >= capture.draws.size()) {
      continue;
    }
    const ReplayDrawState &state = capture.draws[uploaded.prepared.draw_index];
    const RenderStateRecord &render_state = state.draw.render_state;
    if (!render_state.present || render_state.surface_pitch == 0) {
      continue;
    }
    for (const RenderTargetPayloadRecord &payload :
         render_state.color_target_payloads) {
      if (payload.target != 0) {
        continue;
      }
      ++candidates;
      auto reject = [&](const std::string &message) {
        if (first_reject.empty()) {
          first_reject = message;
        }
      };
      if (payload.payload_missing || payload.payload_bytes.empty()) {
        reject("target payload is missing");
        continue;
      }
      if (payload.payload_truncated) {
        reject("target payload is truncated");
        continue;
      }
      if (payload.payload_offset_bytes != 0) {
        reject("target payload starts at nonzero offset");
        continue;
      }
      const uint64_t required_bytes =
          payload.payload_requested_byte_count != 0
              ? payload.payload_requested_byte_count
              : static_cast<uint64_t>(payload.payload_bytes.size());
      if (required_bytes != payload.payload_bytes.size()) {
        reject("target payload sidecar is not the full requested payload");
        continue;
      }
      const uint64_t row_bytes =
          static_cast<uint64_t>(render_state.surface_pitch) * 4u;
      if (row_bytes == 0 || required_bytes < row_bytes ||
          required_bytes % row_bytes != 0) {
        reject("target payload is not linear RGBA8-sized");
        continue;
      }
      const uint32_t target_width = render_state.surface_pitch;
      const uint32_t target_height =
          static_cast<uint32_t>(required_bytes / row_bytes);
      if (target_width != replay_width || target_height != replay_height) {
        reject("target payload dimensions " + std::to_string(target_width) +
               "x" + std::to_string(target_height) +
               " do not match replay target " + std::to_string(replay_width) +
               "x" + std::to_string(replay_height));
        continue;
      }
      seed.payload = &payload;
      seed.draw_index = state.draw_index;
      seed.width = target_width;
      seed.height = target_height;
      reason = "selected draw " + std::to_string(seed.draw_index) +
               " color target 0 payload";
      return true;
    }
  }

  if (candidates == 0) {
    reason = "no captured color target 0 payload in submitted draws";
  } else {
    reason = "no usable captured color target seed among " +
             std::to_string(candidates) + " candidate(s)";
    if (!first_reject.empty()) {
      reason += ": " + first_reject;
    }
  }
  return false;
}

uint64_t CapturedInputLayoutSignature(const VertexFetchRecord &fetch,
                                      uint32_t input_layout_mask) {
  uint64_t hash = 0xC0DEC0DE12345678ull;
  hash = HashCombine64(hash, input_layout_mask);
  hash = HashCombine64(hash, fetch.stride_bytes);
  hash = HashCombine64(hash, fetch.endian);
  hash = HashCombine64(hash, fetch.attributes.size());
  for (const VertexAttributeRecord &attribute : fetch.attributes) {
    hash = HashCombine64(hash, attribute.data_format);
    hash = HashCombine64(hash, static_cast<uint32_t>(attribute.offset));
    hash = HashCombine64(hash, attribute.offset_bytes);
    hash = HashCombine64(hash, attribute.stride_bytes);
    hash = HashCombine64(hash, static_cast<uint32_t>(attribute.exp_adjust));
    hash = HashCombine64(hash, attribute.signed_rf_mode);
    hash = HashCombine64(hash, attribute.is_signed ? 1u : 0u);
    hash = HashCombine64(hash, attribute.is_integer ? 1u : 0u);
    hash = HashCombine64(hash, attribute.is_index_rounded ? 1u : 0u);
  }
  return hash;
}

bool BuildCanonicalVertices(const VertexFetchRecord &fetch,
                            std::vector<RealReplayVertex> &vertices,
                            uint32_t &input_layout_mask,
                            uint64_t &input_layout_signature) {
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
  input_layout_mask = 0;
  for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
    RealReplayVertex &vertex = vertices[vertex_index];
    bool vertex_has_position = false;
    uint32_t vertex_texcoord_count = 0;
    for (const VertexAttributeRecord &attribute : fetch.attributes) {
      std::vector<float> components;
      if (!DecodeFloatAttribute(fetch, attribute, vertex_index, components)) {
        continue;
      }

      if ((attribute.data_format == 38 || attribute.data_format == 57 ||
           attribute.data_format == 36 || attribute.data_format == 26 ||
           (attribute.data_format == 37 && fetch.attributes.size() == 1)) &&
          !components.empty() && !vertex_has_position) {
        vertex_has_position = true;
        input_layout_mask |= kInputLayoutPosition;
        if (attribute.data_format == 26) {
          vertex.position[0] =
              components.size() > 0 ? components[0] / 65535.0f * 1280.0f
                                    : 0.0f;
          vertex.position[1] =
              components.size() > 1 ? components[1] / 65535.0f * 720.0f
                                    : 0.0f;
          vertex.position[2] =
              components.size() > 2 ? components[2] / 65535.0f : 0.0f;
        } else {
          vertex.position[0] = components.size() > 0 ? components[0] : 0.0f;
          vertex.position[1] = components.size() > 1 ? components[1] : 0.0f;
          vertex.position[2] = components.size() > 2 ? components[2] : 0.0f;
        }
        vertex.position[3] = components.size() > 3 ? components[3] : 1.0f;
        if (attribute.data_format == 26 && components.size() >= 4) {
          vertex.color[0] = components[2] / 65535.0f;
          vertex.color[1] = components[3] / 65535.0f;
          vertex.color[2] =
              static_cast<float>((vertex_index * 37u) & 0xFFu) / 255.0f;
          vertex.color[3] = 1.0f;
          input_layout_mask |= kInputLayoutColor0;
        }
      } else if (attribute.data_format == 38 && components.size() >= 4) {
        vertex.color[0] = components[0];
        vertex.color[1] = components[1];
        vertex.color[2] = components[2];
        vertex.color[3] = components[3];
        input_layout_mask |= kInputLayoutColor0;
      } else if (attribute.data_format == 6 && components.size() >= 4) {
        vertex.color[0] = components[0];
        vertex.color[1] = components[1];
        vertex.color[2] = components[2];
        vertex.color[3] = components[3];
        input_layout_mask |= kInputLayoutColor0;
      } else if (attribute.data_format == 7 && components.size() >= 4) {
        if ((input_layout_mask & kInputLayoutColor0) == 0) {
          vertex.color[0] = components[0];
          vertex.color[1] = components[1];
          vertex.color[2] = components[2];
          vertex.color[3] = components[3];
          input_layout_mask |= kInputLayoutColor0;
        } else {
          AssignTexcoordComponents(vertex, input_layout_mask,
                                   vertex_texcoord_count, components[0],
                                   components[1]);
        }
      } else if ((attribute.data_format == 25 || attribute.data_format == 31 ||
                  attribute.data_format == 37) &&
                 components.size() >= 2) {
        if ((attribute.data_format == 37 && fetch.attributes.size() == 1) &&
            !vertex_has_position) {
          vertex_has_position = true;
          input_layout_mask |= kInputLayoutPosition;
          vertex.position[0] = components[0];
          vertex.position[1] = components[1];
          vertex.position[2] = 0.0f;
          vertex.position[3] = 1.0f;
        } else {
          AssignTexcoordComponents(vertex, input_layout_mask,
                                   vertex_texcoord_count, components[0],
                                   components[1]);
        }
      } else if (attribute.data_format == 26 && components.size() >= 3 &&
                 vertex_has_position) {
        vertex.normal[0] = components[0];
        vertex.normal[1] = components[1];
        vertex.normal[2] = components[2];
        vertex.normal[3] = components.size() > 3 ? components[3] : 0.0f;
        input_layout_mask |= kInputLayoutNormal0;
      }
    }
  }

  if ((input_layout_mask & kInputLayoutPosition) == 0) {
    return false;
  }
  input_layout_signature =
      CapturedInputLayoutSignature(fetch, input_layout_mask);
  return true;
}

void ExpandPointListToQuads(std::vector<RealReplayVertex> &vertices) {
  const std::vector<RealReplayVertex> points = vertices;
  vertices.clear();
  vertices.reserve(points.size() * 6);
  constexpr float kHalfSize = 3.0f;
  constexpr std::array<std::array<float, 2>, 6> offsets = {
      std::array<float, 2>{-kHalfSize, -kHalfSize},
      std::array<float, 2>{kHalfSize, -kHalfSize},
      std::array<float, 2>{-kHalfSize, kHalfSize},
      std::array<float, 2>{-kHalfSize, kHalfSize},
      std::array<float, 2>{kHalfSize, -kHalfSize},
      std::array<float, 2>{kHalfSize, kHalfSize},
  };
  for (const RealReplayVertex &point : points) {
    for (const auto &offset : offsets) {
      RealReplayVertex vertex = point;
      vertex.position[0] += offset[0];
      vertex.position[1] += offset[1];
      vertices.push_back(vertex);
    }
  }
}

bool ExpandRectListToTriangles(std::vector<RealReplayVertex> &vertices,
                               uint32_t source_vertex_count,
                               uint32_t input_layout_mask) {
  if (source_vertex_count == 0 || (source_vertex_count % 3) != 0 ||
      source_vertex_count > vertices.size()) {
    return false;
  }

  const std::vector<RealReplayVertex> rect_vertices(
      vertices.begin(), vertices.begin() + source_vertex_count);
  vertices.clear();
  vertices.reserve((source_vertex_count / 3) * 6);
  for (std::size_t i = 0; i < rect_vertices.size(); i += 3) {
    const RealReplayVertex &v0 = rect_vertices[i + 0];
    const RealReplayVertex &v1 = rect_vertices[i + 1];
    const RealReplayVertex &v2 = rect_vertices[i + 2];
    RealReplayVertex v3 = v0;
    v3.position[1] = v2.position[1];
    v3.position[2] = v2.position[2];
    v3.position[3] = v2.position[3];
    if ((input_layout_mask & kInputLayoutTexcoord0) != 0) {
      v3.uv[1] = v2.uv[1];
    }
    if ((input_layout_mask & kInputLayoutTexcoord1) != 0) {
      v3.uv1[1] = v2.uv1[1];
    }

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v0);
    vertices.push_back(v2);
    vertices.push_back(v3);
  }
  return true;
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

bool SupportsVertexlessDraw(const ReplayDrawState &state) {
  const PM4DrawRecord &draw = state.draw;
  if (draw.indexed || !draw.vertex_fetches.empty() || draw.index_count == 0) {
    return false;
  }
  if (state.vertex_shader.hash != 0xB6C9863F710683ECull ||
      state.pixel_shader.hash != 0xA4A965C189287B99ull) {
    return false;
  }
  // The current generated HLSL for this pair initializes r0 to zero and the
  // pixel shader exports r0, so submitting it produces black placeholder output.
  // Keep the path fail-closed until Xenos r0/register initialization semantics
  // are decoded instead of counting black output as scene rendering.
  return false;
}

bool IsKnownZeroColorExportDraw(const ReplayDrawState &state,
                                const std::vector<RealReplayVertex> &vertices,
                                uint32_t input_layout_mask) {
  (void)input_layout_mask;
  if (state.pixel_shader.hash != 0xA4A965C189287B99ull ||
      !state.draw.texture_fetches.empty() || vertices.empty()) {
    return false;
  }
  // A4 is the tiny max(oC0, r0, r0) pixel shader. Without a texture fetch, the
  // current limited translator can only replay whatever r0 happened to be after
  // our approximate VS export mapping. These screen/depth-style passes have
  // repeatedly produced full-screen placeholder triangles, including when the
  // captured render state also writes synthetic replay depth/stencil. Keep pure
  // color-only passes out of the "real scene" path until Xenos export/register
  // semantics and real target/depth side effects are modeled.
  return true;
}

bool ShouldReplayAsDepthOnlyZeroColorDraw(
    const ReplayDrawState &state, const std::vector<RealReplayVertex> &vertices,
    uint32_t input_layout_mask) {
  if (!IsKnownZeroColorExportDraw(state, vertices, input_layout_mask)) {
    return false;
  }
  const RenderStateRecord &render_state = state.draw.render_state;
  if (!render_state.present) {
    return false;
  }
  return render_state.depth_write_enable || render_state.stencil_enable;
}

bool VerticesCoverRenderTarget(const ReplayDrawState &state,
                               const std::vector<RealReplayVertex> &vertices) {
  if (vertices.empty()) {
    return false;
  }

  float min_x = vertices.front().position[0];
  float max_x = min_x;
  float min_y = vertices.front().position[1];
  float max_y = min_y;
  for (const RealReplayVertex &vertex : vertices) {
    min_x = std::min(min_x, vertex.position[0]);
    max_x = std::max(max_x, vertex.position[0]);
    min_y = std::min(min_y, vertex.position[1]);
    max_y = std::max(max_y, vertex.position[1]);
  }

  const RenderStateRecord *render_state =
      state.draw.render_state.present ? &state.draw.render_state : nullptr;
  const float target_width =
      render_state && render_state->surface_pitch != 0
          ? static_cast<float>(render_state->surface_pitch)
          : 1280.0f;
  constexpr float kTargetHeight = 720.0f;
  return min_x <= 1.0f && min_y <= 1.0f &&
         (max_x - min_x) >= target_width * 0.95f &&
         (max_y - min_y) >= kTargetHeight * 0.95f;
}

bool IsAb1eA4ZeroColorFillDraw(const ReplayDrawState &state,
                               const std::vector<RealReplayVertex> &vertices,
                               uint32_t input_layout_mask) {
  if (state.vertex_shader.hash != 0xAB1E86137A0240E8ull ||
      state.pixel_shader.hash != 0xA4A965C189287B99ull) {
    return false;
  }
  const PM4DrawRecord &draw = state.draw;
  if (draw.indexed || draw.primitive_type != 8 || draw.index_count != 3 ||
      !draw.texture_fetches.empty() || !draw.render_state.present) {
    return false;
  }
  if ((input_layout_mask & kInputLayoutColor0) != 0) {
    return false;
  }
  const RenderStateRecord &render_state = draw.render_state;
  const bool param_gen_enabled =
      ((render_state.sq_program_cntl >> 18) & 0x1u) != 0;
  if ((render_state.rb_color_mask & 0xF) == 0 ||
      render_state.depth_write_enable || render_state.stencil_enable ||
      param_gen_enabled) {
    return false;
  }
  return VerticesCoverRenderTarget(state, vertices);
}

bool IsAb1eTextureInterpolatorBlocker(const ReplayDrawState &state) {
  if (state.vertex_shader.hash != 0xAB1E86137A0240E8ull) {
    return false;
  }
  if (state.pixel_shader.hash != 0xEDC17DCC3FFDB040ull &&
      state.pixel_shader.hash != 0xFF01D28E1EF3A880ull) {
    return false;
  }
  return !state.draw.texture_fetches.empty();
}

bool MatchesShaderPairFilter(const ReplayDrawState &state,
                             const ReplayCliOptions &options) {
  if (!options.shader_pair_vertex_hash && !options.shader_pair_pixel_hash) {
    return true;
  }
  return options.shader_pair_vertex_hash &&
         options.shader_pair_pixel_hash &&
         state.vertex_shader.hash == *options.shader_pair_vertex_hash &&
         state.pixel_shader.hash == *options.shader_pair_pixel_hash;
}

uint32_t EffectiveInputLayoutMask(const ReplayDrawState &state,
                                  uint32_t decoded_mask) {
  if (SupportsVertexlessDraw(state)) {
    return 0;
  }
  constexpr uint32_t kBaseInputLayout =
      kInputLayoutPosition | kInputLayoutColor0 | kInputLayoutTexcoord0;
  constexpr uint32_t kExtendedInputLayout =
      kInputLayoutNormal0 | kInputLayoutTexcoord1;
  if (state.vertex_shader.hash == 0x162EAA53D8B42911ull) {
    return (decoded_mask & (kBaseInputLayout | kExtendedInputLayout)) |
           kBaseInputLayout | kExtendedInputLayout;
  }
  return (decoded_mask & kBaseInputLayout) | kBaseInputLayout;
}

void AssignTexcoordComponents(RealReplayVertex &vertex,
                              uint32_t &input_layout_mask,
                              uint32_t &vertex_texcoord_count, float u,
                              float v) {
  if (vertex_texcoord_count == 0) {
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    input_layout_mask |= kInputLayoutTexcoord0;
  } else {
    vertex.uv1[0] = u;
    vertex.uv1[1] = v;
    input_layout_mask |= kInputLayoutTexcoord1;
  }
  ++vertex_texcoord_count;
}

bool IsNoSideEffectNoFetchDraw(const ReplayDrawState &state) {
  if (SupportsVertexlessDraw(state)) {
    return false;
  }

  const PM4DrawRecord &draw = state.draw;
  if (draw.indexed || !draw.vertex_fetches.empty() ||
      !draw.texture_fetches.empty()) {
    return false;
  }
  if (!draw.render_state.present) {
    return false;
  }

  const RenderStateRecord &render_state = draw.render_state;
  const bool color_write_disabled = (render_state.rb_color_mask & 0xF) == 0;
  const bool depth_write_disabled = !render_state.depth_write_enable;
  const bool stencil_disabled = !render_state.stencil_enable;
  return color_write_disabled && depth_write_disabled && stencil_disabled;
}

bool IsKnownNoRasterNoFetchDraw(const ReplayDrawState &state) {
  if (IsNoSideEffectNoFetchDraw(state)) {
    return true;
  }

  const PM4DrawRecord &draw = state.draw;
  if (draw.indexed || !draw.vertex_fetches.empty() ||
      !draw.texture_fetches.empty()) {
    return false;
  }
  if (TopologyForPrimitive(draw.primitive_type) !=
      D3D_PRIMITIVE_TOPOLOGY_POINTLIST) {
    return false;
  }

  // Semantic IR for this MP class has no constants, no vertex fetches, and
  // exports position through `sqrt oPos, -r_abs[0].x` after `setp_clr`.
  // Treat it as an intentional no-raster point until a capture proves visible
  // output; do not synthesize substitute point geometry.
  return state.vertex_shader.hash == 0xDDED7E538422AE73ull;
}

bool TextureDecodesAllZeroRgba(const TextureFetchRecord &fetch) {
  std::vector<uint8_t> rgba;
  std::string error;
  if (!DecodeTextureRgba8(fetch, rgba, error) || rgba.empty()) {
    return false;
  }
  return std::all_of(rgba.begin(), rgba.end(),
                     [](uint8_t value) { return value == 0; });
}

bool IsKnownZeroTextureNoOutputDraw(const ReplayDrawState &state) {
  if (state.vertex_shader.hash != 0xAB1E86137A0240E8ull ||
      state.pixel_shader.hash != 0xEDC17DCC3FFDB040ull) {
    return false;
  }
  const PM4DrawRecord &draw = state.draw;
  if (draw.texture_fetches.size() != 1 || !draw.render_state.present) {
    return false;
  }
  const RenderStateRecord &render_state = draw.render_state;
  if (render_state.depth_write_enable || render_state.stencil_enable) {
    return false;
  }
  const uint32_t blend_control =
      render_state.rb_blendcontrol.empty() ? 0 : render_state.rb_blendcontrol[0];
  if ((render_state.rb_color_mask & 0xF) == 0 || blend_control != 0x010B0706u) {
    return false;
  }
  return TextureDecodesAllZeroRgba(draw.texture_fetches.front());
}

bool SqProgramParamGenEnabled(uint32_t sq_program_cntl) {
  return ((sq_program_cntl >> 18) & 0x1u) != 0;
}

bool IsKnownZeroedInterpolatorNoOutputDraw(const ReplayDrawState &state) {
  if (state.vertex_shader.hash != 0xAB1E86137A0240E8ull ||
      state.pixel_shader.hash != 0xFF01D28E1EF3A880ull) {
    return false;
  }
  const PM4DrawRecord &draw = state.draw;
  if (draw.texture_fetches.size() != 1 || !draw.render_state.present) {
    return false;
  }
  const RenderStateRecord &render_state = draw.render_state;
  if (render_state.depth_write_enable || render_state.stencil_enable ||
      SqProgramParamGenEnabled(render_state.sq_program_cntl)) {
    return false;
  }
  const uint32_t blend_control =
      render_state.rb_blendcontrol.empty() ? 0 : render_state.rb_blendcontrol[0];
  if ((render_state.rb_color_mask & 0xF) == 0 || blend_control != 0x010B0706u) {
    return false;
  }
  // ReXGlue's shader translator zeroes pixel GPRs without an interpolator or
  // PsParamGen source. AB1E exports no interpolators, and FF01's final color is
  // `mul oC0, r1.xxxy, r0`, so r0=0 makes the source alpha zero and preserves
  // the destination under the captured SRC_ALPHA/INV_SRC_ALPHA blend state.
  return true;
}

bool IsKnownIgnoredUtilityDraw(const ReplayDrawState &state) {
  return IsKnownNoRasterNoFetchDraw(state) ||
         IsKnownZeroTextureNoOutputDraw(state) ||
         IsKnownZeroedInterpolatorNoOutputDraw(state);
}

bool IsLikelyFullscreenUtilityPass(const ReplayDrawState &state,
                                   const PreparedRealDraw &prepared) {
  if (prepared.vertices.empty() || prepared.vertexless) {
    return false;
  }

  float min_x = prepared.vertices.front().position[0];
  float max_x = min_x;
  float min_y = prepared.vertices.front().position[1];
  float max_y = min_y;
  for (const RealReplayVertex &vertex : prepared.vertices) {
    min_x = std::min(min_x, vertex.position[0]);
    max_x = std::max(max_x, vertex.position[0]);
    min_y = std::min(min_y, vertex.position[1]);
    max_y = std::max(max_y, vertex.position[1]);
  }

  const RenderStateRecord *render_state =
      state.draw.render_state.present ? &state.draw.render_state : nullptr;
  const float target_width =
      render_state && render_state->surface_pitch != 0
          ? static_cast<float>(render_state->surface_pitch)
          : 1280.0f;
  const float target_height = 720.0f;
  const float width = max_x - min_x;
  const float height = max_y - min_y;
  const bool covers_target = min_x <= 1.0f && min_y <= 1.0f &&
                             width >= target_width * 0.95f &&
                             height >= target_height * 0.95f;
  const bool depth_disabled =
      !render_state ||
      (!render_state->depth_test_enable && !render_state->depth_write_enable &&
       !render_state->stencil_enable);
  const bool known_postprocess_shader =
      state.pixel_shader.hash == 0x246E20EF10E0DDC7ull ||
      state.pixel_shader.hash == 0xC4ED2979F29C9139ull ||
      state.pixel_shader.hash == 0xA4A965C189287B99ull;
  return covers_target && depth_disabled && known_postprocess_shader;
}

bool PrepareRealDrawAtIndex(const ReplayCapture &capture, std::size_t index,
                            bool allow_non_indexed,
                            PreparedRealDraw &prepared) {
  if (index >= capture.draws.size()) {
    return false;
  }
  const ReplayDrawState &state = capture.draws[index];
  const PM4DrawRecord &draw = state.draw;
  const D3D12_PRIMITIVE_TOPOLOGY topology =
      TopologyForPrimitive(draw.primitive_type);
  const bool point_list = topology == D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
  const bool rect_list = draw.primitive_type == 8 && !draw.indexed;
  if (!draw.indexed && !allow_non_indexed) {
    return false;
  }
  if (draw.vertex_fetches.empty() && SupportsVertexlessDraw(state)) {
    prepared = {};
    prepared.draw_index = index;
    prepared.vertex_count = draw.index_count;
    prepared.indexed = false;
    prepared.vertexless = true;
    prepared.topology = topology;
    prepared.input_layout_mask = 0;
    return true;
  }
  if (draw.vertex_fetches.empty() ||
      (topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST && !point_list)) {
    return false;
  }
  if (IsAb1eTextureInterpolatorBlocker(state)) {
    return false;
  }

  std::vector<uint32_t> decoded_indices;
  if (draw.indexed) {
    if (draw.index_payload_missing || draw.index_payload_truncated) {
      return false;
    }
    decoded_indices = DecodeReplayIndices(draw);
    if (decoded_indices.empty()) {
      return false;
    }
  } else if (draw.index_count == 0) {
    return false;
  }

  for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
    PreparedRealDraw candidate;
    candidate.draw_index = index;
    if (!BuildCanonicalVertices(fetch, candidate.vertices,
                                candidate.input_layout_mask,
                                candidate.input_layout_signature)) {
      continue;
    }
    candidate.force_depth_only_color_mask =
        ShouldReplayAsDepthOnlyZeroColorDraw(
            state, candidate.vertices, candidate.input_layout_mask);
    if (IsKnownZeroColorExportDraw(state, candidate.vertices,
                                   candidate.input_layout_mask) &&
        !candidate.force_depth_only_color_mask &&
        !IsAb1eA4ZeroColorFillDraw(state, candidate.vertices,
                                   candidate.input_layout_mask)) {
      continue;
    }

    candidate.indexed = draw.indexed;
    candidate.topology = topology;
    if (point_list) {
      ExpandPointListToQuads(candidate.vertices);
      candidate.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    } else if (rect_list) {
      if (!ExpandRectListToTriangles(candidate.vertices, draw.index_count,
                                     candidate.input_layout_mask)) {
        continue;
      }
      candidate.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
    if (draw.indexed) {
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
      candidate.vertex_count = static_cast<uint32_t>(decoded_indices.size());
    } else {
      const uint32_t vertex_count =
          (point_list || rect_list) ? static_cast<uint32_t>(candidate.vertices.size())
                                    : draw.index_count;
      if (vertex_count > candidate.vertices.size()) {
        continue;
      }
      candidate.uses_32bit_indices = false;
      candidate.vertex_count = vertex_count;
    }
    prepared = std::move(candidate);
    return true;
  }
  return false;
}

std::string DescribeRealDrawGeometrySupport(const ReplayCapture &capture,
                                            std::size_t index,
                                            bool allow_non_indexed) {
  if (index >= capture.draws.size()) {
    return "draw index is out of range";
  }

  const ReplayDrawState &state = capture.draws[index];
  const PM4DrawRecord &draw = state.draw;
  const D3D12_PRIMITIVE_TOPOLOGY topology =
      TopologyForPrimitive(draw.primitive_type);
  if (!draw.indexed && !allow_non_indexed) {
    return "non-indexed draw requires explicit draw/frame replay";
  }
  if (draw.vertex_fetches.empty()) {
    if (SupportsVertexlessDraw(state)) {
      return {};
    }
    if (IsNoSideEffectNoFetchDraw(state)) {
      return "no captured vertex/fetch state, but render state has no modeled "
             "color/depth/stencil side effects";
    }
    if (state.vertex_shader.hash == 0xB6C9863F710683ECull &&
        state.pixel_shader.hash == 0xA4A965C189287B99ull &&
        TopologyForPrimitive(draw.primitive_type) ==
            D3D_PRIMITIVE_TOPOLOGY_POINTLIST) {
      return "B6C986/A4A965 no-fetch point shader currently exports zeroed "
             "r0; needs Xenos register initialization semantics before it "
             "counts as real scene rendering";
    }
    if (state.vertex_shader.hash == 0xDDED7E538422AE73ull &&
        TopologyForPrimitive(draw.primitive_type) ==
            D3D_PRIMITIVE_TOPOLOGY_POINTLIST) {
      return "DDED7E no-fetch point shader needs Xenos r0/register "
             "initialization semantics before D3D12 can draw it";
    }
    return "draw has no captured vertex/fetch state";
  }
  if (topology != D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST &&
      topology != D3D_PRIMITIVE_TOPOLOGY_POINTLIST) {
    return "unsupported primitive topology " +
           std::to_string(draw.primitive_type);
  }
  if (IsAb1eTextureInterpolatorBlocker(state)) {
    return "AB1E texture pass pixel shader reads interpolator r0/r1, but the "
           "captured AB1E vertex shader writes oPos only; needs Xenos "
           "interpolator/register semantics before it counts as scene "
           "rendering";
  }
  if (draw.indexed) {
    if (draw.index_payload_missing) {
      return "indexed draw has no captured index payload";
    }
    if (draw.index_payload_truncated) {
      return "indexed draw index payload is truncated";
    }
    const std::vector<uint32_t> decoded_indices = DecodeReplayIndices(draw);
    if (decoded_indices.empty()) {
      return "indexed draw decoded no index values";
    }
    const uint32_t max_index =
        *std::max_element(decoded_indices.begin(), decoded_indices.end());
    bool saw_truncated_vertex_payload = false;
    uint32_t best_available_vertices = 0;
    for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
      if (fetch.payload_truncated) {
        saw_truncated_vertex_payload = true;
      }
      if (fetch.stride_bytes != 0) {
        best_available_vertices = std::max<uint32_t>(
            best_available_vertices,
            static_cast<uint32_t>(fetch.payload_bytes.size() /
                                  fetch.stride_bytes));
      }
      std::vector<RealReplayVertex> vertices;
      uint32_t input_layout_mask = 0;
      uint64_t input_layout_signature = 0;
      if (BuildCanonicalVertices(fetch, vertices, input_layout_mask,
                                 input_layout_signature) &&
          max_index < vertices.size()) {
        return {};
      }
    }
    if (saw_truncated_vertex_payload) {
      return "captured vertex payload is truncated; max_index=" +
             std::to_string(max_index) +
             " available_vertices=" +
             std::to_string(best_available_vertices);
    }
    return "decoded index range exceeds all captured vertex payloads";
  }

  if (draw.index_count == 0) {
    return "non-indexed draw has zero vertex count";
  }
  for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
    std::vector<RealReplayVertex> vertices;
    uint32_t input_layout_mask = 0;
    uint64_t input_layout_signature = 0;
    if (!BuildCanonicalVertices(fetch, vertices, input_layout_mask,
                                input_layout_signature)) {
      continue;
    }
    if (IsKnownZeroColorExportDraw(state, vertices, input_layout_mask)) {
      if (ShouldReplayAsDepthOnlyZeroColorDraw(state, vertices,
                                               input_layout_mask)) {
        return "";
      }
      if (IsAb1eA4ZeroColorFillDraw(state, vertices, input_layout_mask)) {
        return "";
      }
      return "A4 pass-through color export has no captured color/texture "
             "dependency or exports all zero; needs Xenos export/register "
             "semantics before it counts as scene rendering";
    }
    const uint32_t needed =
        topology == D3D_PRIMITIVE_TOPOLOGY_POINTLIST
            ? static_cast<uint32_t>(vertices.size())
            : draw.index_count;
    if (needed <= vertices.size()) {
      return {};
    }
  }
  return "draw has no decodable canonical vertex payload";
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
    if (PrepareRealDrawAtIndex(capture, i, true, prepared)) {
      return true;
    }
  }

  error =
      options.draw_index
          ? "selected draw has no complete vertex/index snapshot that "
            "the current D3D12 real replay path can bind"
          : "capture has no complete vertex/index snapshot that the "
            "current D3D12 real replay path can bind";
  return false;
}

std::vector<PreparedRealDraw> CollectSupportedRealDraws(
    const ReplayCapture &capture, const ReplayCliOptions &options,
    const PreparedRealDraw &first_draw) {
  std::vector<PreparedRealDraw> draws;
  draws.push_back(first_draw);
  if (options.d3d12_draw_limit <= 1) {
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
    if (PrepareRealDrawAtIndex(capture, i, first_draw.indexed == false,
                               prepared)) {
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

struct PipelineKey {
  uint64_t vertex_shader_hash = 0;
  uint64_t pixel_shader_hash = 0;
  uint32_t topology = 0;
  uint32_t render_target_format = 0;
  uint32_t render_state_present = 0;
  uint32_t rb_colorcontrol = 0;
  uint32_t rb_color_mask = 0;
  uint32_t rb_depthcontrol = 0;
  uint32_t rb_stencilrefmask = 0;
  uint32_t rb_stencilrefmask_bf = 0;
  uint32_t pa_cl_clip_cntl = 0;
  uint32_t pa_su_sc_mode_cntl = 0;
  uint32_t depth_format = 0;
  uint32_t color_format0 = 0;
  uint32_t blendcontrol0 = 0;
  uint32_t msaa_samples = 0;
  uint32_t depth_test_enable = 0;
  uint32_t depth_write_enable = 0;
  uint32_t stencil_enable = 0;
  uint32_t depth_func = 0;
  uint32_t cull_mode = 0;
  uint32_t fill_mode = 0;
  uint32_t front_face = 0;
  uint32_t input_layout_mask = 0;
  uint64_t input_layout_signature = 0;
  uint32_t forced_depth_only_color_mask = 0;

  bool operator<(const PipelineKey &other) const {
    return std::tie(vertex_shader_hash, pixel_shader_hash, topology,
                    render_target_format, render_state_present,
                    rb_colorcontrol, rb_color_mask, rb_depthcontrol,
                    rb_stencilrefmask, rb_stencilrefmask_bf, pa_cl_clip_cntl,
                    pa_su_sc_mode_cntl, depth_format, color_format0,
                    blendcontrol0, msaa_samples, depth_test_enable,
                    depth_write_enable, stencil_enable, depth_func, cull_mode,
                    fill_mode, front_face, input_layout_mask,
                    input_layout_signature, forced_depth_only_color_mask) <
           std::tie(other.vertex_shader_hash, other.pixel_shader_hash,
                    other.topology, other.render_target_format,
                    other.render_state_present, other.rb_colorcontrol,
                    other.rb_color_mask, other.rb_depthcontrol,
                    other.rb_stencilrefmask, other.rb_stencilrefmask_bf,
                    other.pa_cl_clip_cntl, other.pa_su_sc_mode_cntl,
                    other.depth_format, other.color_format0,
                    other.blendcontrol0, other.msaa_samples,
                    other.depth_test_enable, other.depth_write_enable,
                    other.stencil_enable, other.depth_func, other.cull_mode,
                    other.fill_mode, other.front_face, other.input_layout_mask,
                    other.input_layout_signature,
                    other.forced_depth_only_color_mask);
  }
};

using ShaderPairKey = std::tuple<uint64_t, uint64_t>;

uint32_t BoolKey(bool value) { return value ? 1u : 0u; }

uint32_t FirstOrZero(const std::vector<uint32_t> &values) {
  return values.empty() ? 0u : values.front();
}

PipelineKey MakePipelineKey(const ReplayDrawState &state,
                            const PreparedRealDraw &prepared,
                            DXGI_FORMAT render_target_format) {
  PipelineKey key;
  key.vertex_shader_hash = state.vertex_shader.hash;
  key.pixel_shader_hash = state.pixel_shader.hash;
  key.topology = static_cast<uint32_t>(prepared.topology);
  key.render_target_format = static_cast<uint32_t>(render_target_format);
  key.input_layout_mask = prepared.input_layout_mask;
  key.input_layout_signature = prepared.input_layout_signature;
  key.forced_depth_only_color_mask =
      BoolKey(prepared.force_depth_only_color_mask);
  const RenderStateRecord *render_state =
      state.draw.render_state.present ? &state.draw.render_state : nullptr;
  if (!render_state) {
    return key;
  }
  key.render_state_present = 1;
  key.rb_colorcontrol = render_state->rb_colorcontrol;
  key.rb_color_mask = render_state->rb_color_mask;
  key.rb_depthcontrol = render_state->rb_depthcontrol;
  key.rb_stencilrefmask = render_state->rb_stencilrefmask;
  key.rb_stencilrefmask_bf = render_state->rb_stencilrefmask_bf;
  key.pa_cl_clip_cntl = render_state->pa_cl_clip_cntl;
  key.pa_su_sc_mode_cntl = render_state->pa_su_sc_mode_cntl;
  key.depth_format = render_state->depth_format;
  key.color_format0 = FirstOrZero(render_state->color_format);
  key.blendcontrol0 = FirstOrZero(render_state->rb_blendcontrol);
  key.msaa_samples = render_state->msaa_samples;
  key.depth_test_enable = BoolKey(render_state->depth_test_enable);
  key.depth_write_enable = BoolKey(render_state->depth_write_enable);
  key.stencil_enable = BoolKey(render_state->stencil_enable);
  key.depth_func = render_state->depth_func;
  key.cull_mode = render_state->cull_mode;
  key.fill_mode = render_state->fill_mode;
  key.front_face = render_state->front_face;
  return key;
}

struct FrameRealReplayPlan {
  std::size_t frame_index = 0;
  std::size_t frame_draw_count = 0;
  std::size_t skipped_draw_count = 0;
  std::size_t elided_noop_draw_count = 0;
  bool noop_utility_frame = false;
  bool used_pre_frame_bucket = false;
  bool used_sequence_frame_bucket = false;
  uint64_t sequence_begin = 0;
  uint64_t sequence_end = 0;
  std::vector<PreparedRealDraw> supported_draws;
  std::map<std::string, std::size_t> unsupported_reasons;
};

uint64_t FrameBoundarySeq(const ReplayFrame &frame) {
  if (frame.has_end && frame.end_seq != 0) {
    return frame.end_seq;
  }
  return frame.begin_seq;
}

bool FindSequenceFrameDrawRange(const ReplayCapture &capture,
                                std::size_t frame_index, std::size_t &begin,
                                std::size_t &end, uint64_t &sequence_begin,
                                uint64_t &sequence_end) {
  if (frame_index >= capture.frames.size()) {
    return false;
  }

  sequence_begin = 0;
  if (frame_index != 0) {
    sequence_begin = FrameBoundarySeq(capture.frames[frame_index - 1]);
  }
  sequence_end = FrameBoundarySeq(capture.frames[frame_index]);
  if (sequence_end == 0 || sequence_end <= sequence_begin) {
    return false;
  }

  begin = capture.draws.size();
  end = begin;
  for (std::size_t draw_index = 0; draw_index < capture.draws.size();
       ++draw_index) {
    const uint64_t draw_seq = capture.draws[draw_index].seq;
    if (draw_seq <= sequence_begin) {
      continue;
    }
    if (draw_seq > sequence_end) {
      if (begin != capture.draws.size()) {
        break;
      }
      continue;
    }
    if (begin == capture.draws.size()) {
      begin = draw_index;
    }
    end = draw_index + 1;
  }

  return begin < end;
}

bool PrepareFrameRealReplayPlan(const ReplayCapture &capture,
                                const ReplayCliOptions &options,
                                FrameRealReplayPlan &plan,
                                std::string &error) {
  if (!options.frame_index) {
    error = "frame replay plan requested without --frame";
    return false;
  }

  std::size_t begin = 0;
  std::size_t end = 0;
  if (capture.frames.empty()) {
    if (*options.frame_index != 0) {
      error = "selected frame is out of range";
      return false;
    }
    plan.frame_index = 0;
    plan.frame_draw_count = capture.draws.size();
    plan.used_pre_frame_bucket = true;
    begin = 0;
    end = capture.draws.size();
  } else {
    if (*options.frame_index >= capture.frames.size()) {
      error = "selected frame is out of range";
      return false;
    }

    const ReplayFrame &frame = capture.frames[*options.frame_index];
    plan.frame_index = *options.frame_index;
    plan.frame_draw_count = frame.draw_count;
    const bool capture_has_frame_draws =
        std::any_of(capture.frames.begin(), capture.frames.end(),
                    [](const ReplayFrame &candidate) {
                      return candidate.draw_count != 0;
                    });
    begin = frame.first_draw_index;
    end = std::min<std::size_t>(begin + frame.draw_count, capture.draws.size());
    if (frame.draw_count == 0 && !capture_has_frame_draws) {
      uint64_t sequence_begin = 0;
      uint64_t sequence_end = 0;
      if (FindSequenceFrameDrawRange(capture, *options.frame_index, begin, end,
                                     sequence_begin, sequence_end)) {
        plan.frame_draw_count = end - begin;
        plan.used_sequence_frame_bucket = true;
        plan.sequence_begin = sequence_begin;
        plan.sequence_end = sequence_end;
      } else if (*options.frame_index == 0) {
        begin = 0;
        end = capture.draws.size();
        plan.frame_draw_count = capture.draws.size();
        plan.used_pre_frame_bucket = true;
      }
    }
  }

  for (std::size_t draw_index = begin; draw_index < end; ++draw_index) {
    const ReplayDrawState &state = capture.draws[draw_index];
    if (IsKnownIgnoredUtilityDraw(state)) {
      ++plan.elided_noop_draw_count;
      continue;
    }

    PreparedRealDraw prepared;
    if (PrepareRealDrawAtIndex(capture, draw_index, true, prepared)) {
      plan.supported_draws.push_back(std::move(prepared));
      if (plan.supported_draws.size() >= options.d3d12_draw_limit) {
        break;
      }
      continue;
    }

    std::string reason =
        DescribeRealDrawGeometrySupport(capture, draw_index, true);
    if (reason.empty()) {
      reason = "draw is unsupported by current D3D12 real replay path";
    }
    ++plan.skipped_draw_count;
    ++plan.unsupported_reasons[reason];
    if (!options.skip_unsupported) {
      error = "D3D12 frame replay strict failure at frame " +
              std::to_string(*options.frame_index) + " draw " +
              std::to_string(draw_index) + " event " +
              std::to_string(state.draw.event) + " VS=" +
              FormatHex64(state.vertex_shader.hash) + " PS=" +
              FormatHex64(state.pixel_shader.hash) + ": " + reason;
      return false;
    }
  }

  if (plan.supported_draws.empty()) {
    if (plan.frame_draw_count > 0 && plan.elided_noop_draw_count > 0 &&
        plan.skipped_draw_count == 0) {
      plan.noop_utility_frame = true;
      return true;
    }
    error = "selected frame has no draw with complete geometry currently "
            "supported by D3D12 real replay";
    return false;
  }
  return true;
}

bool CheckCapturedTextureSupport(const ReplayDrawState &state,
                                 std::string &reason) {
  const std::size_t texture_count =
      std::min<std::size_t>(state.draw.texture_fetches.size(),
                            kMaxRealReplayTextureSlots);
  for (std::size_t slot = 0; slot < texture_count; ++slot) {
    const TextureFetchRecord &fetch = state.draw.texture_fetches[slot];
    if (fetch.payload_truncated) {
      reason = "captured texture slot " + std::to_string(slot) +
               " payload is truncated; real replay requires a full "
               "resource snapshot";
      return false;
    }
    std::vector<uint8_t> texture_rgba;
    std::string texture_reason;
    if (DecodeTextureRgba8(fetch, texture_rgba, texture_reason)) {
      continue;
    }
    reason = "unsupported captured texture slot " + std::to_string(slot) +
             " format " +
             std::to_string(fetch.format);
    if (!texture_reason.empty()) {
      reason += ": " + texture_reason;
    }
    return false;
  }
  return true;
}

void CopyFramePlanDiagnosticsToLiveBinding(
    const FrameRealReplayPlan &frame_plan, D3D12LiveSubmitBinding &binding) {
  binding.skipped_draws =
      static_cast<uint64_t>(frame_plan.skipped_draw_count);
  binding.elided_noop_draws =
      static_cast<uint64_t>(frame_plan.elided_noop_draw_count);
  binding.unsupported_reasons.clear();
  binding.unsupported_reasons.reserve(frame_plan.unsupported_reasons.size());
  for (const auto &[reason, count] : frame_plan.unsupported_reasons) {
    binding.unsupported_reasons.emplace_back(
        reason, static_cast<uint64_t>(count));
  }
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
    AppendDeviceRemovedReason(device, error);
    return false;
  }

  void *mapped = nullptr;
  D3D12_RANGE read_range{0, 0};
  if (!CheckHr(resource->Map(0, &read_range, &mapped), "ID3D12Resource::Map",
               error)) {
    AppendDeviceRemovedReason(device, error);
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

void DecodeRgb565(uint16_t value, uint8_t color[3]) {
  const uint32_t r = (value >> 11) & 0x1F;
  const uint32_t g = (value >> 5) & 0x3F;
  const uint32_t b = value & 0x1F;
  color[0] = static_cast<uint8_t>((r << 3) | (r >> 2));
  color[1] = static_cast<uint8_t>((g << 2) | (g >> 4));
  color[2] = static_cast<uint8_t>((b << 3) | (b >> 2));
}

void StoreRgba(std::vector<uint8_t> &rgba, uint32_t width, uint32_t height,
               uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b,
               uint8_t a) {
  if (x >= width || y >= height) {
    return;
  }
  const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
  rgba[pixel * 4 + 0] = r;
  rgba[pixel * 4 + 1] = g;
  rgba[pixel * 4 + 2] = b;
  rgba[pixel * 4 + 3] = a;
}

void DecodeDxtColorBlock(const std::vector<uint8_t> &bytes,
                         std::size_t offset, bool allow_1bit_alpha,
                         uint32_t endian, uint8_t colors[16][4]) {
  const uint16_t c0 =
      GpuSwap16(LoadLittleEndian16(bytes, offset + 0), endian);
  const uint16_t c1 =
      GpuSwap16(LoadLittleEndian16(bytes, offset + 2), endian);
  uint8_t palette[4][4]{};
  DecodeRgb565(c0, palette[0]);
  DecodeRgb565(c1, palette[1]);
  palette[0][3] = 255;
  palette[1][3] = 255;
  if (c0 > c1 || !allow_1bit_alpha) {
    for (uint32_t i = 0; i < 3; ++i) {
      palette[2][i] =
          static_cast<uint8_t>((2 * palette[0][i] + palette[1][i]) / 3);
      palette[3][i] =
          static_cast<uint8_t>((palette[0][i] + 2 * palette[1][i]) / 3);
    }
    palette[2][3] = 255;
    palette[3][3] = 255;
  } else {
    for (uint32_t i = 0; i < 3; ++i) {
      palette[2][i] =
          static_cast<uint8_t>((palette[0][i] + palette[1][i]) / 2);
      palette[3][i] = 0;
    }
    palette[2][3] = 255;
    palette[3][3] = 0;
  }

  const uint32_t indices = LoadLittleEndian32(bytes, offset + 4);
  for (uint32_t i = 0; i < 16; ++i) {
    const uint32_t selector = (indices >> (i * 2)) & 0x3;
    for (uint32_t c = 0; c < 4; ++c) {
      colors[i][c] = palette[selector][c];
    }
  }
}

bool DecodeBlockCompressedTextureRgba8(const TextureFetchRecord &fetch,
                                       std::vector<uint8_t> &rgba,
                                       std::string &reason) {
  const bool dxt1 = fetch.format == 18;
  const bool dxt23 = fetch.format == 19;
  const bool dxt45 = fetch.format == 20;
  const bool dxn = fetch.format == 49;
  if (!dxt1 && !dxt23 && !dxt45 && !dxn) {
    return false;
  }

  const uint32_t block_bytes = dxt1 ? 8u : 16u;
  const uint32_t width_blocks = (fetch.width + 3) / 4;
  const uint32_t height_blocks = (fetch.height + 3) / 4;
  const uint32_t pitch_texels =
      fetch.pitch != 0 ? fetch.pitch << 5 : fetch.width;
  const uint32_t pitch_blocks = std::max<uint32_t>(1, (pitch_texels + 3) / 4);
  const uint32_t bytes_per_block_log2 = dxt1 ? 3u : 4u;
  rgba.assign(static_cast<std::size_t>(fetch.width) * fetch.height * 4, 0);
  bool used_truncated_preview = false;

  for (uint32_t by = 0; by < height_blocks; ++by) {
    for (uint32_t bx = 0; bx < width_blocks; ++bx) {
      const std::size_t source_offset =
          fetch.tiled
              ? XenosTiledOffset2D(bx, by, pitch_blocks, bytes_per_block_log2)
              : (static_cast<std::size_t>(by) * pitch_blocks + bx) *
                    block_bytes;
      if (source_offset + block_bytes > fetch.payload_bytes.size()) {
        if (!fetch.payload_truncated) {
          reason = "block-compressed texture payload is smaller than the "
                   "decoded footprint";
          return false;
        }
        used_truncated_preview = true;
        continue;
      }

      uint8_t colors[16][4]{};
      if (dxt1) {
        DecodeDxtColorBlock(fetch.payload_bytes, source_offset, true,
                            fetch.endian, colors);
      } else if (dxt23) {
        DecodeDxtColorBlock(fetch.payload_bytes, source_offset + 8, false,
                            fetch.endian, colors);
        for (uint32_t i = 0; i < 16; ++i) {
          const uint8_t alpha_nibble =
              (fetch.payload_bytes[source_offset + (i >> 1)] >>
               ((i & 1) * 4)) &
              0xF;
          colors[i][3] =
              static_cast<uint8_t>((alpha_nibble << 4) | alpha_nibble);
        }
      } else if (dxt45) {
        DecodeDxtColorBlock(fetch.payload_bytes, source_offset + 8, false,
                            fetch.endian, colors);
        const uint8_t alpha0 = fetch.payload_bytes[source_offset + 0];
        const uint8_t alpha1 = fetch.payload_bytes[source_offset + 1];
        uint8_t alpha_palette[8]{alpha0, alpha1};
        if (alpha0 > alpha1) {
          for (uint32_t i = 1; i <= 6; ++i) {
            alpha_palette[i + 1] = static_cast<uint8_t>(
                ((7 - i) * alpha0 + i * alpha1) / 7);
          }
        } else {
          for (uint32_t i = 1; i <= 4; ++i) {
            alpha_palette[i + 1] = static_cast<uint8_t>(
                ((5 - i) * alpha0 + i * alpha1) / 5);
          }
          alpha_palette[6] = 0;
          alpha_palette[7] = 255;
        }
        uint64_t alpha_bits = 0;
        for (uint32_t i = 0; i < 6; ++i) {
          alpha_bits |= uint64_t(fetch.payload_bytes[source_offset + 2 + i])
                        << (i * 8);
        }
        for (uint32_t i = 0; i < 16; ++i) {
          colors[i][3] = alpha_palette[(alpha_bits >> (i * 3)) & 0x7];
        }
      } else {
        // DXN stores two BC4 alpha-style channels. Use them as RG and provide
        // a neutral B/A preview so strict real replay can bind the resource.
        uint8_t channels[2][16]{};
        for (uint32_t channel = 0; channel < 2; ++channel) {
          const std::size_t channel_offset = source_offset + channel * 8;
          const uint8_t a0 = fetch.payload_bytes[channel_offset + 0];
          const uint8_t a1 = fetch.payload_bytes[channel_offset + 1];
          uint8_t palette[8]{a0, a1};
          if (a0 > a1) {
            for (uint32_t i = 1; i <= 6; ++i) {
              palette[i + 1] =
                  static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
            }
          } else {
            for (uint32_t i = 1; i <= 4; ++i) {
              palette[i + 1] =
                  static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
            }
            palette[6] = 0;
            palette[7] = 255;
          }
          uint64_t bits = 0;
          for (uint32_t i = 0; i < 6; ++i) {
            bits |= uint64_t(fetch.payload_bytes[channel_offset + 2 + i])
                    << (i * 8);
          }
          for (uint32_t i = 0; i < 16; ++i) {
            channels[channel][i] = palette[(bits >> (i * 3)) & 0x7];
          }
        }
        for (uint32_t i = 0; i < 16; ++i) {
          colors[i][0] = channels[0][i];
          colors[i][1] = channels[1][i];
          colors[i][2] = 128;
          colors[i][3] = 255;
        }
      }

      for (uint32_t py = 0; py < 4; ++py) {
        for (uint32_t px = 0; px < 4; ++px) {
          const uint32_t i = py * 4 + px;
          StoreRgba(rgba, fetch.width, fetch.height, bx * 4 + px, by * 4 + py,
                    colors[i][0], colors[i][1], colors[i][2], colors[i][3]);
        }
      }
    }
  }

  if (used_truncated_preview) {
    reason = "block-compressed texture decoded from truncated preview";
  } else {
    reason.clear();
  }
  return true;
}

bool DecodeTextureRgba8(const TextureFetchRecord &fetch,
                        std::vector<uint8_t> &rgba, std::string &reason) {
  rgba.clear();
  if (fetch.payload_missing || fetch.payload_bytes.empty()) {
    reason = "texture payload is missing";
    return false;
  }
  if (fetch.format == 18 || fetch.format == 19 || fetch.format == 20 ||
      fetch.format == 49) {
    return DecodeBlockCompressedTextureRgba8(fetch, rgba, reason);
  }
  if (fetch.format != 6 && fetch.format != 2 && fetch.format != 7 &&
      fetch.format != 23 && fetch.format != 26 && fetch.format != 28 &&
      fetch.format != 38 && fetch.format != 54) {
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
  uint32_t bytes_per_texel = 4;
  uint32_t bytes_per_block_log2 = 2;
  if (fetch.format == 2) {
    bytes_per_texel = 1;
    bytes_per_block_log2 = 0;
  } else if (fetch.format == 23) {
    bytes_per_texel = 4;
    bytes_per_block_log2 = 2;
  } else if (fetch.format == 28) {
    bytes_per_texel = 4;
    bytes_per_block_log2 = 2;
  } else if (fetch.format == 26) {
    bytes_per_texel = 8;
    bytes_per_block_log2 = 3;
  } else if (fetch.format == 38) {
    bytes_per_texel = 16;
    bytes_per_block_log2 = 4;
  }
  if (!fetch.tiled) {
    const std::size_t linear_footprint =
        (static_cast<std::size_t>(pitch_texels) * (fetch.height - 1) +
         fetch.width) *
        bytes_per_texel;
    if (linear_footprint > fetch.payload_bytes.size() &&
        !fetch.payload_truncated) {
      const std::size_t missing_tail =
          linear_footprint - fetch.payload_bytes.size();
      const std::size_t tolerated_tail =
          std::max<std::size_t>(bytes_per_texel * 64u, 256u);
      if (missing_tail > tolerated_tail) {
        reason =
            "linear texture payload is smaller than the decoded footprint";
        return false;
      }
    }
  }

  rgba.resize(output_bytes);
  bool used_truncated_preview = false;
  for (uint32_t y = 0; y < fetch.height; ++y) {
    for (uint32_t x = 0; x < fetch.width; ++x) {
      const std::size_t pixel =
          static_cast<std::size_t>(y) * fetch.width + x;
      const std::size_t source_offset =
          fetch.tiled
              ? XenosTiledOffset2D(x, y, pitch_texels, bytes_per_block_log2)
              : (static_cast<std::size_t>(y) * pitch_texels + x) *
                    bytes_per_texel;
      if (source_offset + bytes_per_texel > fetch.payload_bytes.size()) {
        if (!fetch.payload_truncated) {
          const std::size_t missing_tail =
              source_offset + bytes_per_texel - fetch.payload_bytes.size();
          const std::size_t tolerated_tail =
              std::max<std::size_t>(bytes_per_texel * 64u, 256u);
          if (missing_tail > tolerated_tail) {
            reason = "texture payload is smaller than the decoded footprint";
            return false;
          }
        }
        if (!fetch.payload_truncated && source_offset < fetch.payload_bytes.size()) {
          reason = "texture payload is smaller than the decoded footprint";
          return false;
        }
        rgba[pixel * 4 + 0] = 0;
        rgba[pixel * 4 + 1] = 0;
        rgba[pixel * 4 + 2] = 0;
        rgba[pixel * 4 + 3] = 255;
        used_truncated_preview = true;
        continue;
      }

      if (fetch.format == 2) {
        const uint8_t value = fetch.payload_bytes[source_offset];
        rgba[pixel * 4 + 0] = value;
        rgba[pixel * 4 + 1] = value;
        rgba[pixel * 4 + 2] = value;
        rgba[pixel * 4 + 3] = value;
        continue;
      } else if (fetch.format == 7 || fetch.format == 54) {
        const uint32_t word =
            GpuSwap32(LoadLittleEndian32(fetch.payload_bytes, source_offset),
                      fetch.endian);
        const uint32_t r = word & 0x3FFu;
        const uint32_t g = (word >> 10) & 0x3FFu;
        const uint32_t b = (word >> 20) & 0x3FFu;
        const uint32_t a = (word >> 30) & 0x3u;
        rgba[pixel * 4 + 0] = static_cast<uint8_t>((r * 255u + 511u) / 1023u);
        rgba[pixel * 4 + 1] = static_cast<uint8_t>((g * 255u + 511u) / 1023u);
        rgba[pixel * 4 + 2] = static_cast<uint8_t>((b * 255u + 511u) / 1023u);
        rgba[pixel * 4 + 3] = static_cast<uint8_t>((a * 255u + 1u) / 3u);
        continue;
      } else if (fetch.format == 23) {
        const uint32_t word =
            GpuSwap32(LoadLittleEndian32(fetch.payload_bytes, source_offset),
                      fetch.endian);
        const uint32_t depth24 = word & 0x00FFFFFFu;
        const uint32_t stencil = (word >> 24) & 0xFFu;
        const uint8_t depth8 =
            static_cast<uint8_t>((depth24 * 255ull + 0x7FFFFFull) /
                                 0xFFFFFFull);
        rgba[pixel * 4 + 0] = depth8;
        rgba[pixel * 4 + 1] = depth8;
        rgba[pixel * 4 + 2] = depth8;
        rgba[pixel * 4 + 3] = static_cast<uint8_t>(stencil);
        continue;
      } else if (fetch.format == 26) {
        const uint16_t r = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 0),
            fetch.endian);
        const uint16_t g = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 2),
            fetch.endian);
        const uint16_t b = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 4),
            fetch.endian);
        const uint16_t a = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 6),
            fetch.endian);
        rgba[pixel * 4 + 0] = static_cast<uint8_t>((r >> 8) & 0xFF);
        rgba[pixel * 4 + 1] = static_cast<uint8_t>((g >> 8) & 0xFF);
        rgba[pixel * 4 + 2] = static_cast<uint8_t>((b >> 8) & 0xFF);
        rgba[pixel * 4 + 3] = static_cast<uint8_t>((a >> 8) & 0xFF);
        continue;
      } else if (fetch.format == 28) {
        const uint16_t r = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 0),
            fetch.endian);
        const uint16_t g = GpuSwap16(
            LoadLittleEndian16(fetch.payload_bytes, source_offset + 2),
            fetch.endian);
        rgba[pixel * 4 + 0] = static_cast<uint8_t>((r >> 8) & 0xFF);
        rgba[pixel * 4 + 1] = static_cast<uint8_t>((g >> 8) & 0xFF);
        rgba[pixel * 4 + 2] = static_cast<uint8_t>((r >> 8) & 0xFF);
        rgba[pixel * 4 + 3] = 255;
        continue;
      } else if (fetch.format == 38) {
        const uint32_t raw_r = GpuSwap32(
            LoadLittleEndian32(fetch.payload_bytes, source_offset + 0),
            fetch.endian);
        const uint32_t raw_g = GpuSwap32(
            LoadLittleEndian32(fetch.payload_bytes, source_offset + 4),
            fetch.endian);
        const uint32_t raw_b = GpuSwap32(
            LoadLittleEndian32(fetch.payload_bytes, source_offset + 8),
            fetch.endian);
        const uint32_t raw_a = GpuSwap32(
            LoadLittleEndian32(fetch.payload_bytes, source_offset + 12),
            fetch.endian);
        const float r = std::clamp(FloatFromBits(raw_r), 0.0f, 1.0f);
        const float g = std::clamp(FloatFromBits(raw_g), 0.0f, 1.0f);
        const float b = std::clamp(FloatFromBits(raw_b), 0.0f, 1.0f);
        const float a = std::clamp(FloatFromBits(raw_a), 0.0f, 1.0f);
        rgba[pixel * 4 + 0] = static_cast<uint8_t>(r * 255.0f);
        rgba[pixel * 4 + 1] = static_cast<uint8_t>(g * 255.0f);
        rgba[pixel * 4 + 2] = static_cast<uint8_t>(b * 255.0f);
        rgba[pixel * 4 + 3] = static_cast<uint8_t>(a * 255.0f);
        continue;
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
  reason =
      used_truncated_preview
          ? "decoded partial/truncated format-" + std::to_string(fetch.format) +
                " texture preview"
          : (fetch.format == 23
                 ? "decoded format-23 depth/stencil texture as RGBA8 preview"
                 : (fetch.format == 54
                        ? "decoded format-54 2_10_10_10 texture as RGBA8 preview"
                        : ""));
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
    AppendDeviceRemovedReason(device, error);
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
    AppendDeviceRemovedReason(device, error);
    return false;
  }

  void *mapped = nullptr;
  D3D12_RANGE read_range{0, 0};
  if (!CheckHr(upload->Map(0, &read_range, &mapped),
               "ID3D12Resource::Map(texture upload)", error)) {
    AppendDeviceRemovedReason(device, error);
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

bool UploadLinearRgba8ToTexture(ID3D12Device *device,
                                ID3D12GraphicsCommandList *list,
                                ID3D12Resource *texture, const uint8_t *rgba,
                                uint32_t width, uint32_t height,
                                D3D12_RESOURCE_STATES state_before,
                                D3D12_RESOURCE_STATES state_after,
                                ComPtr<ID3D12Resource> &upload,
                                std::string &error) {
  if (!device || !list || !texture || !rgba || width == 0 || height == 0) {
    error = "UploadLinearRgba8ToTexture called with empty input";
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
               "ID3D12Device::CreateCommittedResource(texture seed upload)",
               error)) {
    AppendDeviceRemovedReason(device, error);
    return false;
  }

  void *mapped = nullptr;
  D3D12_RANGE read_range{0, 0};
  if (!CheckHr(upload->Map(0, &read_range, &mapped),
               "ID3D12Resource::Map(texture seed upload)", error)) {
    AppendDeviceRemovedReason(device, error);
    return false;
  }
  uint8_t *mapped_bytes = static_cast<uint8_t *>(mapped);
  for (uint32_t row = 0; row < height; ++row) {
    std::memcpy(mapped_bytes + static_cast<std::size_t>(row) * row_pitch,
                rgba + static_cast<std::size_t>(row) * row_bytes, row_bytes);
  }
  D3D12_RANGE written_range{0, static_cast<SIZE_T>(upload_size)};
  upload->Unmap(0, &written_range);

  if (state_before != D3D12_RESOURCE_STATE_COPY_DEST) {
    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource = texture;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy.Transition.StateBefore = state_before;
    to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &to_copy);
  }

  D3D12_TEXTURE_COPY_LOCATION src{};
  src.pResource = upload.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  src.PlacedFootprint.Footprint.Width = width;
  src.PlacedFootprint.Footprint.Height = height;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = row_pitch;

  D3D12_TEXTURE_COPY_LOCATION dst{};
  dst.pResource = texture;
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;
  list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  if (state_after != D3D12_RESOURCE_STATE_COPY_DEST) {
    D3D12_RESOURCE_BARRIER from_copy{};
    from_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    from_copy.Transition.pResource = texture;
    from_copy.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    from_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    from_copy.Transition.StateAfter = state_after;
    list->ResourceBarrier(1, &from_copy);
  }

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
  // Captured texture upload currently materializes only the base level.
  // Clamp sampling to mip 0 until packed mip capture/decode/upload is wired.
  sampler.MaxLOD = 0.0f;
  device->CreateSampler(&sampler, descriptor);
}

bool CreateRealGeometryPipeline(
    ID3D12Device *device, ComPtr<ID3D12RootSignature> &root_signature,
    ComPtr<ID3D12PipelineState> &pipeline_state, DXGI_FORMAT format,
    const RenderStateRecord *render_state, const char *vertex_shader_source,
    const char *vertex_shader_name, const char *vertex_shader_entry,
    const char *vertex_shader_profile,
    const std::filesystem::path &vertex_cache_path,
    const std::filesystem::path &vertex_log_path,
    const char *pixel_shader_source, const char *pixel_shader_name,
    const char *pixel_shader_entry, const char *pixel_shader_profile,
    const std::filesystem::path &pixel_cache_path,
    const std::filesystem::path &pixel_log_path, uint32_t input_layout_mask,
    bool force_depth_only_color_mask, std::string &error) {
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
  root_parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameters[1].Descriptor.ShaderRegister = 1;
  root_parameters[1].Descriptor.RegisterSpace = 0;
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

  std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements;
  if (input_layout_mask & (1 << 0)) {
    input_elements.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                              offsetof(RealReplayVertex, position),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
  }
  if (input_layout_mask & (1 << 1)) {
    input_elements.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                              offsetof(RealReplayVertex, color),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
  }
  if (input_layout_mask & (1 << 2)) {
    input_elements.push_back({"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                              offsetof(RealReplayVertex, uv),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
  }
  if (input_layout_mask & (1 << 3)) {
    input_elements.push_back({"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                              offsetof(RealReplayVertex, normal),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
  }
  if (input_layout_mask & (1 << 4)) {
    input_elements.push_back({"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
                              offsetof(RealReplayVertex, uv1),
                              D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
  }
  if (input_elements.empty()) {
    // Fallback to empty
  }

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
  pso_desc.pRootSignature = root_signature.Get();
  pso_desc.VS = {vertex_shader->GetBufferPointer(),
                 vertex_shader->GetBufferSize()};
  pso_desc.PS = {pixel_shader->GetBufferPointer(),
                 pixel_shader->GetBufferSize()};
  pso_desc.BlendState = BlendDescFromRenderState(render_state);
  if (force_depth_only_color_mask) {
    pso_desc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
  }
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = RasterizerDescFromRenderState(render_state);
  pso_desc.DepthStencilState = DepthStencilDescFromRenderState(render_state);
  pso_desc.InputLayout = {
      input_elements.empty() ? nullptr : input_elements.data(),
      static_cast<UINT>(input_elements.size())};
  pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pso_desc.NumRenderTargets = 1;
  pso_desc.RTVFormats[0] = format;
  pso_desc.DSVFormat = DsvFormatForRenderState(render_state);
  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleDesc.Quality = 0;

  const HRESULT pso_hr = device->CreateGraphicsPipelineState(
      &pso_desc, IID_PPV_ARGS(&pipeline_state));
  if (FAILED(pso_hr)) {
    error =
        HrError("ID3D12Device::CreateGraphicsPipelineState(real geometry)",
                pso_hr);
    AppendDeviceRemovedReason(device, error);
    AppendD3D12InfoQueueMessages(device, error);
    return false;
  }
  return true;
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

std::filesystem::path ResolveCacheRecordPath(const std::filesystem::path &root,
                                             const std::filesystem::path &path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }

  std::error_code ec;
  const std::filesystem::path cwd_relative =
      std::filesystem::absolute(path, ec);
  if (!ec && std::filesystem::exists(cwd_relative, ec) && !ec) {
    return cwd_relative;
  }

  std::filesystem::path project_root;
  if (root.filename() == "cache" && root.parent_path().filename() == "shader_work") {
    project_root = root.parent_path().parent_path();
  }
  if (!project_root.empty()) {
    const std::filesystem::path project_relative = project_root / path;
    if (std::filesystem::exists(project_relative, ec) && !ec) {
      return project_relative;
    }
  }

  const std::filesystem::path root_relative = root / path;
  if (std::filesystem::exists(root_relative, ec) && !ec) {
    return root_relative;
  }
  return path;
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

    for (auto record_it = records.rbegin(); record_it != records.rend();
         ++record_it) {
      const ShaderCacheRecord &record = *record_it;
      if (record.diagnostic || LowerAscii(record.backend) != "d3d12" ||
          record.runtime_hash != hash ||
          !StageMatches(record.stage, short_stage, long_stage)) {
        continue;
      }
      const std::string format = LowerAscii(record.format);
      if (!format.empty() && format != "dxbc" && format != "dxil") {
        continue;
      }
      path = ResolveCacheRecordPath(root, record.path);
      source = ResolveCacheRecordPath(root, record.source);
      if (!record.entry.empty()) {
        entry = record.entry;
      }
      profile = record.profile;
      cache_key = record.cache_key;
      if (source.filename().string().find(".translated.") != std::string::npos ||
          source.filename().string().find(".vertexless.") != std::string::npos) {
        entry = "main";
      }
      return true;
    }
  }
  const std::filesystem::path d3d12_root = root / "d3d12";
  if (std::filesystem::is_directory(d3d12_root, ec) && !ec) {
    const std::string stage_prefix =
        std::string(short_stage[0] == 'v' ? "VS_" : "PS_") +
        FormatHex64(hash) + ".";
    std::filesystem::path best_path;
    for (const std::filesystem::directory_entry &entry :
         std::filesystem::directory_iterator(d3d12_root, ec)) {
      if (ec || !entry.is_regular_file()) {
        continue;
      }
      const std::string filename = entry.path().filename().string();
      if (filename.rfind(stage_prefix, 0) != 0) {
        continue;
      }
      if (filename.ends_with(".d3dcompile.dxbc") ||
          filename.ends_with(".dxbc")) {
        if (best_path.empty() || filename > best_path.filename().string()) {
          best_path = entry.path();
        }
      }
    }
    if (!best_path.empty()) {
      path = best_path;
      std::string stem = best_path.filename().string();
      if (stem.ends_with(".d3dcompile.dxbc")) {
        stem.resize(stem.size() - std::strlen(".d3dcompile.dxbc"));
      } else if (stem.ends_with(".dxbc")) {
        stem.resize(stem.size() - std::strlen(".dxbc"));
      }
      cache_key = stem;
      source = root / "hlsl" / (stem + ".hlsl");
      if (!std::filesystem::is_regular_file(source, ec) || ec) {
        source.clear();
      }
      profile = std::string(short_stage) + "_5_0";
      if (stem.find(".translated.") != std::string::npos ||
          stem.find(".vertexless.") != std::string::npos) {
        entry = "main";
      }
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
  constexpr const char *kBindingLayoutVersion = "layout8";
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

    if (!override_path.empty()) {
      if (!ReadTextFile(override_path, source, stage_error)) {
        return false;
      }
      source_path = override_path;
      cache_key = MakeOverrideCacheKey(short_stage, hash, profile, source);
      cache_path =
          options.shader_cache_root / "d3d12" / (cache_key + ".dxbc");
      log_path = options.shader_cache_root / "logs" / (cache_key + ".log");
      return true;
    }

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
        if (!source.empty() && cache_key.rfind("manual_", 0) == 0) {
          const std::string source_cache_key =
              MakeOverrideCacheKey(short_stage, hash, profile, source);
          if (source_cache_key != cache_key) {
            cache_key = source_cache_key;
            cache_path = options.shader_cache_root / "d3d12" /
                         (cache_key + ".dxbc");
            log_path = options.shader_cache_root / "logs" /
                       (cache_key + ".log");
          }
        }
        if (!source.empty() && cache_path.extension() == ".dxil") {
          const std::string d3dcompile_profile =
              std::string(short_stage) + "_5_0";
          profile = d3dcompile_profile;
          cache_path = options.shader_cache_root / "d3d12" /
                       (cache_key + ".d3dcompile.dxbc");
          log_path = options.shader_cache_root / "logs" /
                     (cache_key + ".d3dcompile.log");
        }
      }
      return true;
    }
    if (!cache_error.empty()) {
      stage_error = cache_error;
      return false;
    }

    stage_error = "no translated, cached, or override shader is available "
                  "for " +
                  std::string(long_stage) + " shader " + FormatHex64(hash);
    return false;
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

  auto apply_vertex_variant =
      [&](const char *cache_key, const char *source_name) -> bool {
    pair.vertex_cache_key = cache_key;
    pair.vertex_path = options.shader_cache_root / "hlsl" / source_name;
    pair.vertex_cache_path = options.shader_cache_root / "d3d12" /
                             (std::string(cache_key) + ".d3dcompile.dxbc");
    pair.vertex_log_path = options.shader_cache_root / "logs" /
                           (std::string(cache_key) + ".d3dcompile.log");
    pair.vertex_entry = "main";
    pair.vertex_profile = "vs_5_0";
    pair.vertex_source.clear();
    if (!ReadTextFile(pair.vertex_path, pair.vertex_source, vertex_error)) {
      error = "could not load pair-specific vertex shader variant for draw " +
              std::to_string(draw_state.draw_index) + " VS=" +
              FormatHex64(draw_state.vertex_shader.hash) + " PS=" +
              FormatHex64(draw_state.pixel_shader.hash) + ": " + vertex_error;
      return false;
    }
    return true;
  };

  if (SupportsVertexlessDraw(draw_state)) {
    if (!apply_vertex_variant(
            "VS_0xB6C9863F710683EC.vertexless.v8.dxc",
            "VS_0xB6C9863F710683EC.vertexless.v8.dxc.hlsl")) {
      return false;
    }
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

  const bool ab1e_vertex_shader =
      draw_state.vertex_shader.hash == 0xAB1E86137A0240E8ull;
  auto apply_pixel_variant =
      [&](const char *cache_key, const char *source_name) -> bool {
    pair.pixel_cache_key = cache_key;
    pair.pixel_path = options.shader_cache_root / "hlsl" / source_name;
    pair.pixel_cache_path = options.shader_cache_root / "d3d12" /
                            (std::string(cache_key) + ".d3dcompile.dxbc");
    pair.pixel_log_path = options.shader_cache_root / "logs" /
                          (std::string(cache_key) + ".d3dcompile.log");
    pair.pixel_entry = "main";
    pair.pixel_profile = "ps_5_0";
    pair.pixel_source.clear();
    if (!ReadTextFile(pair.pixel_path, pair.pixel_source, pixel_error)) {
      error = "could not load pair-specific pixel shader variant for draw " +
              std::to_string(draw_state.draw_index) + " VS=" +
              FormatHex64(draw_state.vertex_shader.hash) + " PS=" +
              FormatHex64(draw_state.pixel_shader.hash) + ": " + pixel_error;
      return false;
    }
    return true;
  };

  auto apply_inline_pixel_variant =
      [&](const char *cache_key, const char *source_name,
          const char *source) -> bool {
    pair.pixel_cache_key = cache_key;
    pair.pixel_path = options.shader_cache_root / "hlsl" / source_name;
    pair.pixel_cache_path = options.shader_cache_root / "d3d12" /
                            (std::string(cache_key) + ".d3dcompile.dxbc");
    pair.pixel_log_path = options.shader_cache_root / "logs" /
                          (std::string(cache_key) + ".d3dcompile.log");
    pair.pixel_entry = "main";
    pair.pixel_profile = "ps_5_0";
    pair.pixel_source = source ? source : "";
    return true;
  };

  if (draw_state.pixel_shader.hash == 0xC4ED2979F29C9139ull) {
    if (ab1e_vertex_shader) {
      if (!apply_pixel_variant(
              "PS_0xC4ED2979F29C9139.translated.v7.dxc",
              "PS_0xC4ED2979F29C9139.translated.v7.dxc.hlsl")) {
        return false;
      }
    } else if (!apply_pixel_variant(
                   "PS_0xC4ED2979F29C9139.translated.v5.dxc",
                   "PS_0xC4ED2979F29C9139.translated.v5.dxc.hlsl")) {
      return false;
    }
  } else if (draw_state.pixel_shader.hash == 0x246E20EF10E0DDC7ull) {
    if (!apply_pixel_variant(
            "PS_0x246E20EF10E0DDC7.translated.v8.dxc",
            "PS_0x246E20EF10E0DDC7.translated.v8.dxc.hlsl")) {
      return false;
    }
  } else if (ab1e_vertex_shader &&
             draw_state.pixel_shader.hash == 0xA4A965C189287B99ull) {
    static constexpr const char *kAb1eA4ZeroPixelShader = R"(
// BO2 native renderer pair-specific zero-output PS.
// VS 0xAB1E86137A0240E8 exports position only; it does not declare an
// interpolator feeding A4's r0 input. Replaying this pair with the generic A4
// r0 passthrough produced an unproven diagonal fullscreen artifact.
struct PSInput
{
  float4 position : SV_Position;
};

float4 main(PSInput input) : SV_Target0
{
  return input.position.xxxx * 0.0f;
}
)";
    apply_inline_pixel_variant(
        "PS_0xA4A965C189287B99.ab1e_zero.v1.dxc",
        "PS_0xA4A965C189287B99.ab1e_zero.v1.dxc.hlsl",
        kAb1eA4ZeroPixelShader);
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
  std::string cache_read_error;
  if (!cache_path.empty()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(cache_path, ec) && !ec &&
        ReadCachedShaderBlob(cache_path, blob, error)) {
      return true;
    }
    if (!error.empty()) {
      cache_read_error = error;
      error.clear();
    }
  }

  if (!source || source[0] == '\0') {
    error =
        cache_read_error.empty()
            ? "shader cache miss and no source is available for " +
                  std::string(source_name ? source_name : "<unknown>")
            : cache_read_error + "; no source is available to rebuild it";
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
      if (!cache_read_error.empty()) {
        log << "cache_rebuilt_after=" << cache_read_error << "\n";
      }
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

  std::vector<ShaderCacheRecord> records;
  if (std::filesystem::is_regular_file(index_path, ec) && !ec) {
    std::string load_error;
    if (!LoadShaderCacheIndex(index_path, records, load_error)) {
      error = load_error;
      return false;
    }
  }

  auto upsert = [&](const char *stage, uint64_t hash,
                    const std::string &profile,
                    const std::string &cache_key,
                    const std::filesystem::path &path,
                    const std::filesystem::path &source) {
    records.erase(
        std::remove_if(records.begin(), records.end(),
                       [&](const ShaderCacheRecord &record) {
                         return LowerAscii(record.backend) == "d3d12" &&
                                StageMatches(record.stage, stage, stage) &&
                                record.runtime_hash == hash;
                       }),
        records.end());
    ShaderCacheRecord record;
    record.backend = "d3d12";
    record.stage = stage;
    record.runtime_hash = hash;
    record.profile = profile;
    record.compiler = "D3DCompile";
    record.format = "dxbc";
    record.cache_key = cache_key;
    record.path = path;
    record.source = source;
    records.push_back(std::move(record));
  };

  upsert("vertex", draw_state.vertex_shader.hash, pair.vertex_profile,
         pair.vertex_cache_key, pair.vertex_cache_path, pair.vertex_path);
  upsert("pixel", draw_state.pixel_shader.hash, pair.pixel_profile,
         pair.pixel_cache_key, pair.pixel_cache_path, pair.pixel_path);

  std::ofstream file(index_path, std::ios::binary | std::ios::trunc);
  if (!file) {
    error = "could not write shader cache index " + index_path.string();
    return false;
  }

  file << "{\n"
       << "  \"version\": 1,\n"
       << "  \"records\": [\n";
  for (std::size_t i = 0; i < records.size(); ++i) {
    const ShaderCacheRecord &record = records[i];
    file << "    {\n"
         << "      \"backend\": \"" << record.backend << "\",\n"
         << "      \"stage\": \"" << record.stage << "\",\n"
         << "      \"runtime_hash\": \"" << FormatHex64(record.runtime_hash)
         << "\",\n"
         << "      \"profile\": \"" << record.profile << "\",\n"
         << "      \"compiler\": \"" << record.compiler << "\",\n"
         << "      \"format\": \"" << record.format << "\",\n"
         << "      \"cache_key\": \"" << record.cache_key << "\",\n"
         << "      \"path\": \"" << record.path.generic_string()
         << "\",\n"
         << "      \"source\": \"" << record.source.generic_string()
         << "\"\n"
         << "    }" << (i + 1 == records.size() ? "\n" : ",\n");
  }
  file << "  ]\n"
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
    // Xenos PM4 constant upload indices are byte-like register offsets in the
    // captured stream. The high BO2 shaders reference registers such as c252
    // and c255, so preserve sparse register slots instead of concatenating
    // uploads and making every translated shader read the wrong constants.
    if ((record->index & 0x7u) == 0) {
      const uint32_t first_constant = record->index >> 3;
      for (uint32_t dword_index = 0; dword_index < record->dwords.size();
           ++dword_index) {
        const uint32_t constant_index = first_constant + dword_index / 4;
        if (constant_index >= kCapturedFloat4ConstantCount) {
          break;
        }
        const uint32_t target_dword = constant_index * 4 + dword_index % 4;
        prepared.dwords[target_dword] = record->dwords[dword_index];
      }
      prepared.count = std::max<uint32_t>(
          prepared.count,
          std::min<uint32_t>(
              kCapturedConstantDwordCount,
              (first_constant * 4) +
                  static_cast<uint32_t>(record->dwords.size())));
      continue;
    }

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

std::filesystem::path DepthOutputPathFor(const ReplayCapture &capture,
                                         const ReplayCliOptions &options) {
  if (!options.d3d12_depth_output_path.empty()) {
    return options.d3d12_depth_output_path;
  }
  const std::filesystem::path color_output = OutputPathFor(capture, options);
  return color_output.parent_path() /
         (color_output.stem().string() + "-depth" + color_output.extension().string());
}

struct ReadbackSnapshotStats {
  std::size_t total_bytes = 0;
  std::size_t nonzero_bytes = 0;
};

ReadbackSnapshotStats CountNonZeroBytes(const uint8_t *data,
                                        std::size_t byte_count) {
  ReadbackSnapshotStats stats{};
  stats.total_bytes = byte_count;
  if (!data) {
    return stats;
  }
  for (std::size_t i = 0; i < byte_count; ++i) {
    if (data[i] != 0) {
      ++stats.nonzero_bytes;
    }
  }
  return stats;
}

bool WriteBmp(const std::filesystem::path &path, const uint8_t *mapped,
              uint32_t row_pitch, uint32_t width, uint32_t height,
              std::string &error);

bool WriteDepthPreviewBmp(const std::filesystem::path &path,
                          const uint8_t *mapped, uint32_t row_pitch,
                          uint32_t width, uint32_t height,
                          std::string &error) {
  if (!mapped || width == 0 || height == 0) {
    error = "WriteDepthPreviewBmp called with empty depth readback";
    return false;
  }
  std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 0);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t *src_row = mapped + static_cast<std::size_t>(row_pitch) * y;
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *pixel = src_row + static_cast<std::size_t>(x) * 4;
      const uint32_t packed = pixel[0] | (static_cast<uint32_t>(pixel[1]) << 8) |
                              (static_cast<uint32_t>(pixel[2]) << 16) |
                              (static_cast<uint32_t>(pixel[3]) << 24);
      const uint32_t depth24 = packed & 0x00FFFFFFu;
      const uint8_t depth8 =
          static_cast<uint8_t>((depth24 * 255ull + 0x7FFFFFull) / 0xFFFFFFull);
      const uint8_t stencil = static_cast<uint8_t>((packed >> 24) & 0xFFu);
      const std::size_t offset =
          (static_cast<std::size_t>(y) * width + x) * 4;
      rgba[offset + 0] = depth8;
      rgba[offset + 1] = depth8;
      rgba[offset + 2] = depth8;
      rgba[offset + 3] = stencil;
    }
  }
  return WriteBmp(path, rgba.data(), width * 4, width, height, error);
}

bool CreateReadbackBuffer(ID3D12Device *device, uint64_t total_size,
                          ComPtr<ID3D12Resource> &readback,
                          std::string &error) {
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

  return CheckHr(device->CreateCommittedResource(
                     &readback_heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                     IID_PPV_ARGS(&readback)),
                 "ID3D12Device::CreateCommittedResource(readback)", error);
}

bool CreateReplayRenderTarget(
    ID3D12Device *device, D3D12ReplayRenderTargetCache &cache,
    uint32_t guest_base, uint32_t width, uint32_t height, DXGI_FORMAT format,
    const D3D12_CLEAR_VALUE &clear_value, D3D12ReplayRenderTarget *&target,
    std::string &error) {
  auto existing = cache.targets.find(guest_base);
  if (existing != cache.targets.end()) {
    target = &existing->second;
    return true;
  }
  if (!cache.rtv_heap) {
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.NumDescriptors = 16;
    if (!CheckHr(device->CreateDescriptorHeap(&rtv_heap_desc,
                                              IID_PPV_ARGS(&cache.rtv_heap)),
                 "ID3D12Device::CreateDescriptorHeap(real RTV cache)", error)) {
      return false;
    }
  }
  if (cache.next_descriptor >= 16) {
    error = "D3D12 real replay exceeded offline render target cache capacity";
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

  D3D12_HEAP_PROPERTIES default_heap{};
  default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  default_heap.CreationNodeMask = 1;
  default_heap.VisibleNodeMask = 1;

  D3D12ReplayRenderTarget created{};
  created.guest_base = guest_base;
  if (!CheckHr(device->CreateCommittedResource(
                   &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                   D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                   IID_PPV_ARGS(&created.resource)),
               "ID3D12Device::CreateCommittedResource(real cached render "
               "target)",
               error)) {
    return false;
  }

  const UINT rtv_descriptor_size =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  created.rtv = cache.rtv_heap->GetCPUDescriptorHandleForHeapStart();
  created.rtv.ptr +=
      static_cast<SIZE_T>(cache.next_descriptor) * rtv_descriptor_size;
  ++cache.next_descriptor;
  device->CreateRenderTargetView(created.resource.Get(), nullptr, created.rtv);
  auto [it, inserted] = cache.targets.emplace(guest_base, std::move(created));
  (void)inserted;
  target = &it->second;
  return true;
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

struct D3D12LiveReplaySessionStorage {
  std::map<PipelineKey, D3D12ReplayPipeline> pipelines;
  std::vector<UploadedRealDraw> retained_draw_resources;
  ComPtr<ID3D12DescriptorHeap> retained_srv_heap;
  ComPtr<ID3D12DescriptorHeap> retained_sampler_heap;
  ComPtr<ID3D12Resource> color_accum_target;
  ComPtr<ID3D12DescriptorHeap> color_accum_rtv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE color_accum_rtv{};
  uint32_t color_accum_width = 0;
  uint32_t color_accum_height = 0;
  bool color_accum_ready = false;
  ComPtr<ID3D12Resource> depth_target;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  uint32_t depth_width = 0;
  uint32_t depth_height = 0;
  bool depth_ready = false;
};

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

  ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
    debug->EnableDebugLayer();
  }

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

bool DumpD3D12DecodedTexturePreview(const ReplayCapture &capture,
                                    const ReplayCliOptions &options,
                                    std::string &error) {
#if defined(_WIN32)
  if (!options.draw_index) {
    error = "--dump-texture requires --draw <index>";
    return false;
  }
  if (*options.draw_index >= capture.draws.size()) {
    error = "--draw index is out of range";
    return false;
  }

  const ReplayDrawState &state = capture.draws[*options.draw_index];
  const std::size_t slot = options.texture_slot.value_or(0);
  if (slot >= state.draw.texture_fetches.size()) {
    error = "texture slot " + std::to_string(slot) +
            " is not bound for draw " + std::to_string(*options.draw_index);
    return false;
  }

  const TextureFetchRecord &fetch = state.draw.texture_fetches[slot];
  std::vector<uint8_t> rgba;
  std::string decode_reason;
  if (!DecodeTextureRgba8(fetch, rgba, decode_reason)) {
    error = "could not decode draw " + std::to_string(*options.draw_index) +
            " texture slot " + std::to_string(slot) + " format " +
            std::to_string(fetch.format) + ": " + decode_reason;
    return false;
  }
  if (rgba.empty() || fetch.width == 0 || fetch.height == 0) {
    error = "decoded texture preview is empty";
    return false;
  }

  std::size_t nonzero_bytes = 0;
  std::size_t rgb_nonzero_pixels = 0;
  std::array<uint64_t, 4> sums{};
  for (std::size_t i = 0; i < rgba.size(); ++i) {
    if (rgba[i] != 0) {
      ++nonzero_bytes;
    }
    sums[i & 3u] += rgba[i];
  }
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    if ((rgba[i] | rgba[i + 1] | rgba[i + 2]) != 0) {
      ++rgb_nonzero_pixels;
    }
  }

  std::filesystem::path output = options.texture_output_path;
  if (output.empty()) {
    std::ostringstream name;
    name << "native-renderer-draw" << *options.draw_index << "-texture"
         << slot << ".bmp";
    output = capture.path.parent_path() / name.str();
  }
  std::error_code ec;
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
      error = "could not create texture preview directory: " + ec.message();
      return false;
    }
  }
  if (!WriteBmp(output, rgba.data(), fetch.width * 4u, fetch.width,
                fetch.height, error)) {
    return false;
  }

  std::cout << "D3D12 decoded texture preview draw=" << *options.draw_index
            << " slot=" << slot << " binding=" << fetch.binding_index
            << " fetch=" << fetch.fetch_constant << " format="
            << fetch.format << " size=" << fetch.width << "x"
            << fetch.height << " tiled=" << (fetch.tiled ? "yes" : "no")
            << " endian=" << fetch.endian << " payload="
            << fetch.payload_bytes.size()
            << (fetch.payload_truncated ? " truncated" : "")
            << " nonzero_bytes=" << nonzero_bytes
            << " rgb_nonzero_pixels=" << rgb_nonzero_pixels << " avg_rgba=("
            << (sums[0] / std::max<std::size_t>(1, rgba.size() / 4)) << ","
            << (sums[1] / std::max<std::size_t>(1, rgba.size() / 4)) << ","
            << (sums[2] / std::max<std::size_t>(1, rgba.size() / 4)) << ","
            << (sums[3] / std::max<std::size_t>(1, rgba.size() / 4)) << ")"
            << "\n";
  if (!decode_reason.empty()) {
    std::cout << "D3D12 decoded texture note: " << decode_reason << "\n";
  }
  std::cout << "D3D12 decoded texture output: " << output.string() << "\n";
  return true;
#else
  (void)capture;
  (void)options;
  error = "D3D12 texture preview is only available on Windows";
  return false;
#endif
}

bool RunD3D12RealReplayBackend(const ReplayCapture &capture,
                               const ReplayCliOptions &options,
                               std::string &error) {
#if defined(_WIN32)
  PreparedRealDraw prepared;
  std::vector<PreparedRealDraw> prepared_draws;
  FrameRealReplayPlan frame_plan;
  const bool frame_replay =
      options.frame_index.has_value() && !options.draw_index.has_value();
  if (frame_replay) {
    if (!PrepareFrameRealReplayPlan(capture, options, frame_plan, error)) {
      return false;
    }
    if (frame_plan.noop_utility_frame) {
      D3D12LiveSubmitBinding *live_binding =
          options.live_submit ? options.live_binding : nullptr;
      if (live_binding) {
        live_binding->submitted_draws = 0;
        live_binding->shader_pair_count = 0;
        live_binding->pso_entries = 0;
        live_binding->pso_cache_hits = 0;
        live_binding->pso_cache_misses = 0;
        live_binding->diagnostic_pipelines = 0;
        live_binding->input_layout_variants = 0;
        CopyFramePlanDiagnosticsToLiveBinding(frame_plan, *live_binding);
        live_binding->noop_utility_frame = true;
        if (options.live_session && live_binding->command_list &&
            live_binding->color_target) {
          auto *live_session =
              reinterpret_cast<D3D12LiveReplaySessionStorage *>(
                  options.live_session);
          if (live_session->color_accum_ready &&
              live_session->color_accum_target &&
              live_session->color_accum_width == live_binding->width &&
              live_session->color_accum_height == live_binding->height) {
            D3D12_RESOURCE_BARRIER copy_barriers[2]{};
            copy_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copy_barriers[0].Transition.pResource =
                live_session->color_accum_target.Get();
            copy_barriers[0].Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            copy_barriers[0].Transition.StateBefore =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            copy_barriers[0].Transition.StateAfter =
                D3D12_RESOURCE_STATE_COPY_SOURCE;
            copy_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            copy_barriers[1].Transition.pResource = live_binding->color_target;
            copy_barriers[1].Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            copy_barriers[1].Transition.StateBefore =
                D3D12_RESOURCE_STATE_PRESENT;
            copy_barriers[1].Transition.StateAfter =
                D3D12_RESOURCE_STATE_COPY_DEST;
            live_binding->command_list->ResourceBarrier(2, copy_barriers);

            live_binding->command_list->CopyResource(
                live_binding->color_target,
                live_session->color_accum_target.Get());

            D3D12_RESOURCE_BARRIER restore_barriers[2]{};
            restore_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            restore_barriers[0].Transition.pResource =
                live_session->color_accum_target.Get();
            restore_barriers[0].Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            restore_barriers[0].Transition.StateBefore =
                D3D12_RESOURCE_STATE_COPY_SOURCE;
            restore_barriers[0].Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            restore_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            restore_barriers[1].Transition.pResource =
                live_binding->color_target;
            restore_barriers[1].Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            restore_barriers[1].Transition.StateBefore =
                D3D12_RESOURCE_STATE_COPY_DEST;
            restore_barriers[1].Transition.StateAfter =
                D3D12_RESOURCE_STATE_PRESENT;
            live_binding->command_list->ResourceBarrier(2, restore_barriers);
            live_binding->copied_retained_frame = true;
          }
        }
      }
      if (!options.live_submit) {
        std::cout << "D3D12 frame replay plan frame="
                  << frame_plan.frame_index
                  << " frame_draws=" << frame_plan.frame_draw_count
                  << " geometry_supported=0"
                  << " noop_elided=" << frame_plan.elided_noop_draw_count
                  << " skipped=0 submitted_supported_draws=0"
                  << " noop_utility_frame=yes\n";
      }
      return true;
    }
    prepared = frame_plan.supported_draws.front();
    prepared_draws = frame_plan.supported_draws;
  } else if (options.skip_unsupported && !options.draw_index) {
    for (std::size_t draw_index = 0; draw_index < capture.draws.size();
         ++draw_index) {
      const ReplayDrawState &state = capture.draws[draw_index];
      if (!MatchesShaderPairFilter(state, options)) {
        continue;
      }
      if (IsKnownIgnoredUtilityDraw(state)) {
        ++frame_plan.elided_noop_draw_count;
        continue;
      }

      PreparedRealDraw candidate;
      if (PrepareRealDrawAtIndex(capture, draw_index, true, candidate)) {
        prepared_draws.push_back(std::move(candidate));
        if (prepared_draws.size() >= options.d3d12_draw_limit) {
          break;
        }
        continue;
      }

      std::string reason =
          DescribeRealDrawGeometrySupport(capture, draw_index, true);
      if (reason.empty()) {
        reason = "draw is unsupported by current D3D12 real replay path";
      }
      ++frame_plan.skipped_draw_count;
      ++frame_plan.unsupported_reasons[reason];
    }
    if (prepared_draws.empty()) {
      error = "capture has no complete vertex/index snapshot that the "
              "current D3D12 real replay path can bind";
      return false;
    }
    prepared = prepared_draws.front();
  } else {
    if (!PrepareFirstRealDraw(capture, options, prepared, error)) {
      return false;
    }
    if (!MatchesShaderPairFilter(capture.draws[prepared.draw_index],
                                 options)) {
      error = "selected first real draw does not match --shader-pair filter";
      return false;
    }
    prepared_draws = CollectSupportedRealDraws(capture, options, prepared);
    if (options.shader_pair_vertex_hash || options.shader_pair_pixel_hash) {
      prepared_draws.erase(
          std::remove_if(prepared_draws.begin(), prepared_draws.end(),
                         [&](const PreparedRealDraw &draw) {
                           return !MatchesShaderPairFilter(
                               capture.draws[draw.draw_index], options);
                         }),
          prepared_draws.end());
    }
  }

  const ReplaySurfaceSize size = ChooseSurfaceSize(capture);
  D3D12LiveSubmitBinding *live_binding =
      options.live_submit ? options.live_binding : nullptr;
  const bool live_submit =
      live_binding && live_binding->device && live_binding->command_list &&
      live_binding->color_target && live_binding->rtv_heap;
  const bool log_backend = !live_submit;
  D3D12LiveReplaySessionStorage *live_session = nullptr;
  if (live_submit && options.live_session) {
    live_session = reinterpret_cast<D3D12LiveReplaySessionStorage *>(
        options.live_session);
    live_session->retained_draw_resources.clear();
    live_session->retained_srv_heap.Reset();
    live_session->retained_sampler_heap.Reset();
  }
  const uint32_t width =
      live_submit ? std::clamp<uint32_t>(live_binding->width, 64u, 3840u)
                  : std::clamp<uint32_t>(size.width, 64u, 3840u);
  const uint32_t height =
      live_submit ? std::clamp<uint32_t>(live_binding->height, 64u, 2160u)
                  : std::clamp<uint32_t>(size.height, 64u, 2160u);
  const DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

  ComPtr<ID3D12Device> device;
  if (live_submit) {
    device = live_binding->device;
  } else if (!CheckHr(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&device)),
                      "D3D12CreateDevice", error)) {
    return false;
  }

  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> list;
  if (!live_submit) {
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (!CheckHr(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
                 "ID3D12Device::CreateCommandQueue", error)) {
      return false;
    }

    if (!CheckHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&allocator)),
                 "ID3D12Device::CreateCommandAllocator", error)) {
      return false;
    }

    if (!CheckHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           allocator.Get(), nullptr,
                                           IID_PPV_ARGS(&list)),
                 "ID3D12Device::CreateCommandList", error)) {
      return false;
    }
  } else {
    list = live_binding->command_list;
  }

  ComPtr<ID3D12Resource> target;
  ComPtr<ID3D12DescriptorHeap> rtv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
  D3D12ReplayRenderTargetCache offline_render_targets;
  const uint32_t presented_guest_color_base =
      ChoosePresentedGuestColorBase(prepared_draws, capture);
  D3D12_CLEAR_VALUE clear_value{};
  clear_value.Format = format;
  clear_value.Color[0] = 0.015f;
  clear_value.Color[1] = 0.018f;
  clear_value.Color[2] = 0.022f;
  clear_value.Color[3] = 1.0f;
  if (live_submit) {
    if (!live_session) {
      error = "native D3D12 live replay requires a live session for "
              "persistent color accumulation";
      return false;
    }
    if (!live_session->color_accum_ready ||
        live_session->color_accum_width != width ||
        live_session->color_accum_height != height) {
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

      D3D12_HEAP_PROPERTIES default_heap{};
      default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      default_heap.CreationNodeMask = 1;
      default_heap.VisibleNodeMask = 1;

      if (!CheckHr(device->CreateCommittedResource(
                       &default_heap, D3D12_HEAP_FLAG_NONE, &texture_desc,
                       D3D12_RESOURCE_STATE_RENDER_TARGET, &clear_value,
                       IID_PPV_ARGS(&live_session->color_accum_target)),
                   "ID3D12Device::CreateCommittedResource(live accumulated "
                   "color target)",
                   error)) {
        return false;
      }

      D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
      rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      rtv_heap_desc.NumDescriptors = 1;
      if (!CheckHr(device->CreateDescriptorHeap(
                       &rtv_heap_desc,
                       IID_PPV_ARGS(&live_session->color_accum_rtv_heap)),
                   "ID3D12Device::CreateDescriptorHeap(live accumulated RTV)",
                   error)) {
        return false;
      }
      live_session->color_accum_rtv =
          live_session->color_accum_rtv_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateRenderTargetView(live_session->color_accum_target.Get(),
                                     nullptr,
                                     live_session->color_accum_rtv);
      live_session->color_accum_width = width;
      live_session->color_accum_height = height;
      live_session->color_accum_ready = true;
      list->ClearRenderTargetView(live_session->color_accum_rtv,
                                  clear_value.Color, 0, nullptr);
    }
    target = live_session->color_accum_target;
    rtv_heap = live_session->color_accum_rtv_heap;
    rtv = live_session->color_accum_rtv;
  } else {
    D3D12ReplayRenderTarget *presented_target = nullptr;
    if (!CreateReplayRenderTarget(device.Get(), offline_render_targets,
                                  presented_guest_color_base, width, height,
                                  format, clear_value, presented_target,
                                  error)) {
      return false;
    }
    target = presented_target->resource;
    rtv_heap = offline_render_targets.rtv_heap;
    rtv = presented_target->rtv;
  }

  bool batch_uses_depth = false;
  for (const PreparedRealDraw &prepared_draw : prepared_draws) {
    const ReplayDrawState &state = capture.draws[prepared_draw.draw_index];
    if (state.draw.render_state.present &&
        RenderStateUsesDepthTarget(&state.draw.render_state)) {
      batch_uses_depth = true;
      break;
    }
  }

  ComPtr<ID3D12Resource> depth_target;
  ComPtr<ID3D12DescriptorHeap> dsv_heap;
  D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
  if (batch_uses_depth) {
    if (live_session && live_session->depth_ready &&
        live_session->depth_width == width &&
        live_session->depth_height == height) {
      depth_target = live_session->depth_target;
      dsv_heap = live_session->dsv_heap;
      dsv = live_session->dsv;
    } else {
      D3D12_RESOURCE_DESC depth_desc{};
      depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      depth_desc.Width = width;
      depth_desc.Height = height;
      depth_desc.DepthOrArraySize = 1;
      depth_desc.MipLevels = 1;
      depth_desc.Format = kReplayDepthStencilFormat;
      depth_desc.SampleDesc.Count = 1;
      depth_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

      D3D12_CLEAR_VALUE depth_clear{};
      depth_clear.Format = kReplayDepthStencilFormat;
      depth_clear.DepthStencil.Depth = 1.0f;
      depth_clear.DepthStencil.Stencil = 0;

      D3D12_HEAP_PROPERTIES default_heap{};
      default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
      default_heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
      default_heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
      default_heap.CreationNodeMask = 1;
      default_heap.VisibleNodeMask = 1;

      if (!CheckHr(device->CreateCommittedResource(
                       &default_heap, D3D12_HEAP_FLAG_NONE, &depth_desc,
                       D3D12_RESOURCE_STATE_DEPTH_WRITE, &depth_clear,
                       IID_PPV_ARGS(&depth_target)),
                   "ID3D12Device::CreateCommittedResource(real depth target)",
                   error)) {
        return false;
      }

      D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc{};
      dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
      dsv_heap_desc.NumDescriptors = 1;
      if (!CheckHr(device->CreateDescriptorHeap(&dsv_heap_desc,
                                                IID_PPV_ARGS(&dsv_heap)),
                   "ID3D12Device::CreateDescriptorHeap(DSV)", error)) {
        return false;
      }
      dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
      device->CreateDepthStencilView(depth_target.Get(), nullptr, dsv);
      if (live_session) {
        live_session->depth_target = depth_target;
        live_session->dsv_heap = dsv_heap;
        live_session->dsv = dsv;
        live_session->depth_width = width;
        live_session->depth_height = height;
        live_session->depth_ready = true;
      }
    }
  }

  std::map<PipelineKey, D3D12ReplayPipeline> local_pipelines;
  std::map<PipelineKey, D3D12ReplayPipeline> *pipelines =
      live_session ? &live_session->pipelines : &local_pipelines;
  std::size_t pso_cache_hits = 0;
  std::size_t pso_cache_misses = 0;
  std::size_t diagnostic_pipeline_count = 0;
  std::size_t cache_index_write_count = 0;
  auto get_pipeline_for_draw =
      [&](const ReplayDrawState &state,
          const PreparedRealDraw &prepared_draw) -> D3D12ReplayPipeline * {
    const PipelineKey key = MakePipelineKey(state, prepared_draw, format);
    auto found = pipelines->find(key);
    if (found != pipelines->end()) {
      ++pso_cache_hits;
      return &found->second;
    }

    NativeShaderOverridePair override_pair;
    const char *vertex_shader_source = nullptr;
    const char *pixel_shader_source = nullptr;
    const char *vertex_shader_entry = "VSMain";
    const char *pixel_shader_entry = "PSMain";
    const char *vertex_shader_profile = "vs_5_0";
    const char *pixel_shader_profile = "ps_5_0";
    std::string vertex_shader_name_storage = "NativeRenderReplayDiagnostic.hlsl";
    std::string pixel_shader_name_storage = "NativeRenderReplayDiagnostic.hlsl";
    bool write_override_cache_index = false;
    bool used_diagnostic_shader = false;
    if (LoadNativeShaderOverridePair(state, options, override_pair, error)) {
      vertex_shader_source = override_pair.vertex_source.c_str();
      pixel_shader_source = override_pair.pixel_source.c_str();
      vertex_shader_entry = override_pair.vertex_entry.c_str();
      pixel_shader_entry = override_pair.pixel_entry.c_str();
      vertex_shader_profile = override_pair.vertex_profile.c_str();
      pixel_shader_profile = override_pair.pixel_profile.c_str();
      vertex_shader_name_storage = override_pair.vertex_path.string();
      pixel_shader_name_storage = override_pair.pixel_path.string();
      write_override_cache_index = !override_pair.vertex_source.empty() &&
                                   !override_pair.pixel_source.empty();
    } else if (options.allow_diagnostic_shader) {
      vertex_shader_source = DiagnosticRealGeometryShaderSource();
      pixel_shader_source = DiagnosticRealGeometryShaderSource();
      vertex_shader_name_storage = "NativeRenderReplayDiagnosticVS.hlsl";
      pixel_shader_name_storage = "NativeRenderReplayDiagnosticPS.hlsl";
      used_diagnostic_shader = true;
    } else {
      return nullptr;
    }

    D3D12ReplayPipeline pipeline;
    pipeline.vertex_shader_hash = state.vertex_shader.hash;
    pipeline.pixel_shader_hash = state.pixel_shader.hash;
    pipeline.render_state =
        state.draw.render_state.present ? &state.draw.render_state : nullptr;
    pipeline.used_diagnostic_shader = used_diagnostic_shader;
    pipeline.forced_depth_only_color_mask =
        prepared_draw.force_depth_only_color_mask;
    if (!CreateRealGeometryPipeline(
            device.Get(), pipeline.root_signature, pipeline.pipeline_state,
            format, pipeline.render_state, vertex_shader_source,
            vertex_shader_name_storage.c_str(), vertex_shader_entry,
            vertex_shader_profile, override_pair.vertex_cache_path,
            override_pair.vertex_log_path, pixel_shader_source,
            pixel_shader_name_storage.c_str(), pixel_shader_entry,
            pixel_shader_profile, override_pair.pixel_cache_path,
            override_pair.pixel_log_path,
            EffectiveInputLayoutMask(state, prepared_draw.input_layout_mask),
            prepared_draw.force_depth_only_color_mask, error)) {
      error = "could not create D3D12 real replay PSO for draw " +
              std::to_string(state.draw_index) + " VS=" +
              FormatHex64(state.vertex_shader.hash) + " PS=" +
              FormatHex64(state.pixel_shader.hash) + ": " + error;
      return nullptr;
    }
    if (write_override_cache_index &&
        !WriteOverrideCacheIndex(state, options, override_pair, error)) {
      return nullptr;
    }
    ++pso_cache_misses;
    if (used_diagnostic_shader) {
      ++diagnostic_pipeline_count;
    }
    auto [inserted, _] = pipelines->emplace(key, std::move(pipeline));
    return &inserted->second;
  };

  std::vector<PreparedRealDraw> pipeline_supported_draws;
  pipeline_supported_draws.reserve(prepared_draws.size());
  for (const PreparedRealDraw &prepared_draw : prepared_draws) {
    const ReplayDrawState &state = capture.draws[prepared_draw.draw_index];
    if (get_pipeline_for_draw(state, prepared_draw)) {
      pipeline_supported_draws.push_back(prepared_draw);
      continue;
    }
    if (options.skip_unsupported) {
      std::string reason =
          "D3D12 pipeline creation failed for VS=" +
          FormatHex64(state.vertex_shader.hash) + " PS=" +
          FormatHex64(state.pixel_shader.hash);
      if (!error.empty()) {
        const std::size_t detail = error.rfind(": ");
        reason += detail == std::string::npos ? ": " + error
                                               : error.substr(detail);
      }
      ++frame_plan.skipped_draw_count;
      ++frame_plan.unsupported_reasons[reason];
      error.clear();
      continue;
    }
    return false;
  }
  prepared_draws = std::move(pipeline_supported_draws);
  if (prepared_draws.empty()) {
    error = "selected frame has no draw with both supported geometry and a "
            "creatable D3D12 real replay PSO";
    return false;
  }

  std::vector<PreparedRealDraw> texture_supported_draws;
  texture_supported_draws.reserve(prepared_draws.size());
  for (const PreparedRealDraw &prepared_draw : prepared_draws) {
    const ReplayDrawState &state = capture.draws[prepared_draw.draw_index];
    std::string texture_reason;
    if (CheckCapturedTextureSupport(state, texture_reason) ||
        options.allow_diagnostic_shader) {
      texture_supported_draws.push_back(prepared_draw);
      continue;
    }
    if (options.skip_unsupported) {
      if (texture_reason.empty()) {
        texture_reason = "unsupported captured texture";
      }
      ++frame_plan.skipped_draw_count;
      ++frame_plan.unsupported_reasons[texture_reason];
      continue;
    }
    error = texture_reason.empty() ? "unsupported captured texture"
                                   : texture_reason;
    return false;
  }
  prepared_draws = std::move(texture_supported_draws);
  if (prepared_draws.empty()) {
    error = "selected frame has no draw with supported geometry, PSO, and "
            "captured texture state";
    return false;
  }

  cache_index_write_count = pso_cache_misses - diagnostic_pipeline_count;

  if (frame_replay && log_backend) {
    std::cout << "D3D12 frame replay plan frame=" << frame_plan.frame_index
              << " frame_draws=" << frame_plan.frame_draw_count
              << " geometry_supported=" << frame_plan.supported_draws.size()
              << " noop_elided=" << frame_plan.elided_noop_draw_count
              << " skipped=" << frame_plan.skipped_draw_count
              << " submitted_supported_draws=" << prepared_draws.size()
              << " noop_utility_frame="
              << (frame_plan.noop_utility_frame ? "yes" : "no")
              << " skip_unsupported="
              << (options.skip_unsupported ? "yes" : "no") << "\n";
    if (frame_plan.used_pre_frame_bucket) {
      std::cout << "D3D12 frame replay used pre-frame draw bucket because this "
                   "capture has frame markers but no frame-owned PM4 draws\n";
    }
    if (frame_plan.used_sequence_frame_bucket) {
      std::cout << "D3D12 frame replay used sequence-inferred draw bucket seq=("
                << frame_plan.sequence_begin << ","
                << frame_plan.sequence_end << "] because this capture has "
                   "frame markers but no frame-owned PM4 draws\n";
    }
    if (!frame_plan.unsupported_reasons.empty()) {
      std::cout << "D3D12 frame replay unsupported reason counts:\n";
      for (const auto &[reason, count] : frame_plan.unsupported_reasons) {
        std::cout << "  " << count << " x " << reason << "\n";
      }
    }
  }

  std::vector<UploadedRealDraw> uploaded_draws;
  uploaded_draws.reserve(prepared_draws.size());
  for (const PreparedRealDraw &prepared_draw : prepared_draws) {
    UploadedRealDraw uploaded;
    uploaded.prepared = prepared_draw;
    uploaded.constants =
        BuildCapturedConstants(capture.draws[prepared_draw.draw_index]);
    {
      std::array<uint8_t, kCapturedConstantBufferBytes> constant_bytes{};
      const std::size_t constant_byte_count =
          std::min<std::size_t>(uploaded.constants.count * sizeof(uint32_t),
                                constant_bytes.size());
      if (constant_byte_count > 0) {
        std::memcpy(constant_bytes.data(), uploaded.constants.dwords.data(),
                    constant_byte_count);
      }
      if (!CreateUploadBuffer(device.Get(), constant_bytes.data(),
                              kCapturedConstantBufferBytes,
                              uploaded.constant_buffer, error)) {
        return false;
      }
    }
    uploaded.vertex_bytes =
        uploaded.prepared.vertices.size() * sizeof(RealReplayVertex);
    if (!uploaded.prepared.vertexless) {
      if (!CreateUploadBuffer(device.Get(), uploaded.prepared.vertices.data(),
                              uploaded.vertex_bytes, uploaded.vertex_buffer,
                              error)) {
        return false;
      }
    }

    if (uploaded.prepared.indexed) {
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
  const std::size_t texture_fetch_draw_count =
      std::count_if(uploaded_draws.begin(), uploaded_draws.end(),
                    [&](const UploadedRealDraw &uploaded) {
                      const ReplayDrawState &state =
                          capture.draws[uploaded.prepared.draw_index];
                      return !state.draw.texture_fetches.empty();
                    });
  const std::size_t sampler_descriptor_count =
      std::max<std::size_t>(kMaxRealReplayTextureSlots,
                            (texture_fetch_draw_count + 1) *
                                kMaxRealReplayTextureSlots);
  sampler_heap_desc.NumDescriptors =
      static_cast<UINT>(sampler_descriptor_count);
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
  uint32_t partial_texture_preview_count = 0;
  uint32_t captured_sampler_count = 0;
  uint32_t fallback_sampler_count = 0;
  uint32_t exact_sampler_clamp_count = 0;
  uint32_t fallback_sampler_clamp_count = 0;
  const uint8_t white_texel[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint32_t next_sampler_descriptor_base =
      static_cast<uint32_t>(kMaxRealReplayTextureSlots);
  for (std::size_t slot = 0; slot < kMaxRealReplayTextureSlots; ++slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE sampler_descriptor = sampler_cpu_start;
    sampler_descriptor.ptr += static_cast<SIZE_T>(slot) *
                              sampler_descriptor_size;
    CreateSampler(device.Get(), nullptr, sampler_descriptor);
  }
  for (std::size_t i = 0; i < uploaded_draws.size(); ++i) {
    UploadedRealDraw &uploaded = uploaded_draws[i];
    uploaded.texture_srv_base_index =
        static_cast<uint32_t>(i * kMaxRealReplayTextureSlots);

    const ReplayDrawState &uploaded_state =
        capture.draws[uploaded.prepared.draw_index];
    const bool draw_has_texture_fetches =
        !uploaded_state.draw.texture_fetches.empty();
    if (draw_has_texture_fetches) {
      uploaded.sampler_descriptor_base_index = next_sampler_descriptor_base;
      next_sampler_descriptor_base +=
          static_cast<uint32_t>(kMaxRealReplayTextureSlots);
    } else {
      uploaded.sampler_descriptor_base_index = 0;
    }
    for (std::size_t slot = 0; slot < kMaxRealReplayTextureSlots; ++slot) {
      const std::size_t descriptor_index =
          i * kMaxRealReplayTextureSlots + slot;
      D3D12_CPU_DESCRIPTOR_HANDLE descriptor = srv_cpu_start;
      descriptor.ptr += static_cast<SIZE_T>(descriptor_index) *
                        srv_descriptor_size;
      D3D12_CPU_DESCRIPTOR_HANDLE sampler_descriptor = sampler_cpu_start;
      sampler_descriptor.ptr +=
          static_cast<SIZE_T>(uploaded.sampler_descriptor_base_index + slot) *
          sampler_descriptor_size;

      std::vector<uint8_t> texture_rgba;
      std::string texture_reason;
      const TextureFetchRecord *selected_fetch = nullptr;
      if (slot < uploaded_state.draw.texture_fetches.size()) {
        const TextureFetchRecord &fetch = uploaded_state.draw.texture_fetches[slot];
        if (fetch.payload_truncated) {
          ++unsupported_texture_count;
          texture_reason = "texture payload is truncated";
        } else if (DecodeTextureRgba8(fetch, texture_rgba, texture_reason)) {
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
        uploaded.texture_notes[slot] =
            texture_reason.empty() ? "captured" : texture_reason;
        if (!texture_reason.empty()) {
          ++partial_texture_preview_count;
        }
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
        if (draw_has_texture_fetches) {
          ++fallback_sampler_count;
          ++fallback_sampler_clamp_count;
        }
      }
      CreateTextureSrv(device.Get(), uploaded.textures[slot].Get(),
                       descriptor);
      if (draw_has_texture_fetches) {
        CreateSampler(device.Get(), selected_fetch, sampler_descriptor);
      }
    }
  }
  std::map<ShaderPairKey, std::size_t> submitted_pairs;
  for (const UploadedRealDraw &uploaded : uploaded_draws) {
    const ReplayDrawState &state = capture.draws[uploaded.prepared.draw_index];
    ++submitted_pairs[{state.vertex_shader.hash, state.pixel_shader.hash}];
  }
  if (frame_replay && log_backend) {
    std::cout << "D3D12 real frame replay submitted " << uploaded_draws.size()
              << " supported draw(s) across " << submitted_pairs.size()
              << " shader pair(s)\n";
  } else if (log_backend) {
    std::cout << "D3D12 real replay submitted " << uploaded_draws.size()
              << " supported draw(s) across " << submitted_pairs.size()
              << " shader pair(s)\n";
  }
  if (log_backend) {
    for (const auto &[key, count] : submitted_pairs) {
      std::cout << "  pair VS=" << FormatHex64(std::get<0>(key))
                << " PS=" << FormatHex64(std::get<1>(key))
                << " submitted=" << count << " captured="
                << CountDrawsForShaderPair(capture, std::get<0>(key),
                                           std::get<1>(key))
                << "\n";
    }
  }
  std::set<uint64_t> submitted_input_layouts;
  std::size_t scene_candidate_draws = 0;
  std::size_t depth_only_draws = 0;
  std::size_t utility_draws = 0;
  for (const UploadedRealDraw &uploaded : uploaded_draws) {
    submitted_input_layouts.insert(uploaded.prepared.input_layout_signature);
    const ReplayDrawState &state = capture.draws[uploaded.prepared.draw_index];
    if (uploaded.prepared.force_depth_only_color_mask) {
      ++depth_only_draws;
    } else if (IsAb1eA4ZeroColorFillDraw(
                   state, uploaded.prepared.vertices,
                   uploaded.prepared.input_layout_mask)) {
      ++utility_draws;
    } else {
      ++scene_candidate_draws;
    }
  }
  if (live_submit) {
    CopyFramePlanDiagnosticsToLiveBinding(frame_plan, *live_binding);
    live_binding->submitted_draws =
        static_cast<uint64_t>(uploaded_draws.size());
    live_binding->shader_pair_count =
        static_cast<uint64_t>(submitted_pairs.size());
    live_binding->pso_entries = static_cast<uint64_t>(pipelines->size());
    live_binding->pso_cache_misses = static_cast<uint64_t>(pso_cache_misses);
    live_binding->pso_cache_hits = static_cast<uint64_t>(pso_cache_hits);
    live_binding->diagnostic_pipelines =
        static_cast<uint64_t>(diagnostic_pipeline_count);
    live_binding->input_layout_variants =
        static_cast<uint64_t>(submitted_input_layouts.size());
    live_binding->scene_candidate_draws =
        static_cast<uint64_t>(scene_candidate_draws);
    live_binding->depth_only_draws = static_cast<uint64_t>(depth_only_draws);
    live_binding->utility_draws = static_cast<uint64_t>(utility_draws);
    live_binding->presentable_frame = scene_candidate_draws > 0;
  }
  if (log_backend) {
    std::cout << "D3D12 real replay PSO cache: entries=" << pipelines->size()
              << " misses=" << pso_cache_misses << " hits=" << pso_cache_hits
              << " diagnostic_pipelines=" << diagnostic_pipeline_count
              << " cache_index_writes=" << cache_index_write_count
              << " input_layout_variants=" << submitted_input_layouts.size()
              << "\n";
    std::cout << "D3D12 real replay bound " << captured_texture_count
              << " captured texture SRV(s), " << fallback_texture_count
              << " fallback texture SRV(s), unsupported_texture_attempts="
              << unsupported_texture_count
              << ", partial_texture_previews="
              << partial_texture_preview_count << "\n";
    std::cout << "D3D12 real replay bound " << captured_sampler_count
              << " captured sampler descriptor(s), " << fallback_sampler_count
              << " fallback sampler descriptor(s), exact_clamp_modes="
              << exact_sampler_clamp_count
              << ", clamp_addressing_fallbacks="
              << fallback_sampler_clamp_count << "\n";
    std::cout << "D3D12 real replay draw classes: scene_candidate="
              << scene_candidate_draws << " depth_only=" << depth_only_draws
              << " utility=" << utility_draws << "\n";
  }
  std::size_t depth_enabled_draws = 0;
  std::size_t depth_write_draws = 0;
  std::size_t stencil_enabled_draws = 0;
  std::size_t forced_depth_only_zero_color_draws = 0;
  for (const UploadedRealDraw &uploaded : uploaded_draws) {
    const ReplayDrawState &state = capture.draws[uploaded.prepared.draw_index];
    if (uploaded.prepared.force_depth_only_color_mask) {
      ++forced_depth_only_zero_color_draws;
    }
    if (!state.draw.render_state.present) {
      continue;
    }
    if (state.draw.render_state.depth_test_enable ||
        state.draw.render_state.depth_write_enable) {
      ++depth_enabled_draws;
    }
    if (state.draw.render_state.depth_write_enable) {
      ++depth_write_draws;
    }
    if (state.draw.render_state.stencil_enable) {
      ++stencil_enabled_draws;
    }
  }
  if (log_backend) {
    std::cout << "D3D12 real replay depth target format=D24_UNORM_S8_UINT"
              << " depth_target_bound=" << (batch_uses_depth ? "yes" : "no")
              << " depth_enabled_draws=" << depth_enabled_draws
              << " depth_write_draws=" << depth_write_draws
              << " stencil_enabled_draws=" << stencil_enabled_draws
              << " forced_depth_only_zero_color_draws="
              << forced_depth_only_zero_color_draws << "\n";
  }
  const ReplayDrawState &first_submitted_state =
      capture.draws[uploaded_draws.front().prepared.draw_index];
  const RenderStateRecord *first_pipeline_render_state =
      first_submitted_state.draw.render_state.present
          ? &first_submitted_state.draw.render_state
          : nullptr;
  if (first_pipeline_render_state && log_backend) {
    std::cout << "D3D12 real replay applied render state from draw "
              << first_submitted_state.draw_index << ": color_mask="
              << FormatHex32(first_pipeline_render_state->rb_color_mask)
              << " cull=" << first_pipeline_render_state->cull_mode
              << " depth_test="
              << (first_pipeline_render_state->depth_test_enable ? "yes" : "no")
              << " depth_write="
              << (first_pipeline_render_state->depth_write_enable ? "yes" : "no")
              << " stencil="
              << (first_pipeline_render_state->stencil_enable ? "yes" : "no")
              << "\n";
  } else if (log_backend) {
    std::cout << "D3D12 real replay render state unavailable";
    if (!uploaded_draws.front().prepared.indexed &&
        first_submitted_state.draw.render_state.present) {
      std::cout << " for non-indexed draw bring-up";
    }
    std::cout << "; using default D3D12 PSO state\n";
  }

  D3D12_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(width);
  viewport.Height = static_cast<float>(height);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  D3D12_RECT scissor =
      ScissorRectFromRenderState(first_pipeline_render_state, width, height);

  bool seeded_color_target = false;
  ComPtr<ID3D12Resource> captured_color_target_upload;
  std::string captured_color_target_reason;
  if (!live_submit) {
    CapturedColorTargetSeed seed{};
    if (FindCapturedColorTargetSeed(capture, uploaded_draws, width, height,
                                    seed, captured_color_target_reason)) {
      if (!UploadLinearRgba8ToTexture(
              device.Get(), list.Get(), target.Get(),
              seed.payload->payload_bytes.data(), seed.width, seed.height,
              D3D12_RESOURCE_STATE_RENDER_TARGET,
              D3D12_RESOURCE_STATE_RENDER_TARGET,
              captured_color_target_upload, error)) {
        error = "could not seed D3D12 real replay render target from capture: " +
                error;
        return false;
      }
      seeded_color_target = true;
      std::cout << "D3D12 real replay initialized color target from captured "
                   "payload: "
                << captured_color_target_reason
                << " size=" << seed.width << "x" << seed.height
                << " bytes=" << seed.payload->payload_bytes.size()
                << (seed.payload->payload_loaded_from_resource ? " sidecar"
                                                               : "")
                << "\n";
    } else {
      std::cout << "D3D12 real replay color target initialized by clear: "
                << captured_color_target_reason << "\n";
    }
  }
  if (!seeded_color_target && !live_submit) {
    list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  }
  if (!live_submit) {
    for (auto &entry : offline_render_targets.targets) {
      if (entry.first == presented_guest_color_base) {
        entry.second.initialized = true;
        continue;
      }
      list->ClearRenderTargetView(entry.second.rtv, clear_value.Color, 0,
                                  nullptr);
      entry.second.initialized = true;
    }
  }
  ID3D12DescriptorHeap *descriptor_heaps[] = {srv_heap.Get(),
                                              sampler_heap.Get()};
  list->SetDescriptorHeaps(2, descriptor_heaps);
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  if (batch_uses_depth) {
    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    list->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0,
        nullptr);
  } else {
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  }
  const float constants[4] = {static_cast<float>(width),
                              static_cast<float>(height), 0.0f, 0.0f};

  D3D12ReplayPipeline *bound_pipeline = nullptr;
  for (const UploadedRealDraw &uploaded : uploaded_draws) {
    const ReplayDrawState &uploaded_state =
        capture.draws[uploaded.prepared.draw_index];
    D3D12ReplayPipeline *pipeline =
        get_pipeline_for_draw(uploaded_state, uploaded.prepared);
    if (!pipeline) {
      return false;
    }
    if (pipeline != bound_pipeline) {
      list->SetGraphicsRootSignature(pipeline->root_signature.Get());
      list->SetPipelineState(pipeline->pipeline_state.Get());
      list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
      bound_pipeline = pipeline;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE draw_rtv = rtv;
    if (!live_submit) {
      const uint32_t draw_guest_color_base =
          GuestColorBaseForDraw(uploaded_state);
      D3D12ReplayRenderTarget *draw_target = nullptr;
      if (!CreateReplayRenderTarget(device.Get(), offline_render_targets,
                                    draw_guest_color_base, width, height,
                                    format, clear_value, draw_target, error)) {
        return false;
      }
      if (!draw_target->initialized) {
        list->ClearRenderTargetView(draw_target->rtv, clear_value.Color, 0,
                                    nullptr);
        draw_target->initialized = true;
      }
      draw_rtv = draw_target->rtv;
    }
    const bool draw_uses_depth =
        batch_uses_depth && RenderStateUsesDepthTarget(pipeline->render_state);
    list->OMSetRenderTargets(1, &draw_rtv, FALSE,
                             draw_uses_depth ? &dsv : nullptr);
    list->OMSetStencilRef(StencilRefFromRenderState(pipeline->render_state));
    const D3D12_RECT draw_scissor =
        ScissorRectFromRenderState(pipeline->render_state, width, height);
    list->RSSetScissorRects(1, &draw_scissor);
    list->SetGraphicsRootConstantBufferView(
        1, uploaded.constant_buffer->GetGPUVirtualAddress());
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

    if (uploaded.prepared.vertexless) {
      list->IASetVertexBuffers(0, 0, nullptr);
    } else {
      D3D12_VERTEX_BUFFER_VIEW vertex_view{};
      vertex_view.BufferLocation =
          uploaded.vertex_buffer->GetGPUVirtualAddress();
      vertex_view.SizeInBytes = static_cast<UINT>(uploaded.vertex_bytes);
      vertex_view.StrideInBytes = sizeof(RealReplayVertex);
      list->IASetVertexBuffers(0, 1, &vertex_view);
    }
    list->IASetPrimitiveTopology(uploaded.prepared.topology);
    if (uploaded.prepared.indexed) {
      D3D12_INDEX_BUFFER_VIEW index_view{};
      index_view.BufferLocation =
          uploaded.index_buffer->GetGPUVirtualAddress();
      index_view.SizeInBytes = static_cast<UINT>(uploaded.index_bytes);
      index_view.Format = uploaded.prepared.uses_32bit_indices
                              ? DXGI_FORMAT_R32_UINT
                              : DXGI_FORMAT_R16_UINT;
      list->IASetIndexBuffer(&index_view);
      const UINT index_count =
          static_cast<UINT>(uploaded.prepared.uses_32bit_indices
                                ? uploaded.prepared.indices32.size()
                                : uploaded.prepared.indices16.size());
      list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);
    } else {
      list->IASetIndexBuffer(nullptr);
      list->DrawInstanced(uploaded.prepared.vertex_count, 1, 0, 0);
    }
  }

  if (live_submit) {
    if (live_binding->presentable_frame) {
      D3D12_RESOURCE_BARRIER copy_barriers[2]{};
      copy_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      copy_barriers[0].Transition.pResource = target.Get();
      copy_barriers[0].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      copy_barriers[0].Transition.StateBefore =
          D3D12_RESOURCE_STATE_RENDER_TARGET;
      copy_barriers[0].Transition.StateAfter =
          D3D12_RESOURCE_STATE_COPY_SOURCE;
      copy_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      copy_barriers[1].Transition.pResource = live_binding->color_target;
      copy_barriers[1].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      copy_barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      copy_barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
      list->ResourceBarrier(2, copy_barriers);

      list->CopyResource(live_binding->color_target, target.Get());

      D3D12_RESOURCE_BARRIER restore_barriers[2]{};
      restore_barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      restore_barriers[0].Transition.pResource = target.Get();
      restore_barriers[0].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      restore_barriers[0].Transition.StateBefore =
          D3D12_RESOURCE_STATE_COPY_SOURCE;
      restore_barriers[0].Transition.StateAfter =
          D3D12_RESOURCE_STATE_RENDER_TARGET;
      restore_barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      restore_barriers[1].Transition.pResource = live_binding->color_target;
      restore_barriers[1].Transition.Subresource =
          D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      restore_barriers[1].Transition.StateBefore =
          D3D12_RESOURCE_STATE_COPY_DEST;
      restore_barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      list->ResourceBarrier(2, restore_barriers);
    }
    if (live_session) {
      live_session->retained_draw_resources = std::move(uploaded_draws);
      live_session->retained_srv_heap = srv_heap;
      live_session->retained_sampler_heap = sampler_heap;
    }
    return true;
  }

  D3D12_RESOURCE_BARRIER barriers[2]{};
  std::size_t barrier_count = 0;
  barriers[barrier_count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barriers[barrier_count].Transition.pResource = target.Get();
  barriers[barrier_count].Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barriers[barrier_count].Transition.StateBefore =
      D3D12_RESOURCE_STATE_RENDER_TARGET;
  barriers[barrier_count].Transition.StateAfter =
      D3D12_RESOURCE_STATE_COPY_SOURCE;
  ++barrier_count;
  if (batch_uses_depth && depth_target) {
    barriers[barrier_count].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[barrier_count].Transition.pResource = depth_target.Get();
    barriers[barrier_count].Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[barrier_count].Transition.StateBefore =
        D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barriers[barrier_count].Transition.StateAfter =
        D3D12_RESOURCE_STATE_COPY_SOURCE;
    ++barrier_count;
  }
  list->ResourceBarrier(static_cast<UINT>(barrier_count), barriers);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT color_footprint{};
  UINT color_row_count = 0;
  UINT64 color_row_size = 0;
  UINT64 color_total_size = 0;
  const D3D12_RESOURCE_DESC target_desc = target->GetDesc();
  device->GetCopyableFootprints(&target_desc, 0, 1, 0, &color_footprint,
                                &color_row_count, &color_row_size,
                                &color_total_size);

  ComPtr<ID3D12Resource> color_readback;
  if (!CreateReadbackBuffer(device.Get(), color_total_size, color_readback,
                            error)) {
    return false;
  }

  D3D12_TEXTURE_COPY_LOCATION color_dst{};
  color_dst.pResource = color_readback.Get();
  color_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  color_dst.PlacedFootprint = color_footprint;

  D3D12_TEXTURE_COPY_LOCATION color_src{};
  color_src.pResource = target.Get();
  color_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  color_src.SubresourceIndex = 0;
  list->CopyTextureRegion(&color_dst, 0, 0, 0, &color_src, nullptr);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT depth_footprint{};
  UINT depth_row_count = 0;
  UINT64 depth_row_size = 0;
  UINT64 depth_total_size = 0;
  ComPtr<ID3D12Resource> depth_readback;
  if (batch_uses_depth && depth_target) {
    D3D12_RESOURCE_DESC depth_resource_desc = depth_target->GetDesc();
    device->GetCopyableFootprints(&depth_resource_desc, 0, 1, 0,
                                  &depth_footprint, &depth_row_count,
                                  &depth_row_size, &depth_total_size);
    if (!CreateReadbackBuffer(device.Get(), depth_total_size, depth_readback,
                              error)) {
      return false;
    }

    D3D12_TEXTURE_COPY_LOCATION depth_dst{};
    depth_dst.pResource = depth_readback.Get();
    depth_dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    depth_dst.PlacedFootprint = depth_footprint;

    D3D12_TEXTURE_COPY_LOCATION depth_src{};
    depth_src.pResource = depth_target.Get();
    depth_src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    depth_src.SubresourceIndex = 0;
    list->CopyTextureRegion(&depth_dst, 0, 0, 0, &depth_src, nullptr);
  }

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

  uint8_t *color_mapped = nullptr;
  D3D12_RANGE color_read_range{0, static_cast<SIZE_T>(color_total_size)};
  if (!CheckHr(color_readback->Map(0, &color_read_range,
                                   reinterpret_cast<void **>(&color_mapped)),
               "ID3D12Resource::Map(color readback)", error)) {
    return false;
  }

  const ReadbackSnapshotStats color_stats = CountNonZeroBytes(
      color_mapped + color_footprint.Offset, static_cast<std::size_t>(color_total_size));

  const std::filesystem::path output = OutputPathFor(capture, options);
  std::error_code ec;
  if (!output.parent_path().empty()) {
    std::filesystem::create_directories(output.parent_path(), ec);
  }
  const bool wrote_color =
      !ec && WriteBmp(output, color_mapped + color_footprint.Offset,
                      color_footprint.Footprint.RowPitch, width, height, error);
  D3D12_RANGE empty_range{0, 0};
  color_readback->Unmap(0, &empty_range);

  if (ec) {
    error = "could not create D3D12 output directory: " + ec.message();
    return false;
  }
  if (!wrote_color) {
    return false;
  }

  ReadbackSnapshotStats depth_stats{};
  bool wrote_depth = false;
  const std::filesystem::path depth_output = DepthOutputPathFor(capture, options);
  if (batch_uses_depth && depth_readback && depth_total_size > 0) {
    uint8_t *depth_mapped = nullptr;
    D3D12_RANGE depth_read_range{0, static_cast<SIZE_T>(depth_total_size)};
    if (!CheckHr(depth_readback->Map(0, &depth_read_range,
                                     reinterpret_cast<void **>(&depth_mapped)),
                 "ID3D12Resource::Map(depth readback)", error)) {
      return false;
    }
    depth_stats = CountNonZeroBytes(
        depth_mapped + depth_footprint.Offset,
        static_cast<std::size_t>(depth_total_size));
    if (!depth_output.parent_path().empty()) {
      std::filesystem::create_directories(depth_output.parent_path(), ec);
    }
    wrote_depth =
        !ec &&
        WriteDepthPreviewBmp(
            depth_output, depth_mapped + depth_footprint.Offset,
            depth_footprint.Footprint.RowPitch, width, height, error);
    depth_readback->Unmap(0, &empty_range);
    if (ec) {
      error = "could not create D3D12 depth output directory: " + ec.message();
      return false;
    }
    if (!wrote_depth) {
      return false;
    }
  }

  if (!live_submit) {
    std::cout << "D3D12 real replay offline render targets: count="
              << offline_render_targets.targets.size()
              << " presented_guest_color_base=0x" << std::hex
              << std::uppercase << presented_guest_color_base << std::dec
              << std::nouppercase << "\n";
  }
  std::cout << "D3D12 real replay color readback: bytes=" << color_stats.total_bytes
            << " nonzero=" << color_stats.nonzero_bytes
            << " output=" << output.string() << "\n";
  if (batch_uses_depth) {
    std::cout << "D3D12 real replay depth readback: bytes=" << depth_stats.total_bytes
              << " nonzero=" << depth_stats.nonzero_bytes
              << " output=" << depth_output.string() << "\n";
  } else {
    std::cout << "D3D12 real replay depth readback: skipped=no_depth_target_draws\n";
  }

  return true;
#else
  (void)capture;
  (void)options;
  error = "D3D12 replay backend is only available on Windows";
  return false;
#endif
}

void PrintD3D12RealBackendGaps(const ReplayCapture &capture,
                               const ReplayCliOptions &options) {
#if !defined(_WIN32)
  (void)capture;
  (void)options;
  std::cout << "D3D12 real backend gap report unavailable on this platform\n";
#else
  struct PairStats {
    std::size_t total = 0;
    std::size_t geometry_ok = 0;
    std::size_t shader_ok = 0;
    std::size_t texture_ok = 0;
    std::size_t ready = 0;
    std::size_t utility_ready = 0;
    std::size_t depth_only_zero_color_ready = 0;
    std::size_t scene_candidate_ready = 0;
    std::size_t ignored_utility = 0;
  };

  std::map<std::string, std::size_t> blocker_counts;
  std::map<std::string, std::size_t> ready_class_counts;
  std::map<std::tuple<uint64_t, uint64_t>, PairStats> pair_stats;
  std::size_t geometry_ok = 0;
  std::size_t shader_ok = 0;
  std::size_t texture_ok = 0;
  std::size_t ready = 0;
  std::size_t utility_ready = 0;
  std::size_t depth_only_zero_color_ready = 0;
  std::size_t scene_candidate_ready = 0;
  std::size_t ignored_utility = 0;

  for (std::size_t i = 0; i < capture.draws.size(); ++i) {
    const ReplayDrawState &state = capture.draws[i];
    auto &stats =
        pair_stats[{state.vertex_shader.hash, state.pixel_shader.hash}];
    ++stats.total;

    const std::string geometry_reason =
        DescribeRealDrawGeometrySupport(capture, i, true);
    if (!geometry_reason.empty()) {
      if (IsKnownIgnoredUtilityDraw(state)) {
        ++ignored_utility;
        ++stats.ignored_utility;
        if (IsKnownZeroTextureNoOutputDraw(state)) {
          ++ready_class_counts["ignored utility/zero-texture no-output draw"];
        } else if (IsKnownZeroedInterpolatorNoOutputDraw(state)) {
          ++ready_class_counts
              ["ignored utility/zeroed-interpolator no-output draw"];
        } else {
          ++ready_class_counts["ignored utility/no-raster no-fetch draw"];
        }
        continue;
      }
      ++blocker_counts["geometry: " + geometry_reason];
      continue;
    }
    PreparedRealDraw prepared;
    if (!PrepareRealDrawAtIndex(capture, i, true, prepared)) {
      ++blocker_counts["geometry: draw passed description but failed "
                       "preparation"];
      continue;
    }
    ++geometry_ok;
    ++stats.geometry_ok;

    NativeShaderOverridePair shader_pair;
    std::string shader_error;
    if (!LoadNativeShaderOverridePair(state, options, shader_pair,
                                      shader_error)) {
      const std::size_t detail = shader_error.rfind(": ");
      ++blocker_counts["shader: " +
                       (detail == std::string::npos
                            ? shader_error
                            : shader_error.substr(detail + 2))];
      continue;
    }
    ++shader_ok;
    ++stats.shader_ok;

    std::string texture_reason;
    if (!CheckCapturedTextureSupport(state, texture_reason)) {
      ++blocker_counts["texture: " +
                       (texture_reason.empty() ? "unsupported captured texture"
                                               : texture_reason)];
      continue;
    }
    ++texture_ok;
    ++stats.texture_ok;
    ++ready;
    ++stats.ready;
    if (prepared.force_depth_only_color_mask) {
      ++depth_only_zero_color_ready;
      ++stats.depth_only_zero_color_ready;
      ++ready_class_counts["ready depth-only zero-color A4 pass"];
    } else if (IsLikelyFullscreenUtilityPass(state, prepared)) {
      ++utility_ready;
      ++stats.utility_ready;
      ++ready_class_counts["ready utility/postprocess fullscreen pass"];
    } else {
      ++scene_candidate_ready;
      ++stats.scene_candidate_ready;
      ++ready_class_counts["ready scene-candidate draw"];
    }
  }

  std::cout << "D3D12 real backend gap report:\n";
  std::cout << "  draws=" << capture.draws.size()
            << " geometry_ok=" << geometry_ok << " shader_ok=" << shader_ok
            << " texture_ok=" << texture_ok << " ready=" << ready
            << " utility_ready=" << utility_ready
            << " depth_only_zero_color_ready="
            << depth_only_zero_color_ready
            << " scene_candidate_ready=" << scene_candidate_ready
            << " ignored_utility=" << ignored_utility << "\n";

  std::vector<std::pair<std::string, std::size_t>> blockers(
      blocker_counts.begin(), blocker_counts.end());
  std::sort(blockers.begin(), blockers.end(), [](const auto &a, const auto &b) {
    if (a.second != b.second) {
      return a.second > b.second;
    }
    return a.first < b.first;
  });
  std::cout << "  top blockers:\n";
  for (std::size_t i = 0; i < std::min<std::size_t>(blockers.size(), 20); ++i) {
    std::cout << "    " << blockers[i].second << " x " << blockers[i].first
              << "\n";
  }
  if (!ready_class_counts.empty()) {
    std::vector<std::pair<std::string, std::size_t>> ready_classes(
        ready_class_counts.begin(), ready_class_counts.end());
    std::sort(ready_classes.begin(), ready_classes.end(),
              [](const auto &a, const auto &b) {
                if (a.second != b.second) {
                  return a.second > b.second;
                }
                return a.first < b.first;
              });
    std::cout << "  ready classifications:\n";
    for (const auto &[label, count] : ready_classes) {
      std::cout << "    " << count << " x " << label << "\n";
    }
  }

  std::vector<std::pair<std::tuple<uint64_t, uint64_t>, PairStats>> pairs(
      pair_stats.begin(), pair_stats.end());
  std::sort(pairs.begin(), pairs.end(), [](const auto &a, const auto &b) {
    if (a.second.total != b.second.total) {
      return a.second.total > b.second.total;
    }
    return a.first < b.first;
  });
  std::cout << "  top shader pairs:\n";
  for (std::size_t i = 0;
       i < std::min<std::size_t>(pairs.size(), options.top_shaders); ++i) {
    const auto &[key, stats] = pairs[i];
    std::cout << "    VS=" << FormatHex64(std::get<0>(key))
              << " PS=" << FormatHex64(std::get<1>(key))
              << " draws=" << stats.total
              << " geometry_ok=" << stats.geometry_ok
              << " shader_ok=" << stats.shader_ok
              << " texture_ok=" << stats.texture_ok
              << " ready=" << stats.ready
              << " depth_only_zero_color="
              << stats.depth_only_zero_color_ready
              << " utility_ready=" << stats.utility_ready
              << " scene_candidate_ready=" << stats.scene_candidate_ready
              << " ignored_utility=" << stats.ignored_utility
              << "\n";
  }
#endif
}

D3D12LiveReplaySession *CreateD3D12LiveReplaySession() {
#if defined(_WIN32)
  return reinterpret_cast<D3D12LiveReplaySession *>(
      new D3D12LiveReplaySessionStorage());
#else
  return nullptr;
#endif
}

void DestroyD3D12LiveReplaySession(D3D12LiveReplaySession *session) {
#if defined(_WIN32)
  delete reinterpret_cast<D3D12LiveReplaySessionStorage *>(session);
#else
  (void)session;
#endif
}

bool RunD3D12LiveFrameBackend(const ReplayCapture &capture,
                              const ReplayCliOptions &base_options,
                              D3D12LiveSubmitBinding &binding,
                              std::string &error) {
#if defined(_WIN32)
  ReplayCliOptions options = base_options;
  options.live_submit = true;
  options.live_binding = &binding;
  options.skip_unsupported = true;
  options.allow_diagnostic_shader = base_options.allow_diagnostic_shader;
  options.frame_index = 0;
  if (!binding.session) {
    binding.session = CreateD3D12LiveReplaySession();
  }
  options.live_session = binding.session;
  return RunD3D12RealReplayBackend(capture, options, error);
#else
  (void)capture;
  (void)base_options;
  (void)binding;
  error = "D3D12 live replay backend is only available on Windows";
  return false;
#endif
}

} // namespace bo2::native::replay
