/*
  VapourSynth Script Reader for AviSynth2.6x

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


#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <limits.h>
#include <emmintrin.h>
#include <windows.h>
#include "avisynth.h"
#include "VSScript.h"

#pragma warning(disable:4996)

#define VS_VERSION "0.0.1"


static const AVS_Linkage* AVS_linkage = 0;


static size_t get_filesize(const char* filename)
{
    struct stat st;
    if (stat(filename, &st)) {
        return 0;
    }
    return (size_t)st.st_size;
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
                        PVideoFrame& dst, int num_planes, IScriptEnvironment* env)
{
    int plane[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    for (int p = 0; p < num_planes; p++) {
        env->BitBlt(dst->GetWritePtr(plane[p]),
                    dst->GetPitch(plane[p]),
                    vsapi->getReadPtr(src, p),
                    vsapi->getStride(src, p),
                    dst->GetRowSize(plane[p]),
                    dst->GetHeight(plane[p]));
    }
}


static void __stdcall
write_stacked_frame(const VSAPI* vsapi, const VSFrameRef* src,
                    PVideoFrame& dst, int num_planes, IScriptEnvironment* env)
{
    int plane[] = {PLANAR_Y, PLANAR_U, PLANAR_V};

    __m128i mask = _mm_srli_epi16(_mm_set1_epi8((char)0xFF), 8);

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

                xmm0 = _mm_srli_si128(xmm0, 1);
                xmm1 = _mm_srli_si128(xmm1, 1);
                __m128i msb = _mm_packus_epi16(_mm_and_si128(mask, xmm0),
                                               _mm_and_si128(mask, xmm1));

                _mm_store_si128((__m128i*)(dstp0 + x), msb);
                _mm_store_si128((__m128i*)(dstp1 + x), lsb);
            }

            srcp += src_pitch;
            dstp0 += dst_pitch;
            dstp1 += dst_pitch;
        }
    }
}


class VapourSource : public IClip {

    int is_init;
    VSScript* se;
    const VSAPI* vsapi;
    VSNodeRef* node;
    const VSVideoInfo* vsvi;
    char* script;
    VideoInfo vi;

    void (__stdcall *func_write_frame)(const VSAPI*, const VSFrameRef*,
                                       PVideoFrame&, int, IScriptEnvironment*);
public:
    VapourSource(const char* source, bool stacked, int index,
                 IScriptEnvironment* env);
    virtual ~VapourSource();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
    bool __stdcall GetParity(int n) { return false; }
    void __stdcall GetAudio(void*, __int64, __int64, IScriptEnvironment*) {}
    const VideoInfo& __stdcall GetVideoInfo() { return vi; }
    int __stdcall SetCacheHints(int, int) { return 0; }
};


VapourSource::VapourSource(const char* source, bool stacked, int index,
                           IScriptEnvironment* env)
{
    is_init = 0;
    se = 0;
    node = 0;
    script = 0;
    memset(&vi, 0, sizeof(vi));

    is_init = vseval_init();
    if (is_init == 0) {
        env->ThrowError("VapourSource: failed to initialize VapourSynth.");
    }

    vsapi = vseval_getVSApi();
    if (!vsapi) {
        env->ThrowError("VapourSource: failed to get vsapi pointer.");
    }

    size_t filesize = get_filesize(source);
    if (filesize == 0) {
        env->ThrowError("VapourSource: source does not exist, or it is empty.");
    }
    if (filesize > 16 * 1024 * 1024) {
        env->ThrowError("VapourSource: filesize of source is over 16MiB.");
    }

    script = (char *)calloc(filesize + 1, 1);
    if (!script) {
        env->ThrowError("VapourSource: failed to allocate script buffer.");
    }

    FILE *file = fopen(source, "rb");
    if (!file) {
        env->ThrowError("VapourSource: failed to open source script file.");
    }
    fread(script, 1, filesize, file);
    fclose(file);

    if (vseval_evaluateScript(&se, script, source)) {
        env->ThrowError("VapourSource: failed to evaluate script.\n%s",
                        vseval_getError(se));
    }

    node = vseval_getOutput(se, index);
    if (!node) {
        env->ThrowError("VapourSource:"
                        " failed to get VapourSynth clip(index:%d).", index);
    }

    vsvi = vsapi->getVideoInfo(node);

    if (vsvi->numFrames == 0) {
        env->ThrowError("VapourSource: input clip has infinite length.");
    }

    if (!vsvi->format || vsvi->width == 0 || vsvi->height == 0) {
        env->ThrowError("VapourSource: input clip is not constant format.");
    }

    if (vsvi->fpsNum == 0) {
        env->ThrowError("VapourSource: input clip is not constant framerate.");
    }

    if (vsvi->fpsNum > UINT_MAX) {
        env->ThrowError("VapourSource: clip has over %u fpsnum.", UINT_MAX);
    }

    if (vsvi->fpsDen > UINT_MAX) {
        env->ThrowError("VapourSource: clip has over %u fpsden.", UINT_MAX);
    }

    vi.pixel_type = get_avs_pixel_type(vsvi->format->id);
    if (vi.pixel_type == VideoInfo::CS_UNKNOWN) {
        env->ThrowError("VapourSource: input clip is unsupported format.");
    }

    int over_8bit = vi.IsPlanar() ? vsvi->format->bytesPerSample - 1 : 0;
    vi.width = vsvi->width << (over_8bit * (stacked ? 0 : 1));
    vi.height = vsvi->height << (over_8bit * (stacked ? 1 : 0));
    vi.fps_numerator = (unsigned)vsvi->fpsNum;
    vi.fps_denominator = (unsigned)vsvi->fpsDen;
    vi.num_frames = vsvi->numFrames;
    vi.SetFieldBased(false);

    func_write_frame = (over_8bit && stacked) ? write_stacked_frame :
                                                write_interleaved_frame;
}


VapourSource::~VapourSource()
{
    if (script) {
        free(script);
    }
    if (node) {
        vsapi->freeNode(node);
    }
    if (se) {
        vseval_freeScript(se);
    }
    if (is_init) {
        vseval_finalize();
    }
}


PVideoFrame __stdcall VapourSource::GetFrame(int n, IScriptEnvironment* env)
{
    PVideoFrame dst = env->NewVideoFrame(vi);
    const VSFrameRef* src = vsapi->getFrame(n, node, 0, 0);
    if (!src) {
        return dst;
    }

    func_write_frame(vsapi, src, dst, vsvi->format->numPlanes, env);

    vsapi->freeFrame(src);

    return dst;
}


AVSValue __cdecl
create_vapoursource(AVSValue args, void* user_data, IScriptEnvironment* env)
{
    if (!args[0].Defined()) {
        env->ThrowError("VapourSource: No source specified");
    }
    return new VapourSource(args[0].AsString(), args[1].AsBool(false),
                            args[2].AsInt(0), env);
}


extern "C" __declspec(dllexport) const char* __stdcall
AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
    AVS_linkage = vectors;
    env->AddFunction("VapourSource", "[source]s[stacked]b[index]i",
                     create_vapoursource, 0);
    return "VapourSynth Script Reader ver."VS_VERSION" by Oka Motofumi";
}
