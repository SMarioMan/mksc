# GBA ring input exchange proof of concept

This is a standalone homebrew experiment, not the Mario Kart mod. The stock
Mario Kart build remains byte-identical. The extra menu option, game integration,
eight-human-player Vs./Battle logic, and new player colors are not implemented.

Build using the installed ARM toolchain:

```sh
source /workspace/.setup/activate.sh
make -C /workspace/mksc tools libraries
make -C /workspace/mksc/tools/ring-poc
```

The ROM is `build/ring-poc/ring-poc.gba`. Every console runs the same ROM. Press
Start on exactly one console to start enumeration; the remaining consoles wait.
No player count, slot number, or simulated input is written into the ROM's RAM
by the emulator. IDs are assigned by a discovery packet passing around the ring.

## Wiring contract

GBA normal serial mode, 32-bit transfers, 256 kHz clock:

- One common SC clock, driven by the selected host; other consoles use external clock.
- SO of each console connects to SI of the next console.
- The final console's SO connects back to the first console's SI.
- Common ground. No connection of multiple SO outputs to the same wire.

This is an explicit cable model, not a claim that an unmodified collection of
standard GBA cables supports it. GBATEK documents that standard multiplayer
cables ground the master's SI. Closing the ring requires replacing that grounded
input with the return path. Real cable construction, electrical loading, and
hardware timing remain unvalidated. 2 MHz is deliberately not used.

## Protocol

A word consists of type (4 bits), node (3), frame sequence (7), payload (10),
and CRC-8 (polynomial 0x07). Zero is idle. Types:

1. DISCOVER: assign consecutive software IDs and determine 2–8 members.
2. ANNOUNCE: circulate the agreed membership count.
3. FRAME: permit each console to insert one input packet in the stream.
4. INPUT: node ID, sequence, and all ten keypad bits. Forward other nodes'
   packets; remove your own after a full lap.
5. COMMIT: publish the complete input set after the host sees all inputs and
   its own packet's return. Every node checks the complete membership bitmap.

Each console has a bounded 16-word transmit FIFO. Sequence, duplicate-input,
CRC, membership, and queue overflow checks increment observable error counters.
The host re-enumerates after 128 transfers without a commit. This permits a fresh
session after a broken return wire is restored; it does not provide seamless
race recovery. A game integration must freeze/abort races on connection loss,
not silently re-enumerate and continue divergent simulations.

The 4096 CPU-clock turnaround was selected after shorter delays lost packets.
Eight players exchange a full input set in approximately 177,500 CPU clocks
(10.6 ms), below the GBA's 280,896 clocks per video frame. This measures the
homebrew only; the game still needs a CPU-budget and interrupt-latency analysis.

Status is at EWRAM 0x02000000: magic, software ID, membership count, committed
frames, errors, timeouts, transfers seen, last commit, eight committed keypad
values, and the host's measured full-frame cycle count (all 32-bit fields).
The clock measurement uses cascaded timers so it does not truncate at 16 bits.

## Reproducible validation

```sh
source /workspace/.setup/activate.sh
python3 /workspace/mgba/tools/link/test_ring.py \
  --runner /workspace/.setup/mgba-build/mgba-link \
  --rom /workspace/mksc/build/ring-poc/ring-poc.gba \
  --bios /workspace/attachments/58098acf-8e29-4a0e-9936-f293c6a94520/gba.bin \
  --results /workspace/.setup/results/ring-suite
```

The script requires the supplied BIOS hash. It checks 2–8 consoles with distinct
held keys, a host other than console zero, and return-wire interruption followed
by restoration. Positive runs require zero missed receivers, collisions, CRC
errors, and timeouts, correct software IDs, complete identical input sets, and
at least 100 committed frames per console. The fault case requires detectable
errors/timeouts and at least 100 additional committed frames after restoration.
No ROM, BIOS, or copyrighted asset is included in the source commit.
