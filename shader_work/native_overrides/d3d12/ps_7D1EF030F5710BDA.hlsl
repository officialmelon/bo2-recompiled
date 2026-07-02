// BO2 native renderer manual D3D12 override for runtime PS 0x7D1EF030F5710BDA.
// Semantic Xenos subset from live_d3d12_mp_010:
//   tfetch2D r1._x__ from tf1
//   tfetch2D r1._x__/__y_/___z from tf2
//   mul r0._yzw, r1.yyzw, r0.xxyz
//   mad r0.x, abs(r1.x), c235.z, c254.x
//   mul o0.xyz0, r0.yzww, r0.x
// Keep the override scalar/mask based. The older broad RGB texture averaging
// produced the green/purple false-color MP menu image.

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[512];
};

Texture2D native_texture0 : register(t0);
Texture2D native_texture1 : register(t1);
Texture2D native_texture2 : register(t2);
Texture2D native_texture3 : register(t3);
SamplerState native_sampler0 : register(s0);
SamplerState native_sampler1 : register(s1);
SamplerState native_sampler2 : register(s2);
SamplerState native_sampler3 : register(s3);

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
  // Follow the decoded Xenos scalar path closely enough to preserve the BO2
  // animated atlas coordinates. The backend repacks captured constants by
  // semantic register index, so c72 is captured_constants[72], not a raw PM4
  // dword offset.
  const float4 c72 = captured_constants[72];
  const float4 c235 = captured_constants[235];
  const float4 c252 = captured_constants[252];
  const float4 c253 = captured_constants[253];
  const float4 c254 = captured_constants[254];
  const float4 c255 = captured_constants[255];

  const float2 uv = input.uv;
  float4 r0 = input.color;
  float4 r1 = float4(uv, uv);
  float4 r2 = c72.w * c255.xzwy;

  r2.y = c235.w + r2.y;
  r1.zw = frac(r2.xw);
  r2.x = (r2.z >= 0.0f) ? frac(abs(r2.z)) : -frac(abs(r2.z));
  r1.w = dot(r1.xy, c235.xy) + r1.w;
  r1.z = dot(r1.xy, c253.zw) + r1.z;
  r1.x = (r2.y >= 0.0f) ? frac(abs(r2.y)) : -frac(abs(r2.y));
  r2.yz = r1.zw * c252.ww;
  r1.zw = r2.yx + c252.yx;

  const float tf1_x = native_texture0.Sample(native_sampler0, r1.xz).r;
  r1.x = tf1_x;
  r1.x = r1.w + r2.x;
  r1.y = abs(r1.y) * abs(r1.y);
  r2.yz = r2.yz + c252.yz;
  r2.xw = (-r1.y) * c253.xy + r2.zz;

  const float tf2_x = native_texture1.Sample(native_sampler1, r2.xy).r;
  const float tf2_y = native_texture2.Sample(native_sampler2, r2.wy).r;
  const float tf2_z = native_texture3.Sample(native_sampler3, r2.zy).r;
  r1.x = tf2_x;
  r1.y = tf2_y;
  r1.z = tf2_z;

  r0.yzw = r1.yzw * r0.xyz;
  r0.x = abs(r1.x) * c235.z + c254.x;
  return float4(saturate(r0.yzw * r0.x), 0.0f);
}
