# Retro Special GP maps and Docker releases

## Map lifetime

The whole-course display previously treated native HUD cells as proof that
the next course's overview had loaded. Those cells survive the scene heap
reset. On later GP courses this could cache empty VRAM, leaving only player
labels over black. The map now captures the overview immediately after the
native HUD loader decompresses it, along with its palette. Each new scene
resets that cache. The padding tile is explicitly cleared because native
`frmheap_calloc` only clears part of a reused allocation.

The clean build remains byte-identical to the US ROM (SHA-1
`9d327c030c3e2d9007990518594f70c3340ac56f`). The new loader call is conditional
on the ring build, uses a symbol, and passes the 1,024-byte code-shift audit.

## Local BIOS-backed evidence

Evidence is retained under `/workspace/.setup/results/review-16/`; this is a
local evidence path, not a committed artifact or a CI download.

* `graphics-baseline`: the original nine-console Retro Special GP reproduced
  a black final map. Its captured image had only 264 colored pixels.
* `graphics-map-hook`: all four maps remain visible with the new loader hook.
* `fixed-special-150-items`: nine consoles, eight human racers, all four Retro
  Special courses at 150cc, item use, and 900 final results frames. All maps,
  scores and synchronized driver positions passed. Settled results show all
  eight native portrait textures and their rendered opaque pixels.
* `portrait-stress-old` and `portrait-stress-restored`: first-course results
  observed for 1,800 frames with original and restored samples/Japanese art.
  Neither reproduced a missing portrait. This does not establish that the
  reported intermittent P4/Bowser case is fixed.
* `bowser-p4-100cc-grid`: the specific 100cc P4/Bowser arrangement, eight
  racers plus a map console, all four Retro Special courses with items, and
  900 final results frames. Native character IDs confirm P4/Bowser (2) and
  P8/Toad (6). All eight portrait textures and their displayed opaque pixels
  passed in 9,544 settled per-console results frames. P4/Bowser won the first
  races and its first-place portrait was present on P4's own display. All
  four maps also passed. The reported intermittent portrait remains open;
  these checks establish a passing reproduction attempt, not a fix for an
  unreproduced symptom.
* `map-entry.log`: map entry, geometry, labels, silence and synchronized
  physics pass in both Vs. and Battle.

The GP test records every results entry and checks actual displayed pixels,
rather than accepting a nonempty texture or a marker-only map. Checks exclude
the next course's scene initialization, which clears standings while the old
scene pointer is still present. Captures and state logs are kept separately
from the immutable ROM copy used by each run.

## Release validation

See [the release guide](../tools/release/README.md) for Docker Desktop,
PowerShell and Linux commands. The image was built from the Dockerfile, then
run without network access as an unprivileged user with read-only inputs:

* `docker-final-release`: all 18 ROMs, including 16 ring variants.
* `docker-partial-release`: ten ROMs with original samples only, without
  mounting the optional audio patch.
* `docker-audit.log`: a second container independently reconstructed all 18
  final IPS patches, compared exact bytes and SHA-256, verified every exported
  checksum, checked the patch-only ZIP, and confirmed no supplied ROM, BIOS or
  IPS is embedded in the image. Bad ROM/patch inputs and a nonempty output
  directory were rejected.
* `tools/release/test_ips.py`: five format tests pass, covering 16 MiB output,
  reserved EOF offsets, record boundaries, truncation, malformed records and
  randomized deltas, with an independent decoder for valid patches.

Packaging validation does not run the gameplay gates or establish Windows
rendering, physical cable operation, or all previous acceptance criteria.
