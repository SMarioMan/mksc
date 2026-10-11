# MKSC ring variant

Use the private cloud toolchain with `source /workspace/.setup/activate.sh`,
then run `python3 tools/ring/build.py` from the MKSC checkout. This creates
`mksc-ring.gba`, its ELF, and symbols in separate build directories. `make`
still builds and hash-checks the byte-identical original ROM. The uploaded ROM
and BIOS are local, ignored inputs; neither is redistributed.

The title link submenu adds **RING LINK** using lettering sampled from the
original title sprites and their selected/unselected palettes. Connect 2–8
consoles, enter Ring Link on each, and press Start on one console. That console
owns the clock and menus. Choose Vs./Battle, engine class (Vs.), individual
characters, and a track or arena. Eight controllers, character slots, and
converted driver input words have separate storage; unused slots stay released.

The normal-32 transport assigns software IDs around the ring. CRC-protected
READY, FRAME, INPUT, and COMMIT packets form a deterministic input barrier;
physics advances only after everyone receives the same input vector. Serial-completion IRQs keep receivers armed while rendering and mixing audio.
Timer 2 supplies forwarding turnaround, preserving audio timers 0/1 and native serial timer 3.
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

Player identity badges, map markers, and Battle balloon pips use red, green,
yellow, pink, blue, orange, cyan, and purple. Portraits use native assets. Menus
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

`test_rounds.py` additionally needs `--elf`; it verifies full rounds and common
rankings/wins with actual driving and item inputs. `test_flows.py` checks native
pause/restart, cancellation, and wire-loss reconnect. `test_repeat.py` needs the
matching round-test directory as `--baseline`, and verifies cumulative results
for a second Battle round. All symbol-based scripts accept `--nm`.
