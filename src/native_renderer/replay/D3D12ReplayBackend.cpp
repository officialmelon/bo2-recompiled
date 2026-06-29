#include "NativeRenderReplay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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

bool CompileShader(const char *source, const char *entry, const char *target,
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
    const ReplayDrawState &state = capture.draws[i];
    const PM4DrawRecord &draw = state.draw;
    if (!draw.indexed || draw.index_payload_missing ||
        draw.index_payload_truncated ||
        draw.vertex_fetches.empty() ||
        TopologyForPrimitive(draw.primitive_type) !=
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
      continue;
    }

    std::vector<uint32_t> decoded_indices = DecodeReplayIndices(draw);
    if (decoded_indices.empty()) {
      continue;
    }

    for (const VertexFetchRecord &fetch : draw.vertex_fetches) {
      PreparedRealDraw candidate;
      candidate.draw_index = i;
      if (!BuildCanonicalVertices(fetch, candidate.vertices)) {
        continue;
      }

      const uint32_t max_index = *std::max_element(decoded_indices.begin(),
                                                  decoded_indices.end());
      if (max_index >= candidate.vertices.size()) {
        continue;
      }

      candidate.uses_32bit_indices =
          draw.index_format != 0 || max_index > UINT16_MAX;
      if (candidate.uses_32bit_indices) {
        candidate.indices32 = decoded_indices;
      } else {
        candidate.indices16.reserve(decoded_indices.size());
        for (uint32_t index : decoded_indices) {
          candidate.indices16.push_back(static_cast<uint16_t>(index));
        }
      }
      prepared = std::move(candidate);
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

bool CreateRealGeometryPipeline(ID3D12Device *device,
                                ComPtr<ID3D12RootSignature> &root_signature,
                                ComPtr<ID3D12PipelineState> &pipeline_state,
                                DXGI_FORMAT format, std::string &error) {
  D3D12_ROOT_PARAMETER root_parameter{};
  root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  root_parameter.Constants.ShaderRegister = 0;
  root_parameter.Constants.RegisterSpace = 0;
  root_parameter.Constants.Num32BitValues = 4;

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
cbuffer FrameConstants : register(b0)
{
  float2 surface_size;
  float2 _pad;
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

float4 PSMain(VSOut input) : SV_Target0
{
  float2 uv = saturate(input.uv);
  float3 tint = float3(0.25f + 0.75f * uv.x, 0.25f + 0.75f * uv.y, 1.0f);
  return float4(saturate(input.color.rgb * tint), 1.0f);
}
)";

  ComPtr<ID3DBlob> vertex_shader;
  ComPtr<ID3DBlob> pixel_shader;
  if (!CompileShader(kShaderSource, "VSMain", "vs_5_0", vertex_shader, error) ||
      !CompileShader(kShaderSource, "PSMain", "ps_5_0", pixel_shader, error)) {
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
  pso_desc.BlendState = DefaultBlendDesc();
  pso_desc.SampleMask = UINT_MAX;
  pso_desc.RasterizerState = DefaultRasterizerDesc();
  pso_desc.DepthStencilState = DefaultDepthStencilDesc();
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

bool CompileShader(const char *source, const char *entry, const char *target,
                   ComPtr<ID3DBlob> &blob, std::string &error) {
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
  ComPtr<ID3DBlob> errors;
  const HRESULT hr =
      D3DCompile(source, std::strlen(source), "NativeRenderReplayDebug.hlsl",
                 nullptr, nullptr, entry, target, flags, 0, &blob, &errors);
  if (SUCCEEDED(hr)) {
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
  if (!CompileShader(kShaderSource, "VSMain", "vs_5_0", vertex_shader, error) ||
      !CompileShader(kShaderSource, "PSMain", "ps_5_0", pixel_shader, error)) {
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
  if (!options.allow_diagnostic_shader) {
    error =
        "no translated, cached, or override shader pair is available for the "
        "selected draw. Re-run with --allow-diagnostic-shader only for the "
        "temporary resource-backed geometry diagnostic path.";
    return false;
  }

  PreparedRealDraw prepared;
  if (!PrepareFirstRealDraw(capture, options, prepared, error)) {
    return false;
  }

  const ReplayDrawState &draw_state = capture.draws[prepared.draw_index];
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
  if (!CreateRealGeometryPipeline(device.Get(), root_signature, pipeline_state,
                                  format, error)) {
    return false;
  }

  ComPtr<ID3D12Resource> vertex_buffer;
  const uint64_t vertex_bytes =
      prepared.vertices.size() * sizeof(RealReplayVertex);
  if (!CreateUploadBuffer(device.Get(), prepared.vertices.data(), vertex_bytes,
                          vertex_buffer, error)) {
    return false;
  }

  ComPtr<ID3D12Resource> index_buffer;
  const void *index_data = prepared.uses_32bit_indices
                               ? static_cast<const void *>(
                                     prepared.indices32.data())
                               : static_cast<const void *>(
                                     prepared.indices16.data());
  const uint64_t index_bytes =
      prepared.uses_32bit_indices
          ? prepared.indices32.size() * sizeof(uint32_t)
          : prepared.indices16.size() * sizeof(uint16_t);
  if (!CreateUploadBuffer(device.Get(), index_data, index_bytes, index_buffer,
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

  list->ClearRenderTargetView(rtv, clear_value.Color, 0, nullptr);
  list->SetGraphicsRootSignature(root_signature.Get());
  list->SetPipelineState(pipeline_state.Get());
  list->RSSetViewports(1, &viewport);
  list->RSSetScissorRects(1, &scissor);
  list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
  const float constants[4] = {static_cast<float>(width),
                              static_cast<float>(height), 0.0f, 0.0f};
  list->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

  D3D12_VERTEX_BUFFER_VIEW vertex_view{};
  vertex_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
  vertex_view.SizeInBytes = static_cast<UINT>(vertex_bytes);
  vertex_view.StrideInBytes = sizeof(RealReplayVertex);

  D3D12_INDEX_BUFFER_VIEW index_view{};
  index_view.BufferLocation = index_buffer->GetGPUVirtualAddress();
  index_view.SizeInBytes = static_cast<UINT>(index_bytes);
  index_view.Format =
      prepared.uses_32bit_indices ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

  list->IASetVertexBuffers(0, 1, &vertex_view);
  list->IASetIndexBuffer(&index_view);
  list->IASetPrimitiveTopology(TopologyForPrimitive(
      draw_state.draw.primitive_type));
  const UINT index_count =
      static_cast<UINT>(prepared.uses_32bit_indices
                            ? prepared.indices32.size()
                            : prepared.indices16.size());
  list->DrawIndexedInstanced(index_count, 1, 0, 0, 0);

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
