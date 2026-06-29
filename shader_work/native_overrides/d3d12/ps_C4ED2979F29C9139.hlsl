struct PSIn
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[8];
};

Texture2D native_texture0 : register(t0);
SamplerState native_sampler0 : register(s0);

float4 PSMain(PSIn input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float3 tint = float3(0.25f + 0.75f * uv.x,
                             0.25f + 0.75f * uv.y,
                             1.0f);
  const float constant_bias = saturate(abs(captured_constants[0].x) * 8.0f);
  const float4 captured_texture = native_texture0.Sample(native_sampler0, uv);
  const float3 base_color =
      saturate(input.color.rgb * tint +
               float3(constant_bias, constant_bias * 0.25f, 0.0f));
  return float4(saturate(lerp(base_color, captured_texture.rgb, 0.35f) +
                         captured_texture.aaa * 0.05f),
                1.0f);
}
