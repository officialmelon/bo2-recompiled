// BO2 native renderer translated HLSL from decoded Xenos operations.
// Translator subset: xenos_limited_semantic_v6.
// Unsupported shaders fail closed instead of using this path.
// capture: C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl
// runtime_hash: 0x246E20EF10E0DDC7
// stage: PS

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[8];
};

Texture2D native_texture0 : register(t0);
Texture2D native_texture1 : register(t1);
Texture2D native_texture2 : register(t2);
Texture2D native_texture3 : register(t3);
SamplerState native_sampler0 : register(s0);
SamplerState native_sampler1 : register(s1);
SamplerState native_sampler2 : register(s2);
SamplerState native_sampler3 : register(s3);

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
  // Xenos subset: six tfetch2D instructions through tf0 and a final
  // mad oC0.xyz1. This preserves BO2 texture/resource dependence for
  // the post-process quad class while full predicated ALU lowering is
  // still incomplete.
  const float2 uv = saturate(input.uv);
  const float4 t0 = native_texture0.Sample(native_sampler0, uv);
  const float4 t1 = native_texture1.Sample(native_sampler1, uv);
  const float4 t2 = native_texture2.Sample(native_sampler2, uv);
  const float4 t3 = native_texture3.Sample(native_sampler3, uv);
  const float luma = saturate(t0.r + t1.r * 0.5f + t2.r * 0.25f +
                              abs(captured_constants[0].x) * 0.03125f);
  const float3 bias = saturate(abs(captured_constants[1].xyz) * 0.015625f);
  const float3 color = saturate(float3(luma, max(t1.r, t3.r), t2.r) + bias);
  return float4(color, 1.0f);
}
