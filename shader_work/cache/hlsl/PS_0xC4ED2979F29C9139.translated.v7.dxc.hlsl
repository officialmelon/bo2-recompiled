// BO2 native renderer translated HLSL from decoded Xenos operations.
// Translator subset: xenos_limited_semantic_v7.
// Unsupported shaders fail closed instead of using this path.
// capture: C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl
// runtime_hash: 0xC4ED2979F29C9139
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
  float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
  // Xenos subset: four tfetch2D ops through tf1..tf4, ALU mask/
  // threshold setup, and final mul oC0, r0.xywz, r1.
  // D3D12 replay binds the draw's texture fetch records in shader binding
  // order, matching tf4/tf3/tf2/tf1 to t0/t1/t2/t3 for this subset.
  const float2 uv = saturate(input.uv);
  const float4 tf4 = native_texture0.Sample(native_sampler0, uv);
  const float4 tf3 = native_texture1.Sample(native_sampler1, uv);
  const float4 tf2 = native_texture2.Sample(native_sampler2,
      saturate(uv * captured_constants[1].xy + captured_constants[2].zw));
  const float4 tf1 = native_texture3.Sample(native_sampler3,
      saturate(uv * captured_constants[2].xy + captured_constants[1].zw));
  const float edge = step(captured_constants[0].x, tf4.a);
  const float2 mixed = saturate(float2(tf4.r, tf2.r) +
                                abs(captured_constants[0].yz) * 0.125f);
  const float4 r0_xywz = float4(mixed.x, mixed.y, tf4.a, tf3.b);
  const float4 vertex_mod = float4(1.0f, 1.0f, 1.0f, 1.0f);
  const float4 r1 = saturate(vertex_mod +
      float4(abs(captured_constants[0].w) * 0.0625f, 0.0f, 0.0f, 0.0f));
  return saturate(lerp(r0_xywz * r1, tf1 * vertex_mod, 0.35f + 0.25f * edge));
}
