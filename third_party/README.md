# third_party

`libmpeg-fixed.a` is ps2sdk's libmpeg with one fix in `_mpeg_dmac_handler`
(`libmpeg_core.c`): when colour conversion continues in a second run, the
handler programmed the two DMA channels (to and from the IPU) with each
other's transfer lengths (QWC). Any picture over 1023 macroblocks then never
finished converting, which hung playback above about 640x400.

It is baked into the published build image (see `Dockerfile.release`), so a
normal build does not need it. It is kept here so the fix stays with the
code and the image can be rebuilt.
