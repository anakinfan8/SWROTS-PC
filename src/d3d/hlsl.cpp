// HLSL templates for translated Xbox shaders.
//
// Adapted from Cxbx-Reloaded's CxbxVertexShaderTemplate.hlsl and
// CxbxPixelShaderTemplate.hlsl (GPL-2.0-or-later, (c) the Cxbx-Reloaded
// authors), which model the NV2A's vertex program and register combiner
// semantics. Generated code is inserted between the parts below.

#include "d3d/shaders.h"

namespace swrots::d3d::hlsl {

// -----------------------------------------------------------------------------
// Vertex shaders (vs_3_0). Constant layout:
//   c0..c191   Xbox constants (Xbox register + 96)
//   c192..207  default values for vertex registers missing from the stream
//   c208..211  per-register "use default" flags
//   c212/c213  screen-space scale/offset used to undo the Xbox viewport transform
//   c214..217  texture coordinate scale (linear textures use texel coordinates)
// -----------------------------------------------------------------------------
const char* const kVertexHead = R"HLSL(
struct VS_INPUT
{
	float4 v[16] : TEXCOORD;
};

struct VS_OUTPUT
{
	float4 oPos : POSITION;
	float4 oD0  : COLOR0;
	float4 oD1  : COLOR1;
	float  oFog : FOG;
	float  oPts : PSIZE;
	float4 oB0  : TEXCOORD4;
	float4 oB1  : TEXCOORD5;
	float4 oT0  : TEXCOORD0;
	float4 oT1  : TEXCOORD1;
	float4 oT2  : TEXCOORD2;
	float4 oT3  : TEXCOORD3;
};

#define X_D3DSCM_CORRECTION 96
#define X_D3DVS_CONSTREG_COUNT 192

uniform float4 C[X_D3DVS_CONSTREG_COUNT] : register(c0);
uniform float4 vRegisterDefaultValues[16] : register(c192);
uniform float4 vRegisterDefaultFlagsPacked[4] : register(c208);
uniform float4 xboxScreenspaceScale : register(c212);
uniform float4 xboxScreenspaceOffset : register(c213);
uniform float4 xboxTextureScale[4] : register(c214);

float4 _tof4(float  src) { return float4(src, src, src, src); }
float4 _tof4(float2 src) { return src.xyyy; }
float4 _tof4(float3 src) { return src.xyzz; }
float4 _tof4(float4 src) { return src; }
float4 _ssss(float s)    { return float4(s, s, s, s); }
#define _scalar(src) _tof4(src).x

float4 c(int register_number)
{
	register_number += X_D3DSCM_CORRECTION;
	if (register_number >= X_D3DVS_CONSTREG_COUNT)
		register_number = -1; // out of range reads return zero, like the NV2A
	return C[register_number];
}

#define BIAS 0.001
float x_floor(float src) { return floor(src + BIAS); }

#define x_arl(dest, mask, src0) dest.mask = x_floor(_tof4(src0).x).mask
#define x_mov(dest, mask, src0) dest.mask = (_tof4(src0)).mask
#define x_mul(dest, mask, src0, src1) dest.mask = (_tof4(src0) * _tof4(src1)).mask
#define x_add(dest, mask, src0, src1) dest.mask = (_tof4(src0) + _tof4(src1)).mask
#define x_mad(dest, mask, src0, src1, src2) dest.mask = (_tof4(src0) * _tof4(src1) + _tof4(src2)).mask
#define x_dp3(dest, mask, src0, src1) dest.mask = _ssss(dot(_tof4(src0).xyz, _tof4(src1).xyz)).mask
#define x_dp4(dest, mask, src0, src1) dest.mask = _ssss(dot(_tof4(src0), _tof4(src1))).mask
#define x_dst(dest, mask, src0, src1) dest.mask = dst(_tof4(src0), _tof4(src1)).mask
#define x_min(dest, mask, src0, src1) dest.mask = min(_tof4(src0), _tof4(src1)).mask
#define x_max(dest, mask, src0, src1) dest.mask = max(_tof4(src0), _tof4(src1)).mask
#define x_slt(dest, mask, src0, src1) dest.mask = _slt(_tof4(src0), _tof4(src1)).mask
#define x_sge(dest, mask, src0, src1) dest.mask = _sge(_tof4(src0), _tof4(src1)).mask
#define x_dph(dest, mask, src0, src1) dest.mask = _ssss(_dph(_tof4(src0), _tof4(src1))).mask
#define x_rsq(dest, mask, src0) dest.mask = _ssss(_rsq(_scalar(src0))).mask
#define x_expp(dest, mask, src0) dest.mask = _expp(_scalar(src0)).mask
#define x_logp(dest, mask, src0) dest.mask = _logp(_scalar(src0)).mask
#define x_lit(dest, mask, src) dest.mask = _lit(_tof4(src)).mask
#define x_rcc(dest, mask, src0) dest.mask = _ssss(_rcc(_scalar(src0))).mask
#define x_rcp(dest, mask, src0) dest.mask = _ssss(_rcp(_scalar(src0))).mask

float4 _slt(float4 a, float4 b) { return float4(a.x < b.x, a.y < b.y, a.z < b.z, a.w < b.w); }
float4 _sge(float4 a, float4 b) { return float4(a.x >= b.x, a.y >= b.y, a.z >= b.z, a.w >= b.w); }
float _dph(float4 a, float4 b) { return dot(a.xyz, b.xyz) + b.w; }
float _rsq(float src) { return rsqrt(abs(src)); }

float4 _expp(float src)
{
	float floor_src = x_floor(src);
	return float4(exp2(floor_src), src - floor_src, exp2(src), 1);
}

float4 _logp(float src)
{
	float exponent;
	float mantissa = frexp(src, exponent);
	return float4(exponent, mantissa, log2(src), 1);
}

float4 _lit(float4 src0)
{
	const float epsilon = 1.0f / 256.0f;
	float diffuse = src0.x;
	float blinn = src0.y;
	float specPower = clamp(src0.w, -(128 - epsilon), (128 - epsilon));
	float4 dest;
	dest.x = 1;
	dest.y = max(0, diffuse);
	dest.z = (diffuse > 0) && (blinn > 0) ? pow(blinn, specPower) : 0;
	dest.w = 1;
	return dest;
}

float _rcc(float src)
{
	float r = 1 / src;
	return (r >= 0) ? clamp(r, 5.42101e-020f, 1.84467e+019f) : clamp(r, -1.84467e+019f, -5.42101e-020f);
}

float _rcp(float src) { return _rcc(src); }

// Xbox shaders output screen-space positions (the viewport transform is part
// of the program, via c-38/c-37); undo it to get clip space.
float4 reverseScreenspaceTransform(float4 oPos)
{
	oPos -= xboxScreenspaceOffset;
	oPos /= xboxScreenspaceScale;
	if (oPos.w == 0) oPos.w = 1;
	oPos.xyz *= oPos.w;
	return oPos;
}

VS_OUTPUT main(const VS_INPUT xIn)
{
	float4 oPos, oD0, oD1, oB0, oB1, oT0, oT1, oT2, oT3;
	oPos = oD0 = oD1 = oB0 = oB1 = oT0 = oT1 = oT2 = oT3 = float4(0, 0, 0, 1);
	float4 oFog, oPts;
	oFog = 1;
	oPts = 0;
	int1 a0 = 0;
	float4 r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11;
	r0 = r1 = r2 = r3 = r4 = r5 = r6 = r7 = r8 = r9 = r10 = r11 = float4(0, 0, 0, 0);
	#define r12 oPos

	float4 v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15;
	float vRegisterDefaultFlags[16] = (float[16])vRegisterDefaultFlagsPacked;
	#define init_v(i) v##i = lerp(xIn.v[i], vRegisterDefaultValues[i], vRegisterDefaultFlags[i]);
	init_v( 0); init_v( 1); init_v( 2); init_v( 3);
	init_v( 4); init_v( 5); init_v( 6); init_v( 7);
	init_v( 8); init_v( 9); init_v(10); init_v(11);
	init_v(12); init_v(13); init_v(14); init_v(15);

	float4 temp;
)HLSL";

const char* const kVertexTail = R"HLSL(
	VS_OUTPUT xOut;
	xOut.oPos = reverseScreenspaceTransform(oPos);
	xOut.oD0 = saturate(oD0);
	xOut.oD1 = saturate(oD1);
	xOut.oFog = oFog.x;
	xOut.oPts = oPts.x;
	xOut.oB0 = saturate(oB0);
	xOut.oB1 = saturate(oB1);
	xOut.oT0 = oT0 / xboxTextureScale[0];
	xOut.oT1 = oT1 / xboxTextureScale[1];
	xOut.oT2 = oT2 / xboxTextureScale[2];
	xOut.oT3 = oT3 / xboxTextureScale[3];
	return xOut;
}
)HLSL";

// -----------------------------------------------------------------------------
// Pixel shaders (ps_3_0). Constant layout:
//   c0..c7 stage C0, c8..c15 stage C1, c16/c17 final combiner C0/C1, c18 fog color,
//   c19.. COLORSIGN[4], c23.. COLORKEYOP[4], c27.. COLORKEYCOLOR[4], c31.. BEM[4],
//   c35.. LUM[4], c39 front-face factor, c40 fog parameters, c41 fog enable
// -----------------------------------------------------------------------------
const char* const kPixelHead = R"HLSL(
struct PS_INPUT
{
	float2 iPos : VPOS;
	float4 iD0  : COLOR0;
	float4 iD1  : COLOR1;
	float  iFog : FOG;
	float  iPts : PSIZE;
	float4 iB0  : TEXCOORD4;
	float4 iB1  : TEXCOORD5;
	float4 iT0  : TEXCOORD0;
	float4 iT1  : TEXCOORD1;
	float4 iT2  : TEXCOORD2;
	float4 iT3  : TEXCOORD3;
	float  iFF  : VFACE;
};

struct PS_OUTPUT
{
	float4 oR0 : COLOR;
};

#define s_sat(x)    saturate(x)
#define s_comp(x)       (1 - x)
#define s_bx2(x)      (( 2 * x) - 1.0)
#define s_negbx2(x)   ((-2 * x) + 1.0)
#define s_bias(x)         (  x  - 0.5)
#define s_negbias(x)     (-  x  + 0.5)
#define s_ident(x)           x
#define s_neg(x)           (-x)

#define d_ident(x) x
#define d_bias(x) (x - 0.5)
#define d_x2(x)  ( x        * 2)
#define d_bx2(x) ((x - 0.5) * 2)
#define d_x4(x)  ( x        * 4)
#define d_bx4(x) ((x - 0.5) * 4)
#define d_d2(x)  ( x        / 2)
#define d_bd2(x) ((x - 0.5) / 2)

uniform const float4 c0_[8] : register(c0);
uniform const float4 c1_[8] : register(c8);
uniform const float4 FC0 : register(c16);
uniform const float4 FC1 : register(c17);
uniform const float4 c_fog : register(c18);
uniform const float4 COLORSIGN[4] : register(c19);
uniform const float4 COLORKEYOP[4] : register(c23);
uniform const float4 COLORKEYCOLOR[4] : register(c27);
uniform const float4 BEM[4] : register(c31);
uniform const float4 LUM[4] : register(c35);
uniform const float  FRONTFACE_FACTOR : register(c39);
uniform const float4 FOGINFO : register(c40);
uniform const float  FOGENABLE : register(c41);

#define CM_LT(c) if(c < 0) clip(-1);
#define CM_GE(c) if(c >= 0) clip(-1);
)HLSL";

const char* const kPixelMid = R"HLSL(
#ifdef PS_COMBINERCOUNT_UNIQUE_C0
	#define C0 c0_[stage]
#else
	#define C0 c0_[0]
#endif
#ifdef PS_COMBINERCOUNT_UNIQUE_C1
	#define C1 c1_[stage]
#else
	#define C1 c1_[0]
#endif
#ifdef PS_COMBINERCOUNT_MUX_MSB
	#define FCS_MUX (r0.a >= 0.5)
#else
	#define FCS_MUX (((r0.a * 255) % 2) >= 1)
#endif
#ifdef PS_FINALCOMBINERSETTING_COMPLEMENT_V1
	#define FCS_V1 s_comp
#else
	#define FCS_V1 s_ident
#endif
#ifdef PS_FINALCOMBINERSETTING_COMPLEMENT_R0
	#define FCS_R0 s_comp
#else
	#define FCS_R0 s_ident
#endif
#ifdef PS_FINALCOMBINERSETTING_CLAMP_SUM
	#define FCS_SUM s_sat
#else
	#define FCS_SUM s_ident
#endif

#define xdot(s0, s1) dot((s0).rgb, (s1).rgb)
#define xmma(d0, d1, d2,  s0, s1, s2, s3, m, tmp) tmp = d0 = m(s0 * s1); d1 = m(s2 * s3); d2 = d1 + tmp
#define xmmc(d0, d1, d2,  s0, s1, s2, s3, m, tmp) tmp = d0 = m(s0 * s1); d1 = m(s2 * s3); d2 = FCS_MUX ? d1 : tmp
#define xdm(d0, d1,  s0, s1, s2, s3, m) d0 = m(xdot(s0 , s1)); d1 = m(     s2 * s3 )
#define xdd(d0, d1,  s0, s1, s2, s3, m) d0 = m(xdot(s0 , s1)); d1 = m(xdot(s2 , s3))
#define xmd(d0, d1,  s0, s1, s2, s3, m) d0 = m(     s0 * s1 ); d1 = m(xdot(s2 , s3))

#define fcin(r) saturate(r)
#define xfc_sum sum = FCS_SUM(float4(FCS_V1(fcin(v1.rgb)) + FCS_R0(fcin(r0.rgb)), 0))
#define xfc_prod(e, f) prod = float4(fcin(e) * fcin(f), 0)
#define xfc_rgb(a, b, c, d) r0.rgb = lerp(fcin(c), fcin(b), fcin(a)) + fcin(d)
#define xfc_alpha(g) r0.a = fcin(g)
#define xfc(a, b, c, d, e, f, g) xfc_sum; xfc_prod(e, f); xfc_rgb(a, b, c, d); xfc_alpha(g)

float m21d(const float input) { int tmp = (int)(input * 255); tmp -= 128; return (float)tmp / 127; }
float m21g(const float input) { int tmp = (int)(input * 255); if (tmp >= 128) tmp -= 256; return ((float)tmp + 0.5) / 127.5; }
float m21(const float input) { int tmp = (int)(input * 255); if (tmp >= 128) tmp -= 256; return (float)tmp / 127; }
float hls(float input) { return (input < 32768) ? input / 32767 : (input - 65536) / 32767; }
float hlu(float input) { return input / 65535; }
float p2(float input) { return input * input; }

#define TwoIntoOne(a,b) (((a * 256) + b) * 255)
#define CalcHiLo(in) H = TwoIntoOne(in.x, in.y); L = TwoIntoOne(in.z, in.w)

#define PS_DOTMAPPING_ZERO_TO_ONE(in)         dm = in.rgb
#define PS_DOTMAPPING_MINUS1_TO_1_D3D(in)     dm = float3(m21d(in.x), m21d(in.y), m21d(in.z))
#define PS_DOTMAPPING_MINUS1_TO_1_GL(in)      dm = float3(m21g(in.x), m21g(in.y), m21g(in.z))
#define PS_DOTMAPPING_MINUS1_TO_1(in)         dm = float3(m21( in.x), m21( in.y), m21( in.z))
#define PS_DOTMAPPING_HILO_1(in)              CalcHiLo(in); dm = float3(hlu(H), hlu(L), 1)
#define PS_DOTMAPPING_HILO_HEMISPHERE_D3D(in) CalcHiLo(in); dm = float3(hls(H), hls(L), sqrt(1-p2(H)-p2(L)))
#define PS_DOTMAPPING_HILO_HEMISPHERE_GL(in)  CalcHiLo(in); dm = float3(hls(H), hls(L), sqrt(1-p2(H)-p2(L)))
#define PS_DOTMAPPING_HILO_HEMISPHERE(in)     CalcHiLo(in); dm = float3(hls(H), hls(L), sqrt(1-p2(H)-p2(L)))

sampler samplers[4] : register(s0);

#ifndef ALPHAKILL
	#define ALPHAKILL {false, false, false, false}
#endif
static bool alphakill[4] = ALPHAKILL;

static const float4 WarningColor = float4(0, 1, 1, 1);
#define unsigned_to_signed(x) (((x) * 2) - 1)
#define signed_to_unsigned(x) (((x) + 1) / 2)

float4 PerformColorSign(const float4 ColorSign, float4 t)
{
	if (ColorSign.r > 0) t.r = unsigned_to_signed(t.r);
	if (ColorSign.g > 0) t.g = unsigned_to_signed(t.g);
	if (ColorSign.b > 0) t.b = unsigned_to_signed(t.b);
	if (ColorSign.a > 0) t.a = unsigned_to_signed(t.a);
	if (ColorSign.r < 0) t.r = signed_to_unsigned(t.r);
	if (ColorSign.g < 0) t.g = signed_to_unsigned(t.g);
	if (ColorSign.b < 0) t.b = signed_to_unsigned(t.b);
	if (ColorSign.a < 0) t.a = signed_to_unsigned(t.a);
	return t;
}

float4 PerformColorKeyOp(const float ColorKeyOp, const float4 ColorKeyColor, float4 t)
{
	if (ColorKeyOp == 0) return t;
	if (any(t - ColorKeyColor)) return t;
	if (ColorKeyOp == 1) return float4(t.rgb, 0);
	if (ColorKeyOp == 2) return 0;
	if (ColorKeyOp == 3) discard;
	return WarningColor;
}

void PerformAlphaKill(const float AlphaKill, float4 t)
{
	if (AlphaKill)
		if (t.a == 0)
			discard;
}

float4 PostProcessTexel(const int ts, float4 t)
{
	t = PerformColorSign(COLORSIGN[ts], t);
	t = PerformColorKeyOp(COLORKEYOP[ts].x, COLORKEYCOLOR[ts], t);
	PerformAlphaKill(alphakill[ts], t);
	return t;
}

float4 Sample2D(int ts, float3 s) { return PostProcessTexel(ts, tex2D(samplers[ts], s.xy)); }
float4 Sample3D(int ts, float3 s) { return PostProcessTexel(ts, tex3D(samplers[ts], s.xyz)); }
float4 Sample6F(int ts, float3 s) { return PostProcessTexel(ts, texCUBE(samplers[ts], s.xyz)); }

float3 DoBumpEnv(const float4 TexCoord, const float4 BumpEnvMat, const float4 BumpMap)
{
	const float u = TexCoord.x + (BumpEnvMat.x * BumpMap.r) + (BumpEnvMat.z * BumpMap.g);
	const float v = TexCoord.y + (BumpEnvMat.y * BumpMap.r) + (BumpEnvMat.w * BumpMap.g);
	return float3(u, v, 0);
}

#define t0 t[0]
#define t1 t[1]
#define t2 t[2]
#define t3 t[3]
#define src(ts) t[PS_INPUTTEXTURE_[ts]]
#define CalcDot(ts) PS_DOTMAPPING_ ## ts(src(ts)); dot_[ts] = dot(iT[ts].xyz, dm)

#define Passthru(ts)  float4(saturate(iT[ts]))
#define Brdf(ts)      float3(t[ts-2].y,  t[ts-1].y,  t[ts-2].x - t[ts-1].x)
#define Normal2(ts)   float3(dot_[ts-1], dot_[ts],   0)
#define Normal3(ts)   float3(dot_[ts-2], dot_[ts-1], dot_[ts])
#define Eye           float3(iT[1].w,    iT[2].w,    iT[3].w)
#define Reflect(n, e) 2 * (dot(n, e) / dot(n, n)) * n - e
#define BumpEnv(ts)   DoBumpEnv(iT[ts], BEM[ts], src(ts))
#define LSO(ts)       (LUM[ts].x * src(ts).b) + LUM[ts].y

#define PS_TEXTUREMODES_NONE(ts)                                                                    v = black;           t[ts] = v
#define PS_TEXTUREMODES_PROJECT2D(ts)                                          s = iT[ts].xyz;      v = Sample2D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_PROJECT3D(ts)                                          s = iT[ts].xyz;      v = Sample3D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_CUBEMAP(ts)                                            s = iT[ts].xyz;      v = Sample6F(ts, s); t[ts] = v
#define PS_TEXTUREMODES_PASSTHRU(ts)                                                                v = Passthru(ts);    t[ts] = v
#define PS_TEXTUREMODES_CLIPPLANE(ts)            PS_COMPAREMODE_ ## ts(iT[ts]);                     v = black;           t[ts] = v
#define PS_TEXTUREMODES_BUMPENVMAP(ts)                                         s = BumpEnv(ts);     v = Sample2D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_BUMPENVMAP_LUM(ts)       PS_TEXTUREMODES_BUMPENVMAP(ts);                    v.rgb *= LSO(ts);    t[ts] = v
#define PS_TEXTUREMODES_BRDF(ts)                                               s = Brdf(ts);        v = Sample3D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOT_ST(ts)               CalcDot(ts); n = Normal2(ts); s = n;               v = Sample2D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOT_ZW(ts)               CalcDot(ts); n = Normal2(ts); if (n.y==0) v=1;else v = n.x / n.y;       t[ts] = v
#define PS_TEXTUREMODES_DOT_RFLCT_DIFF(ts)       CalcDot(ts); n = Normal2(ts); s = n;               v = Sample6F(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOT_RFLCT_SPEC(ts)       CalcDot(ts); n = Normal3(ts); s = Reflect(n, Eye); v = Sample6F(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOT_STR_3D(ts)           CalcDot(ts); n = Normal3(ts); s = n;               v = Sample3D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOT_STR_CUBE(ts)         CalcDot(ts); n = Normal3(ts); s = n;               v = Sample6F(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DPNDNT_AR(ts)                                          s = src(ts).arg;     v = Sample2D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DPNDNT_GB(ts)                                          s = src(ts).gba;     v = Sample2D(ts, s); t[ts] = v
#define PS_TEXTUREMODES_DOTPRODUCT(ts)           CalcDot(ts);                                       v = float4(dm, 0);   t[ts] = v
#define PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST(ts) CalcDot(ts); n = Normal3(ts); s = Reflect(n, C0);  v = Sample6F(ts, s); t[ts] = v

float FogFactor(float fogDepth)
{
	const int fogTableMode = FOGINFO.x;
	const float fogDensity = FOGINFO.y;
	const float fogStart = FOGINFO.z;
	const float fogEnd = FOGINFO.w;
	if (FOGENABLE == 0) return 1;
	if (fogTableMode == 1) return 1 / exp(fogDepth * fogDensity);
	if (fogTableMode == 2) return 1 / exp(pow(fogDepth * fogDensity, 2));
	if (fogTableMode == 3) return (fogEnd - fogDepth) / (fogEnd - fogStart);
	return fogDepth;
}

PS_OUTPUT main(const PS_INPUT xIn)
{
	const float4 zero = 0;
	const float4 half = 0.5;
	const float4 one = 1;
	const float4 black = float4(0, 0, 0, 1);
	const float4 iT[4] = { xIn.iT0, xIn.iT1, xIn.iT2, xIn.iT3 };

	float4 r0, r1;
	float4 t[4];
	float4 v0, v1;
	float4 _discard;
	float4 fog;
	float4 sum, prod;

	int stage = 0;
	float4 tmp;
	float H, L;
	float dot_[4];
	float3 dm;
	float3 n;
	float3 s;
	float4 v;

	bool isFrontFace = (xIn.iFF * FRONTFACE_FACTOR) >= 0;
	r0 = r1 = black;
	v0 = isFrontFace ? xIn.iD0 : xIn.iB0;
	v1 = isFrontFace ? xIn.iD1 : xIn.iB1;
	fog = float4(c_fog.rgb, saturate(FogFactor(xIn.iFog.x)));
)HLSL";

const char* const kPixelTail = R"HLSL(
	PS_OUTPUT xOut;
	xOut.oR0 = r0;
	return xOut;
}
)HLSL";

} // namespace swrots::d3d::hlsl
