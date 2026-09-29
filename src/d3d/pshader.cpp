// Xbox pixel shaders (NV2A register combiners) -> HLSL ps_3_0, plus a ps_3_0
// equivalent of the fixed-function texture stages.
//
// A pixel shader definition configures 4 texture stages (addressing modes),
// up to 8 general combiner stages (RGB and alpha halves) and a final combiner.
// The decoding and HLSL generation follow Cxbx-Reloaded's XbPixelShader.cpp
// and PixelShader.cpp (GPL-2.0-or-later).

#include "d3d/shaders.h"

#include <cstring>
#include <sstream>
#include <unordered_map>

#include "core/log.h"
#include "d3d/d3d.h"
#include "xapi/xapi.h"

namespace swrots::d3d {

using namespace xd3d;

namespace {

enum : uint8_t {
    REG_ZERO = 0, REG_DISCARD = 0, REG_C0 = 1, REG_C1 = 2, REG_FOG = 3, REG_V0 = 4, REG_V1 = 5, REG_T0 = 8,
    REG_R0 = 12, REG_R1 = 13, REG_SUM = 14, REG_PROD = 15, REG_FC0 = 16, REG_FC1 = 17,
};
constexpr uint8_t CHANNEL_ALPHA = 0x10;

struct Input {
    uint8_t reg = 0, channel = 0, mapping = 0;
    void Decode(uint8_t v, bool finalCombiner)
    {
        reg = v & 0x0F;
        channel = v & 0x10;
        mapping = v & 0xE0;
        if (finalCombiner && reg == REG_C0) reg = REG_FC0;
        if (finalCombiner && reg == REG_C1) reg = REG_FC1;
    }
};

struct Output {
    uint8_t reg = 0;
    Input in[2];
    bool dot = false;
    unsigned blueToAlpha = 0;
};

struct Channel {
    Output ab, cd;
    uint8_t muxSum = 0;
    bool mux = false;
    uint8_t mapping = 0;
    void Decode(uint32_t inputs, uint32_t outputs)
    {
        cd.reg = outputs & 0xF;
        ab.reg = (outputs >> 4) & 0xF;
        muxSum = (outputs >> 8) & 0xF;
        ab.in[0].Decode(uint8_t(inputs >> 24), false);
        ab.in[1].Decode(uint8_t(inputs >> 16), false);
        cd.in[0].Decode(uint8_t(inputs >> 8), false);
        cd.in[1].Decode(uint8_t(inputs >> 0), false);
        uint32_t flags = outputs >> 12;
        cd.dot = (flags & 0x01) != 0;
        ab.dot = (flags & 0x02) != 0;
        mux = (flags & 0x04) != 0;
        mapping = flags & 0x38;
        cd.blueToAlpha = (flags & 0x40) >> 6;
        ab.blueToAlpha = (flags & 0x80) >> 7;
    }
};

struct Combiner {
    unsigned textureModes[4];
    unsigned dotMapping[4];
    bool compareMode[4][4];
    int inputTexture[4];
    unsigned count;
    bool muxMsb, uniqueC0, uniqueC1;
    Channel rgb[8], alpha[8];
    Input fc[7];
    bool complementV1, complementR0, clampSum;

    void Decode(const PixelShaderDef& d, bool fogEnable, bool specularEnable)
    {
        count = d.PSCombinerCount & 0xF;
        uint32_t flags = d.PSCombinerCount >> 8;
        muxMsb = (flags & 0x001) != 0;
        uniqueC0 = (flags & 0x010) != 0;
        uniqueC1 = (flags & 0x100) != 0;
        for (int i = 0; i < 4; ++i) {
            textureModes[i] = (d.PSTextureModes >> (i * 5)) & 0x1F;
            dotMapping[i] = i ? (d.PSDotMapping >> ((i - 1) * 4)) & 7 : 0;
            uint32_t cm = (d.PSCompareMode >> (i * 4)) & 0xF;
            for (int k = 0; k < 4; ++k)
                compareMode[i][k] = (cm >> k) & 1;
        }
        inputTexture[0] = -1;
        inputTexture[1] = 0;
        inputTexture[2] = (d.PSInputTexture >> 16) & 1;
        inputTexture[3] = (d.PSInputTexture >> 20) & 3;
        for (unsigned i = 0; i < count && i < 8; ++i) {
            rgb[i].Decode(d.PSRGBInputs[i], d.PSRGBOutputs[i]);
            alpha[i].Decode(d.PSAlphaInputs[i], d.PSAlphaOutputs[i]);
        }
        bool hasFinal = d.PSFinalCombinerInputsABCD || d.PSFinalCombinerInputsEFG;
        if (hasFinal) {
            fc[0].Decode(uint8_t(d.PSFinalCombinerInputsABCD >> 24), true);
            fc[1].Decode(uint8_t(d.PSFinalCombinerInputsABCD >> 16), true);
            fc[2].Decode(uint8_t(d.PSFinalCombinerInputsABCD >> 8), true);
            fc[3].Decode(uint8_t(d.PSFinalCombinerInputsABCD >> 0), true);
            fc[4].Decode(uint8_t(d.PSFinalCombinerInputsEFG >> 24), true);
            fc[5].Decode(uint8_t(d.PSFinalCombinerInputsEFG >> 16), true);
            fc[6].Decode(uint8_t(d.PSFinalCombinerInputsEFG >> 8), true);
            uint32_t s = d.PSFinalCombinerInputsEFG & 0xFF;
            complementV1 = (s & 0x40) != 0;
            complementR0 = (s & 0x20) != 0;
            clampSum = (s & 0x80) != 0;
        } else {
            // What the XDK programs when the definition leaves the final combiner
            // unset: fog blend, plus specular when enabled.
            for (Input& in : fc)
                in = Input();
            fc[0].reg = REG_FOG;
            fc[0].channel = CHANNEL_ALPHA;
            fc[1].reg = REG_R0;
            fc[2].reg = fogEnable ? REG_FOG : REG_R0;
            fc[3].reg = specularEnable ? REG_V1 : REG_ZERO;
            fc[6].reg = REG_R0;
            fc[6].channel = CHANNEL_ALPHA;
            complementV1 = complementR0 = clampSum = false;
        }
    }
};

const char* kRegister[18] = { "_discard", "C0", "C1", "fog", "v0", "v1", "?r6?", "?r7?", "t0", "t1", "t2", "t3",
    "r0", "r1", "sum", "prod", "FC0", "FC1" };

void EmitInput(std::ostringstream& s, const Input& in, unsigned channelIndex, bool last, bool finalCombiner)
{
    static const char* kChannel[3][2] = { { ".b", ".a" }, { ".rgb", ".aaa" }, { ".rgbb", ".aaaa" } };
    static const char* kMapping[8][3] = {
        { "zero", "s_sat", "s_sat" }, { "one", "s_comp", "s_comp" }, { "-one", "s_bx2", "s_bx2" },
        { "one", "s_negbx2", "s_negbx2" }, { "-half", "s_bias", "s_bias" }, { "half", "s_negbias", "s_negbias" },
        { "zero", "s_ident", "s_ident" }, { "zero", "s_neg", "s_neg" },
    };
    const char* ch = kChannel[channelIndex][in.channel >> 4];
    unsigned m = (in.mapping >> 5) & 7;
    if (in.reg == REG_ZERO) {
        s << kMapping[m][0] << ch;
    } else if (m == 6) {
        s << kRegister[in.reg] << ch;
    } else if (m == 7) {
        s << '-' << kRegister[in.reg] << ch;
    } else {
        s << kMapping[m][1 + (finalCombiner ? 1 : 0)] << '(' << kRegister[in.reg] << ch << ')';
    }
    if (!last)
        s << ',';
}

void EmitStage(std::ostringstream& s, const Channel& c, unsigned channelIndex)
{
    unsigned op = c.ab.dot ? (c.cd.dot ? 0 : 1) : (c.cd.dot ? 2 : (c.mux ? 4 : 3));
    static const char* kOp[5] = { "xdd", "xdm", "xmd", "xmma", "xmmc" };
    if (c.ab.reg == REG_DISCARD && c.cd.reg == REG_DISCARD && (op <= 2 || c.muxSum == REG_DISCARD)) {
        s << "// discarded";
        return;
    }
    static const char* kDst[3] = { ".a", ".rgb", ".rgba" };
    unsigned abCh = channelIndex + c.ab.blueToAlpha, cdCh = channelIndex + c.cd.blueToAlpha;
    s << kOp[op] << '(' << kRegister[c.ab.reg] << kDst[abCh] << ',' << kRegister[c.cd.reg] << kDst[cdCh];
    if (op >= 3)
        s << ',' << kRegister[c.muxSum] << kDst[channelIndex];
    s << ", ";
    EmitInput(s, c.ab.in[0], abCh, false, false);
    EmitInput(s, c.ab.in[1], abCh, false, false);
    EmitInput(s, c.cd.in[0], cdCh, false, false);
    EmitInput(s, c.cd.in[1], cdCh, false, false);
    static const char* kOutMap[8] = { "d_ident", "d_bias", "d_x2", "d_bx2", "d_x4", "d_bx4", "d_d2", "d_bd2" };
    s << ' ' << kOutMap[c.mapping >> 3];
    if (op >= 3)
        s << ",tmp" << kDst[abCh];
    s << ");";
}

std::string BuildPixelHlsl(const Combiner& c, const bool alphaKill[4])
{
    static const char* kTexMode[19] = { "PS_TEXTUREMODES_NONE", "PS_TEXTUREMODES_PROJECT2D",
        "PS_TEXTUREMODES_PROJECT3D", "PS_TEXTUREMODES_CUBEMAP", "PS_TEXTUREMODES_PASSTHRU",
        "PS_TEXTUREMODES_CLIPPLANE", "PS_TEXTUREMODES_BUMPENVMAP", "PS_TEXTUREMODES_BUMPENVMAP_LUM",
        "PS_TEXTUREMODES_BRDF", "PS_TEXTUREMODES_DOT_ST", "PS_TEXTUREMODES_DOT_ZW", "PS_TEXTUREMODES_DOT_RFLCT_DIFF",
        "PS_TEXTUREMODES_DOT_RFLCT_SPEC", "PS_TEXTUREMODES_DOT_STR_3D", "PS_TEXTUREMODES_DOT_STR_CUBE",
        "PS_TEXTUREMODES_DPNDNT_AR", "PS_TEXTUREMODES_DPNDNT_GB", "PS_TEXTUREMODES_DOTPRODUCT",
        "PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST" };
    static const char* kDotMap[8] = { "PS_DOTMAPPING_ZERO_TO_ONE", "PS_DOTMAPPING_MINUS1_TO_1_D3D",
        "PS_DOTMAPPING_MINUS1_TO_1_GL", "PS_DOTMAPPING_MINUS1_TO_1", "PS_DOTMAPPING_HILO_1",
        "PS_DOTMAPPING_HILO_HEMISPHERE_D3D", "PS_DOTMAPPING_HILO_HEMISPHERE_GL", "PS_DOTMAPPING_HILO_HEMISPHERE" };

    std::ostringstream s;
    s << hlsl::kPixelHead;
    s << "\n#define ALPHAKILL {" << (alphaKill[0] ? "true" : "false") << ", " << (alphaKill[1] ? "true" : "false")
      << ", " << (alphaKill[2] ? "true" : "false") << ", " << (alphaKill[3] ? "true" : "false") << "}";
    if (c.uniqueC0) s << "\n#define PS_COMBINERCOUNT_UNIQUE_C0";
    if (c.uniqueC1) s << "\n#define PS_COMBINERCOUNT_UNIQUE_C1";
    if (c.muxMsb) s << "\n#define PS_COMBINERCOUNT_MUX_MSB";
    for (int i = 0; i < 4; ++i) {
        s << "\n#define PS_COMPAREMODE_" << i << "(in)";
        for (int k = 0; k < 4; ++k)
            s << (c.compareMode[i][k] ? " CM_GE(in." : " CM_LT(in.") << "xyzw"[k] << ")";
    }
    s << "\nstatic const int PS_INPUTTEXTURE_[4] = { -1, " << c.inputTexture[1] << ", " << c.inputTexture[2] << ", "
      << c.inputTexture[3] << " };";
    for (int i = 1; i < 4; ++i)
        s << "\n#define PS_DOTMAPPING_" << i << " " << kDotMap[c.dotMapping[i]];
    if (c.complementV1) s << "\n#define PS_FINALCOMBINERSETTING_COMPLEMENT_V1";
    if (c.complementR0) s << "\n#define PS_FINALCOMBINERSETTING_COMPLEMENT_R0";
    if (c.clampSum) s << "\n#define PS_FINALCOMBINERSETTING_CLAMP_SUM";
    s << "\n" << hlsl::kPixelMid;

    for (int i = 0; i < 4; ++i) {
        unsigned mode = c.textureModes[i] < 19 ? c.textureModes[i] : 0;
        s << "\n\t" << kTexMode[mode] << "(" << i << ");";
        if (i == 0) s << " r0.a = t0.a;";
        if (i == 1) s << " r1.a = t1.a;";
    }
    for (unsigned i = 0; i < c.count; ++i) {
        s << "\n\tstage = " << i << "; ";
        EmitStage(s, c.rgb[i], 1);
        s << "\n\t";
        EmitStage(s, c.alpha[i], 0);
    }
    std::ostringstream args;
    for (int i = 0; i < 7; ++i)
        EmitInput(args, c.fc[i], i == 6 ? 0 : 1, i == 6, true);
    s << "\n\txfc(" << args.str() << ");";
    s << hlsl::kPixelTail;
    return s.str();
}

} // namespace

IDirect3DPixelShader9* HostPixelShaderFor(const PixelShaderDef* def, const bool alphaKill[4])
{
    const DWORD* rs = XboxRenderStates();
    bool fog = rs[RS_FOGENABLE] != 0, specular = rs[RS_SPECULARENABLE] != 0;
    struct Key {
        PixelShaderDef def;
        bool alphaKill[4], fog, specular;
    } key = {};
    key.def = *def;
    std::memset(key.def.PSConstant0, 0, sizeof(key.def.PSConstant0)); // constants are uniforms
    std::memset(key.def.PSConstant1, 0, sizeof(key.def.PSConstant1));
    key.def.PSFinalCombinerConstant0 = key.def.PSFinalCombinerConstant1 = 0;
    key.def.PSC0Mapping = key.def.PSC1Mapping = key.def.PSFinalCombinerConstants = 0;
    std::memcpy(key.alphaKill, alphaKill, 4);
    bool hasFinal = def->PSFinalCombinerInputsABCD || def->PSFinalCombinerInputsEFG;
    key.fog = !hasFinal && fog;
    key.specular = !hasFinal && specular;
    uint64_t hash = Hash64(&key, sizeof(key), 0x5053ull);

    IDirect3DPixelShader9* cached;
    if (CachedPixelShader(hash, &cached))
        return cached;
    Combiner c;
    c.Decode(*def, fog, specular);
    IDirect3DPixelShader9* ps = CompilePixelShader(BuildPixelHlsl(c, alphaKill), hash);
    LOG_DEBUG("Pixel shader %016llX: %u combiners -> %s", hash, c.count, ps ? "ok" : "FAILED");
    return ps;
}

// --- Fixed-function texture stages as ps_3_0 -----------------------------------------------

namespace {

// Xbox D3DTOP values.
enum : DWORD {
    TOP_DISABLE = 1, TOP_SELECTARG1, TOP_SELECTARG2, TOP_MODULATE, TOP_MODULATE2X, TOP_MODULATE4X, TOP_ADD,
    TOP_ADDSIGNED, TOP_ADDSIGNED2X, TOP_SUBTRACT, TOP_ADDSMOOTH, TOP_BLENDDIFFUSEALPHA, TOP_BLENDCURRENTALPHA,
    TOP_BLENDTEXTUREALPHA, TOP_BLENDFACTORALPHA, TOP_BLENDTEXTUREALPHAPM, TOP_PREMODULATE,
    TOP_MODULATEALPHA_ADDCOLOR, TOP_MODULATECOLOR_ADDALPHA, TOP_MODULATEINVALPHA_ADDCOLOR,
    TOP_MODULATEINVCOLOR_ADDALPHA, TOP_DOTPRODUCT3, TOP_MULTIPLYADD, TOP_LERP, TOP_BUMPENVMAP,
    TOP_BUMPENVMAPLUMINANCE,
};

std::string Arg(DWORD a, int stage)
{
    std::string base;
    switch (a & 0x0F) {
    case 0: base = "diffuse"; break;       // D3DTA_DIFFUSE
    case 1: base = "cur"; break;           // D3DTA_CURRENT
    case 2: base = "t" + std::to_string(stage); break; // D3DTA_TEXTURE
    case 3: base = "tfactor"; break;       // D3DTA_TFACTOR
    case 4: base = "specular"; break;      // D3DTA_SPECULAR
    case 5: base = "temp"; break;          // D3DTA_TEMP
    default: base = "cur"; break;
    }
    if (a & 0x20) base = base + ".aaaa";   // D3DTA_ALPHAREPLICATE
    if (a & 0x10) base = "(1 - " + base + ")"; // D3DTA_COMPLEMENT
    return base;
}

std::string Op(DWORD op, const std::string& a0, const std::string& a1, const std::string& a2, int stage)
{
    std::string t = "t" + std::to_string(stage);
    switch (op) {
    case TOP_SELECTARG1: return a1;
    case TOP_SELECTARG2: return a2;
    case TOP_MODULATE: return a1 + " * " + a2;
    case TOP_MODULATE2X: return "(" + a1 + " * " + a2 + ") * 2";
    case TOP_MODULATE4X: return "(" + a1 + " * " + a2 + ") * 4";
    case TOP_ADD: return a1 + " + " + a2;
    case TOP_ADDSIGNED: return a1 + " + " + a2 + " - 0.5";
    case TOP_ADDSIGNED2X: return "(" + a1 + " + " + a2 + " - 0.5) * 2";
    case TOP_SUBTRACT: return a1 + " - " + a2;
    case TOP_ADDSMOOTH: return a1 + " + " + a2 + " - " + a1 + " * " + a2;
    case TOP_BLENDDIFFUSEALPHA: return "lerp(" + a2 + ", " + a1 + ", diffuse.a)";
    case TOP_BLENDCURRENTALPHA: return "lerp(" + a2 + ", " + a1 + ", cur.a)";
    case TOP_BLENDTEXTUREALPHA: return "lerp(" + a2 + ", " + a1 + ", " + t + ".a)";
    case TOP_BLENDFACTORALPHA: return "lerp(" + a2 + ", " + a1 + ", tfactor.a)";
    case TOP_BLENDTEXTUREALPHAPM: return a1 + " + " + a2 + " * (1 - " + t + ".a)";
    case TOP_PREMODULATE: return a1;
    case TOP_MODULATEALPHA_ADDCOLOR: return a1 + " + " + a1 + ".a * " + a2;
    case TOP_MODULATECOLOR_ADDALPHA: return a1 + " * " + a2 + " + " + a1 + ".a";
    case TOP_MODULATEINVALPHA_ADDCOLOR: return "(1 - " + a1 + ".a) * " + a2 + " + " + a1;
    case TOP_MODULATEINVCOLOR_ADDALPHA: return "(1 - " + a1 + ") * " + a2 + " + " + a1 + ".a";
    case TOP_DOTPRODUCT3: return "dot((" + a1 + ").rgb * 2 - 1, (" + a2 + ").rgb * 2 - 1).xxxx";
    case TOP_MULTIPLYADD: return a0 + " + " + a1 + " * " + a2;
    case TOP_LERP: return "lerp(" + a2 + ", " + a1 + ", " + a0 + ")";
    default: return a1;
    }
}

} // namespace

IDirect3DPixelShader9* FixedFunctionPixelShader()
{
    const DWORD* rs = XboxRenderStates();
    struct StageKey {
        DWORD colorOp, colorArg[3], alphaOp, alphaArg[3], resultArg, texType;
    } stages[4] = {};
    for (int i = 0; i < 4; ++i) {
        const DWORD* ts = XboxTextureStates(i);
        stages[i] = { ts[TSS_COLOROP], { ts[TSS_COLORARG0], ts[TSS_COLORARG1], ts[TSS_COLORARG2] }, ts[TSS_ALPHAOP],
            { ts[TSS_ALPHAARG0], ts[TSS_ALPHAARG1], ts[TSS_ALPHAARG2] }, ts[TSS_RESULTARG], 0 };
        BaseTexture* tex = State().textures[i];
        if (tex)
            stages[i].texType = (tex->Format & FORMAT_CUBEMAP) ? 2 : (((tex->Format & FORMAT_DIMENSION_MASK) >> FORMAT_DIMENSION_SHIFT) == 3 ? 3 : 1);
        if (stages[i].colorOp <= TOP_DISABLE)
            break;
    }
    struct Key {
        StageKey stages[4];
        DWORD fog, specular;
    } key = { {}, rs[RS_FOGENABLE] != 0, rs[RS_SPECULARENABLE] != 0 };
    std::memcpy(key.stages, stages, sizeof(stages));
    uint64_t hash = Hash64(&key, sizeof(key), 0x4646ull);

    IDirect3DPixelShader9* cached;
    if (CachedPixelShader(hash, &cached))
        return cached;

    std::ostringstream s;
    s << R"(
struct PS_INPUT {
	float4 iD0 : COLOR0; float4 iD1 : COLOR1; float iFog : FOG;
	float4 iT0 : TEXCOORD0; float4 iT1 : TEXCOORD1; float4 iT2 : TEXCOORD2; float4 iT3 : TEXCOORD3;
};
uniform const float4 c_fog : register(c18);
uniform const float4 FOGINFO : register(c40);
uniform const float  FOGENABLE : register(c41);
uniform const float4 TFACTOR : register(c42);
sampler samplers[4] : register(s0);
float FogFactor(float d)
{
	if (FOGENABLE == 0) return 1;
	if (FOGINFO.x == 1) return 1 / exp(d * FOGINFO.y);
	if (FOGINFO.x == 2) return 1 / exp(pow(d * FOGINFO.y, 2));
	if (FOGINFO.x == 3) return (FOGINFO.w - d) / (FOGINFO.w - FOGINFO.z);
	return d;
}
float4 main(PS_INPUT xIn) : COLOR
{
	float4 diffuse = xIn.iD0, specular = xIn.iD1, tfactor = TFACTOR;
	float4 cur = diffuse, temp = 0;
	float4 iT[4] = { xIn.iT0, xIn.iT1, xIn.iT2, xIn.iT3 };
)";
    for (int i = 0; i < 4; ++i) {
        const StageKey& st = stages[i];
        if (st.colorOp <= TOP_DISABLE)
            break;
        const char* sampleFn = st.texType == 2 ? "texCUBE" : st.texType == 3 ? "tex3D" : "tex2D";
        const char* coord = st.texType == 1 || st.texType == 0 ? ".xy" : ".xyz";
        s << "\tfloat4 t" << i << " = " << sampleFn << "(samplers[" << i << "], iT[" << i << "]" << coord << ");\n";
        std::string rgb = Op(st.colorOp, Arg(st.colorArg[0], i), Arg(st.colorArg[1], i), Arg(st.colorArg[2], i), i);
        std::string a = st.alphaOp <= TOP_DISABLE ? "cur" :
            Op(st.alphaOp, Arg(st.alphaArg[0], i), Arg(st.alphaArg[1], i), Arg(st.alphaArg[2], i), i);
        const char* dst = (st.resultArg & 0xF) == 5 ? "temp" : "cur";
        s << "\t{ float4 c = saturate(" << rgb << "); float4 al = saturate(" << a << "); " << dst
          << " = float4(c.rgb, al.a); }\n";
    }
    if (key.specular)
        s << "\tcur.rgb = saturate(cur.rgb + specular.rgb);\n";
    if (key.fog)
        s << "\tcur.rgb = lerp(c_fog.rgb, cur.rgb, saturate(FogFactor(xIn.iFog)));\n";
    s << "\treturn cur;\n}\n";

    return CompilePixelShader(s.str(), hash);
}

// --- API ------------------------------------------------------------------------------------

// Like the XDK, SetPixelShader loads the definition into render states
// 0..56 (which mirror the start of the definition) and PSTEXTUREMODES.
static void __stdcall XbSetPixelShader(DWORD handle)
{
    auto* ps = reinterpret_cast<PixelShader*>(uintptr_t(handle));
    State().pixelShader = ps;
    if (ps && ps->pPSDef) {
        DWORD* rs = XboxRenderStates();
        std::memcpy(rs, ps->pPSDef, (RS_PSINPUTTEXTURE + 1) * sizeof(DWORD));
        rs[RS_PS_RESERVED] = ps->pPSDef->PSTextureModes;
        rs[RS_PSTEXTUREMODES] = ps->pPSDef->PSTextureModes;
    }
}

static DWORD FloatToColor(const float* f)
{
    auto c = [](float v) { return DWORD(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); };
    return (c(f[3]) << 24) | (c(f[0]) << 16) | (c(f[1]) << 8) | c(f[2]);
}

// Maps D3D pixel shader constants onto combiner constants through the
// definition's PSC0/PSC1/final-combiner mappings, as the XDK does.
static void __stdcall XbSetPixelShaderConstant(DWORD reg, const float* data, DWORD count)
{
    PixelShader* ps = State().pixelShader;
    if (!ps || !ps->pPSDef)
        return;
    const PixelShaderDef* d = ps->pPSDef;
    DWORD* rs = XboxRenderStates();
    for (DWORD i = 0; i < count; ++i) {
        DWORD c = reg + i;
        DWORD color = FloatToColor(data + i * 4);
        for (int s = 0; s < 8; ++s) {
            if (((d->PSC0Mapping >> (s * 4)) & 0xF) == c)
                rs[RS_PSCONSTANT0_0 + s] = color;
            if (((d->PSC1Mapping >> (s * 4)) & 0xF) == c)
                rs[RS_PSCONSTANT1_0 + s] = color;
        }
        if ((d->PSFinalCombinerConstants & 0xF) == c)
            rs[RS_PSFINALCOMBINERCONSTANT0] = color;
        if (((d->PSFinalCombinerConstants >> 4) & 0xF) == c)
            rs[RS_PSFINALCOMBINERCONSTANT1] = color;
    }
}

SDK_REPLACE("D3DDevice_SetPixelShader", XbSetPixelShader);
SDK_REPLACE("D3DDevice_SetPixelShaderConstant", XbSetPixelShaderConstant);

} // namespace swrots::d3d
