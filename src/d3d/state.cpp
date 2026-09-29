// Xbox D3D state setters. The engine writes most render states straight into
// D3D's global arrays itself (see d3d.h); the "complex" states, texture
// bindings, streams, shaders, transforms and constants arrive through the
// functions below, which record them for the next draw.

#include "d3d/d3d.h"

#include <cstring>

#include "core/log.h"
#include "xapi/xapi.h"

namespace swrots::d3d {

using namespace xd3d;

// --- Render states ------------------------------------------------------------

// Simple states: the engine stores the value in D3D's array after this call,
// and the encoded method is only meaningful to the NV2A.
static void __fastcall XbSetRenderStateSimple(DWORD method, DWORD value)
{
    (void)method;
    (void)value;
}

static void __stdcall XbSetRenderStateNotInline(DWORD state, DWORD value)
{
    if (state < RS_COUNT)
        XboxRenderStates()[state] = value;
}

#define COMPLEX_STATE(Name, Index)                                   \
    static void __stdcall XbSetRS_##Name(DWORD value)                \
    {                                                                \
        XboxRenderStates()[Index] = value;                           \
    }                                                                \
    SDK_REPLACE("D3DDevice_SetRenderState_" #Name, XbSetRS_##Name)

COMPLEX_STATE(PSTextureModes, RS_PSTEXTUREMODES);
COMPLEX_STATE(VertexBlend, RS_VERTEXBLEND);
COMPLEX_STATE(FogColor, RS_FOGCOLOR);
COMPLEX_STATE(FillMode, RS_FILLMODE);
COMPLEX_STATE(BackFillMode, RS_BACKFILLMODE);
COMPLEX_STATE(TwoSidedLighting, RS_TWOSIDEDLIGHTING);
COMPLEX_STATE(NormalizeNormals, RS_NORMALIZENORMALS);
COMPLEX_STATE(ZEnable, RS_ZENABLE);
COMPLEX_STATE(StencilEnable, RS_STENCILENABLE);
COMPLEX_STATE(StencilFail, RS_STENCILFAIL);
COMPLEX_STATE(FrontFace, RS_FRONTFACE);
COMPLEX_STATE(CullMode, RS_CULLMODE);
COMPLEX_STATE(TextureFactor, RS_TEXTUREFACTOR);
COMPLEX_STATE(ZBias, RS_ZBIAS);
COMPLEX_STATE(LogicOp, RS_LOGICOP);
COMPLEX_STATE(EdgeAntiAlias, RS_EDGEANTIALIAS);
COMPLEX_STATE(MultiSampleAntiAlias, RS_MULTISAMPLEANTIALIAS);
COMPLEX_STATE(MultiSampleMask, RS_MULTISAMPLEMASK);
COMPLEX_STATE(MultiSampleMode, RS_MULTISAMPLEMODE);
COMPLEX_STATE(MultiSampleRenderTargetMode, RS_MULTISAMPLERENDERTARGETMODE);
COMPLEX_STATE(ShadowFunc, RS_SHADOWFUNC);
COMPLEX_STATE(LineWidth, RS_LINEWIDTH);
COMPLEX_STATE(Dxt1NoiseEnable, RS_DXT1NOISEENABLE);
COMPLEX_STATE(YuvEnable, RS_YUVENABLE);
COMPLEX_STATE(OcclusionCullEnable, RS_OCCLUSIONCULLENABLE);
COMPLEX_STATE(StencilCullEnable, RS_STENCILCULLENABLE);
COMPLEX_STATE(RopZCmpAlwaysRead, RS_ROPZCMPALWAYSREAD);
COMPLEX_STATE(RopZRead, RS_ROPZREAD);
COMPLEX_STATE(DoNotCullUncompressed, RS_DONOTCULLUNCOMPRESSED);

static HRESULT __stdcall XbSetRS_SampleAlpha(DWORD value)
{
    XboxRenderStates()[RS_SAMPLEALPHA] = value;
    return D3D_OK;
}

SDK_REPLACE("D3DDevice_SetRenderState_Simple", XbSetRenderStateSimple);
SDK_REPLACE("D3DDevice_SetRenderStateNotInline", XbSetRenderStateNotInline);
SDK_REPLACE("D3DDevice_SetRenderState_SampleAlpha", XbSetRS_SampleAlpha);

// --- Texture stage states ------------------------------------------------------

static void StoreTextureState(DWORD stage, DWORD type, DWORD value)
{
    if (stage >= 4 || type >= TSS_COUNT)
        return;
    if (type <= TSS_TEXTURETRANSFORMFLAGS)
        XboxTextureStates(stage)[type] = value;
    else
        State().textureStates[stage][type] = value;
}

static void __stdcall XbSetTextureStageStateNotInline(DWORD stage, DWORD type, DWORD value)
{
    StoreTextureState(stage, type, value);
}

static void __stdcall XbSetTexCoordIndex(DWORD stage, DWORD value) { StoreTextureState(stage, TSS_TEXCOORDINDEX, value); }
static void __stdcall XbSetBorderColor(DWORD stage, DWORD value) { StoreTextureState(stage, TSS_BORDERCOLOR, value); }
static void __stdcall XbSetColorKeyColor(DWORD stage, DWORD value) { StoreTextureState(stage, TSS_COLORKEYCOLOR, value); }
static void __stdcall XbSetBumpEnv(DWORD stage, DWORD type, DWORD value) { StoreTextureState(stage, type, value); }

SDK_REPLACE("D3DDevice_SetTextureStageStateNotInline", XbSetTextureStageStateNotInline);
SDK_REPLACE("D3DDevice_SetTextureState_TexCoordIndex", XbSetTexCoordIndex);
SDK_REPLACE("D3DDevice_SetTextureState_BorderColor", XbSetBorderColor);
SDK_REPLACE("D3DDevice_SetTextureState_ColorKeyColor", XbSetColorKeyColor);
SDK_REPLACE("D3DDevice_SetTextureState_BumpEnv", XbSetBumpEnv);

// --- Bindings -------------------------------------------------------------------

static void __stdcall XbSetTexture(DWORD stage, BaseTexture* texture)
{
    if (stage < 4)
        State().textures[stage] = texture;
}

static void __stdcall XbSetPalette(DWORD stage, Palette* palette)
{
    if (stage < 4)
        State().palettes[stage] = palette;
}

static void __stdcall XbSetStreamSource(UINT stream, VertexBuffer* buffer, UINT stride)
{
    if (stream < 16)
        State().streams[stream] = { buffer, stride };
}

static void __stdcall XbSetIndices(IndexBuffer* buffer, UINT baseVertexIndex)
{
    State().indexBuffer = buffer;
    State().baseVertexIndex = baseVertexIndex;
    // The engine's inlined DrawIndexedPrimitive reads the index data address
    // from this XDK global and passes pointers into it to DrawIndexedVertices.
    *reinterpret_cast<DWORD*>(uintptr_t(xbox_globals::kIndexData)) = buffer ? buffer->Data : 0;
}


static void __stdcall XbSetTransform(DWORD state, const D3DMATRIX* matrix)
{
    if (state < TS_COUNT && matrix)
        State().transforms[state] = *matrix;
}

SDK_REPLACE("D3DDevice_SetTexture", XbSetTexture);
SDK_REPLACE("D3DDevice_SetPalette", XbSetPalette);
SDK_REPLACE("D3DDevice_SetStreamSource", XbSetStreamSource);
SDK_REPLACE("D3DDevice_SetIndices", XbSetIndices);
SDK_REPLACE("D3DDevice_SetTransform", XbSetTransform);

// --- Shader constants -------------------------------------------------------------

// Registers arrive already biased into 0..191 (Xbox -96..95 + 96); counts of
// the NotInline variants are in DWORDs. Like the XDK, keep D3D's own shadow of
// the constants current as well.
static void StoreVertexConstants(int reg, const void* data, DWORD dwords)
{
    DWORD count = dwords / 4;
    if (reg < 0 || reg + int(count) > 192) {
        LOG_WARN("SetVertexShaderConstant(%d, %lu dwords): out of range", reg, dwords);
        return;
    }
    std::memcpy(State().vertexConstants[reg], data, count * 16);
    std::memcpy(reinterpret_cast<uint8_t*>(uintptr_t(xbox_globals::kVertexConstants)) + reg * 16, data, count * 16);
}

static void __fastcall XbSetVertexShaderConstant1(int reg, const void* data) { StoreVertexConstants(reg, data, 4); }
static void __fastcall XbSetVertexShaderConstant4(int reg, const void* data) { StoreVertexConstants(reg, data, 16); }
static void __fastcall XbSetVertexShaderConstantNotInline(int reg, const void* data, DWORD dwords)
{
    StoreVertexConstants(reg, data, dwords);
}

static void __stdcall XbSetShaderConstantMode(DWORD mode) { State().shaderConstantMode = mode; }


SDK_REPLACE("D3DDevice_SetVertexShaderConstant1", XbSetVertexShaderConstant1);
SDK_REPLACE("D3DDevice_SetVertexShaderConstant4", XbSetVertexShaderConstant4);
SDK_REPLACE("D3DDevice_SetVertexShaderConstantNotInline", XbSetVertexShaderConstantNotInline);
SDK_REPLACE("D3DDevice_SetVertexShaderConstantNotInlineFast", XbSetVertexShaderConstantNotInline);
SDK_REPLACE("D3DDevice_SetShaderConstantMode", XbSetShaderConstantMode);

} // namespace swrots::d3d
