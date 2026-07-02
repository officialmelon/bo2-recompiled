// BO2 native renderer manual D3D12 override for runtime PS 0xFF01D28E1EF3A880.
// Decoded Xenos subset:
//   tfetch2D r1, r1.xy, tf1
//   mul o0, r1.xxxy, r0
// The paired VS exports glyph UV in TEXCOORD0 and vertex color in COLOR0.

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
  const float alpha = saturate(max(max(texel.r, texel.g), max(texel.b, texel.a)));
  return float4(saturate(input.color.rgb), saturate(input.color.a * alpha));
}
