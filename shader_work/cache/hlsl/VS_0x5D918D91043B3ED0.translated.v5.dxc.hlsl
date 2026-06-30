// BO2 native renderer translated HLSL from decoded Xenos operations.
// Translator subset: xenos_limited_semantic_v5.
// Unsupported shaders fail closed instead of using this path.
// capture: C:\Users\braxt\bo2-recompiled\native_captures\shader_probe_capture_007\events.jsonl
// runtime_hash: 0x5D918D91043B3ED0
// stage: VS

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

VSOutput main(VSInput input)
{
  VSOutput output;
  // Xenos: vfetch r0/r1/r3, transform dp4 chain, export oPos/o0/o1.
  // Replay canonicalization has already decoded vf95 into POSITION,
  // COLOR0, and TEXCOORD0 attributes for the supported draw class.
  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,
                            1.0f - input.position.y / surface_size.y * 2.0f);
  output.position = float4(ndc, input.position.z, 1.0f);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
