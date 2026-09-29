// Xbox vertex programs -> HLSL vs_3_0.
//
// The NV2A executes 128-bit instructions (4 DWORDs), each pairing a MAC
// (multiply/accumulate) and an ILU (inverse/log unit) operation. Programs are
// uploaded into 136 instruction slots; a draw runs the program starting at the
// selected slot until the instruction flagged FINAL. The decoder and HLSL
// emitter follow Cxbx-Reloaded's XbVertexShader.cpp / VertexShader.cpp
// (GPL-2.0-or-later), which document the microcode format.

#include "d3d/shaders.h"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "core/log.h"
#include "d3d/d3d.h"
#include "xapi/xapi.h"

namespace swrots::d3d {

using namespace xd3d;

uint64_t Hash64(const void* data, size_t bytes, uint64_t seed)
{
    uint64_t h = seed;
    auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

// --- Compilation ---------------------------------------------------------------------

static std::unordered_map<uint64_t, IDirect3DVertexShader9*> g_VertexShaders;
static std::unordered_map<uint64_t, IDirect3DPixelShader9*> g_PixelShaders;

static ID3DBlob* Compile(const std::string& hlsl, const char* profile)
{
    // Debugging aid: env SWROTS_SHADER_DUMP=<dir> writes every generated shader.
    static std::string dumpDir = [] {
        char v[MAX_PATH] = {};
        return GetEnvironmentVariableA("SWROTS_SHADER_DUMP", v, sizeof(v)) ? std::string(v) : std::string();
    }();
    if (!dumpDir.empty()) {
        char name[64];
        snprintf(name, sizeof(name), "/%s_%016llX.hlsl", profile, Hash64(hlsl.data(), hlsl.size()));
        if (FILE* f = fopen((dumpDir + name).c_str(), "wb")) {
            fwrite(hlsl.data(), 1, hlsl.size(), f);
            fclose(f);
        }
    }

    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), nullptr, nullptr, nullptr, "main", profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_AVOID_FLOW_CONTROL, 0, &code, &errors);
    if (FAILED(hr)) {
        LOG_ERROR("%s compile failed (%08lX): %s", profile, hr,
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "");
        static int dumped = 0;
        if (dumped++ < 4) {
            std::string numbered;
            std::istringstream in(hlsl);
            std::string line;
            for (int n = 1; std::getline(in, line); ++n)
                numbered += std::to_string(n) + ": " + line + "\n";
            OutputDebugStringA(numbered.c_str());
        }
    }
    if (errors)
        errors->Release();
    return SUCCEEDED(hr) ? code : nullptr;
}

bool CachedVertexShader(uint64_t key, IDirect3DVertexShader9** out)
{
    auto it = g_VertexShaders.find(key);
    if (it == g_VertexShaders.end())
        return false;
    *out = it->second;
    return true;
}

bool CachedPixelShader(uint64_t key, IDirect3DPixelShader9** out)
{
    auto it = g_PixelShaders.find(key);
    if (it == g_PixelShaders.end())
        return false;
    *out = it->second;
    return true;
}

IDirect3DVertexShader9* CompileVertexShader(const std::string& hlsl, uint64_t key)
{
    auto it = g_VertexShaders.find(key);
    if (it != g_VertexShaders.end())
        return it->second;
    IDirect3DVertexShader9* shader = nullptr;
    if (ID3DBlob* code = Compile(hlsl, "vs_3_0")) {
        Device()->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()), &shader);
        code->Release();
    }
    g_VertexShaders[key] = shader;
    return shader;
}

IDirect3DPixelShader9* CompilePixelShader(const std::string& hlsl, uint64_t key)
{
    auto it = g_PixelShaders.find(key);
    if (it != g_PixelShaders.end())
        return it->second;
    IDirect3DPixelShader9* shader = nullptr;
    if (ID3DBlob* code = Compile(hlsl, "ps_3_0")) {
        Device()->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &shader);
        code->Release();
    }
    g_PixelShaders[key] = shader;
    return shader;
}

void ReleaseShaders()
{
    for (auto& [k, s] : g_VertexShaders)
        if (s) s->Release();
    for (auto& [k, s] : g_PixelShaders)
        if (s) s->Release();
    g_VertexShaders.clear();
    g_PixelShaders.clear();
}

// --- Microcode decoding ---------------------------------------------------------------

namespace {

enum Field {
    FLD_ILU, FLD_MAC, FLD_CONST, FLD_V,
    FLD_A_NEG, FLD_A_SWZ_X, FLD_A_SWZ_Y, FLD_A_SWZ_Z, FLD_A_SWZ_W, FLD_A_R, FLD_A_MUX,
    FLD_B_NEG, FLD_B_SWZ_X, FLD_B_SWZ_Y, FLD_B_SWZ_Z, FLD_B_SWZ_W, FLD_B_R, FLD_B_MUX,
    FLD_C_NEG, FLD_C_SWZ_X, FLD_C_SWZ_Y, FLD_C_SWZ_Z, FLD_C_SWZ_W, FLD_C_R_HIGH, FLD_C_R_LOW, FLD_C_MUX,
    FLD_OUT_MAC_MASK, FLD_OUT_R, FLD_OUT_ILU_MASK, FLD_OUT_O_MASK, FLD_OUT_ORB, FLD_OUT_ADDRESS, FLD_OUT_MUX,
    FLD_A0X, FLD_FINAL,
};

struct FieldInfo {
    uint8_t token, bit, bits;
};

const FieldInfo kFields[] = {
    { 1, 25, 3 }, { 1, 21, 4 }, { 1, 13, 8 }, { 1, 9, 4 },
    { 1, 8, 1 }, { 1, 6, 2 }, { 1, 4, 2 }, { 1, 2, 2 }, { 1, 0, 2 }, { 2, 28, 4 }, { 2, 26, 2 },
    { 2, 25, 1 }, { 2, 23, 2 }, { 2, 21, 2 }, { 2, 19, 2 }, { 2, 17, 2 }, { 2, 13, 4 }, { 2, 11, 2 },
    { 2, 10, 1 }, { 2, 8, 2 }, { 2, 6, 2 }, { 2, 4, 2 }, { 2, 2, 2 }, { 2, 0, 2 }, { 3, 30, 2 }, { 3, 28, 2 },
    { 3, 24, 4 }, { 3, 20, 4 }, { 3, 16, 4 }, { 3, 12, 4 }, { 3, 11, 1 }, { 3, 3, 8 }, { 3, 2, 1 },
    { 3, 1, 1 }, { 3, 0, 1 },
};

inline uint32_t Get(const uint32_t* t, Field f)
{
    const FieldInfo& fi = kFields[f];
    return (t[fi.token] >> fi.bit) & ((1u << fi.bits) - 1);
}

enum Mac { MAC_NOP, MAC_MOV, MAC_MUL, MAC_ADD, MAC_MAD, MAC_DP3, MAC_DPH, MAC_DP4, MAC_DST, MAC_MIN, MAC_MAX,
    MAC_SLT, MAC_SGE, MAC_ARL };
enum Ilu { ILU_NOP, ILU_MOV, ILU_RCP, ILU_RCC, ILU_RSQ, ILU_EXP, ILU_LOG, ILU_LIT };
enum DestType { DEST_C, DEST_R, DEST_O, DEST_A0X };
enum ParamType { PARAM_UNKNOWN, PARAM_R, PARAM_V, PARAM_C };
constexpr int MASK_X = 8, MASK_Y = 4, MASK_Z = 2, MASK_W = 1;

struct Dest {
    DestType type = DEST_R;
    int16_t address = 0;
    int8_t mask = 0;
};

struct Param {
    ParamType type = PARAM_UNKNOWN;
    bool neg = false;
    uint8_t swizzle[4] = { 0, 1, 2, 3 };
    int16_t address = 0;
};

struct Instr {
    Mac mac = MAC_NOP;
    Dest macDest;
    int macParams = 0;
    Param macParam[3];
    Ilu ilu = ILU_NOP;
    Dest iluDest;
    Param iluParam;
    bool oRegFromIlu = false;
    Dest oRegDest;
    bool a0x = false;
};

int16_t ConvertCRegister(int16_t c) { return int16_t(((((c >> 5) & 7) - 3) * 32) + (c & 31)); }

Param GetParam(const uint32_t* t, Field mux, Field neg, int16_t r, int16_t v, int16_t c)
{
    Param p;
    p.type = ParamType(Get(t, mux));
    p.address = p.type == PARAM_R ? r : p.type == PARAM_V ? v : c;
    int d = neg - FLD_A_NEG;
    p.neg = Get(t, Field(FLD_A_NEG + d)) != 0;
    for (int i = 0; i < 4; ++i)
        p.swizzle[i] = uint8_t(Get(t, Field(FLD_A_SWZ_X + d + i)));
    return p;
}

// Decodes one instruction; returns false when it was the last.
bool Decode(const uint32_t* t, std::vector<Instr>& out)
{
    Ilu ilu = Ilu(Get(t, FLD_ILU));
    Mac mac = Mac(Get(t, FLD_MAC));
    int16_t rAddr = int16_t(Get(t, FLD_OUT_R));
    bool paired = mac != MAC_NOP && ilu != ILU_NOP;
    int16_t ar = int16_t(Get(t, FLD_A_R)), br = int16_t(Get(t, FLD_B_R));
    int16_t cr = int16_t(Get(t, FLD_C_R_HIGH) << 2 | Get(t, FLD_C_R_LOW));
    int16_t v = int16_t(Get(t, FLD_V)), c = ConvertCRegister(int16_t(Get(t, FLD_CONST)));

    Instr in;
    if (mac != MAC_NOP && mac <= MAC_ARL) {
        in.mac = mac;
        if (paired && rAddr == 1) {
            // paired MAC writes to R1 are dropped (the ILU owns R1)
        } else if (mac == MAC_ARL) {
            in.macDest = { DEST_A0X, 0, MASK_X };
        } else {
            in.macDest = { DEST_R, rAddr, int8_t(Get(t, FLD_OUT_MAC_MASK)) };
        }
        if (mac >= MAC_MOV)
            in.macParam[in.macParams++] = GetParam(t, FLD_A_MUX, FLD_A_NEG, ar, v, c);
        if (mac == MAC_MUL || (mac >= MAC_MAD && mac <= MAC_SGE))
            in.macParam[in.macParams++] = GetParam(t, FLD_B_MUX, FLD_B_NEG, br, v, c);
        if (mac == MAC_ADD || mac == MAC_MAD)
            in.macParam[in.macParams++] = GetParam(t, FLD_C_MUX, FLD_C_NEG, cr, v, c);
    }
    if (ilu != ILU_NOP) {
        in.ilu = ilu;
        in.iluDest = { DEST_R, int16_t(paired ? 1 : rAddr), int8_t(Get(t, FLD_OUT_ILU_MASK)) };
        in.iluParam = GetParam(t, FLD_C_MUX, FLD_C_NEG, cr, v, c);
    }
    int16_t outAddr = int16_t(Get(t, FLD_OUT_ADDRESS));
    if (Get(t, FLD_OUT_ORB) == 0 /* OUTPUT_C */) {
        in.oRegDest = { DEST_C, ConvertCRegister(outAddr), int8_t(Get(t, FLD_OUT_O_MASK)) };
    } else {
        in.oRegDest = { DEST_O, int16_t(outAddr & 0xF), int8_t(Get(t, FLD_OUT_O_MASK)) };
    }
    in.oRegFromIlu = Get(t, FLD_OUT_MUX) == 1;
    in.a0x = Get(t, FLD_A0X) != 0;
    out.push_back(in);
    return Get(t, FLD_FINAL) == 0;
}

// --- HLSL emission --------------------------------------------------------------------

void EmitDest(std::ostringstream& s, const Dest& d)
{
    static const char* kOut[16] = { "oPos", "o1", "o2", "oD0", "oD1", "oFog", "oPts", "oB0", "oB1",
        "oT0", "oT1", "oT2", "oT3", "o13", "o14", "a0" };
    switch (d.type) {
    case DEST_C: s << "C[" << d.address + 96 << "]"; break;
    case DEST_R: s << "r" << d.address; break;
    case DEST_O: s << kOut[d.address & 15]; break;
    case DEST_A0X: s << "a0"; break;
    }
    s << ",";
    int mask = d.mask;
    if (d.type == DEST_O && d.address == 5 /*oFog*/ && mask != MASK_X)
        mask = MASK_X;
    if (mask & MASK_X) s << "x";
    if (mask & MASK_Y) s << "y";
    if (mask & MASK_Z) s << "z";
    if (mask & MASK_W) s << "w";
}

void EmitParam(std::ostringstream& s, const Param& p, bool a0x, bool useTemp)
{
    if (p.neg)
        s << "-";
    if (useTemp) {
        s << "temp";
    } else if (p.type == PARAM_C) {
        if (a0x)
            s << "c(a0.x" << (p.address >= 0 ? "+" : "") << p.address << ")";
        else
            s << "c(" << p.address << ")";
    } else {
        s << (p.type == PARAM_R ? "r" : p.type == PARAM_V ? "v" : "r") << p.address;
    }
    const uint8_t* sw = p.swizzle;
    if (!(sw[0] == 0 && sw[1] == 1 && sw[2] == 2 && sw[3] == 3)) {
        s << ".";
        bool same = sw[0] == sw[1] && sw[0] == sw[2] && sw[0] == sw[3];
        for (int i = 0; i < (same ? 1 : 4); ++i)
            s << "xyzw"[sw[i] & 3];
    }
}

void EmitOp(std::ostringstream& s, const char* op, const Dest& d, int n, const Param* p, bool a0x, bool temp)
{
    s << "\n  " << op << "(";
    EmitDest(s, d);
    for (int i = 0; i < n; ++i) {
        s << ", ";
        EmitParam(s, p[i], a0x, temp);
    }
    s << ");";
}

std::string BuildHlsl(const std::vector<Instr>& prog)
{
    static const char* kMac[] = { "", "x_mov", "x_mul", "x_add", "x_mad", "x_dp3", "x_dph", "x_dp4", "x_dst",
        "x_min", "x_max", "x_slt", "x_sge", "x_arl", "", "" };
    static const char* kIlu[] = { "", "x_mov", "x_rcp", "x_rcc", "x_rsq", "x_expp", "x_logp", "x_lit" };

    std::ostringstream s;
    s << hlsl::kVertexHead;
    for (const Instr& in : prog) {
        bool paired = in.mac != MAC_NOP && in.ilu != ILU_NOP && (in.macDest.mask || !in.oRegFromIlu) &&
                      (in.iluDest.mask || in.oRegFromIlu);
        // If the MAC writes a register the paired ILU reads, the ILU must see the old value.
        const Dest* iluTemp = nullptr;
        if (paired) {
            const Param& ip = in.iluParam;
            if (in.macDest.address == ip.address &&
                ((in.macDest.type == DEST_R && ip.type == PARAM_R) || (in.macDest.type == DEST_C && ip.type == PARAM_C)))
                iluTemp = &in.macDest;
            else if (!in.oRegFromIlu && in.oRegDest.type == DEST_O && in.oRegDest.address == 0 && ip.type == PARAM_R &&
                     ip.address == 12)
                iluTemp = &in.oRegDest; // oPos is r12
            if (iluTemp) {
                bool conflict = false;
                for (int k = 0; k < 4; ++k) {
                    int sw = ip.swizzle[k];
                    if ((iluTemp->mask & MASK_X && sw == 0) || (iluTemp->mask & MASK_Y && sw == 1) ||
                        (iluTemp->mask & MASK_Z && sw == 2) || (iluTemp->mask & MASK_W && sw == 3))
                        conflict = true;
                }
                if (!conflict)
                    iluTemp = nullptr;
            }
        }
        if (iluTemp) {
            s << "\n  temp = ";
            Dest d = *iluTemp;
            switch (d.type) {
            case DEST_R: s << "r" << d.address; break;
            case DEST_C: s << "C[" << d.address + 96 << "]"; break;
            default: s << "oPos"; break;
            }
            s << ";";
        }
        if (in.mac != MAC_NOP) {
            if (in.macDest.mask)
                EmitOp(s, kMac[in.mac], in.macDest, in.macParams, in.macParam, in.a0x, false);
            if (!in.oRegFromIlu && in.oRegDest.mask)
                EmitOp(s, kMac[in.mac], in.oRegDest, in.macParams, in.macParam, in.a0x, false);
        }
        if (in.ilu != ILU_NOP) {
            if (in.iluDest.mask)
                EmitOp(s, kIlu[in.ilu], in.iluDest, 1, &in.iluParam, in.a0x, iluTemp != nullptr);
            if (in.oRegFromIlu && in.oRegDest.mask)
                EmitOp(s, kIlu[in.ilu], in.oRegDest, 1, &in.iluParam, in.a0x, iluTemp != nullptr);
        }
    }
    s << hlsl::kVertexTail;
    return s.str();
}

} // namespace

// --- Program slots and vertex shader state ----------------------------------------------

constexpr int kSlots = 136;
static uint32_t g_Slots[(kSlots + 1) * 4];
static DWORD g_ProgramStart = 0;
static VertexMode g_Mode = VertexMode::Fvf;
static const VertexShader* g_XboxShader = nullptr;
static float g_RegisterDefaults[16][4];

float* VertexRegisterDefaults() { return &g_RegisterDefaults[0][0]; }
VertexMode CurrentVertexMode() { return g_Mode; }
const VertexShader* CurrentXboxVertexShader() { return g_XboxShader; }

static void SetDefaultAttributes(DWORD flags)
{
    // Registers not supplied by the stream fall back to these (see Cxbx notes).
    for (int i = 0; i < 16; ++i) {
        float* v = g_RegisterDefaults[i];
        v[0] = v[1] = v[2] = 0.0f;
        v[3] = 1.0f;
    }
    if (!(flags & 0x0400)) std::fill(g_RegisterDefaults[VSDE_DIFFUSE], g_RegisterDefaults[VSDE_DIFFUSE] + 4, 1.0f);
    if (!(flags & 0x1000)) std::fill(g_RegisterDefaults[VSDE_BACKDIFFUSE], g_RegisterDefaults[VSDE_BACKDIFFUSE] + 4, 1.0f);
    if (!(flags & 0x0800)) std::fill(g_RegisterDefaults[VSDE_SPECULAR], g_RegisterDefaults[VSDE_SPECULAR] + 4, 0.0f);
    if (!(flags & 0x2000)) std::fill(g_RegisterDefaults[VSDE_BACKSPECULAR], g_RegisterDefaults[VSDE_BACKSPECULAR] + 4, 0.0f);
}

// Parses the NV2A upload commands stored in a vertex shader object.
static void LoadProgram(const VertexShader* vs, DWORD address)
{
    const DWORD* p = vs->ProgramAndConstants;
    const DWORD* end = p + (vs->ProgramAndConstantsDwords ? vs->ProgramAndConstantsDwords : 4096);
    DWORD constAddress = 0;
    while (p < end) {
        DWORD header = *p++;
        DWORD method = header & 0x1FFC, count = (header >> 18) & 0x7FF;
        if (!count)
            break;
        if (method == 0x0B00) { // NV097_SET_TRANSFORM_PROGRAM (upload instructions)
            DWORD n = count / 4;
            if (address + n <= kSlots)
                std::memcpy(&g_Slots[address * 4], p, n * 16);
            address += n;
        } else if (method == 0x1EA4) { // constant load address
            constAddress = *p;
        } else if (method == 0x0B80) { // upload constants
            DWORD n = count / 4;
            if (constAddress + n <= 192) {
                std::memcpy(State().vertexConstants[constAddress], p, n * 16);
                std::memcpy(reinterpret_cast<uint8_t*>(uintptr_t(xbox_globals::kVertexConstants)) + constAddress * 16, p, n * 16);
            }
            constAddress += n;
        } else {
            break;
        }
        p += count;
    }
    g_Slots[kSlots * 4 + 3] = 1; // guarantee termination
}

static void __stdcall XbLoadVertexShader(DWORD handle, DWORD address)
{
    if (handle & 1)
        LoadProgram(reinterpret_cast<const VertexShader*>(uintptr_t(handle - 1)), address);
}

static void __stdcall XbSelectVertexShader(DWORD handle, DWORD address)
{
    g_ProgramStart = address;
    g_Mode = VertexMode::Program;
    if (handle) {
        State().vertexShader = handle;
        g_XboxShader = reinterpret_cast<const VertexShader*>(uintptr_t(handle & ~1u));
    }
}

static void __stdcall XbSetVertexShader(DWORD handle)
{
    State().vertexShader = handle;
    if (!(handle & 1)) {
        g_Mode = VertexMode::Fvf;
        g_XboxShader = nullptr;
        return;
    }
    g_XboxShader = reinterpret_cast<const VertexShader*>(uintptr_t(handle - 1));
    if (g_XboxShader->Flags & VERTEXSHADER_FLAG_PROGRAM) {
        LoadProgram(g_XboxShader, 0);
        g_ProgramStart = 0;
        g_Mode = VertexMode::Program;
    } else {
        SetDefaultAttributes(g_XboxShader->Flags);
        g_Mode = (g_XboxShader->Flags & VERTEXSHADER_FLAG_PASSTHROUGH) ? VertexMode::Passthrough : VertexMode::FixedFunction;
    }
}

IDirect3DVertexShader9* CurrentHostVertexShader()
{
    if (g_Mode != VertexMode::Program)
        return nullptr;
    const uint32_t* start = &g_Slots[g_ProgramStart * 4];
    size_t n = 0;
    while (g_ProgramStart + n < kSlots && !(start[n * 4 + 3] & 1))
        ++n;
    ++n; // include the final instruction
    uint64_t key = Hash64(start, n * 16);

    IDirect3DVertexShader9* cached;
    if (CachedVertexShader(key, &cached))
        return cached;

    std::vector<Instr> prog;
    for (size_t i = 0; i < n; ++i)
        if (!Decode(start + i * 4, prog))
            break;
    IDirect3DVertexShader9* vs = CompileVertexShader(BuildHlsl(prog), key);
    LOG_DEBUG("Vertex program %016llX: %zu instructions -> %s", key, prog.size(), vs ? "ok" : "FAILED");
    return vs;
}

SDK_REPLACE("D3DDevice_SetVertexShader", XbSetVertexShader);
SDK_REPLACE("D3DDevice_LoadVertexShader", XbLoadVertexShader);
SDK_REPLACE("D3DDevice_SelectVertexShader", XbSelectVertexShader);

} // namespace swrots::d3d
