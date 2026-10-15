The supplied USA 60 FPS IPS patch replaces the two-byte store at ROM offset
`0x30C18` (`str r0, [r2, #0x50]`) with zero. It removes the write-buffer swap
at the end of `sub_SetHDMA` in `src/camera.c`.

The ring build uses the equivalent ARM projection kernel in
`nonmatching/ringCamera.s`. That kernel now leaves `Camera.writeAffineBuffer`
unchanged. Race display DMA consumes bank zero; alternating writes between
banks previously made only every other projection visible. Other camera
publication helpers retain their original behavior.

A real-BIOS moving eight-console comparison measured 299 changed visible
road projections across 599 frame intervals in the previous build and 599
across 599 intervals after the fix, independently on all eight consoles.
The displayed bank stayed zero in both runs. See the CLI video log's
`roadDisplayBuffer`, `roadWriteBuffer` and `roadProjectionHash` columns.

The original build retains the original store and its expected SHA-1. The
ring fix uses symbols and field offsets, including in shifted builds.
`tools/ring/test_camera.py` checks the actual ARM kernel against native C
arithmetic for 393,216 scanlines and both initial banks, including signed
overflow, and requires the selected write bank to remain unchanged.
