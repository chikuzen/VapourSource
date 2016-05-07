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
#include <limits.h>
#include <emmintrin.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <avisynth.h>
#include <VSScript.h>

#pragma warning(disable:4996)

#define VS_VERSION "0.0.3"


static const AVS_Linkage* AVS_linkage = 0;


static char* convert_ansi_to_utf8(const char* ansi_string)
{
    if (!ansi_string) {
        return 0;
    }

    int length = MultiByteToWideChar(CP_THREAD_ACP, 0, ansi_string, -1, 0, 0);

    wchar_t* wc_string = (wchar_t*)malloc(sizeof(wchar_t) * length);
    if (!wc_string) {
        return 0;
    }
    MultiByteToWideChar(CP_THREAD_ACP, 0, ansi_string, -1, wc_string, length);

    length = WideCharToMultiByte(CP_UTF8, 0, wc_string, -1, 0, 0, 0, 0);
    char* utf8_string = (char*)malloc(length);
    if (!utf8_string) {
        free(wc_string);
        return 0;
    }
    WideCharToMultiByte(CP_UTF8, 0, wc_string, -1, utf8_string, length, 0, 0);
    free(wc_string);

    return utf8_string;
}


static int get_avs_pixel_type(int in)
{
    struct {
        int vs_format;
        int avs_pixel_type;
    } table[] = {
        {pfGray8,       VideoInfo::CS_Y8     },
        {pfGray16,      VideoInfo::CS_Y8     },
        {pfYUV420P8,    VideoInfo::CS_I420   },
        {pfYUV420P9,    VideoInfo::CS_I420   },
        {pfYUV420P10,   VideoInfo::CS_I420   },
        {pfYUV420P16,   VideoInfo::CS_I420   },
        {pfYUV422P8,    VideoInfo::CS_YV16   },
        {pfYUV422P9,    VideoInfo::CS_YV16   },
        {pfYUV422P10,   VideoInfo::CS_YV16   },
        {pfYUV422P16,   VideoInfo::CS_YV16   },
        {pfYUV444P8,    VideoInfo::CS_YV24   },
        {pfYUV444P9,    VideoInfo::CS_YV24   },
        {pfYUV444P10,   VideoInfo::CS_YV24   },
        {pfYUV444P16,   VideoInfo::CS_YV24   },
        {pfYUV411P8,    VideoInfo::CS_YV411  },
        {pfRGB24,       VideoInfo::CS_BGR24  },
        {pfCompatBGR32, VideoInfo::CS_BGR32  },
        {pfCompatYUY2,  VideoInfo::CS_YUY2   },
        {in,            VideoInfo::CS_UNKNOWN}
    };

    int i = 0;
    while (table[i].vs_format != in) i++;
    return table[i].avs_pixel_type;
}


static void __stdcall
write_interleaved_frame(const VSAPI* vsapi, const VSFrameRef* src,
                        PVideoFrame& dst, int num_planes,
                        IScriptEnvironment* env)
{
    int planes[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    int pitch_mul = 1;
    int dstp_adjust = 0;
    if ((vsapi->getFrameFormat(src))->id == pfCompatBGR32) {
        pitch_mul = -1;
        dstp_adjust = 1;
    }

    for (int p = 0; p < num_planes; p++) {
        int dst_pitch = dst->GetPitch(planes[p]) * pitch_mul;
        int height = dst->GetHeight(planes[p]);
        uint8_t *dstp = dst->GetWritePtr(planes[p]) +
                        dstp_adjust * (-dst_pitch * (height - 1));

        env->BitBlt(dstp, dst_pitch,
                    vsapi->getReadPtr(src, p),
                    vsapi->getStride(src, p),
                    dst->GetRowSize(planes[p]),
                    height);
    }
}


static void __stdcall
write_stacked_frame(const VSAPI* vsapi, const VSFrameRef* src,
                    PVideoFrame& dst, int num_planes, IScriptEnvironment* env)
{
    int plane[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    __m128i mask = _mm_set1_epi16(0x00FF);

    for (int p = 0; p < num_planes; p++) {

        int rowsize = dst->GetRowSize(plane[p]);
        int height = dst->GetHeight(plane[p]) / 2;
        int src_pitch = vsapi->getStride(src, p);
        int dst_pitch = dst->GetPitch(plane[p]);
        const uint8_t* srcp = vsapi->getReadPtr(src, p);
        uint8_t* dstp0 = dst->GetWritePtr(plane[p]);
        uint8_t* dstp1 = dstp0 + dst_pitch * height;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < rowsize; x += 16) {
                __m128i xmm0 = _mm_load_si128((__m128i*)(srcp + 2 * x));
                __m128i xmm1 = _mm_load_si128((__m128i*)(srcp + 2 * x + 16));
                __m128i lsb = _mm_packus_epi16(_mm_and_si128(mask, xmm0),
                                               _mm_and_si128(mask, xmm1));

                __m128i msb = _mm_packus_epi16(_mm_srli_epi16(xmm0, 8),
                                               _mm_srli_epi16(xmm1, 8));

                _mm_store_si128((__m128i*)(dstp0 + x), msb);
                _mm_store_si128((__m128i*)(dstp1 + x), lsb);
            }

            srcp += src_pitch;
            dstp0 += dst_pitch;
            dstp1 += dst_pitch;
        }
    }
}


static void __stdcall
write_bgr24_frame(const VSAPI* vsapi, const VSFrameRef* src,
                  PVideoFrame& dst, int num_planes, IScriptEnvironment* env)
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

    int is_init;
    VSScript* se;
    const VSAPI* vsapi;
    VSNodeRef* node;
    const VSVideoInfo* vsvi;
    char* script_buffer;
    VideoInfo vi;

    void (__stdcall *func_write_frame)(const VSAPI*, const VSFrameRef*,
                                       PVideoFrame&, int, IScriptEnvironment*);
public:
    VapourSource(const char* source, bool stacked, int index, const char* mode,
                 IScriptEnvironment* env);
    virtual ~VapourSource();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n) { return false; }
    void __stdcall GetAudio(void*, __int64, __int64, IScriptEnvironment*) {}
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int, int) { return 0; }
};


VapourSource::VapourSource(const char* source, bool stacked, int index,
                           const char* mode, IScriptEnvironment* env)
{
    is_init = 0;
    se = 0;
    node = 0;
    script_buffer = 0;
    memset(&vi, 0, sizeof(vi));

    is_init = vsscript_init();
    if (is_init == 0) {
        env->ThrowError("%s: failed to initialize VapourSynth.", mode);
    }

    vsapi = vsscript_getVSApi();
    if (!vsapi) {
        env->ThrowError("%s: failed to get vsapi pointer.", mode);
    }

    script_buffer = convert_ansi_to_utf8(source);
    if (!script_buffer) {
        env->ThrowError("%s: failed to convert to UTF-8.\n", mode);
    }

    if (mode[2] == 'I'
        && vsscript_evaluateFile(&se, script_buffer, efSetWorkingDir)) {
        env->ThrowError("%s: failed to evaluate script.\n%s", mode,
                        vsscript_getError(se));
    }

    if (mode[2] == 'E') {
        char *name = convert_ansi_to_utf8(env->GetVar("$ScriptName$").AsString());
        int ret = vsscript_evaluateScript(&se, script_buffer, name,
                                          efSetWorkingDir);
        free(name);
        if (ret) {
            env->ThrowError("%s: failed to evaluate script.\n%s", mode,
                            vsscript_getError(se));
        }
    }

    node = vsscript_getOutput(se, index);
    if (!node) {
        env->ThrowError("%s: failed to get VapourSynth clip(index:%d).", mode,
                        index);
    }

    vsvi = vsapi->getVideoInfo(node);

    if (vsvi->numFrames == 0) {
        env->ThrowError("%s: input clip has infinite length.", mode);
    }

    if (!vsvi->format || vsvi->width == 0 || vsvi->height == 0) {
        env->ThrowError("%s: input clip is not constant format.", mode);
    }

    if (vsvi->fpsNum == 0) {
        env->ThrowError("%s: input clip is not constant framerate.", mode);
    }

    if (vsvi->fpsNum > UINT_MAX) {
        env->ThrowError("%s: clip has over %u fpsnum.", mode, UINT_MAX);
    }

    if (vsvi->fpsDen > UINT_MAX) {
        env->ThrowError("%s: clip has over %u fpsden.", mode, UINT_MAX);
    }

    vi.pixel_type = get_avs_pixel_type(vsvi->format->id);
    if (vi.pixel_type == VideoInfo::CS_UNKNOWN) {
        env->ThrowError("%s: input clip is unsupported format.", mode);
    }

    int over_8bit = vi.IsPlanar() ? vsvi->format->bytesPerSample - 1 : 0;
    vi.width = vsvi->width << (over_8bit * (stacked ? 0 : 1));
    vi.height = vsvi->height << (over_8bit * (stacked ? 1 : 0));
    vi.fps_numerator = (unsigned)vsvi->fpsNum;
    vi.fps_denominator = (unsigned)vsvi->fpsDen;
    vi.num_frames = vsvi->numFrames;
    vi.SetFieldBased(false);

    func_write_frame = vi.IsRGB24()           ? write_bgr24_frame :
                       over_8bit && stacked   ? write_stacked_frame :
                                                write_interleaved_frame;
}


VapourSource::~VapourSource()
{
    if (script_buffer) {
        free(script_buffer);
    }
    if (node) {
        vsapi->freeNode(node);
    }
    if (se) {
        vsscript_freeScript(se);
    }
    if (is_init) {
        vsscript_finalize();
    }
}


PVideoFrame __stdcall VapourSource::GetFrame(int n, IScriptEnvironment* env)
{
    const VSFrameRef* src = vsapi->getFrame(n, node, 0, 0);
    if (!src) {
        env->ThrowError("%s: failed to get frame from vapoursynth.");
    }

    PVideoFrame dst = env->NewVideoFrame(vi);

    func_write_frame(vsapi, src, dst, vsvi->format->numPlanes, env);

    vsapi->freeFrame(src);

    return dst;
}


AVSValue __cdecl
create_vapoursource(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    const char* mode = (char*)user_data;
    if (!args[0].Defined()) {
        env->ThrowError("%s: No source specified", mode);
    }
    return new VapourSource(args[0].AsString(), args[1].AsBool(false),
                            args[2].AsInt(0), mode, env);
}


extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;
    env->AddFunction("VSImport", "[source]s[stacked]b[index]i",
                     create_vapoursource, (void*)"VSImport");
    env->AddFunction("VSEval", "[source]s[stacked]b[index]i",
                     create_vapoursource, (void*)"VSEval");
    return "VapourSynth Script importer ver." VS_VERSION " by Oka Motofumi";
}
