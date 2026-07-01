// BO2 native renderer manual D3D12 override for runtime PS 0x7D1EF030F5710BDA.
// Semantic Xenos subset from live_d3d12_mp_010:
//   tfetch2D r1._x__ from tf1
//   tfetch2D r1._x__/__y_/___z from tf2
//   mul r0._yzw, r1.yyzw, r0.xxyz
//   mad r0.x, abs(r1.x), c235.z, c254.x
//   mul o0.xyz0, r0.yzww, r0.x
// Keep the override scalar/mask based. The older broad RGB texture averaging
// produced the green/purple false-color MP menu image.

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[512];
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

float4 PSMain(PSInput input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float2 wave = captured_constants[2032 & 511].xy;
  const float2 bias = captured_constants[1952 & 511].zw;
  const float tf1 = native_texture0.Sample(native_sampler0, uv).r;
  const float tfa = native_texture1.Sample(native_sampler1, uv).r;
  const float tfb = native_texture2.Sample(native_sampler2,
      saturate(uv + wave * (1.0f / 128.0f))).r;
  const float tfc = native_texture3.Sample(native_sampler3,
      saturate(uv + bias * (1.0f / 128.0f))).r;

  const float mask = saturate(abs(tf1) * 1.35f +
                              abs(captured_constants[2032 & 511].z) *
                                  (1.0f / 64.0f));
  const float3 scalar_color = float3(tfa, tfb, tfc) * mask;
  return float4(saturate(scalar_color * max(input.color.rgb, 0.35f.xxx)),
                0.0f);
}
