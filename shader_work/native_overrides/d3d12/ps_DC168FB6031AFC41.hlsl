// BO2 native renderer manual D3D12 override for runtime PS 0xDC168FB6031AFC41.
// Decoded Xenos subset:
//   constant-adjusted tfetch2D r1, r1.zy, tf1
//   mul o0, r1, r0
// The override keeps the captured texture dependency and uses replay UVs until
// full constant/register lowering is implemented.

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
