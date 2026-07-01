// BO2 native renderer manual D3D12 override for runtime PS 0x79E1F538A5074A65.
// MP003 binds this shader for three no-texture, screen-space indexed quads.
// The captured payload is large enough that the current semantic disassembler
// times out; keep this override conservative until the full Xenos shader is
// decoded. It preserves the captured vertex color/alpha dependency and does
// not introduce any synthetic texture or diagnostic color input.

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
  const float alpha = saturate(input.color.a);
  return float4(saturate(input.color.rgb * max(alpha, 1.0f / 255.0f)), alpha);
}
