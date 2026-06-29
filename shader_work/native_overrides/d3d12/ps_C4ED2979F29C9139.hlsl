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

float4 PSMain(PSIn input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float3 tint = float3(0.25f + 0.75f * uv.x,
                             0.25f + 0.75f * uv.y,
                             1.0f);
  const float constant_bias = saturate(abs(captured_constants[0].x) * 8.0f);
  return float4(saturate(input.color.rgb * tint +
                         float3(constant_bias, constant_bias * 0.25f, 0.0f)),
                1.0f);
}
