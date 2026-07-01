// BO2 native renderer manual D3D12 override for runtime VS 0x3C4F6D40D699817B.
// Decoded Xenos subset:
//   vfetch position/color/uv from vf95
//   transform dp4 chain
//   export oPos, o0.xy, o1
// For the current MP capture class, the decoded vf95 position payload is
// already screen-space UI geometry.

cbuffer FrameConstants : register(b0)
{
  float2 surface_size;
  float2 _pad;
};

struct VSInput
{
  float4 position : POSITION;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

struct VSOutput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
  VSOutput output;
  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,
                            1.0f - input.position.y / surface_size.y * 2.0f);
  output.position = float4(ndc, input.position.z, 1.0f);
  output.uv = input.uv;
  output.color = saturate(input.color);
  return output;
}
