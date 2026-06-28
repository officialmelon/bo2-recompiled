#include "NativeRenderReplay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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
  (void)options;
  const bool has_constants = capture.summary.constant_uploads_with_payload != 0;
  error = "capture/replay now has bounded index and vertex snapshots where "
          "the command stream provides them, but d3d12-real still lacks "
          "translated input layouts, native shader replacements/translations, "
          "texture/sampler state, and render-target/depth state. Refusing to "
          "synthesize output in d3d12-real; use d3d12-diagnostic for the "
          "current debug renderer. constant_payloads=" +
          std::to_string(capture.summary.constant_uploads_with_payload) +
          (has_constants ? " vertex_snapshots=" : " (none) vertex_snapshots=") +
          std::to_string(capture.summary.vertex_buffer_snapshots) +
          " index_snapshots=" +
          std::to_string(capture.summary.index_buffer_snapshots);
  return false;
}

} // namespace bo2::native::replay
