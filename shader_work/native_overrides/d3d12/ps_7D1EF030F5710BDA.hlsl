// BO2 native renderer manual D3D12 override for runtime PS 0x7D1EF030F5710BDA.
// Decoded Xenos subset includes tfetch2D from tf1/tf2 and final o0 export.
// This override preserves the captured texture dependency while the complete
// constant-driven address perturbation and ALU lowering is still being built.

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

float4 PSMain(PSInput input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float4 base = native_texture0.Sample(native_sampler0, uv);
  const float4 detail0 = native_texture1.Sample(native_sampler1, uv);
  const float4 detail1 = native_texture2.Sample(native_sampler2, uv);
  const float4 detail2 = native_texture3.Sample(native_sampler3, uv);
  const float3 mixed = saturate(base.rgb * 0.50f +
                                detail0.rgb * 0.25f +
                                detail1.rgb * 0.15f +
                                detail2.rgb * 0.10f);
  return float4(saturate(mixed * max(input.color.rgb, 0.25f.xxx)),
                saturate(max(base.a, input.color.a)));
}
