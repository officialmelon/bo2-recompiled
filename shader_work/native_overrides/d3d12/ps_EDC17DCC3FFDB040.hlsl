// BO2 native renderer manual D3D12 override for runtime PS 0xEDC17DCC3FFDB040.
// Decoded Xenos subset:
//   tfetch2D r0, r0.xy, tf0
//   mul o0, r0, r1
// The paired VS exports UV in TEXCOORD0 and vertex color in COLOR0.

Texture2D native_texture0 : register(t0);
SamplerState native_sampler0 : register(s0);

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
  const float4 texel = native_texture0.Sample(native_sampler0, input.uv);
  return saturate(texel * input.color);
}
