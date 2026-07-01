// BO2 native renderer manual D3D12 override for runtime PS 0x8645E8BA65E424B2.
// The decoded shader has constant-driven coordinate perturbation, one tf2
// sample, four tf1 samples, and a final color multiply. This override keeps the
// captured texture dependencies live while full Xenos ALU lowering is built.

Texture2D native_texture0 : register(t0);
Texture2D native_texture1 : register(t1);
Texture2D native_texture2 : register(t2);
Texture2D native_texture3 : register(t3);
Texture2D native_texture4 : register(t4);
SamplerState native_sampler0 : register(s0);
SamplerState native_sampler1 : register(s1);
SamplerState native_sampler2 : register(s2);
SamplerState native_sampler3 : register(s3);
SamplerState native_sampler4 : register(s4);

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float4 tf2 = native_texture0.Sample(native_sampler0, uv);
  const float4 tf1a = native_texture1.Sample(native_sampler1, uv);
  const float4 tf1b = native_texture2.Sample(native_sampler2, uv);
  const float4 tf1c = native_texture3.Sample(native_sampler3, uv);
  const float4 tf1d = native_texture4.Sample(native_sampler4, uv);
  const float4 combined =
      saturate(tf2 * 0.45f + tf1a * 0.25f + tf1b * 0.15f +
               tf1c * 0.10f + tf1d * 0.05f);
  return saturate(combined * max(input.color, 0.35f.xxxx));
}
