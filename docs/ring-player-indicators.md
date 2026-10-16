# Player indicators and native kart colors

Ring races use the native character kart palettes. Player identity is carried
by P1–P8 overhead labels, minimap dots and Battle balloons instead of rewriting
opaque colors in the eight kart palette banks.

The indicators are 8-bpp objects whose opaque pixels use palette indices
16, 32, …, 160. Those entries are transparent zero indices in every native
4-bpp palette bank. Hardware palette changes are applied after the native
palette copy; the software palette buffer and all opaque native colors remain
unchanged. Blue/orange/cyan/purple identify players 5–8.

`ringIdentity.O3.c` allocates a persistent atlas among unused OBJ tiles below
the kart texture banks. It checks both visible native objects and queued native
DMA, restores borrowed tiles before native uploads, and relocates conflicting
entries. Stable frames reuse the atlas. Scene changes restore every borrowed
tile and palette entry. Insufficient space hides an indicator and records an
overflow; gameplay validation requires zero overflows.

Regenerate the tracked tile data with `tools/ring/generate_identity_art.py`.
This reads the private US base ROM; normal builds need no image tools or
additional ROM. The detached real-BIOS oracle `test_identity_atlas.py` checks
128 layouts, native sprite and palette preservation, stable cache reuse, native
DMA and late-object eviction, a native P8 tile resembling an indicator tag, and
exact restoration. `test_rendering.py` checks consecutive in-game visibility
and native kart textures; `test_battle_moving.py` checks moving balloons/dots.

Extended character-menu cursors use the same transparent-entry technique.
Their blue/orange/cyan/purple highlight/body/shadow colors occupy fourteen
palette-zero slots, including white and black. They no longer borrow colors
from portraits that animate or turn gray. A frame-by-frame hardware-palette
hash checks stability. The original 2–4-player cursor palette remains native.
`test_cc_palette.py` compares the CC inset against unmodified four-pak menus
at 50/100/150cc for every console, including the new count/player labels.

The packed menu structures explicitly store enum-valued fields as 32-bit
integers. This matches native assembly and agbcc; it prevents modern GCC from
shrinking packed enum fields. Compile-time size checks protect the boundary.
The clean ROM remains byte-identical after these declarations and the clearer
sprite-sort names.

Current performance validation remains in progress. The previous shared-audio
build passed all 48 courses; normal audio still exposes brief slow windows on
GBA Battle Course 3 and Snow Land. The new atlas and collision improvements
require another complete course matrix before a release can be marked complete.
