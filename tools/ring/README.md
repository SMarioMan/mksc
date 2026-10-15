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
Grand Prix supports 1–8 humans, fills the remaining grid slots with CPUs, and
runs four-course cups with native 9/6/3/1 points and the native three Super /
five Retro laps. Awards use the original points table; cumulative standings
use the original eight-racer builder and its tied ranks and human/CPU rules.
Select in the disconnected
Ring Link lobby starts solo GP. In linked GP, choose a cup rather than a single
course; A advances results to the next race, then returns to cup selection.

The normal-32 transport uses the standard 2 MHz serial clock and assigns
software IDs around the ring. External-clock receivers HALT between interrupts
while preserving VBlank audio service. CRC-protected
READY packets carry the sampled inputs; a following COMMIT forms a deterministic input barrier;
physics advances only after everyone receives the same input vector. Serial-completion IRQs keep receivers armed while rendering and mixing audio. Critical transport code runs from IWRAM. A peer that missed an earlier READY holds COMMIT until a replay restores its complete input vector.
Timer 2 supplies a 512-cycle forwarding turnaround, preserving audio timers 0/1 and native serial timer 3.
Once a READY has returned around the ring its sender stops repeating it. At
five or more players the owner spaces idle polling by 2048 cycles while waiting
for rendering; active forwarding and protected input trains retain 512 cycles.
Failures stop the game and show a connection error. Re-enter Ring Link to start
a fresh session. The stock Multi-Pak/Single-Pak paths remain separate.

Use `mgba-link` with the real uploaded BIOS for tests. `test_game.py` exercises
menu/gameplay entry and independent controls for every count from 2 through 8
in both modes. Its default snapshot is before physics at a common input barrier.
`--arena 0..3` selects a Battle arena. `test_lobby.py` checks the complete PCM
buffer while waiting for discovery. `test_singleplayer.py` exercises item use
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
After the BIOS-backed matrix passed, optional
HLE passed two- and eight-player entry and complete rounds in both modes, plus
the ordinary GP character-voice regression. Add `--hle` to `test_game.py`,
`test_rounds.py`, or `test_singleplayer.py` to select it explicitly. Their BIOS
argument still identifies the supplied validation fixture; it is not loaded
in HLE mode. The complete 2–8-player matrix, recovery, and replay coverage use
the real BIOS.

This is an experimental playable build. See `docs/ring-investigation.md` for
verified coverage and outstanding acceptance criteria. No physical-cable test
has been performed. The intended stock AGB-005 arrangement puts each large plug
on a GBA and each small plug in the next cable's middle socket, closing the ring.

The modified mGBA Qt GUI supports **File → GBA link topology... → Ring link
(up to 8 players)**, followed by New multiplayer window and loading this ROM
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

## Build and launch options

```sh
python3 tools/ring/build.py                         # Normal title-menu boot/music
python3 tools/ring/build.py --boot-ring --shared-music
python3 tools/ring/build.py --boot-ring --no-music
```

Each command writes `mksc-ring.gba`, `.elf`, and `.sym`; changing options
rebuilds the variant's objects automatically. The stock matching build and ROM
remain separate. `--music normal|off|shared` is an alternative to the two music
flags. `--boot-ring` enters the connection menu directly and still waits for
Start on one console. It supports the same connection, Vs., Battle, and GP flow.

`--no-music` disables player-zero BGM and jingles, retaining effects and voices.
`--shared-music` plays BGM only on software P1, the clock owner, while keeping each console's own engine and character voices. Remote engine/voice mixer players are
muted; spectator-camera voice requests are checked against the local character.
Other sound effects remain available on all consoles. It retains course/Battle music, suppresses
individual power-up music, and derives final-lap tempo from all racers,
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
in P1 to host. Select switches between Super and Retro on both the cup and track
screens, and the Retro track screen displays course names.

Battle slots 5–8 pair with slots 1–4. Each pair moves symmetrically left/right
of the native marker's heading; candidate distances shrink together if either
side would land on a void/lava tile. The native marker remains the midpoint.
`test_spawns.py` checks the midpoint, heading, right-hand side, and synchronization
on all four arenas.

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
`test_gp.py` checks first-race points and next-course loading at one, two, and
eight humans. `test_gp_cup.py` completes a four-course Retro cup, checking
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
for the native opening song to end, preserves common final-lap/result jingles,
and ignores local power-up themes. Peers retain sound effects and voices.

For relocation checks, build the same configuration with and without
`--code-padding 0x400`, save immutable ROM/ELF copies, and run
`audit_relocation.py --rom ORIGINAL --elf ORIGINAL_ELF --shifted-rom SHIFTED
--shifted-elf SHIFTED_ELF`. Follow this static callback check with the real-BIOS
`test_singleplayer.py` voice/item regression and the relevant ring game tests.
This flag only pads the ROM for validation; normal builds use no added padding.

The ring mixer keeps the native 10,512 Hz sample rate and six-channel budget.
It snapshots its PCM destination before the music callbacks. Shared audio
prepares one extra frame ahead (about 16.7 ms additional output latency);
normal and silent builds prepare two extra frames ahead (about 33.5 ms),
allowing for their retained remote engine and voice mixing. Delayed segments
are recovered in playback order before their deadlines. Frame/counter reads are
atomic, and diagnostics retain both missed services and recovered/late
segments. The native RAM mixing kernel remains unchanged. `test_audio_queue.py`
executes the actual ROM mixer entry across 440 counter/override/IRQ fixtures;
use `--ahead 2` for shared audio and `--ahead 3` for normal/silent audio.
`test_audio.py --gp --frames 90000` records a complete eight-player Retro cup,
checks local voice ownership, remote-player suppression and PCM deadlines,
and retains eight per-instance WAV files.

`test_all_courses.py` profiles eight moving racers with items on all 20 Super
courses, 20 Retro courses and four Battle arenas. `--complete` runs races
through the native result transition. Reports preserve instruction-cycle
profiles, video-by-video simulation/audio timing and screenshots. Profiles
record individual ARM/Thumb instruction addresses, accounting for the CPU's
prefetch, rather than attributing whole address ranges to nearby functions.
The runner observes instructions inside its normal scheduling quantum.
`test_profile.py` compares complete eight-player snapshots and every logged
video frame with profiling enabled/disabled; both must be byte-identical.
Performance captures require `profileScheduling: "quantum"`; `--resume`
recaptures results made by the older stepping profiler.
Use `--audio-policy normal` or `--audio-policy off` when checking those builds;
only shared audio requires remote engine/voice channels to remain silent. Performance
checks require both a mean and every measured 300-frame window to reach 95%
of the video rate; emulator FPS alone is insufficient.
`--resume` rechecks already passing captures from the same ROM and runs the
remaining cases; `--analyze-only` rechecks captures without running emulation.
Audio recordings also accept `--audio-policy` and `--boot-ring` for testing
each variant with the appropriate inputs and sound ownership checks.

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
It covers all 44 courses in normal/shared audio, all 2–8-player game entries
and complete Vs./Battle rounds, native menu/rendering/recovery regressions,
extended audio, full GP cups, private GUI rows, native-code arithmetic oracles,
and controlled ROM relocation. HLE starts only after every BIOS queue passes.
A successful run writes `verification-complete.json`; partial runs retain their
logs but do not receive that completion marker. Results and builds contain
private ROM copies and must remain outside version control. The BIOS/base ROM
are never part of a distributable release.
