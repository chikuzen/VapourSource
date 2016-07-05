============
VapourSource
============
VapourSynth Script Importer for Avisynth2.6/Avisynth+

version:
---------
0.1.0

requirement:
------------
VapourSynth r30 or later

AviSynth2.60 / Avisynth+MT r2005 or greater

Windows Vista sp2 or later

Microsoft Visual C++ 2015 Redistributable Package

SSE2 capable CPU

usage:
------
as follows.::

    VSImport(string source, bool "stacked", int "index")

    source  - input script path.
    stacked - if this is set to true, MSB/LSB will be separated and be stacked vertially(default:false).
    index   - index of input clip(default:0).


    VSEval(string source, bool "stacked", int "index")
    
    source - vapoursynth script.
    stacked and index are same as VSImport.

Measurement table of formats:
------------------------------

    VapourSynth         Avisynth2.6         Avisynth+MT
    GRAY8               Y8                  Y8
    GRAY16/H            Y8(x2 width)        Y16
    GRAYS               Y8(x4 width)        Y32
    YUV444P8            YV24                YV24
    YUV444P9/10/16/H    YV24(x2 width)      YUV444P16
    YUV444PS            YV24(x4 width)      YUV444PS
    YUV422P8            YV16                YV16
    YUV422P9/10/16      YV16(x2 width)      YUV422P16
    YUV420P8            YV12                YV12
    YUV420P9/10/16      YV12(x2 width)      YUV420P16
    COMPATBGR32         RGB32               RGB32
    COMPATYUY2          YUY2                YUY2

Others are not supported.


examples:
---------
VSImport example::

    LoadPlugin("c:/avisynth/plugins/VapourSource.dll")
    VSImport("d:/my_scripts/16bits_yuv_clip.vpy", stacked=false)

VSEval example::

    LoadPlugin("c:/avisynth/plugins/VapourSource.dll")
    script = """
    import vapoursynth as vs
    bc = vs.get_core().std.BlankClip
    clip0 = bc(format=vs.YUV422P8, color=[0, 128, 128])
    clip1 = bc(format=vs.YUV422P8, color=[255, 128, 128])
    clip0.set_output(index=0)
    clip1.set_output(index=1)
    """
    VSEval(script, index=1)

note:
-----
Not constant format/resolution/framerate clips are unsupported.

source code:
------------
https://github.com/chikuzen/VapourSource/
