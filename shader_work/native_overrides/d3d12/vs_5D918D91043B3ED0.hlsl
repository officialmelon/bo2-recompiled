cbuffer FrameConstants : register(b0)
{
  float2 surface_size;
  float2 _pad;
};

struct VSIn
{
  float4 position : POSITION;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

struct VSOut
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(VSIn input)
{
  VSOut output;
  const float2 ndc =
      float2(input.position.x / surface_size.x * 2.0f - 1.0f,
             1.0f - input.position.y / surface_size.y * 2.0f);
  output.position = float4(ndc, input.position.z, 1.0f);
  output.color = input.color;
  output.uv = input.uv;
  return output;
}
