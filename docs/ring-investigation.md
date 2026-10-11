# Ring multiplayer implementation and validation

The separate `mksc-ring.gba` build adds Ring Link to the title's link submenu.
The normal build remains byte-identical to the supplied AMKE ROM, SHA-1
`9d327c030c3e2d9007990518594f70c3340ac56f`. ROMs, BIOS files, extracted artwork,
and test snapshots are private ignored inputs/outputs, not repository assets.

## Matching decompilation and relocation

Before functional changes, matching C replaced the serial master/slave tests,
checksum, startup, shutdown, and readiness routines. Key buffers and race
controller fields gained meaningful names with full-ROM hash checks. These
changes were committed separately. Relevant native menu and physics assembly
remains assembly; this is not a complete C decompilation of those systems.

Commit `d22dc6e` replaces eleven fixed-address script callbacks with linker
symbols while preserving the stock hash. A shifted ordinary GP regression
uses real keypad inputs and item use, observes character-voice script callbacks,
and checks that they return safely. The earlier shifted ROM jumped into an
invalid address under the same inputs. Hash matching alone does not establish
relocation correctness; the shifted-build regression is required as well.

## Game integration

`src/ringLink.c` implements discovery, software IDs, shared menus, normal-32
serial transport, eight controller slots, gameplay setup, failure recovery,
rankings, and cumulative wins. The title lettering samples native loaded glyphs
and preserves their selected/unselected palettes. Menus use native portraits;
their bitmap layout is still simpler than the original Multi-Pak artwork.
Title glyphs are sampled once after the native assets load, rather than during
every draw. Repeated sampling previously crossed VBlank, allowing `oam_reset`
to erase the stock Single-Pak/Multi-Pak labels and slowing the submenu.

A clock owner circulates discovery and announces membership. CRC-8 packets
carry software ID, sequence, and keypad/control bits. Repeated READY probes
establish a barrier before FRAME/INPUT/COMMIT propagation. Forwarding runs from
serial completion interrupts so audio mixing and menu rendering do not leave
receivers unarmed. Timer 2 supplies forwarding turnaround; audio timers 0/1 and
stock serial timer 3 remain available. Bulk graphics DMA is deferred during an
input barrier, and the audio mixer is serviced on intervening video frames.

The eight held/trigger/repeat/raw vectors reside at `03006B00..03006B3F`.
Protocol and session state follow them, with a linker assertion below the
system stack reserve. Four-element native serial fields are never enlarged
in place. Driver input conversion and peer-word access use dedicated eight-slot
vectors. Vs. uses the native eight-position GP grid; Battle adds nearby valid
spawn positions to each arena's four native markers. Native physics, items,
camera, collision, lap rules, balloon counts, and pause menus remain in use.

Native four-row results routing is intercepted before touching those arrays.
Vs. finishes when all but one racer finish; the remaining racer ranks last.
Battle records elimination order and ends with at most one survivor. Results
include all participants and persistent win totals. Results, pause restart,
track/character changes, cancellation, and a fresh connection have separate
explicit routes.

Identity digits, balloon pips, and map markers use red, green, yellow, pink,
blue, orange, cyan, and purple. Their art uses the BG HUD gap `06002B00..06002DFF`
and palette 9, avoiding every kart texture slot and course-object sprite bank.
Some courses occupy both complete object banks, so the tail of OBJ memory is
not a generally safe allocation. Native portraits and kart textures are reused.

## Reproducible checks

The modified mGBA `mgba-link` runner boots the supplied real BIOS by default,
runs independent cores in timestamp order, and supplies keypad inputs only.
It can open/reconnect a wire, stop at a common committed-input barrier, observe
memory/scripts, and capture complete video frames. It does not inject racers,
CPU flags, items, rankings, or menu state into game RAM.

- `mgba/tools/link/test_stock.py`: original four-console Vs./Battle menu and
  gameplay entry baseline, including distinct controls and synchronized positions.
- `mgba/tools/link/test_ring.py`: fully armed homebrew protocol matrix, counts
  2–8, nonzero clock owner, sequence wrap, and wire-loss recovery.
- `tools/ring/test_game.py`: game menu/entry/controls, 2–8 participants in both
  modes, nonzero owner, and selectable Battle arenas.
- `tools/ring/test_rounds.py`: complete Vs. races and Battle rounds using keypad
  route following/item use or actual arena falls; compare rankings, wins,
  elimination/lap state, positions, and input rounds across all consoles.
- `tools/ring/test_flows.py`: cancellation, native pause/restart, and wire-loss
  recovery followed by a new synchronized race, with two and eight participants.
- `tools/ring/test_repeat.py`: two consecutive Battle rounds, using measured
  completion timing from the matching baseline; cumulative wins must be two.
- `tools/ring/test_lobby.py`: all 1584 PCM buffer bytes are cleared while waiting.
- `tools/ring/test_singleplayer.py`: ordinary GP with eight native racers and
  observed character-voice callbacks after item use in the shifted ROM.
- `tools/ring/test_title.py`: native Single-Pak/Multi-Pak/Ring Link sprite
  visibility and 200 menu updates across 200 video frames using the real BIOS.

The interrupt build has passed full rounds with counts 2–8 in both modes,
all recovery/restart/cancellation flows with two and eight participants,
consecutive rounds, the lobby audio check, and the ordinary GP regression.
All four Battle arenas pass eight-player entry under distinct controller input.
A nonzero physical clock owner also passes both mode entry checks. Keep the current result manifest with its immutable ROM
hash when assessing a test. An entry snapshot is not a completed-round test.

The emulator's 30 CTest targets passed. The ring driver supports normal-8 and
normal-32, native transfer timing, start/IRQ behavior, unarmed receivers, wire
loss, reset cancellation, and collision reporting. Local Qt windows now expose
a shared File-menu ring topology selector, eight input/save slots, and group
pause. See `mgba/tools/link/GUI.md` for build/use and fixture-based GUI checks.
Network play and connected-session savestates are not included.
The model does not resolve intermediate bit edges or electrical loading.

The Qt build has 34 passing CTest targets. Private real-BIOS GUI fixtures passed
two/eight-window Ring Vs. and Battle entry, four-window standard Vs. and Battle
entry using the mod ROM, group pause/resume, reset, middle-window removal,
close/reopen, synchronized two-window playback, and staggered startup.
Concurrent eight-window runs were below real time on the shared cloud VM;
desktop GUI throughput remains to be measured. GUI checks establish entry and
lifecycle behavior; completed rounds remain covered by the headless matrix.

## Sprite and scheduler corrections

The stock opponent-marker switch has only three entries. When a fourth remote
kart becomes visible it passes a null cell pointer at the original `0804A1F6`
call site, producing invalid OAM and displacing normal kart sprites. The ring
build supplies one valid diamond cell per human ID, with eight identity colors,
and reuses the stock marker tile region at `06012640`. Native kart texture banks
and course-object banks remain available. The native remote texture throttle
also uses video-frame parity, which can permanently skip slots when a ring
barrier consumes multiple video frames; ring races update these descriptors
each simulation frame. Both assembly hooks are conditional on `MKSC_RING`.
The ordinary build still matches SHA-1 `9d327c030c3e2d9007990518594f70c3340ac56f`.

The lobby/results/error bitmap renderer draws to the inactive Mode 4 page and
flips at VBlank only after the complete menu is ready. Palette uploads and
native audio service remain active. `test_rendering.py` checks every completed
frame: 2,400 identical frozen-menu frames across both pages, all eight karts and
seven identity markers in 700 consecutive rear-view frames, distinct texture
sources matching native VRAM banks, and 5,600 valid Battle sprite frames.

The transport now uses standard 2 MHz normal-32 serial with the existing
4,096-cycle forwarding turnaround. External-clock consoles sleep in HALT
between serial/VBlank interrupts. The GUI adapter batches bounded timestamp-
ordered slices on sleeping peers rather than handing off an OS thread for each
small serial slice. Only a core's owning thread waits on its audio/video output;
GUI interruption and lifecycle operations remain responsive with synchronized
eight-window playback. This changes scheduling overhead, not serial timing.

For ring ROM SHA-256
`23a83a53e90504ffe0882367e02a46a300d37ab5982d1a7fd36e82efd3498f44`,
the real-BIOS revalidation passed all fourteen 2–8-player Vs./Battle entry and
control cases, complete rounds in both modes at two and eight players, the
consecutive-frame rendering checks, all title labels with 200 updates in 200
frames, lobby PCM clearing, and ordinary GP with ten character-voice callbacks.
The GUI suite passed all ten cases, with an additional synchronized eight-window
Vs. lifecycle run. The emulator's 34 default CTest targets also passed.
Cancellation, wire-loss reconnect, pause/resume, and restart passed at two and
eight players. After BIOS validation, optional HLE entry/control checks also
passed in both modes at two and eight players.

## Research sources and hardware limits

Inspected GBATEK normal-serial documentation, Faceball2000DX enumeration fixes,
and DoubleCherryGB's ring implementation. The linked YouTube and Reddit pages
returned HTTP 403; their contents were not reviewed. Ghidra was not required
for the matching C work, which used the repository assembly and pinned agbcc.

Physical testing is unavailable because the user has no GBAs. The intended
AGB-005 arrangement puts each large plug on a console and each small plug in
the next cable's middle socket, closing SO-to-SI with shared SC/GND. No custom
cable is assumed. Software tests cannot establish cable loading, oscillator
phase/drift tolerance, or signal integrity on real hardware.

Stock-menu visual parity remains a stretch goal. After the BIOS-backed core
checks passed, explicit HLE validation passed entry and complete Vs./Battle
rounds at two and eight players, and ordinary GP character-voice callbacks.
The runner rejects unsupported BIOS calls and confirms that no external BIOS
was loaded. Recovery/replay and the intermediate player counts retain the
real-BIOS coverage described above. Do not describe untested hardware, every possible
course/item combination, or a complete C menu decompilation as verified.
