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

The interrupt build has passed full rounds with counts 2–8 in both modes,
all recovery/restart/cancellation flows with two and eight participants,
consecutive rounds, the lobby audio check, and the ordinary GP regression.
All four Battle arenas pass eight-player entry under distinct controller input.
A nonzero physical clock owner also passes both mode entry checks. Keep the current result manifest with its immutable ROM
hash when assessing a test. An entry snapshot is not a completed-round test.

The emulator's 30 CTest targets passed. The ring driver supports normal-8 and
normal-32, native transfer timing, start/IRQ behavior, unarmed receivers, wire
loss, reset cancellation, and collision reporting. It is currently a local
headless runner feature; Qt/network/savestate ring integration is not included.
The model does not resolve intermediate bit edges or electrical loading.

## Research and remaining limits

Inspected GBATEK normal-serial documentation, Faceball2000DX enumeration fixes,
and DoubleCherryGB's ring implementation. The linked YouTube and Reddit pages
returned HTTP 403; their contents were not reviewed. Ghidra was not required
for the matching C work, which used the repository assembly and pinned agbcc.

Physical testing is unavailable because the user has no GBAs. The intended
AGB-005 arrangement puts each large plug on a console and each small plug in
the next cable's middle socket, closing SO-to-SI with shared SC/GND. No custom
cable is assumed. Software tests cannot establish cable loading, oscillator
phase/drift tolerance, or signal integrity on real hardware.

Stock-menu visual parity and HLE are stretch goals. HLE validation follows the
BIOS-backed core checks. Do not describe untested hardware, every possible
course/item combination, or a complete C menu decompilation as verified.
