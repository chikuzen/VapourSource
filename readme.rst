============
VapourSource
============
VapourSynth Script Importer for AviSynth2.6x

requirement:
------------
VapourSynth r19 or later

AviSynth2.6alpha4 or later

WindowsXPsp3/Vista/7/8

Microsoft Visual C++ 2010 Redistributable Package

SSE2 capable CPU

usage:
------
as follows.::

    VSImport(string source, bool "stacked", int "index")

    source  - input script path.
    stacked - if this is set to true, MSB/LSB will be separated and be stacked vertically (default: false).
    index   - index of input clip(default:0).


    VSEval(string source, bool "stacked", int "index", bool "utf8")
    
    source - vapoursynth script.
    stacked and index are same as VSImport.
    utf8   - if this is set to true, source is assumed to be UTF-8 encoded (default: false).

examples:
---------
VSImport example::

    LoadPlugin("c:/avisynth/plugins/VapourSource.dll")
    VSImport("d:/my_scripts/10bits_yuv_clip.vpy", stacked=true)

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
Supported formats are YUV420P8/9/10/16, YUV422P8/9/10/16, YUV444P8/9/10/16,
YUV411P8, GRAY8/16, RGB24, COMPATBGR32 and COMPATYUY2.
Others are not.

Not constant format/resolution/framerate clips are unsupported.

source code:
------------
https://github.com/chikuzen/VapourSource/
