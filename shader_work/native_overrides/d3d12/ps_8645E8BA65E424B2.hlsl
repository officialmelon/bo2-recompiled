// BO2 native renderer manual D3D12 override for runtime PS 0x8645E8BA65E424B2.
// Semantic Xenos subset from live_d3d12_mp_010:
//   tfetch2D r4.x from tf2
//   four tfetch2D scalar channels from tf1
//   ALU builds r1 and scalar predicate/mask r0
//   mul o0, r1, r0
// Keep this scalar/mask based. The older RGB weighted average was useful for
// bring-up, but it invented the strong green/purple false-color background.

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[512];
};

// PM4 constant upload indices in captures are dword/register-file offsets.
// The D3D12 native backend stores them as sparse float4 slots using index >> 3.
static const uint kBo2Const244 = 1952u >> 3;
static const uint kBo2Const245 = kBo2Const244 + 1u;
static const uint kBo2Const246 = kBo2Const244 + 2u;
static const uint kBo2Const247 = kBo2Const244 + 3u;
static const uint kBo2Const254 = 2032u >> 3;

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
  const float2 uv = input.uv;
  const float2 c232 = captured_constants[kBo2Const244].xy;
  const float2 c233 = captured_constants[kBo2Const245].zw;
  const float2 c234 = captured_constants[kBo2Const246].xy;
  const float2 c235 = captured_constants[kBo2Const247].zw;

  const float tf2 = native_texture0.Sample(native_sampler0,
      uv + c233 * (1.0f / 128.0f)).r;
  const float tf1a = native_texture1.Sample(native_sampler1,
      uv + c232 * (1.0f / 128.0f)).r;
  const float tf1b = native_texture2.Sample(native_sampler2,
      uv + c234 * (1.0f / 128.0f)).r;
  const float tf1c = native_texture3.Sample(native_sampler3,
      uv + c235 * (1.0f / 128.0f)).r;
  const float tf1d = native_texture4.Sample(native_sampler4, uv).r;

  const float mask = saturate(max(tf2, max(tf1a, tf1b)) +
                              abs(captured_constants[kBo2Const254].w) *
                                  (1.0f / 64.0f));
  const float3 scalar_color = float3(tf1a, tf1b, max(tf1c, tf1d)) * mask;
  const float alpha = saturate(mask * max(input.color.a, 0.5f));
  return float4(saturate(scalar_color * max(input.color.rgb, 0.35f.xxx)),
                alpha);
}
