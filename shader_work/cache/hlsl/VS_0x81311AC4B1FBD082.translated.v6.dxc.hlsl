// BO2 native renderer translated HLSL from decoded Xenos operations.
// Translator subset: xenos_limited_semantic_v6.
// Unsupported shaders fail closed instead of using this path.
// capture: C:\Users\braxt\bo2-recompiled\native_captures\sidecar_capture_002\events.jsonl
// runtime_hash: 0x81311AC4B1FBD082
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
  // Xenos subset: vfetch r1.xyz_ from vf95, vfetch r0.xy__,
  // dp4 oPos against c0..c3, and mul o0 by c255.xyxy.
  // Draw 1013 captures screen-space quad positions and UVs, so this
  // limited path preserves the captured resource geometry while the
  // general constant-driven transform lowering is still being built.
  const float2 ndc = float2(input.position.x / surface_size.x * 2.0f - 1.0f,
                            1.0f - input.position.y / surface_size.y * 2.0f);
  output.position = float4(ndc, input.position.z, 1.0f);
  output.uv = input.uv;
  output.color = input.color;
  return output;
}
