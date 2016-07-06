/*
  VapourSynth Script Importer for AviSynth2.6x

  Copyright (C) 2013 Oka Motofumi

  Authors: Oka Motofumi (chikuzen.mo at gmail dot com)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
*/


#include <cstdio>
#include <cstdint>
#include <climits>
#include <string>
#include <vector>
#include <stdexcept>
#include <emmintrin.h>
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#define NOGDI
#include <windows.h>
#include <avisynth.h>
#include <VSScript.h>

#pragma warning(disable:4996)

#define VS_VERSION "0.1.0"


typedef IScriptEnvironment ise_t;


static void
convert_ansi_to_utf8(const char* ansi_string, std::vector<char>& buff)
{
    int length = MultiByteToWideChar(CP_THREAD_ACP, 0, ansi_string, -1, 0, 0);

    std::vector<wchar_t> wc_string(length, 0);
    MultiByteToWideChar(
        CP_THREAD_ACP, 0, ansi_string, -1, wc_string.data(), length);

    length = WideCharToMultiByte(CP_UTF8, 0, wc_string.data(), -1, 0, 0, 0, 0);
    buff.resize(length, 0);
    WideCharToMultiByte(
        CP_UTF8, 0, wc_string.data(), -1, buff.data(), length, 0, 0);
}


static int get_avs_pixel_type(int in, bool is_plus) noexcept
{
    struct {
        int vs_format;
        int avs_pixel_type;
    } table[] = {
        {pfGray8,       VideoInfo::CS_Y8 },
        {pfGray16,      is_plus ? VideoInfo::CS_Y16 : VideoInfo::CS_Y8 },
        {pfGrayH,       is_plus ? VideoInfo::CS_Y16 : VideoInfo::CS_Y8 },
        {pfGrayS,       is_plus ? VideoInfo::CS_Y32 : VideoInfo::CS_Y8 },
        {pfYUV420P8,    VideoInfo::CS_I420 },
        {pfYUV420P9,    is_plus ? VideoInfo::CS_YUV420P16 : VideoInfo::CS_I420 },
        {pfYUV420P10,   is_plus ? VideoInfo::CS_YUV420P16 : VideoInfo::CS_I420 },
        {pfYUV420P16,   is_plus ? VideoInfo::CS_YUV420P16 : VideoInfo::CS_I420 },
        {pfYUV422P8,    VideoInfo::CS_YV16 },
        {pfYUV422P9,    is_plus ? VideoInfo::CS_YUV422P16 : VideoInfo::CS_YV16 },
        {pfYUV422P10,   is_plus ? VideoInfo::CS_YUV422P16 : VideoInfo::CS_YV16 },
        {pfYUV422P16,   is_plus ? VideoInfo::CS_YUV422P16 : VideoInfo::CS_YV16 },
        {pfYUV444P8,    VideoInfo::CS_YV24 },
        {pfYUV444P9,    is_plus ? VideoInfo::CS_YUV444P16 : VideoInfo::CS_YV24 },
        {pfYUV444P10,   is_plus ? VideoInfo::CS_YUV444P16 : VideoInfo::CS_YV24 },
        {pfYUV444P16,   is_plus ? VideoInfo::CS_YUV444P16 : VideoInfo::CS_YV24 },
        {pfYUV444PH,    is_plus ? VideoInfo::CS_YUV444P16 : VideoInfo::CS_YV24 },
        {pfYUV444PS,    is_plus ? VideoInfo::CS_YUV444PS : VideoInfo::CS_YV24 },
        {pfYUV411P8,    VideoInfo::CS_YV411 },
        {pfRGB24,       VideoInfo::CS_BGR24 },
        {pfCompatBGR32, VideoInfo::CS_BGR32 },
        {pfCompatYUY2,  VideoInfo::CS_YUY2 },
        {in,            VideoInfo::CS_UNKNOWN }
    };

    int i = 0;
    while (table[i].vs_format != in) i++;
    return table[i].avs_pixel_type;
}


static void __stdcall
write_interleaved_frame(const VSAPI* vsapi, const VSFrameRef* src,
                        PVideoFrame& dst, const int num_planes, ise_t* env) noexcept
{
    static const int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    int pitch_mul = 1;
    int dstp_adjust = 0;
    if ((vsapi->getFrameFormat(src))->id == pfCompatBGR32) {
        pitch_mul = -1;
        dstp_adjust = -1;
    }

    for (int p = 0; p < num_planes; p++) {
        const int plane = planes[p];

        int dst_pitch = dst->GetPitch(plane) * pitch_mul;
        int height = dst->GetHeight(plane);
        uint8_t *dstp = dst->GetWritePtr(plane) +
                        dstp_adjust * (dst_pitch * (height - 1));

        env->BitBlt(dstp, dst_pitch, vsapi->getReadPtr(src, p),
                    vsapi->getStride(src, p), dst->GetRowSize(plane), height);
    }
}


static void __stdcall
write_stacked_frame(const VSAPI* vsapi, const VSFrameRef* src,
                    PVideoFrame& dst, const int num_planes, ise_t* env) noexcept
{
    int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    __m128i mask = _mm_set1_epi16(0x00FF);

    for (int p = 0; p < num_planes; p++) {
        const int plane = planes[p];

        int rowsize = dst->GetRowSize(plane);
        int height = dst->GetHeight(plane) / 2;
        int src_pitch = vsapi->getStride(src, p);
        int dst_pitch = dst->GetPitch(plane);
        const uint8_t* srcp = vsapi->getReadPtr(src, p);
        uint8_t* dstp0 = dst->GetWritePtr(plane);
        uint8_t* dstp1 = dstp0 + dst_pitch * height;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < rowsize; x += 16) {
                __m128i xmm0 = _mm_load_si128((__m128i*)(srcp + 2 * x));
                __m128i xmm1 = _mm_load_si128((__m128i*)(srcp + 2 * x + 16));
                __m128i lsb = _mm_packus_epi16(_mm_and_si128(mask, xmm0),
                                               _mm_and_si128(mask, xmm1));

                __m128i msb = _mm_packus_epi16(_mm_srli_epi16(xmm0, 8),
                                               _mm_srli_epi16(xmm1, 8));

                _mm_stream_si128((__m128i*)(dstp0 + x), msb);
                _mm_stream_si128((__m128i*)(dstp1 + x), lsb);
            }

            srcp += src_pitch;
            dstp0 += dst_pitch;
            dstp1 += dst_pitch;
        }
    }
}


static void __stdcall
write_bgr24_frame(const VSAPI* vsapi, const VSFrameRef* src,
                  PVideoFrame& dst, int num_planes, ise_t* env) noexcept
{
    int width = vsapi->getFrameWidth(src, 0);
    int height = vsapi->getFrameHeight(src, 0);

    const uint8_t* srcpb = vsapi->getReadPtr(src, 2);
    const uint8_t* srcpg = vsapi->getReadPtr(src, 1);
    const uint8_t* srcpr = vsapi->getReadPtr(src, 0);
    int src_pitch = vsapi->getStride(src, 0);

    int dst_pitch = dst->GetPitch();
    uint8_t* dstp = dst->GetWritePtr() + dst_pitch * (height - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            dstp[3 * x    ] = srcpb[x];
            dstp[3 * x + 1] = srcpg[x];
            dstp[3 * x + 2] = srcpr[x];
        }
        srcpb += src_pitch;
        srcpg += src_pitch;
        srcpr += src_pitch;
        dstp -= dst_pitch;
    }
}


class VapourSource : public IClip {
    const char* mode;
    int isInit;
    VSScript* vsEnv;
    const VSAPI* vsapi;
    VSNodeRef* node;
    const VSVideoInfo* vsvi;
    VideoInfo vi;

    void (__stdcall *func_write_frame)(const VSAPI*, const VSFrameRef*,
                                       PVideoFrame&, int, ise_t*);
    void validate(bool cond, std::string msg);
    void destroy() noexcept;

public:
    VapourSource(const char* source, bool stacked, int index, bool utf8, 
                 const char* mode, ise_t* env);
    virtual ~VapourSource();
    PVideoFrame __stdcall GetFrame(int n, ise_t* env);
    bool __stdcall GetParity(int n) { return false; }
    void __stdcall GetAudio(void*, __int64, __int64, ise_t*) {}
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int, int) { return 0; }
};


void VapourSource::destroy() noexcept
{
    if (node) {
        vsapi->freeNode(node);
    }
    if (vsEnv) {
        vsscript_freeScript(vsEnv);
    }
    if (isInit) {
        vsscript_finalize();
    }
}


void VapourSource::validate(bool cond, std::string msg)
{
    if (cond) {
        destroy();
        throw std::runtime_error(msg);
    }
}


VapourSource::
VapourSource(const char* source, bool stacked, int index, bool utf8,
             const char* m, ise_t* env) :
    mode(m), isInit(0), vsEnv(nullptr), node(nullptr)
{
    using std::string;

    memset(&vi, 0, sizeof(vi));

    isInit = vsscript_init();
    validate(isInit == 0, "failed to initialize VapourSynth.");

    vsapi = vsscript_getVSApi();
    validate(!vsapi, "failed to get vsapi pointer.");

    std::vector<char> script;
    if (utf8) {
        script.resize(strlen(source) + 1, 0);
        memcpy(script.data(), source, script.size() - 1);
    } else {
        convert_ansi_to_utf8(source, script);
    }

    if (mode[2] == 'I') {
        int ret = vsscript_evaluateFile(&vsEnv, script.data(), efSetWorkingDir);
        if (ret != 0) {
            auto msg = string("failed to evaluate script.\n")
                + vsscript_getError(vsEnv);
            destroy();
            throw std::runtime_error(msg);
        }
    }

    if (mode[2] == 'E') {
        std::vector<char> name;
        convert_ansi_to_utf8(env->GetVar("$ScriptName$").AsString(), name);
        int ret = vsscript_evaluateScript(
            &vsEnv, script.data(), name.data(), efSetWorkingDir);
        if (ret != 0) { 
            auto msg = string("failed to evaluate script.\n")
                + vsscript_getError(vsEnv);
            destroy();
            throw std::runtime_error(msg);
        }
    }

    const string idx = std::to_string(index);
    node = vsscript_getOutput(vsEnv, index);
    validate(!node, "failed to get VapourSynth clip(index:" + idx + ")");

    vsvi = vsapi->getVideoInfo(node);
    validate(vsvi->numFrames == 0, "input clip has infinite length.");

    validate(!vsvi->format || vsvi->width == 0 || vsvi->height == 0,
             "input clip is not constant format.");

    validate(vsvi->fpsNum == 0, "input clip is not constant framerate.");

    const string umax = std::to_string(UINT_MAX);
    validate(vsvi->fpsNum > UINT_MAX, "clip has over" + umax + "fpsnum.");
    validate(vsvi->fpsDen > UINT_MAX, "clip has over " + umax + "fpsden.");

    bool is_plus = env->FunctionExists("SetFilterMTMode") && !stacked;

    vi.pixel_type = get_avs_pixel_type(vsvi->format->id, is_plus);
    validate(vi.pixel_type == vi.CS_UNKNOWN, "input clip is unsupported format.");

    vi.width = vsvi->width;
    vi.height = vsvi->height;
    vi.fps_numerator = (unsigned)vsvi->fpsNum;
    vi.fps_denominator = (unsigned)vsvi->fpsDen;
    vi.num_frames = vsvi->numFrames;
    vi.SetFieldBased(false);

    int bytes = vi.IsPlanar() ? vsvi->format->bytesPerSample : 1;
    if (!is_plus && bytes > 1) {
        vi.width *= bytes;
    }
    if (stacked && bytes > 1) {
        vi.width /= 2;
        vi.height *= 2;
    }

    func_write_frame = vi.IsRGB24()         ? write_bgr24_frame :
                       stacked && bytes > 1 ? write_stacked_frame :
                                              write_interleaved_frame;
}


VapourSource::~VapourSource()
{
    destroy();
}


PVideoFrame __stdcall VapourSource::GetFrame(int n, ise_t* env)
{
    const VSFrameRef* src = vsapi->getFrame(n, node, 0, 0);
    if (!src) {
        env->ThrowError("%s: failed to get frame from vapoursynth.", mode);
    }

    PVideoFrame dst = env->NewVideoFrame(vi);

    func_write_frame(vsapi, src, dst, vsvi->format->numPlanes, env);

    vsapi->freeFrame(src);

    return dst;
}


AVSValue __cdecl
create_vapoursource(AVSValue args, void* user_data, ise_t* env)
{
    const char* mode = reinterpret_cast<char*>(user_data);
    if (!args[0].Defined()) {
        env->ThrowError("%s: No source specified", mode);
    }
    bool utf8 = mode[2] == 'E' ? args[3].AsBool(false) : false;
    
    try {
        return new VapourSource(args[0].AsString(), args[1].AsBool(false),
                                args[2].AsInt(0), utf8, mode, env);
    } catch (std::runtime_error& e) {
        env->ThrowError("%s: %s", mode, e.what());
    }
    return 0;
}


static const AVS_Linkage* AVS_linkage = 0;


extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(ise_t* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;

    env->AddFunction("VSImport", "[source]s[stacked]b[index]i",
                     create_vapoursource, "VSImport");
    env->AddFunction("VSEval", "[source]s[stacked]b[index]i[utf8]b",
                     create_vapoursource, "VSEval");

    if (env->FunctionExists("SetFilterMTMode")) {
        auto env2 = static_cast<IScriptEnvironment2*>(env);
        env2->SetFilterMTMode("VSImport", MT_SERIALIZED, true);
        env2->SetFilterMTMode("VSEval", MT_SERIALIZED, true);
    }

    return "VapourSynth Script importer ver." VS_VERSION " by Oka Motofumi";
}
