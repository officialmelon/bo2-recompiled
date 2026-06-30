// BO2 native renderer translated HLSL from decoded Xenos operations.
// Translator subset: xenos_vertexless_point_v8.
// runtime_hash: 0xB6C9863F710683EC
// stage: VS

struct VSOutput
{
  float4 position : SV_Position;
  float4 r0 : TEXCOORD0;
};

VSOutput main(uint vertex_id : SV_VertexID)
{
  VSOutput output;
  // Xenos: max o0.0000, r0, r0; max oPos.0001, r1, r1.
  // This runtime shader declares no vertex fetches or constants in semantic IR.
  const float vertex_bias = (float)vertex_id * 0.0f;
  output.r0 = float4(0.0f, 0.0f, 0.0f, 0.0f);
  output.position = float4(vertex_bias, 0.0f, 0.0f, 1.0f);
  return output;
}
