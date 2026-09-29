#pragma once
// Translation of Xbox vertex programs and pixel shaders into D3D9 shaders.

#include <d3d9.h>

#include <cstdint>
#include <string>

#include "d3d/xd3d.h"

namespace swrots::d3d {

namespace hlsl {
extern const char* const kVertexHead;
extern const char* const kVertexTail;
extern const char* const kPixelHead;
extern const char* const kPixelMid;
extern const char* const kPixelTail;
} // namespace hlsl

// Host vertex shader constant registers beyond the 192 Xbox constants.
constexpr UINT kVsRegDefaults = 192;      // 16 registers
constexpr UINT kVsRegDefaultFlags = 208;  // 4 registers (16 floats)
constexpr UINT kVsRegScreenScale = 212;
constexpr UINT kVsRegScreenOffset = 213;
constexpr UINT kVsRegTextureScale = 214;  // 4 registers

// Host pixel shader constant registers.
constexpr UINT kPsRegC0 = 0, kPsRegC1 = 8, kPsRegFC0 = 16, kPsRegFC1 = 17, kPsRegFogColor = 18;
constexpr UINT kPsRegColorSign = 19, kPsRegColorKeyOp = 23, kPsRegColorKeyColor = 27, kPsRegBem = 31, kPsRegLum = 35;
constexpr UINT kPsRegFrontFace = 39, kPsRegFogInfo = 40, kPsRegFogEnable = 41;
// Fixed-function pixel emulation: texture factor and bump-env constants.
constexpr UINT kPsRegTextureFactor = 42;

uint64_t Hash64(const void* data, size_t bytes, uint64_t seed = 0xcbf29ce484222325ull);

// Looks up an already compiled shader (including cached failures).
bool CachedVertexShader(uint64_t key, IDirect3DVertexShader9** out);
bool CachedPixelShader(uint64_t key, IDirect3DPixelShader9** out);
// Compiles HLSL with the shader cache; returns null (and logs) on failure.
IDirect3DVertexShader9* CompileVertexShader(const std::string& hlsl, uint64_t key);
IDirect3DPixelShader9* CompilePixelShader(const std::string& hlsl, uint64_t key);

// --- Vertex programs ------------------------------------------------------------

enum class VertexMode { Fvf, FixedFunction, Passthrough, Program };

// Current vertex processing mode and, for programs, the host shader.
VertexMode CurrentVertexMode();
const xd3d::VertexShader* CurrentXboxVertexShader();
IDirect3DVertexShader9* CurrentHostVertexShader();

// vs_3_0 for a pre-transformed FVF draw that uses an Xbox pixel shader
// (constant c0 = host render target width, height).
IDirect3DVertexShader9* ScreenSpaceVertexShader(DWORD fvf);
// Vertex declaration matching `fvf` for use with ScreenSpaceVertexShader.
IDirect3DVertexDeclaration9* ScreenSpaceDeclaration(DWORD fvf);

// Default values for vertex registers missing from the stream (SetVertexData*).
float* VertexRegisterDefaults(); // 16 x float4

// --- Pixel shaders -----------------------------------------------------------------

IDirect3DPixelShader9* HostPixelShaderFor(const xd3d::PixelShaderDef* def, const bool alphaKill[4]);
// ps_3_0 equivalent of the Xbox fixed-function texture stages (for draws that
// use a vertex program but no pixel shader).
IDirect3DPixelShader9* FixedFunctionPixelShader();

void ReleaseShaders();

} // namespace swrots::d3d
