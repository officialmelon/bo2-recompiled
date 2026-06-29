struct PSIn
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSIn input) : SV_Target0
{
  const float2 uv = saturate(input.uv);
  const float3 tint = float3(0.25f + 0.75f * uv.x,
                             0.25f + 0.75f * uv.y,
                             1.0f);
  return float4(saturate(input.color.rgb * tint), 1.0f);
}
