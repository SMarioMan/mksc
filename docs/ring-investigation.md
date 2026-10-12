## Follow-up: host audio, Battle rendering, and Grand Prix (2026-10-12)

Shared BGM now belongs only to software P1 (the clock owner), including direct
MP2000 start, continue and fade-in calls. Other consoles retain effects and
voices. A real-BIOS complete eight-player race hosted from physical window four
verified silent peer music players, the leader's final-lap policy, synchronized
physics, and 0.999 native updates/video frame. GP music considers the CPUs too.

The cup/footer hint is white at x=78, to the right of the A/B text at x=6;
published-pixel checks verify both screens and all four Retro course names.
Back is consumed once per menu transition, game changes reset the course, and
Battle clamps its index to 0–3. An eight-console Rainbow Road → pause/change
track → Back → Battle-course-two regression passes with common positions.
The title's native glyph crops remove neighboring E/L fragments rather than
painting over them with reused texture edges.

Moving Battle exposed course palette animation replacing balloon tint entries
with gray shades after HUD rendering. Publication now restores reserved OBJ
identity entries after queued DMA and before palette upload. Native highlight
shading remains intact. Battle portraits and pips select the four highest
remaining balloon counts, with stable player-order ties; the viewed-player
highlight follows its new row. Minimap dots include the native artwork's inset
in their anchor, leaving BG minimap tiles and course palettes untouched.

Battle target/depth response was converted to optimized C, retaining the
original Thumb implementation for ordinary play. A standalone BIOS-backed ROM
compares both actual implementations on 4096 fixtures, including byte wrapping,
checkpoint boundaries and effect order. Per-frame balloon offset caching avoids
repeated exact divisions without changing sprite sorting or animation.

Completed graphics can publish later in VBlank when the queued DMA byte budget
fits, retaining eight scanlines for the callback/IRQ tail. This remains scoped
to initialized physics; menus and countdown use their original barrier. READY
senders stop replaying after their own word returns around the ring; the owner
spaces idle polling by 1024 cycles at five or more racers, preserving 512-cycle
active forwarding and the original protected-vector recovery/replay behavior.
Moving normal-music Battle on the four arenas measured 0.996, 1.000, 0.956 and
0.992 native updates/video frame on all peers, with no collisions, CRC errors,
timeouts, invalid sprites or untinted balloon texels. These are simulation/video
ratios, not host-machine FPS or a claim that every workload is exactly 60 Hz.
Shared host-only music measured 0.997, 1.000, 0.993 and 0.999 on the same
four arenas. The final scripted eight-window GUI launch passed independent
persisted profiles, P1–P8 titles, Alt/menu behavior, BIOS overrides and two
character-selection cycles; all peers measured 1.000 updates/video frame in
its speed window. The stock build retained its original SHA-1 and all 35 CTest
targets passed with Qt's offscreen backend.

Grand Prix supports one through eight humans, filling the grid to eight with
unique CPU characters. Select in the disconnected lobby starts solo GP.
Native GP initialization assumes one human and initially gave linked humans
duplicate ranks; its rank-pointer setup then wrote into adjacent score memory.
The ring variant now establishes the CPU roster and unique grid ranks before
native AI setup. Four races award 9/6/3/1 points, cumulative standings choose the
next grid, and A advances to the next course or back to cups after the final
race. Pause/restart keeps cup progress; choosing a new cup resets the scores.

The runner adds result-screen A pulses and refreshes its route on each course,
checking the header and checkpoint count as well as the data pointer.
It does not steer during loading/countdown. Earlier long-cup attempts used a
fixture that steered during initialization; those attempts are not passing
validation evidence. A later eight-player attempt retained 31 cached route
entries on Donut Plains even though its native course has 37 checkpoints;
several scripted drivers then circled off-road. Checking the header and count
restores all 37 entries and native lap progression on every peer. That failed
attempt is also diagnostic evidence, not a completed cup. The permanent GP fixtures check first-race points,
next-course loading, and all four courses with a 76-point cup total.

Final real-BIOS entry/control coverage passes all 21 Vs./Battle/GP cases at
2–8 humans. GP first-race points and next-course loading pass at 1/2/8 humans,
with common positions for all eight racers. A complete two-human/six-CPU Retro
cup passes all four races and common 76-point totals. Complete Vs. and Battle
rounds pass at two and eight humans. Loss/reconnect, cancellation, pause/resume
and restart pass at both counts. Rendering passes 2,400 unchanged menu frames,
700 consecutive views with eight karts/seven markers, intact horizon/minimap,
and 5,600 valid Battle sprite frames. The survivor regression eliminates players 1–4 and verifies players 5–8 occupy
all four portrait rows on every console. The disconnected lobby clears its full
1,584-byte PCM buffer, and solo engine-class Back returns to the title.
The current normal ROM SHA-256 is
`355579d823bfbf11708b0d668a173e39d8acbd7981c701c439799fcf757e91db`;
older hashes below describe earlier builds.

## Follow-up: race deadlines and scripted sessions (2026-10-12)

A short stationary FPS test did not cover the user's moving-race slowdown.
A busy eight-player section measured 0.766 native updates/video frame; the
complete shared-music race measured about 0.935. Completed drawing just after
VBlank was waiting an extra frame. The variant now publishes finished buffers
through VCOUNT 190, only after the native physics loop begins. An elapsed-video
tick budget allows catch-up without overclocking or advancing the whole race
faster than video. The busy section improved to 0.983; the final full moving
race, including item use and the leader's final lap, measured 0.992435 on all
peers. The sky callback rearms every video IRQ even when bulk publication waits.

Applying early publication to menus/loading caused a frozen-menu discrepancy
and a two-player Battle start failure. Restricting the path to initialized
physics restored all 14 2–8-player entry/control tests and frozen-menu/rendering
checks. The protocol's fixed diagnostic ABI now uses a dedicated .ring_state
section at 0x03006B40, independent of music-option local/static variables.

The native cell renderer is retained outside ring sessions. The stock 3×3
DK-to-Wario selector was driven into Wario GP with item/voice callbacks, and
its image matches the unmodified ROM byte-for-byte. Qt keyboard events also
cycle through the ring selector twice before starting an eight-player match.

The builder adds --boot-ring and mutually exclusive --no-music/--shared-music
policies. Shared music ignores individual power-ups and follows the leading
racer's final-lap flag, with the normal next-video-frame audio latency. A common
victory jingle preserves a session-wide result theme. Effects/voices remain
available when music is disabled. Retro names and the Select hint are checked
from published menu pixels on both cup and track screens.

Battle additions use symmetric offsets perpendicular to each marker's facing:
original slots move left and new slots move right. Both candidate tiles are
checked against void/lava; the distance shrinks symmetrically if needed. All
four arena grids pass midpoint, heading, separation, and eight-peer equality
checks against native four-player grids.

The Qt emulator adds --ring-players N ROM, --instance-settings DIR, --hide-menu,
and P1–P8 window suffixes. BIOS/configuration overrides propagate to independent
profiles; settings directories remain unique when a middle window is replaced.
SDL preference loading updates only its player's device slot. LCD reset epochs
align to a shared frame boundary. Audio fallback startup now starts its timer;
otherwise additional consoles could stall waiting on undrained audio buffers.

Final normal ROM SHA-256 is
`5b0105543a750026bfb303563899fc57ec5efbae15f84cc59d8bd832d917e72f`.
Complete real-BIOS Vs./Battle rounds pass at two and eight players. The full Qt
suite passes 12 results without skips, including original four-player modes,
ring modes, staggered startup, wire loss, and core lifecycle failures. A separate
scripted eight-window launch passes BIOS-argument propagation, two character
selection cycles, independent persisted bindings, hidden menus/Alt, and profile
reuse after closing a middle window. Both GUI race modes and the scripted race
measure 1.000 native updates/video frame in their 600-frame windows. All 35
CTest targets pass, and the unmodified build still matches its original hash.
After these real-BIOS checks, optional HLE passes complete Vs./Battle rounds at
two and eight players and the DK-to-Wario single-player GP regression. The
runner confirms HLE was selected and that no external BIOS was loaded.

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
barrier consumes multiple video frames; ring races use the committed simulation
round for that parity while retaining the native texture update budget. Both assembly hooks are conditional on `MKSC_RING`.
The ordinary build still matches SHA-1 `9d327c030c3e2d9007990518594f70c3340ac56f`.

The lobby/results/error bitmap renderer draws to the inactive Mode 4 page and
flips at VBlank only after the complete menu is ready. Palette uploads and
native audio service remain active. `test_rendering.py` checks every completed
frame: 2,400 identical cached-menu frames without redundant redraws, all eight karts and
seven identity markers in 700 consecutive rear-view frames, distinct texture
sources matching native VRAM banks, and 5,600 valid Battle sprite frames.

The transport now uses standard 2 MHz normal-32 serial with the existing
512-cycle forwarding turnaround driven by timer 2. External-clock consoles sleep in HALT
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

## 2026-10-12 corrections and revalidation

Current ROM SHA-256:
`86b490a49bc735fefd9277cac377607c32e6fc4c080e622f6c1c98536f497a62`.
The earlier `23a83a...` build's tests did not measure native game updates per
video frame. Its apparent emulator FPS concealed roughly half-speed gameplay.

The menu now services native audio once per video frame and caches unchanged
pages. Character names use menu indices, separately from native character IDs;
Yoshi/DK and Wario/Bowser match their portraits. The native four-row race HUD
selects portraits by current position, using the original GP renderer. Ring
markers no longer write sky palette bank 9 or erase minimap background tiles.
Both floating balloons and HUD pips use the owning player's color. Identical
balloon transforms share affine matrices, leaving capacity for other objects.

The owner clocks normal-32 transfers from timer 2 while the main thread mixes
audio. Critical IRQ code and the exact ARM HDMA kernel run from IWRAM, with a
linker assertion protecting the system stack reserve. Sprite division, default
unrotated sprites, and single scaled cells have exact arithmetic shortcuts.
The extended controller mapping now matches native Up/Down and L-trigger bits.
Native texture budgeting uses simulation rounds rather than video-frame parity.

An intact link can miss an earlier READY during graphics DMA. A peer now holds
COMMIT until it has the complete vector. The owner replays the collected inputs
and COMMIT after an overdue return; completed peers forward replays without
applying them again. The emulator applies every receiver's wire-completion
event before raising the owner's IRQ, preventing scheduler-induced overlap.
Core shutdown and GUI interruption also wait until a borrowed callback finishes,
including callbacks that crash or request shutdown. The user's Windows
`_ring_write()` fix remains included.

Real-BIOS validation of this ROM passed:

- All fourteen 2–8-player Vs./Battle entry and independent-control cases.
- Complete Vs. and Battle rounds at two and eight players, with common rankings,
  positions, and wins; two consecutive Battle rounds at both counts.
- Cancellation, pause/resume, restart, and wire-loss reconnect at both counts.
- 2,400 cached-menu frames, 700 consecutive frames with all eight Vs. karts and
  seven markers, intact horizon palette/minimap/four-row HUD, distinct textures
  matching VRAM, and 5,600 valid Battle frames. Every local player retained all
  three correctly paletted floating balloons.
- Original title labels and 200 menu updates per 200 video frames; complete
  1,584-byte lobby PCM clearing; ordinary GP with ten character-voice callbacks.
- 393,216 homebrew scanlines comparing the actual ARM kernel against native
  projection arithmetic, including 32-bit wrapping and both HDMA buffers.
- Original four-player stock-ROM Vs./Battle and the 2–8-player homebrew transport,
  including a nonzero host and bounded wire-loss recovery.

The speed test now checks the native simulation counter as well as protocol
commits, using 600 video frames on each console. All cases had zero collisions,
CRC errors, or timeouts:

| Players | Vs. updates/video frame | Battle updates/video frame |
| --- | --- | --- |
| 5 | 1.000 | 1.000 |
| 6 | 1.000 | 1.000 |
| 7 | 1.000 | 1.000 |
| 8 | 1.000 | 0.992 |

The emulator passed 35 CTest targets, all 12 private GUI test results without
skips, and synchronized eight-window Vs. entry/lifecycle. GUI coverage includes
standard four-window Vs./Battle, ring two/eight-window Vs./Battle, a start-time
wire loss, staggered startup, pause/reset/removal/reopen, and borrowed-core
crash/shutdown. These tests use the supplied BIOS. Pure arithmetic checks also
passed 536 million sprite quotients plus fallback boundaries, and all
1,048,576 keypad/trigger combinations against the native mapping.

After the real-BIOS checks, HLE passed two/eight-player entry and complete rounds
in both modes, plus ordinary GP character-voice callbacks. It remains an explicit
optional runner mode; real BIOS stays the primary validation fixture. The
unmodified build still matches SHA-1
`9d327c030c3e2d9007990518594f70c3340ac56f`.

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
