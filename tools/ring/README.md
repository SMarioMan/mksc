# MKSC ring variant

Use the private cloud toolchain with `source /workspace/.setup/activate.sh`,
then run `python3 tools/ring/build.py` from the MKSC checkout. This creates
`mksc-ring.gba`, its ELF, and symbols in separate build directories. `make`
still builds and hash-checks the byte-identical original ROM. The uploaded ROM
and BIOS are local, ignored inputs; neither is redistributed.

The title link submenu adds **RING LINK** using lettering sampled from the
original title sprites and their selected/unselected palettes. Connect 2–8
consoles, enter Ring Link on each, and press Start on one console. That console
owns the clock and menus. Choose Vs., Battle, or Grand Prix, engine class (races), individual
characters, and a track or arena. Eight controllers, character slots, and
converted driver input words have separate storage; unused slots stay released.
Grand Prix supports 2–8 humans, fills the remaining grid slots with CPUs, and
runs four-course cups with native 9/6/3/1 points and the native three Super /
five Retro laps. Awards use the original points table; cumulative standings
use the original eight-racer builder and its tied ranks and human/CPU rules.
Ring Link supports 2–8 racers on 2–16 connected consoles. In GP, choose a cup rather than a single
course; A advances results to the next race, then returns to cup selection.

The normal-32 transport uses the standard 2 MHz serial clock and assigns
software IDs around the ring. External-clock receivers HALT between interrupts
while preserving VBlank audio service. CRC-protected
READY packets carry the sampled inputs; a following COMMIT forms a deterministic input barrier;
Map consoles acknowledge their readiness without claiming a racer input slot.
The host waits for all racer inputs and all map acknowledgements before
issuing COMMIT; physics advances only after everyone receives the same vector. Serial-completion IRQs keep receivers armed while rendering and mixing audio. Critical transport code runs from IWRAM. A peer that missed an earlier READY holds COMMIT until a replay restores its complete input vector.
Timer 2 supplies a 512-cycle forwarding turnaround, preserving audio timers 0/1 and native serial timer 3.
Once a READY has returned around the ring its sender stops repeating it. At
five or more players the owner spaces idle polling by 2048 cycles while waiting
for rendering; active forwarding and protected input trains retain 512 cycles.
Failures stop the game and show a connection error. Re-enter Ring Link to start
a fresh session. The stock Multi-Pak/Single-Pak paths remain separate.

Use `mgba-link` with the real uploaded BIOS for tests. `test_game.py` exercises
menu/gameplay entry and independent controls for every count from 2 through 8
in both modes. Its default snapshot is before physics at a common input barrier.
`--arena 0..3` selects a Battle arena. `test_lobby.py` checks the stock link-menu
song and continuously changing PCM while waiting for discovery. It also needs
the matching `--elf`. `test_singleplayer.py` exercises item use
and character sound scripts in ordinary GP, checking that shifted callbacks
return safely. Each script needs `--runner`, `--rom`, `--bios`, and `--results`;
the single-player script additionally needs the matching `--elf` and optionally
`--nm`. Tests copy the ROM to an immutable content-addressed path.

Player identity diamonds, sprite map markers, and both floating Battle balloons and HUD pips use red, green,
yellow, pink, blue, orange, cyan, and purple. Portraits use native assets. The race HUD keeps the original four portrait rows and native rank selection.
Battle shows the four players with the most remaining balloons, with stable
player-order ties. Floating balloons retain native highlights; the reserved
identity palettes are restored after course animation and graphics DMA so
those highlights do not become white/gray while moving. Course palettes and minimap background tiles remain owned by the original renderer. Mode/class, character and course selection share the native menu state,
graphics and page implementations. An eight-player input coordinator supplies
independent character picks without extending the native four-slot widgets.
Results reuse the native eight-row graphics/renderer; GP standings and tied
ranks use the original builder. Menu accessors and positional volume queries
are matching C, and the clean build remains hash-identical.
The previous release passed optional HLE entry and complete rounds at two and
eight players, plus the ordinary GP character-voice regression. The pending
release must repeat its BIOS checks before HLE is tested again.
Add `--hle` to `test_game.py`,
`test_rounds.py`, or `test_singleplayer.py` to select it explicitly. Their BIOS
argument still identifies the supplied validation fixture; it is not loaded
in HLE mode. The complete 2–8-player matrix, recovery, and replay coverage use
the real BIOS.

This is an experimental playable build. See `docs/ring-investigation.md` for
verified coverage and outstanding acceptance criteria. No physical-cable test
has been performed. The intended stock AGB-005 arrangement puts each large plug
on a GBA and each small plug in the next cable's middle socket, closing the ring.

### Automatic full-screen map displays

Every ring build includes the map display. Load the same ROM on 2–16
consoles. The first eight consoles are racers; consoles 9–16 automatically
become silent full-screen maps. Press Start on one console to anchor the
software order. No map build flag, companion ROM, or emulator role setting
is required. Grand Prix still fills unused racer slots with CPUs when there
are fewer than eight consoles.

```sh
mGBA --ring-players 16 --instance-settings ring-settings --bios gba.bin mksc-ring.gba
```

The map uses the native whole-course overview, live colored player markers,
and identical synchronized race state. Every map console forwards ring
traffic without contributing another input slot. Window titles/settings
remain P1 through P16, independently of the role assigned by the game.

`test_map_display.py --runner ... --rom ... --elf ... --bios ... --results ...`
sweeps totals 9–16 in Vs., Battle, and GP with the same image everywhere.
Use `--host N` (zero-based physical console) to check discovery with a
different host; role IDs rotate from that console. Release manifests identify
all four ordinary variants with `automatic_maps: true` and `ring_consoles: 16`.
Old map-only images from earlier releases are obsolete and are never emitted
by the current builder.
It checks every map's geometry, marker alignment, silence, physics agreement,
and simulation speed. `test_rounds.py` and `test_gp.py` accept
`--players 8 --consoles 9..16` to check results and GP advancement with maps.


The modified mGBA Qt GUI supports **File → GBA link topology... → Ring link
(up to 16 players)**, followed by New multiplayer window and loading this ROM
in every window. See `mgba/tools/link/GUI.md` in the emulator repository.
The title label uses native font glyphs cached after title asset loading;
`test_title.py` checks visibility of all three labels and normal menu speed.

`test_rendering.py` verifies a cached, complete eight-player menu without redundant page redraws,
all eight karts and seven colored opponent markers in 700 consecutive stationary
Vs. frames, distinct native character textures matching their VRAM banks, intact minimap tiles and horizon palette, the native four-row HUD, and
5,600 valid Battle sprite frames with local balloons using each player’s palette. It needs `--runner`, `--rom`, `--bios`, and
`--results`, using a runner built with the video-log diagnostics. Its stationary
grid keeps all eight racers visible so ordinary movement cannot hide flicker.

`test_rounds.py` additionally needs `--elf`; it verifies full rounds and common
rankings/wins with actual driving and item inputs. `test_flows.py` checks native
pause/restart, cancellation, and wire-loss reconnect. `test_repeat.py` needs the
matching round-test directory as `--baseline`, and verifies cumulative results
for a second Battle round. All symbol-based scripts accept `--nm`.

`test_timing.py` checks actual committed game updates per completed video frame
at five through eight players in both modes. It reads the native simulation counter as well as committed input rounds and requires at least 0.95 updates
per video frame on every console, rather than relying on emulator FPS. Native
sprite rendering has exact shortcuts for unrotated/unit sprites and single
scaled sprites. `test_divide.py` exhaustively checks the actual quotient helper
against signed division, including fallback boundaries. Hidden balloons retain
animation and elimination updates while avoiding unused projection work.

`test_inputs.py` compares all keypad/trigger combinations against the native
controller bit layout. The ARM HDMA kernel uses native truncation and 32-bit
product wrapping; `test_camera.py` builds a separate homebrew and compares
393,216 scanlines against the native arithmetic using the supplied BIOS.
Balloons sharing the same transform reuse an affine matrix, preserving space
in the GBA's 32-matrix OAM budget for karts and course objects.
Ring sessions also expand the native sorted sprite queue from 32 to 128
entries. Its allocation belongs to the scene heap and is invalidated when
that heap resets. This lets all 24 Battle balloons reach the renderer while
retaining the hardware limit of 128 sprites.

## Build and launch options

```sh
python3 tools/ring/build.py                         # Normal title-menu boot/music
python3 tools/ring/build.py --boot-ring --shared-music
```

Each command writes `mksc-ring.gba`, `.elf`, and `.sym`; changing options
rebuilds the variant's objects automatically. The stock matching build and ROM
remain separate. `--music normal|shared` selects the audio policy. `--boot-ring` enters the connection menu directly and still waits for
Start on one console. It supports the same connection, Vs., Battle, and GP flow.

The connection screen plays the stock link-menu song. Before discovery, each
standalone console plays it locally; in shared mode, peers stop their BGM as
soon as their software ID is known. After connection, `--shared-music` plays
BGM only on software P1, the clock owner, while keeping each console's own engine and character voices. Remote engine/voice mixer players are
muted; spectator-camera voice requests are checked against the local character.
Other sound effects remain available on all consoles. Non-music effects play at one quarter of their normal volume, including menus. The host combines every racer's effect state using the stock priority: Star, Boo, then lightning. It derives final-lap tempo from all racers,
including GP CPUs. The first racer reaching
the final lap triggers the common jingle and fast course tempo. Audio reacts
on the next video frame. End-of-race jingles use the common victory theme.

The modified Qt emulator can launch all windows in one command:

```sh
mgba-qt --ring-players 8 --instance-settings ./ring-settings --hide-menu \
  --bios gba.bin mksc-ring.gba
```

Replace the executable with `mGBA.exe` on Windows. Open Settings in each window
to save independent bindings; Alt reveals the hidden windowed menu. Press Start
in P1 to host. Use the stock L/R prompts to switch between Super and Retro
courses. The Retro track screen displays course names. Battle also supports
L/R, selecting four restored SNES arenas alongside the four GBA arenas.

Battle uses the eight distinct spawn markers already authored in each arena.
Each kart faces inward toward the center of those markers. No custom paired
offsets are applied. `test_spawns.py` checks the exact authored coordinates,
terrain, heading and synchronization at every player count from two to eight
on all eight arenas. See `docs/ring-retro-battle.md` for restoration details
and reference-patch attribution.

Completed race graphics that miss the initial VBlank IRQ can publish during
the remaining blanking interval, with a deadline based on queued DMA bytes and
an eight-scanline callback/IRQ reserve. This applies only after
the native physics loop starts; menus/loading retain their VBlank barrier.
The tick budget tracks elapsed video frames, allowing missed ticks to catch up
without running the whole race faster than the video clock. The horizon
callback runs every video IRQ, including when bulk graphics publication waits.
The native cell renderer is retained outside ring sessions.

Additional regression tools: `test_course_menu.py` checks the published text
pixels, `test_music.py` checks the music policies through an entire moving race,
and `test_singleplayer.py --character 6` traverses DK to Wario in the native
3×3 selector, then exercises GP and voice callbacks.

`test_battle_moving.py` drives eight players on all four arenas and checks
actual simulation updates/video frame (at least 0.95), ranked portraits,
published balloon colors and eight aligned minimap dots. It accepts
`--boot-ring` for that build option and `--arenas 0..3` to select arenas.
`test_battle_survivors.py` eliminates
players 1–4 and checks that players 5–8 occupy every console's Battle HUD. It
also checks a mixed-count snapshot where a living low-index racer has fewer
balloons than players 5–8, so merely skipping eliminated racers cannot pass.
`test_transition.py` exercises Rainbow Road → pause/change track → Back →
Battle, guarding against carried course indexes and double-consumed Back.
`test_gp.py` checks first-race points and next-course loading at every count
from two through eight humans. `test_gp_cup.py` completes a four-course Retro cup, checking
synchronized human/CPU physics, five laps per Retro race, and a total of 76
awarded points. Its optional `--autofire` also uses items. Both GP
scripts need the matching `--elf`. `test_target.py` compares the Battle target
response C implementation to the actual native Thumb code in 16,384 fixtures, including its real camera-facing query. Entire
416-byte driver records are compared after each update, rather than just
comparing calls or animation fields.

Music regressions accept `--players 2..8`, `--gp` and `--retro`; the GP/Retro
case checks that final-lap BGM begins on lap five and remains host-only while
six CPUs and two humans race.

The GP cup fixture writes `*-progress.csv` during the run. Its steering revisits
the current route point when a corner-cut leaves a car blocked, using only
keypad inputs. The GP finish-name updater repairs invalid stock menu records
at rendering time so late finishers can continue racing without corrupting
scene memory.

`test_start_music.py` covers the opening fanfare before course BGM in eight-player
Vs./Battle/GP (including a host in another physical window). Shared music waits
for the native countdown to request the course song, preserves common final-lap/result jingles,
and chooses a common Star or lightning theme from every racer's effect state.
Peers retain their own engine and voice sounds. Common menu sounds play on
the host; shared-build effects use half their original volume. The repeating low-coin
warning remains at quarter volume.

`test_battle_menu.py` compares the published SNES preview graphics and palette
with the supplied reference patch, then checks a round trip back to the GBA
page. `test_threaded_video.py` requires exact CPU/game/transport snapshots,
framebuffers and PCM recordings with threaded rendering enabled and disabled.
`verify_release.py` runs host-clock SDL and Qt audio checks after its CPU-heavy
checks have finished. Those checks need a working audio server and fail on
any underrun during the five-minute moving measurement. HLE runs last.
MKSC fixtures use a 2,048-cycle normal-32 cable quantum, matching the GUI's
transfer limit. The generic protocol runner retains its conservative default
for other ROMs and serial modes. `test_shared_themes.py` compares 349,504
effect-mask combinations with the original Thumb music-request handler,
including inactive slots and native Star/Boo/lightning priority.

For relocation checks, build the same configuration with and without
`--code-padding 0x400`, save immutable ROM/ELF copies, and run
`audit_relocation.py --rom ORIGINAL --elf ORIGINAL_ELF --shifted-rom SHIFTED
--shifted-elf SHIFTED_ELF`. Follow this static callback check with the real-BIOS
`test_singleplayer.py` voice/item regression and the relevant ring game tests.
This flag only pads the ROM for validation; normal builds use no added padding.

The ring mixer keeps the native 10,512 Hz sample rate and six-channel budget.
It snapshots its PCM destination before the music callbacks. Shared audio
prepares one extra frame ahead (about 16.7 ms additional output latency);
normal builds prepare two extra frames ahead (about 33.5 ms),
allowing for their retained remote engine and voice mixing. Delayed segments
are recovered in playback order before their deadlines. Frame/counter reads are
atomic, and diagnostics retain both missed services and recovered/late
segments. The native RAM mixing kernel remains unchanged. `test_audio_queue.py`
executes the actual ROM mixer entry across 440 counter/override/IRQ fixtures;
use `--ahead 2` for shared audio and `--ahead 3` for normal audio.
`test_audio.py --gp --frames 90000` records a complete eight-player Retro cup,
checks local voice ownership, remote-player suppression and PCM deadlines,
and retains eight per-instance WAV files.

`test_all_courses.py` profiles eight moving racers with items on all 20 Super
courses, 20 Retro courses and all eight GBA/SNES Battle arenas. `--complete` runs races
through the native result transition. Reports preserve instruction-cycle
profiles, video-by-video simulation/audio timing and screenshots. Profiles
record individual ARM/Thumb instruction addresses, accounting for the CPU's
prefetch, rather than attributing whole address ranges to nearby functions.
The runner observes instructions inside its normal scheduling quantum.
`test_profile.py` compares complete eight-player snapshots and every logged
video frame with profiling enabled/disabled; both must be byte-identical.
Performance captures require `profileScheduling: "quantum"`; `--resume`
recaptures results made by the older stepping profiler.
Use `--audio-policy normal` when checking those builds;
only shared audio requires remote engine/voice channels to remain silent. Performance
checks require both a mean and every measured 300-frame window to reach 95%
of the video rate; emulator FPS alone is insufficient.
`--resume` rechecks already passing captures from the same ROM and runs the
remaining cases; `--analyze-only` rechecks captures without running emulation.
`--profile-from N --profile-frames N` targets the instruction profile at a
slow interval identified in the video log. Every overlapping 300-frame window
is checked, including intervals crossing the old sampling boundaries.
Reanalysis requires the captured ROM, emulator/core bytes, music policy and
profile interval; moving identical binaries to another directory is allowed.
`--profile-player 0..7` profiles a particular console's camera rather than
always the host. Use `timing_by_player` in the course report to identify the
slow participant. The selected console is part of capture provenance too;
resume/reanalysis cannot relabel a host instruction profile as a peer profile.
Audio recordings also accept `--audio-policy` and `--boot-ring` for testing
each variant with the appropriate inputs and sound ownership checks.

`test_rounds.py --modes vs --engine 1` completes 100cc races and records the
actual route-following keypad inputs. Point `test_results_back.py --baseline`
at those captures to replay results -> course preview -> course grid ->
character selection -> Back. It checks that native Back completes, menu
mixing/PCM continue, and all players can select characters again and start a
150cc race. Both tools accept `--players 2 8` (or any other counts from 2–8).

Ring builds compile the pure target calculations for ARM, and the collision code/
renderer for Thumb, with explicit wrapping arithmetic and interworking. The
projection and native positional sound/track controls also use optimized
Thumb C. Projection preserves every native product/truncation and checks all
signed quotient boundaries; OAM tests compare all 1024 bytes and affine state
with the original renderer. Mixer controls retain the native track state and
identification lock while stopping once no selected tracks remain. The
matching build still uses agbcc. Collision regression compares the exact
matching Thumb object with the optimized Thumb object, including callback
ordering, mutations seen by subsequent collisions and the full 24/64-list
limits. The original stable OAM sort and exact reciprocal division helper run
from IWRAM, along with the exact first-minimum checkpoint scan. Its caller
must remain out of line (`test_hot_code.py` checks the actual compiled call).
Checkpoint index filling snapshots one actor's checkpoint before writing
the separate index array. Cold menu/session records
are above the native EWRAM scratch area. Symbolic loader addresses and linker
assertions protect the native globals and system-stack reserve.

## Reproducible release verification

The release helpers retain immutable ROM/ELF/symbol variants and run the existing
regressions in four queues. Source the prepared ARM/Qt environment first:

```sh
python3 tools/ring/build_release.py --output /path/to/private-builds
python3 tools/ring/verify_release.py \
  --runner /path/to/mgba-link --bios /path/to/gba.bin \
  --builds /path/to/private-builds --results /path/to/private-results \
  --mgba-build /path/to/mgba-build --mgba-source /path/to/mgba
```

This is an extensive release check, rather than a quick development smoke test.
It covers all 48 courses in normal/shared audio, all 2–8-player game entries
and complete Vs./Battle rounds, native menu/rendering/recovery regressions,
extended audio, full GP cups, private GUI rows, native-code arithmetic oracles,
and controlled ROM relocation. HLE starts only after every BIOS queue passes.
A successful run writes `verification-complete.json`; partial runs retain their
logs but do not receive that completion marker. Results and builds contain
private ROM copies and must remain outside version control. The BIOS/base ROM
are never part of a distributable release.

Replacement menu art specifications and the private-reference export command are in [ring-menu-art.md](../../docs/ring-menu-art.md).

Sunset Wilds art is optional (`--sunset-art japanese`; default `original`).
The supplied higher-quality audio is optional through
`--music-restoration-ips PATH`. See [the audio import and validation notes](../../docs/ring-restored-audio.md).

## Watch a regression on your desktop

Configure modified mGBA with `-DBUILD_QT=ON -DBUILD_SUITE=ON` and the Qt
Test/Multimedia development modules, then build the GUI test target
(`cmake --build BUILD --target test-qt-ring_gui`).
Then, from this checkout:

```sh
python3 tools/ring/build.py --shared-music
python3 tools/ring/review_gui.py --gui-test ../mgba/BUILD/qt/test-qt-ring_gui \
  --rom mksc-ring.gba --bios /path/to/gba.bin \
  --mode battle --players 8 --seconds 120 --log visual-battle.log
```

On Windows use the target's `test-qt-ring_gui.exe` path. Python 3 is required.
Use a normal-boot ring ROM, not a `--boot-ring` build. The real BIOS is required.
The launcher exports the **same** input fixtures as `test_game.py`, opens and tiles
2–8 actual Qt windows in player order, and enables real-time synchronization and
Qt audio. Modes are `vs`, `battle`, and `gp`. It watches 35–300 seconds of moving
gameplay after BIOS boot and menu entry, then performs the regression's ordinary
state/audio/lifecycle checks. A nonzero exit code remains a test failure. Window
settings and save outputs are temporary; your normal GUI settings are preserved.

The default window display is software. Add `--display opengl` to review the
OpenGL window path; add `--hardware-renderer` for hardware GBA rendering too
(requires a compatible emulator build). `--threaded-video 0` or `1` overrides
the native software rendering worker for comparisons such as the Windows
startup regression. The fixture verifies requested backends are actually active.

Use the normal Pause command to pause/resume all consoles for inspection.
Ctrl+C stops the review. Keep other CPU-heavy test/build jobs stopped while
judging sound or wall-clock performance. The test reports native updates per
video frame, wall-clock FPS, audio underruns, and dropped PCM samples.

To inspect an existing scenario, add `--fixture /path/to/battle-8.csv` and select
its expected final mode/player count. Input rows are `frame,player,hex-key-mask`,
with zero-based players and the native GBA keypad bits. Measurement waits until
after the complete CSV. Only recorded keypad actions replay: headless debugger
memory edits, cable-fault injection, and dynamically generated autodriving are
not encoded in these CSVs. Headless committed-input barriers and GUI scheduling
can also differ; retain the headless result for strict synchronization checks.
The visual run is an additional graphics/sound review, not a replacement for it.

Fixture export without running an emulator is also available:

```sh
python3 tools/ring/test_game.py --inputs-only --results visual-inputs \
  --players 2 3 4 5 6 7 8 --modes vs battle gp
```

## Complete Grand Prix cups and results graphics

The GP regression can drive every course of a Retro cup, including automatic
map consoles. For the reported Retro Special Cup case:

```sh
python3 tools/ring/test_gp_cup.py --runner ../mgba/BUILD/mgba-link \
  --rom mksc-ring.gba --elf mksc-ring.elf --bios /path/to/gba.bin \
  --results gp-special --players 8 --consoles 9 --cup 4 --engine 1 \
  --bowser-p4 --autofire --video-log --results-frames 900
```

Engine values are 0=50cc, 1=100cc, 2=150cc. `--bowser-p4` exchanges the
initial P4/Toad and P8/Bowser selections using native keypad navigation. The
test verifies the resulting character IDs, scores, sorted standings, common
physics, and five laps per Retro course. Graphics checks compare all eight
portrait textures with the ROM and their opaque pixels with the displayed
image. Each map must remain visible and stable through racing and results;
all four courses must use different map geometry. Screenshots are saved on
every results entry, alongside a frame log and a graphics report.

`--races 1` stops at the first results. `--results-frames` observes the final
results without confirming, and `--analyze-only` rechecks an existing recording
without running emulation. Reanalysis requires the same ROM and menu options.
Use a fresh directory for a new run; existing recordings are protected.

The `*-recorded.csv` file contains actual keypad actions, including adaptive
driving, and can be passed to `review_gui.py --fixture` with `--mode gp` to
review the race visibly. Course-by-course screenshots and strict graphics
assertions belong to the headless run; the desktop review remains an additional
sound/graphics check.
