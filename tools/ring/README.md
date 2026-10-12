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
runs four-course cups with native 9/6/3/1 points. Select in the disconnected
Ring Link lobby starts solo GP. In linked GP, choose a cup rather than a single
course; A advances results to the next race, then returns to cup selection.

The normal-32 transport uses the standard 2 MHz serial clock and assigns
software IDs around the ring. External-clock receivers HALT between interrupts
while preserving VBlank audio service. CRC-protected
READY packets carry the sampled inputs; a following COMMIT forms a deterministic input barrier;
physics advances only after everyone receives the same input vector. Serial-completion IRQs keep receivers armed while rendering and mixing audio. Critical transport code runs from IWRAM. A peer that missed an earlier READY holds COMMIT until a replay restores its complete input vector.
Timer 2 supplies a 512-cycle forwarding turnaround, preserving audio timers 0/1 and native serial timer 3.
Once a READY has returned around the ring its sender stops repeating it. At
five or more players the owner spaces idle polling by 1024 cycles while waiting
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
those highlights do not become white/gray while moving. Course palettes and minimap background tiles remain owned by the original renderer. Menus
currently use a simple bitmap layout; matching the stock Multi-Pak menu artwork
is a later visual improvement. After the BIOS-backed matrix passed, optional
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
`--shared-music` plays BGM only on software P1, the clock owner, while keeping
effects and voices on all consoles. It retains course/Battle music, suppresses
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
players 1–4 and checks that players 5–8 occupy every console's Battle HUD.
`test_transition.py` exercises Rainbow Road → pause/change track → Back →
Battle, guarding against carried course indexes and double-consumed Back.
`test_gp.py` checks first-race points and next-course loading at one, two, and
eight humans. `test_gp_cup.py` completes a four-course Retro cup, checking
synchronized human/CPU physics and a total of 76 awarded points. Both GP
scripts need the matching `--elf`. `test_target.py` compares the Battle target
response C implementation to the actual native Thumb code in 4096 fixtures.
