# Ring multiplayer implementation status

The requested game mod is unfinished. The current stock ROM still matches
SHA-1 `9d327c030c3e2d9007990518594f70c3340ac56f`. Do not describe the homebrew or
emulator transport as eight-player Mario Kart support.

## Verified groundwork

- Built the decomp with pret/agbcc da598c1d918402c42c0c0d7128ba14567f3175e9.
- Converted the master/slave checks and header checksum to matching C in
  `src/sio2.c`; split the surrounding assembly to preserve placement.
- Named `Sio2State.playerCount`, `localKeys`, and `spm_chooseCharacter` without
  changing a ROM byte.
- Built mGBA with headless, Lua, debugger, and cmocka support; 30 CTest targets pass.
- Implemented an independent 2–8-node normal-serial ring and BIOS-backed runner.
- Tested software enumeration, distinct input propagation, nonzero host,
  sequence wrap, and fresh-session recovery after opening/restoring a wire.
- Connected the stock ROM using the stock mGBA lockstep driver through title,
  Multi-Pak Link, game selection, four-console character selection, and original
  Vs./Battle gameplay. `mgba/tools/link/test_stock.py` reproduces menu inputs and
  checks local IDs, membership, independent held keys, mode flags, and matching
  racer positions across four consoles. This baseline covers gameplay entry,
  not a complete race or all Battle mechanics.

## Sources inspected

- [GBATEK GBA serial documentation](https://github.com/mgba-emu/gbatek/blob/master/gba.md),
  especially normal-mode protocol, transfer rates, and one-way multiplayer chains.
- [Faceball2000DX enumeration fixes](https://github.com/Zarithya/Faceball2000DX/blob/master/src/16.asm).
  The receiver increments software player numbers and forwards discovery data.
- [DoubleCherryGB ring cable](https://github.com/TimOelrichs/doublecherryGB-libretro/blob/master/gb_core/linkcable/faceball2000_cable.cpp).
  Its Game Boy device forwards through secondary units. This is context, not
  an implementation copied into the GBA driver.

The supplied YouTube pages and Reddit thread returned HTTP 403 from the current
network proxy. Domain additions have been saved in the environment draft but
have not been applied/published. Their contents were not reviewed. Ghidra was
not installed; these matching functions were decompiled from the assembly and
validated with the pinned matching compiler and full ROM comparison.

## Game integration audit

Eight racer objects already exist (`RaceState.drivers[8]`), but that does not
establish eight human controllers or eight-player Battle support.

- `src/title.c` constructs two link submenu entries from `dword_80D8E67`, and
  selected options 1/4 route to `multilink_main`. Adding a third entry also
  requires new cell/label assets and enabled-state/dispatch handling.
- `asm/text08001B4C.s` contains `multilink_main` (stock 0x08013598), preliminary
  menu handshaking, `spm_main`, and `spm_chooseCharacter` (stock 0x08007138).
- Menu state has four-element character IDs at +0x4EC and selection states at
  +0x4FC. These must be restructured; changing loop bounds alone overwrites
  adjacent fields. The character-confirmation routine can consume only one
  simultaneous confirmation before leaving its loop; scripts must stagger
  selection inputs when establishing the stock baseline.
- `src/main.c` has four-element key-repeat counters and loops over four input
  buffers. Fixed locations are trigger keys 0x03004EA0, held keys 0x03004F30,
  repeat keys 0x03004F48, raw serial keys 0x03004F58. Eight raw serial keys would
  extend to 0x03004F68 and overwrite active serial state starting at 0x03004F60.
  Relocate and audit all references rather than increasing an array declaration.
- `asm/text0802D708.s` is the high-level serial state/handshake layer. The new
  tail file `asm/text0802DCF0.s` contains lower-level serial IRQ/stream queues.
  Preserve stock Multi-Pak and Single-Pak behavior as independent transports.
- `asm/play.s` gates race entry on link readiness and member count and chooses
  the local player driver. Audit mode distinctions and every racer-to-human
  mapping, synchronization barrier, disconnect path, results table, and Battle
  object allocation before enabling eight participants.
- The normal-serial PoC uses timers 0/1. These cannot simply be reused in the
  game: its audio and old serial infrastructure already own timing resources.
  Integrate serial IRQs and frame pacing with a measured game CPU budget.

The requested blue/orange/cyan/purple assets and the alleged shifted-code voice
bug remain unimplemented/unverified. Do not claim the bug is fixed or assume
that a successful stock hash establishes relocation behavior. A separate shifted
build and voice-channel/output trace are needed before game code expansion.

## Remaining acceptance criteria

1. Extend the stock four-player baseline through full races and Battle rounds,
   including results and disconnection behavior.
2. Matching C for the relevant menu/transport paths, with a hash-verified commit
   before functional changes.
3. A separate ring menu path, host/lobby/membership flow, integrated transport,
   frame barriers, connection-loss UX, and graceful cancellation.
4. Eight human input/state slots, game-mode limits, Vs./Battle logic, results,
   and all eight requested identity colors without overflowing existing storage.
5. BIOS-backed matrix of 2–8 players in both modes, varied joining/inputs,
   connection failure, replay determinism, and resource/timing bounds.
6. Real ring-cable validation. Emulator success does not demonstrate electrical
   compatibility of a cable assembled from stock accessories.
