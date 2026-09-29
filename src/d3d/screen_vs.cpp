// Vertex shaders for pre-transformed (screen-space) draws that use an Xbox
// pixel shader. D3D9 cannot pair fixed-function vertex processing with the
// ps_3_0 shaders the pixel-shader translator produces, so these vs_3_0
// shaders do what fixed function would: turn host pixel positions into clip
// space and pass colors and texture coordinates through, in the layout the
// translated pixel shaders read (see the vertex program outputs).

#include "d3d/d3d.h"

#include <sstream>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "d3d/shaders.h"

namespace swrots::d3d {

namespace {

std::string BuildScreenSpaceShader(DWORD fvf, const int setForStage[4])
{
    const int sets = int((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    std::ostringstream s;
    s << "struct VIn {\n  float4 pos : POSITION; // x, y in host pixels, z, rhw\n";
    if (fvf & D3DFVF_DIFFUSE)
        s << "  float4 diffuse : COLOR0;\n";
    if (fvf & D3DFVF_SPECULAR)
        s << "  float4 specular : COLOR1;\n";
    for (int t = 0; t < sets && t < 8; ++t)
        s << "  float4 tex" << t << " : TEXCOORD" << t << ";\n";
    s << "};\n"
         "struct VOut {\n"
         "  float4 pos : POSITION;\n"
         "  float4 d0 : COLOR0;\n"
         "  float4 d1 : COLOR1;\n"
         "  float fog : FOG;\n"
         "  float4 b0 : TEXCOORD4;\n"
         "  float4 b1 : TEXCOORD5;\n"
         "  float4 t0 : TEXCOORD0;\n"
         "  float4 t1 : TEXCOORD1;\n"
         "  float4 t2 : TEXCOORD2;\n"
         "  float4 t3 : TEXCOORD3;\n"
         "};\n"
         "uniform float4 target : register(c0); // host render target width, height\n"
         "VOut main(VIn i) {\n"
         "  VOut o;\n"
         "  float w = i.pos.w != 0 ? 1 / i.pos.w : 1;\n"
         // D3D9 places pixel centers at integer screen coordinates.
         "  float2 ndc = float2((i.pos.x + 0.5) * 2 / target.x - 1, 1 - (i.pos.y + 0.5) * 2 / target.y);\n"
         "  o.pos = float4(ndc * w, i.pos.z * w, w);\n";
    s << "  o.d0 = " << ((fvf & D3DFVF_DIFFUSE) ? "i.diffuse" : "float4(1, 1, 1, 1)") << ";\n";
    s << "  o.d1 = " << ((fvf & D3DFVF_SPECULAR) ? "i.specular" : "float4(0, 0, 0, 0)") << ";\n";
    s << "  o.b0 = o.d0;\n  o.b1 = o.d1;\n  o.fog = 1;\n";
    for (int stage = 0; stage < 4; ++stage) {
        int set = setForStage[stage];
        s << "  o.t" << stage << " = ";
        if (set >= 0 && set < sets)
            s << "i.tex" << set << ";\n";
        else
            s << "float4(0, 0, 0, 1);\n";
    }
    s << "  return o;\n}\n";
    return s.str();
}

} // namespace

// The FVF's implicit declaration marks the position as POSITIONT, which a
// vertex shader cannot take as input; this one calls it POSITION.
IDirect3DVertexDeclaration9* ScreenSpaceDeclaration(DWORD fvf)
{
    static std::unordered_map<DWORD, IDirect3DVertexDeclaration9*> s_Declarations;
    IDirect3DVertexDeclaration9*& decl = s_Declarations[fvf];
    if (decl)
        return decl;
    std::vector<D3DVERTEXELEMENT9> e;
    WORD offset = 0;
    auto add = [&](BYTE type, BYTE usage, BYTE index, WORD size) {
        e.push_back({ 0, offset, type, D3DDECLMETHOD_DEFAULT, usage, index });
        offset = WORD(offset + size);
    };
    add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0, 16);
    if (fvf & D3DFVF_PSIZE)
        add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0, 4);
    if (fvf & D3DFVF_DIFFUSE)
        add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0, 4);
    if (fvf & D3DFVF_SPECULAR)
        add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1, 4);
    const int sets = int((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    for (int t = 0; t < sets && t < 8; ++t) {
        static const BYTE kTypes[4] = { D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1 };
        static const WORD kSizes[4] = { 8, 12, 16, 4 };
        int format = (fvf >> (16 + t * 2)) & 3;
        add(kTypes[format], D3DDECLUSAGE_TEXCOORD, BYTE(t), kSizes[format]);
    }
    e.push_back(D3DDECL_END());
    if (FAILED(Device()->CreateVertexDeclaration(e.data(), &decl)))
        decl = nullptr;
    return decl;
}

IDirect3DVertexShader9* ScreenSpaceVertexShader(DWORD fvf)
{
    // Each pixel shader stage reads the coordinate set its TEXCOORDINDEX selects.
    int setForStage[4];
    for (int stage = 0; stage < 4; ++stage)
        setForStage[stage] = int(State().textureStates[stage][xd3d::TSS_TEXCOORDINDEX] & 0xFFFF);
    struct {
        DWORD fvf;
        int sets[4];
    } id = { fvf, { setForStage[0], setForStage[1], setForStage[2], setForStage[3] } };
    uint64_t key = Hash64(&id, sizeof(id), 0x5C4EE45Aull);

    IDirect3DVertexShader9* vs;
    if (CachedVertexShader(key, &vs))
        return vs;
    vs = CompileVertexShader(BuildScreenSpaceShader(fvf, setForStage), key);
    LOG_DEBUG("Screen-space vertex shader for FVF %08lX -> %s", fvf, vs ? "ok" : "FAILED");
    return vs;
}

} // namespace swrots::d3d
