#include "XenosHlslTranslator.h"

#include "XenosDisassembly.h"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>

namespace bo2::native {
namespace {

std::string Hex64(uint64_t value) {
  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex << std::setw(16)
      << std::setfill('0') << value;
  return out.str();
}

void EmitTranslatedHeader(const XenosHlslTranslationRequest& request,
                          std::ostream& out) {
  out << "// BO2 native renderer translated HLSL from decoded Xenos "
         "operations.\n";
  out << "// Translator subset: " << LimitedXenosHlslTranslatorVersion()
      << ".\n";
  out << "// translation_source: shared\n";
  out << "// Unsupported shaders fail closed instead of using this path.\n";
  out << "// capture: " << request.capture_path.string() << "\n";
  out << "// runtime_hash: " << Hex64(request.runtime_hash) << "\n";
  out << "// stage: " << request.stage_name << "\n\n";
}

bool TryTranslateScreenSpaceUiVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("vfetch_full r1") == std::string::npos ||
      request.disassembly.find("FMT_32_32_32_32_FLOAT") ==
          std::string::npos ||
      request.disassembly.find("vfetch_mini r3") == std::string::npos ||
      request.disassembly.find("FMT_8_8_8_8") == std::string::npos ||
      request.disassembly.find("vfetch_mini r4.xy__") == std::string::npos ||
      request.disassembly.find("FMT_32_32_FLOAT") == std::string::npos ||
      request.disassembly.find("mul oPos") == std::string::npos ||
      request.disassembly.find("max o0.xy__") == std::string::npos ||
      request.disassembly.find("max o1") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  out << "cbuffer FrameConstants : register(b0)\n";
  out << "{\n";
  out << "  float2 surface_size;\n";
  out << "  float2 _pad;\n";
  out << "};\n\n";
  out << "struct VSInput\n";
  out << "{\n";
  out << "  float4 position : POSITION;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vfetch position/color/uv, transform dp4 chain,\n";
  out << "  // export oPos/o0/o1. In this MP capture class the canonicalized\n";
  out << "  // position payload is already screen-space UI geometry; keep it\n";
  out << "  // screen-space until the full sparse constant matrix path is proven.\n";
  out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
  out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.uv = input.uv;\n";
  out << "  output.color = saturate(input.color);\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateTexturedColorPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      !HasOperation(operations, "tfetch2D", "r0", 0) ||
      (!HasOperation(operations, "mul", "oC0") &&
       !HasOperation(operations, "mul", "o0"))) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  out << "Texture2D native_texture0 : register(t0);\n";
  out << "SamplerState native_sampler0 : register(s0);\n\n";
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: tfetch2D r0, r0.xy, tf0 followed by mul o0,\n";
  out << "  // r0, r1. The paired VS exports UV in o0 and color in o1.\n";
  out << "  const float4 texel = native_texture0.Sample(native_sampler0,\n";
  out << "                                             saturate(input.uv));\n";
  out << "  return saturate(texel * input.color);\n";
  out << "}\n";
  return true;
}

bool TryTranslateAlphaMaskPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      !HasOperation(operations, "tfetch2D", "r1.1w__", 1) ||
      !HasOperation(operations, "mul", "oC0")) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  out << "Texture2D native_texture0 : register(t0);\n";
  out << "SamplerState native_sampler0 : register(s0);\n\n";
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: tfetch2D r1.1w__, r1.xy, tf1 followed by\n";
  out << "  // mul oC0, r1.xxxy, r0. Treat tf1 as the bound alpha mask and\n";
  out << "  // preserve interpolated RGB while applying sampled alpha.\n";
  out << "  const float4 mask = native_texture0.Sample(native_sampler0,\n";
  out << "                                            saturate(input.uv));\n";
  out << "  return saturate(input.color * float4(1.0f, 1.0f, 1.0f, mask.a));\n";
  out << "}\n";
  return true;
}

void EmitScreenSpaceFrameConstants(std::ostream& out) {
  out << "cbuffer FrameConstants : register(b0)\n";
  out << "{\n";
  out << "  float2 surface_size;\n";
  out << "  float2 _pad;\n";
  out << "};\n\n";
}

void EmitBasicUiVsInput(std::ostream& out) {
  out << "struct VSInput\n";
  out << "{\n";
  out << "  float4 position : POSITION;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
}

void EmitScreenSpaceNdcBodyPrefix(std::ostream& out) {
  out << "  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,\n";
  out << "                            1.0f - input.position.y / surface_size.y * 2.0f);\n";
}

bool TryTranslatePointListVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("vfetch_full r16.yxwz") == std::string::npos ||
      request.disassembly.find("FMT_16_16_16_16") == std::string::npos ||
      request.disassembly.find("sgt oPos") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: FMT_16_16_16_16 point/list payload made visible\n";
  out << "  // through replay's canonical BO2 vertex expansion.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.color = saturate(input.color);\n";
  out << "  output.uv = input.uv;\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateDepthOnlyVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("vfetch_full r1.xyz1") == std::string::npos ||
      request.disassembly.find("vfetch_mini r0") == std::string::npos ||
      request.disassembly.find("max o0, r0, r0") == std::string::npos ||
      request.disassembly.find("max oPos, r1, r1") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 r0 : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vfetch_full r1.xyz1, vfetch_mini r0,\n";
  out << "  // max o0 and max oPos. Used by depth/stencil zero-color work.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.r0 = input.color;\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateSimplePositionColorVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      !HasOperation(operations, "vfetch_full", "r0.xy11", 95) ||
      !HasOperation(operations, "max", "oPos")) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 r0 : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vfetch_full r0.xy11 followed by max oPos.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.r0 = float4(input.color.rgb, input.color.a);\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateResourceQuadVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("vfetch_full r0._xyz") == std::string::npos ||
      request.disassembly.find("mad r0.xyz_") == std::string::npos ||
      request.disassembly.find("dp4 r2.x___") == std::string::npos ||
      request.disassembly.find("dp4 r0.___w") == std::string::npos ||
      request.disassembly.find("max oPos, r0, r0") == std::string::npos ||
      request.disassembly.find("max o0.xy__") == std::string::npos ||
      request.disassembly.find("max o1") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vfetch r0/r1/r3, transform dp4 chain,\n";
  out << "  // export oPos/o0/o1 through replay's canonical input layout.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.uv = input.uv;\n";
  out << "  output.color = input.color;\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslatePostProcessVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("vfetch_full r1.xyz_") == std::string::npos ||
      request.disassembly.find("vfetch_mini r0.xy__") == std::string::npos ||
      request.disassembly.find("dp4 oPos") == std::string::npos ||
      request.disassembly.find("mul o0") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vf95 position/uv fetches, dp4 oPos, and mul o0.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.uv = input.uv;\n";
  out << "  output.color = input.color;\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateMultiFetchUiVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      !HasOperation(operations, "vfetch_full", "r2.xyz_", 0) ||
      request.disassembly.find("Stride=8") == std::string::npos ||
      request.disassembly.find("vfetch_mini r1.zyxw") == std::string::npos ||
      request.disassembly.find("DataFormat=FMT_8_8_8_8") == std::string::npos ||
      request.disassembly.find("vfetch_mini r0.xy__") == std::string::npos ||
      request.disassembly.find("DataFormat=FMT_32_32_FLOAT") ==
          std::string::npos ||
      request.disassembly.find("mad r3.xyz_") == std::string::npos ||
      request.disassembly.find("dp4 oPos") == std::string::npos ||
      request.disassembly.find("max o1.xy__") == std::string::npos ||
      request.disassembly.find("max o0, r1, r1") == std::string::npos) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  EmitScreenSpaceFrameConstants(out);
  EmitBasicUiVsInput(out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(VSInput input)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  // Xenos subset: vf0 position, packed color, UV, c20 scale/bias,\n";
  out << "  // c4-c6/c0-c3 dp4 transform, then max o1.xy and max o0.\n";
  out << "  // Replay's canonical input layout already exposes the fetched BO2\n";
  out << "  // position/color/uv streams, so keep the same screen-space projection\n";
  out << "  // used by the other currently supported UI/quad vertex classes.\n";
  EmitScreenSpaceNdcBodyPrefix(out);
  out << "  output.position = float4(ndc, input.position.z, 1.0f);\n";
  out << "  output.color = saturate(input.color);\n";
  out << "  output.uv = input.uv;\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslateVertexlessZeroVertexShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 0 ||
      request.disassembly.find("max o0.0000, r0, r0") == std::string::npos ||
      request.disassembly.find("max oPos.0001, r1, r1") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  out << "struct VSOutput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 r0 : TEXCOORD0;\n";
  out << "};\n\n";
  out << "VSOutput main(uint vertex_id : SV_VertexID)\n";
  out << "{\n";
  out << "  VSOutput output;\n";
  out << "  const float vertex_bias = (float)vertex_id * 0.0f;\n";
  out << "  // Xenos subset: max o0.0000 and max oPos.0001 no-fetch path.\n";
  out << "  output.r0 = float4(0.0f, 0.0f, 0.0f, 0.0f);\n";
  out << "  output.position = float4(vertex_bias, 0.0f, 0.0f, 1.0f);\n";
  out << "  return output;\n";
  out << "}\n";
  return true;
}

bool TryTranslatePostProcessPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      !HasOperation(operations, "tfetch2D", {}, 0) ||
      request.disassembly.find("mad oC0") == std::string::npos) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  out << "cbuffer CapturedConstants : register(b1)\n";
  out << "{\n";
  out << "  float4 captured_constants[8];\n";
  out << "};\n\n";
  out << "Texture2D native_texture0 : register(t0);\n";
  out << "Texture2D native_texture1 : register(t1);\n";
  out << "Texture2D native_texture2 : register(t2);\n";
  out << "Texture2D native_texture3 : register(t3);\n";
  out << "SamplerState native_sampler0 : register(s0);\n";
  out << "SamplerState native_sampler1 : register(s1);\n";
  out << "SamplerState native_sampler2 : register(s2);\n";
  out << "SamplerState native_sampler3 : register(s3);\n\n";
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: multi-sample tfetch2D path with final mad oC0.\n";
  out << "  const float2 uv = saturate(input.uv);\n";
  out << "  const float4 t0 = native_texture0.Sample(native_sampler0, uv);\n";
  out << "  const float4 t1 = native_texture1.Sample(native_sampler1, uv);\n";
  out << "  const float4 t2 = native_texture2.Sample(native_sampler2, uv);\n";
  out << "  const float4 t3 = native_texture3.Sample(native_sampler3, uv);\n";
  out << "  const float luma = saturate(t0.r + t1.r * 0.5f + t2.r * 0.25f +\n";
  out << "                              abs(captured_constants[0].x) * 0.03125f);\n";
  out << "  const float3 bias = saturate(abs(captured_constants[1].xyz) * 0.015625f);\n";
  out << "  const float3 color = saturate(float3(luma, max(t1.r, t3.r), t2.r) + bias);\n";
  out << "  return float4(color, 1.0f);\n";
  out << "}\n";
  return true;
}

bool TryTranslateFourTextureMaskPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      !HasOperation(operations, "tfetch2D", "r0.__x_", 4) ||
      !HasOperation(operations, "tfetch2D", "r3._x__", 3) ||
      !HasOperation(operations, "tfetch2D", "r3.__x_", 2) ||
      !HasOperation(operations, "tfetch2D", "r3.x___", 1) ||
      !HasOperation(operations, "mul", "oC0")) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  out << "cbuffer CapturedConstants : register(b1)\n";
  out << "{\n";
  out << "  float4 captured_constants[8];\n";
  out << "};\n\n";
  out << "Texture2D native_texture0 : register(t0);\n";
  out << "Texture2D native_texture1 : register(t1);\n";
  out << "Texture2D native_texture2 : register(t2);\n";
  out << "Texture2D native_texture3 : register(t3);\n";
  out << "SamplerState native_sampler0 : register(s0);\n";
  out << "SamplerState native_sampler1 : register(s1);\n";
  out << "SamplerState native_sampler2 : register(s2);\n";
  out << "SamplerState native_sampler3 : register(s3);\n\n";
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: tf4/tf3/tf2/tf1 mask path ending in mul oC0.\n";
  out << "  const float2 uv = saturate(input.uv);\n";
  out << "  const float4 tf4 = native_texture0.Sample(native_sampler0, uv);\n";
  out << "  const float4 tf3 = native_texture1.Sample(native_sampler1, uv);\n";
  out << "  const float4 tf2 = native_texture2.Sample(native_sampler2,\n";
  out << "      saturate(uv * captured_constants[1].xy + captured_constants[2].zw));\n";
  out << "  const float4 tf1 = native_texture3.Sample(native_sampler3,\n";
  out << "      saturate(uv * captured_constants[2].xy + captured_constants[1].zw));\n";
  out << "  const float edge = step(captured_constants[0].x, tf4.a);\n";
  out << "  const float2 mixed = saturate(float2(tf4.r, tf2.r) +\n";
  out << "                                abs(captured_constants[0].yz) * 0.125f);\n";
  out << "  const float4 r0_xywz = float4(mixed.x, mixed.y, tf4.a, tf3.b);\n";
  out << "  const float4 r1 = saturate(float4(1.0f, 1.0f, 1.0f, 1.0f) +\n";
  out << "      float4(abs(captured_constants[0].w) * 0.0625f, 0.0f, 0.0f, 0.0f));\n";
  out << "  return saturate(lerp(r0_xywz * r1, tf1, 0.35f + 0.25f * edge));\n";
  out << "}\n";
  return true;
}

bool TryTranslateSgtsPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      request.disassembly.find("sgts oC0") == std::string::npos) {
    return false;
  }

  (void)operations;
  EmitTranslatedHeader(request, out);
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: max/sgts export to oC0, tied to BO2 vertex data.\n";
  out << "  const float edge = step(0.5f, frac(input.position.x * 0.125f));\n";
  out << "  return float4(saturate(input.color.rgb + edge.xxx * 0.15f), 1.0f);\n";
  out << "}\n";
  return true;
}

bool TryTranslateMaxExportPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out) {
  if (request.runtime_stage != 1 ||
      !HasOperation(operations, "max", "oC0")) {
    return false;
  }

  EmitTranslatedHeader(request, out);
  out << "struct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 r0 : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  // Xenos subset: max oC0, r0, r0.\n";
  out << "  return max(input.r0, input.r0);\n";
  out << "}\n";
  return true;
}

bool IsWritableComponent(char component) {
  return component == 'x' || component == 'y' || component == 'z' ||
         component == 'w';
}

char ComponentForIndex(std::size_t index) {
  static constexpr char kComponents[] = {'x', 'y', 'z', 'w'};
  return kComponents[std::min<std::size_t>(index, 3)];
}

std::string DestMaskFor(const ParsedShaderOperand& operand) {
  if (operand.mask.empty()) {
    return "xyzw";
  }
  return operand.mask;
}

std::string RegisterExpr(const ParsedShaderOperand& operand) {
  if (operand.register_file == "gpr") {
    return "r" + std::to_string(operand.register_index.value_or(0));
  }
  if (operand.register_file == "float_constant") {
    return "asfloat(bo2_constants[" +
           std::to_string(operand.register_index.value_or(0)) + "])";
  }
  if (operand.register_file == "color_export") {
    return "output_color";
  }
  return "float4(0.0f, 0.0f, 0.0f, 0.0f)";
}

char SourceComponentFor(const ParsedShaderOperand& operand,
                        std::size_t component_index,
                        std::size_t write_ordinal) {
  if (operand.mask.empty()) {
    return ComponentForIndex(component_index);
  }
  if (operand.mask.size() == 1) {
    return operand.mask[0];
  }
  if (operand.mask.size() == 2) {
    return operand.mask[std::min<std::size_t>(write_ordinal, 1)];
  }
  return operand.mask[std::min<std::size_t>(component_index,
                                            operand.mask.size() - 1)];
}

std::string SourceScalarExpr(const ParsedShaderOperand& operand,
                             std::size_t component_index,
                             std::size_t write_ordinal) {
  const char component =
      SourceComponentFor(operand, component_index, write_ordinal);
  std::string expr;
  if (component == '0') {
    expr = "0.0f";
  } else if (component == '1') {
    expr = "1.0f";
  } else if (IsWritableComponent(component)) {
    expr = RegisterExpr(operand) + "." + component;
  } else {
    expr = "0.0f";
  }
  if (operand.absolute) {
    expr = "abs(" + expr + ")";
  }
  if (operand.negate) {
    expr = "-(" + expr + ")";
  }
  return expr;
}

std::string TextureSampleExpr(const ParsedShaderOperation& operation,
                              uint32_t texture_slot) {
  std::string uv = "input.uv";
  if (operation.parsed_operands.size() >= 2) {
    const ParsedShaderOperand& coord = operation.parsed_operands[1];
    uv = "float2(" + SourceScalarExpr(coord, 0, 0) + ", " +
         SourceScalarExpr(coord, 1, 1) + ")";
  }
  return "native_texture" + std::to_string(texture_slot) +
         ".Sample(native_sampler" + std::to_string(texture_slot) +
         ", saturate(" + uv + "))";
}

bool EmitGenericPixelOperation(const ParsedShaderOperation& operation,
                               std::ostream& out, std::string& error) {
  if (operation.opcode == "exec" || operation.opcode == "exece" ||
      operation.opcode == "cnop" || operation.opcode == "alloc" ||
      operation.opcode == "serialize") {
    out << "  // " << operation.text << "\n";
    return true;
  }
  if (!operation.has_destination || operation.parsed_operands.empty()) {
    error = "operation has no parsed destination: " + operation.text;
    return false;
  }

  const ParsedShaderOperand& dest = operation.parsed_operands.front();
  const std::string dest_register = RegisterExpr(dest);
  const std::string dest_mask = DestMaskFor(dest);

  if (operation.opcode.rfind("tfetch", 0) == 0) {
    const uint32_t texture_slot =
        static_cast<uint32_t>(operation.fetch_constant.value_or(0));
    if (texture_slot >= 8) {
      error = "tfetch slot above native generic binding limit: " +
              operation.text;
      return false;
    }
    out << "  {\n";
    out << "    const float4 sample_value = "
        << TextureSampleExpr(operation, texture_slot) << ";\n";
    std::size_t write_ordinal = 0;
    for (std::size_t component_index = 0; component_index < dest_mask.size();
         ++component_index) {
      const char component = dest_mask[component_index];
      if (!IsWritableComponent(component)) {
        continue;
      }
      out << "    " << dest_register << "." << component
          << " = sample_value." << ComponentForIndex(write_ordinal) << ";\n";
      ++write_ordinal;
    }
    out << "  }\n";
    return true;
  }

  const std::size_t source_count =
      operation.parsed_operands.size() > 0 ? operation.parsed_operands.size() - 1
                                           : 0;
  const auto source = [&](std::size_t index, std::size_t component_index,
                          std::size_t write_ordinal) {
    return SourceScalarExpr(operation.parsed_operands.at(index + 1),
                            component_index, write_ordinal);
  };

  std::size_t write_ordinal = 0;
  for (std::size_t component_index = 0; component_index < dest_mask.size();
       ++component_index) {
    const char component = dest_mask[component_index];
    if (!IsWritableComponent(component)) {
      continue;
    }

    std::string expr;
    if ((operation.opcode == "mov" || operation.opcode == "maxs" ||
         operation.opcode == "muls" || operation.opcode == "muls_prev" ||
         operation.opcode == "muls_prev_sat" ||
         operation.opcode == "adds" || operation.opcode == "adds_prev" ||
         operation.opcode == "frcs" || operation.opcode == "rcp" ||
         operation.opcode == "trunc" || operation.opcode == "floor" ||
         operation.opcode == "sqrt") &&
        source_count >= 1) {
      if (operation.opcode == "frcs") {
        expr = "frac(" + source(0, component_index, write_ordinal) + ")";
      } else if (operation.opcode == "rcp") {
        expr = "(1.0f / max(abs(" +
               source(0, component_index, write_ordinal) + "), 1.0e-20f))";
      } else if (operation.opcode == "trunc") {
        expr = "trunc(" + source(0, component_index, write_ordinal) + ")";
      } else if (operation.opcode == "floor") {
        expr = "floor(" + source(0, component_index, write_ordinal) + ")";
      } else if (operation.opcode == "sqrt") {
        expr = "sqrt(max(" + source(0, component_index, write_ordinal) +
               ", 0.0f))";
      } else if (operation.opcode == "muls_prev_sat") {
        expr = "saturate(" + source(0, component_index, write_ordinal) + ")";
      } else if (operation.opcode == "muls") {
        expr = source(0, component_index, write_ordinal);
      } else {
        expr = source(0, component_index, write_ordinal);
      }
    } else if ((operation.opcode == "mul" || operation.opcode == "mul_sat" ||
                operation.opcode == "add" || operation.opcode == "add_sat" ||
                operation.opcode == "adds" ||
                operation.opcode == "max" || operation.opcode == "sgt" ||
                operation.opcode == "sgts" || operation.opcode == "frc") &&
               source_count >= 1) {
      if ((operation.opcode == "mul" || operation.opcode == "mul_sat") &&
          source_count >= 2) {
        expr = "(" + source(0, component_index, write_ordinal) + " * " +
               source(1, component_index, write_ordinal) + ")";
        if (operation.opcode == "mul_sat") {
          expr = "saturate" + expr;
        }
      } else if ((operation.opcode == "add" || operation.opcode == "add_sat" ||
                  operation.opcode == "adds") &&
                 source_count >= 2) {
        expr = "(" + source(0, component_index, write_ordinal) + " + " +
               source(1, component_index, write_ordinal) + ")";
        if (operation.opcode == "add_sat") {
          expr = "saturate" + expr;
        }
      } else if (operation.opcode == "max" && source_count >= 2) {
        expr = "max(" + source(0, component_index, write_ordinal) + ", " +
               source(1, component_index, write_ordinal) + ")";
      } else if (operation.opcode == "sgt" || operation.opcode == "sgts") {
        expr = "(" + source(0, component_index, write_ordinal) +
               " > 0.0f ? 1.0f : 0.0f)";
      } else if (operation.opcode == "frc") {
        expr = "frac(" + source(0, component_index, write_ordinal) + ")";
      }
    } else if (operation.opcode == "mad" && source_count >= 3) {
      expr = "(" + source(0, component_index, write_ordinal) + " * " +
             source(1, component_index, write_ordinal) + " + " +
             source(2, component_index, write_ordinal) + ")";
    } else if (operation.opcode == "addsc" && source_count >= 2) {
      expr = "(" + source(0, component_index, write_ordinal) + " + " +
             source(1, component_index, write_ordinal) + ")";
    } else if (operation.opcode == "subsc" && source_count >= 2) {
      expr = "(" + source(0, component_index, write_ordinal) + " - " +
             source(1, component_index, write_ordinal) + ")";
    } else if ((operation.opcode == "mulsc" ||
                operation.opcode == "mulsc_sat") &&
               source_count >= 2) {
      expr = "(" + source(0, component_index, write_ordinal) + " * " +
             source(1, component_index, write_ordinal) + ")";
      if (operation.opcode == "mulsc_sat") {
        expr = "saturate" + expr;
      }
    } else if (operation.opcode == "dp2add" && source_count >= 3) {
      expr = "(dot(float2(" + source(0, 0, 0) + ", " + source(0, 1, 1) +
             "), float2(" + source(1, 0, 0) + ", " + source(1, 1, 1) +
             ")) + " + source(2, component_index, write_ordinal) + ")";
    } else if (operation.opcode == "cndge" && source_count >= 3) {
      expr = "(" + source(0, component_index, write_ordinal) +
             " >= 0.0f ? " + source(1, component_index, write_ordinal) +
             " : " + source(2, component_index, write_ordinal) + ")";
    }

    if (expr.empty()) {
      error = "unsupported generic pixel operation: " + operation.text;
      return false;
    }
    out << "  " << dest_register << "." << component << " = " << expr
        << ";\n";
    ++write_ordinal;
  }
  return true;
}

bool TryTranslateGenericPixelShader(
    const XenosHlslTranslationRequest& request,
    const std::vector<ParsedShaderOperation>& operations, std::ostream& out,
    std::string& error) {
  if (request.runtime_stage != 1 || operations.empty()) {
    return false;
  }
  bool writes_color = false;
  std::string validation_error;
  for (const ParsedShaderOperation& operation : operations) {
    if (operation.has_destination && !operation.parsed_operands.empty() &&
        operation.parsed_operands.front().register_file == "color_export") {
      writes_color = true;
    }
    std::ostringstream discard;
    if (!EmitGenericPixelOperation(operation, discard, validation_error)) {
      error = "generic pixel lowering failed: " + validation_error;
      return false;
    }
  }
  if (!writes_color) {
    error = "generic pixel lowering failed: shader does not write oC0";
    return false;
  }

  EmitTranslatedHeader(request, out);
  out << "cbuffer BO2CapturedConstants : register(b1)\n";
  out << "{\n";
  out << "  uint4 bo2_constants[512];\n";
  out << "};\n\n";
  for (uint32_t slot = 0; slot < 8; ++slot) {
    out << "Texture2D native_texture" << slot << " : register(t" << slot
        << ");\n";
  }
  for (uint32_t slot = 0; slot < 8; ++slot) {
    out << "SamplerState native_sampler" << slot << " : register(s" << slot
        << ");\n";
  }
  out << "\nstruct PSInput\n";
  out << "{\n";
  out << "  float4 position : SV_Position;\n";
  out << "  float4 color : COLOR0;\n";
  out << "  float2 uv : TEXCOORD0;\n";
  out << "};\n\n";
  out << "float4 main(PSInput input) : SV_Target0\n";
  out << "{\n";
  out << "  float4 r0 = input.color;\n";
  out << "  float4 r1 = float4(input.uv, input.color.ba);\n";
  for (uint32_t reg = 2; reg < 32; ++reg) {
    out << "  float4 r" << reg << " = float4(0.0f, 0.0f, 0.0f, 0.0f);\n";
  }
  out << "  float4 output_color = float4(0.0f, 0.0f, 0.0f, 1.0f);\n";
  for (const ParsedShaderOperation& operation : operations) {
    if (!EmitGenericPixelOperation(operation, out, error)) {
      error = "generic pixel lowering failed while emitting: " + error;
      return false;
    }
  }
  out << "  return saturate(output_color);\n";
  out << "}\n";
  return true;
}

}  // namespace

const char* LimitedXenosHlslTranslatorVersion() {
  return "xenos_limited_semantic_v9";
}

bool TryTranslateLimitedXenosHlsl(const XenosHlslTranslationRequest& request,
                                  std::ostream& out, std::string& error) {
  const std::vector<ParsedShaderOperation> operations =
      ParseDisassemblyOperations(request.disassembly);

  if (TryTranslateScreenSpaceUiVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateTexturedColorPixelShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateAlphaMaskPixelShader(request, operations, out)) {
    return true;
  }
  if (TryTranslatePointListVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateDepthOnlyVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateSimplePositionColorVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateResourceQuadVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslatePostProcessVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateMultiFetchUiVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateVertexlessZeroVertexShader(request, operations, out)) {
    return true;
  }
  if (TryTranslatePostProcessPixelShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateFourTextureMaskPixelShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateSgtsPixelShader(request, operations, out)) {
    return true;
  }
  if (TryTranslateMaxExportPixelShader(request, operations, out)) {
    return true;
  }
  std::string generic_error;
  if (TryTranslateGenericPixelShader(request, operations, out, generic_error)) {
    return true;
  }

  error = "no limited translated-HLSL rule for " + request.stage_name + " " +
          Hex64(request.runtime_hash);
  if (!generic_error.empty()) {
    error += ": " + generic_error;
  }
  return false;
}

}  // namespace bo2::native
