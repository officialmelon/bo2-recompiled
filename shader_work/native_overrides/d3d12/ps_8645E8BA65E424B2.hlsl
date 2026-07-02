// BO2 native renderer manual D3D12 override for runtime PS 0x8645E8BA65E424B2.
// Semantic Xenos subset from live_d3d12_mp_010:
//   tfetch2D r4.x from tf2
//   four tfetch2D scalar channels from tf1
//   ALU builds r1 and scalar predicate/mask r0
//   mul o0, r1, r0
// Keep this scalar/mask based. The older RGB weighted average was useful for
// bring-up, but it invented the strong green/purple false-color background.

cbuffer CapturedConstants : register(b1)
{
  float4 captured_constants[512];
};

Texture2D native_texture0 : register(t0);
Texture2D native_texture1 : register(t1);
Texture2D native_texture2 : register(t2);
Texture2D native_texture3 : register(t3);
Texture2D native_texture4 : register(t4);
SamplerState native_sampler0 : register(s0);
SamplerState native_sampler1 : register(s1);
SamplerState native_sampler2 : register(s2);
SamplerState native_sampler3 : register(s3);
SamplerState native_sampler4 : register(s4);

struct PSInput
{
  float4 position : SV_Position;
  float4 color : COLOR0;
  float2 uv : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target0
{
  const float4 c72 = captured_constants[72];
  const float4 c73 = captured_constants[73];
  const float4 c232 = captured_constants[232];
  const float4 c233 = captured_constants[233];
  const float4 c234 = captured_constants[234];
  const float4 c235 = captured_constants[235];
  const float4 c252 = captured_constants[252];
  const float4 c253 = captured_constants[253];
  const float4 c254 = captured_constants[254];
  const float4 c255 = captured_constants[255];

  float4 r0 = input.color;
  float4 r1 = float4(input.uv, input.uv);
  float4 r2 = r1.yxyx * c255.yzxy;
  float4 r3 = 0.0f;
  float4 r4 = 0.0f;
  float4 r5 = 0.0f;
  float4 r6 = 0.0f;

  r4.yz = c72.ww * c235.xy;
  r4.xw = r4.yz + c235.zw;
  r3.xyz = frac(abs(r4.wxy));
  r1.w = max(r2.z, r2.z);
  r6.x = (r4.w >= 0.0f) ? r3.x : -r3.x;
  r3.x = (r4.x >= 0.0f) ? r3.y : -r3.y;
  r3.z = (r4.y >= 0.0f) ? r3.z : -r3.z;
  r3.w = r3.z - c252.w;
  r3.y = c233.z + r1.w;
  r1.zw = r3.wx + r3.zx;
  r3.z = c233.w + r2.x;
  r3.x = abs(r1.z) * c232.y;
  r3.w = c233.w + r2.y;
  r3 += c253.xyzz;
  r6.yzw = r3.zyw + r2.ywx;
  r4.x = native_texture0.Sample(native_sampler0, r6.xy).r;

  r3.yz = c72.ww * c234.xy;
  r4.yz = c73.xy + c252.xx;
  r5.x = frac(r3.y);
  r1.xy = r4.yz + r1.xy;
  r5.y = frac(r3.z);
  r4.yz = r2.wy + r5.xy;
  r1.x = dot(r1.xy, r1.xy) + c255.w;
  r1.yz = r4.yz + r2.zx;
  r1.x = sqrt(abs(r1.x));
  r2.yzw = r1.yzx * c234.zzw;
  r1.w = -r1.w + c252.w;
  r1.y = -r4.x;
  r2.x = saturate(r1.w + r2.w);
  r1.x = saturate(c253.w * r1.x);
  r4.xzw = r2.yzx + c252.zyx;
  r0 = max(r0, r1.xxxx);
  r1.x = saturate(abs(r4.w) * c232.z);
  r1.z = saturate(r1.x * r3.x);
  r3.xyz = (-r1.xzx) * c254.wzx + c254.yzx;
  r1.x = r3.z * r1.y;
  r1.y = c232.x * r1.y;
  r1.z = r1.y * r3.z;
  r1.xy = r6.zz + r1.xz;
  r0 = max(r0, r6.wwww);
  r1.yz = r1.xy + r5.xx;
  r1.x = r5.y + r1.x;
  r2.xzw = r1.zxy * c234.zzz;
  r4.y = c234.z * r1.x;

  r2.x = native_texture1.Sample(native_sampler1, r2.xz).r;
  r2.y = native_texture2.Sample(native_sampler2, r4.xy).r;
  r2.z = native_texture3.Sample(native_sampler3, r2.wz).r;
  r1.w = native_texture4.Sample(native_sampler4, r4.xz).r;

  r2.w = c252.w - r1.x;
  r2 = r3.xxxy * r2;
  r1.w = r2.w + r1.x;
  r1.xy = -r2.xy + c233.xy;
  r0 = max(r0, -r2.zzzz);
  r1.xy = r3.yy * r1.xy;
  r1.z = r0.x * r3.y;
  r1.xyz = r2.xyz + r1.xyz;
  return saturate(r1 * r0);
}
