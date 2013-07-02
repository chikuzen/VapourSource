==========================================================
VapourSource - VapourSynth Script Reader for AviSynth2.6x
==========================================================
VapourSynth Script Reader for AviSynth2.6x

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

    LoadPlugin("/path/to/VapourSource.dll")
    VapourSource(source="/path/to/the/script.vpy", index=0, stacked=false)

    source: input script path.
    index:  index of input clip(default 0).
    stacked: if this is set to true, MSB/LSB will be separated and be stacked vertially(default false).

note:
-----
    Supported formats are YUV420P8/9/10/16, YUV422P8/9/10/16, YUV444P8/9/10/16,
    YUV411P8, GRAY8/16, COMPATBGR32 and COMPATYUY2.
    Others are not.

    not constant format/framerate clip is unsupported.

source code:
------------
    https://github.com/chikuzen/VapourSource/
