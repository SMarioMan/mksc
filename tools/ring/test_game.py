#!/usr/bin/env python3
"""BIOS-backed, real-keypad ring menu and gameplay regression matrix.

Snapshots stop each console at the same committed-input barrier. ROM copies
are immutable for each run, so rebuilding the working tree cannot change a test.
This checks gameplay entry and controls; it does not assert round completion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

BIOS_SHA1 = '300c20df6731a33952ded8c436f7f186d25d3492'
HELD = [1, 17, 33, 257, 513, 65, 129, 1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', required=True, type=Path)
p.add_argument('--rom', required=True, type=Path)
p.add_argument('--bios', required=True, type=Path)
p.add_argument('--results', required=True, type=Path)
p.add_argument('--players', nargs='+', type=int, default=list(range(2, 9)))
p.add_argument('--host', type=int, default=0)
p.add_argument('--arena', type=int, choices=range(4), default=0)
p.add_argument('--modes', nargs='+', choices=['vs', 'battle', 'gp'], default=['vs', 'battle'])
p.add_argument('--hle', action='store_true', help='Exercise optional HLE; real BIOS is the default')
a = p.parse_args()
if hashlib.sha1(a.bios.read_bytes()).hexdigest() != BIOS_SHA1:
    p.error('The supplied real GBA BIOS is required')
if a.host < 0 or any(a.host >= n for n in a.players):
    p.error('The host console index must exist in every test')
if any(n < 2 or n > 8 for n in a.players):
    p.error('Player counts must be 2 through 8')
a.results.mkdir(parents=True, exist_ok=True)
rom_data = a.rom.read_bytes()
rom_hash = hashlib.sha256(rom_data).hexdigest()
rom = a.results / f'{rom_hash}.gba'
if not rom.exists():
    rom.write_bytes(rom_data)
base = []
for line in (Path(__file__).parent / 'inputs/ring-vs.csv').read_text().splitlines():
    frame, slot, keys = line.split(',')
    frame, slot, keys = int(frame), int(slot), int(keys, 16)
    if frame < 3500 and slot == 0:
        base.append((frame, keys))
for n in a.players:
    for mode in a.modes:
        events = []
        for frame, keys in base:
            if (mode == 'gp' and frame in (2500, 2515)) or (mode == 'battle' and frame in (2200, 2215)):
                continue
            shared = frame < 1500 or frame in (2000, 2015)
            for slot in range(n if shared else 1):
                events.append((frame, slot if shared else a.host, keys))
        if mode == 'gp':
            events.extend([(1600, a.host, 64), (1615, a.host, 0)])
        if mode == 'battle':
            events.extend([(1600, a.host, 128), (1615, a.host, 0)])
            for step in range(a.arena):
                events.extend([(2300 + step * 40, a.host, 128), (2320 + step * 40, a.host, 0)])
        events.extend((3500, slot, HELD[(slot + n - a.host) % n]) for slot in range(n))
        name = f'{mode}-{n}'
        inputs = a.results / f'{name}.csv'
        inputs.write_text(''.join(f'{f},{i},{k:x}\n' for f, i, k in sorted(events)))
        command = [str(a.runner), '--ring', '--ring-quantum', '2048', '--rom', str(rom),
                               '--bios', str(a.bios), '--players', str(n),
                               '--frames', '3750', '--input', str(inputs),
                               '--keys-address', '0x03006B00', '--key-slots', '8',
                               '--dump', '0x03006B40', '--barrier-before-input', '--barrier-state', '0x03006B40',
                               '--screens', str(a.results / name)]
        if a.hle:
            bios_index = command.index('--bios')
            del command[bios_index:bios_index + 2]
            command += ['--hle']
        proc = subprocess.run(command,
                              capture_output=True, text=True, timeout=300)
        (a.results / f'{name}.json').write_text(proc.stdout)
        (a.results / f'{name}.stderr').write_text(proc.stderr)
        if proc.returncode:
            raise RuntimeError(f'{name}: {proc.stderr}')
        data = json.loads(proc.stdout)
        assert data['bios'] == ('hle' if a.hle else 'real'), name
        assert data['collisions'] == 0, name
        positions, rounds = [], []
        for node in data['nodes']:
            state = node['dump']
            assert state[22:24] == [0, 0], (name, 'CRC/errors/timeouts', state[22:24])
            rounds.append(state[20])
            game = node['game']
            assert game['humanCount'] == n and game['localId'] == (node['slot'] + n - a.host) % n, (name, game)
            assert game['raceFlags'] & {'vs':0x400,'battle':0x800,'gp':0x100}[mode], (name, game)
            assert node['menu']['heldKeys'] == HELD[:n] + [0] * (8 - n), (name, node['menu'])
            racers = game['drivers'][:8 if mode == 'gp' else n]
            assert all(r['address'] for r in racers), name
            if mode == 'gp':
                assert len(racers)==8 and len({r['character'] for r in racers})==8, (name,'GP roster must contain eight distinct characters',racers)
            if 'id' in racers[0]:
                assert [r['id'] for r in racers] == list(range(8 if mode == 'gp' else n)), (name, racers)
                if mode == 'battle':
                    assert all(r['balloons'] == 3 for r in racers), (name, racers)
            positions.append([r['position'] for r in racers])
        assert min(rounds) > 100 and len(set(rounds)) == 1, (name, rounds)
        assert all(s == positions[0] for s in positions), (name, positions)
        assert len({tuple(pos) for pos in positions[0]}) == (8 if mode == 'gp' else n), (name, positions[0])
        print(f'PASS {name}: {rounds[0]} commits, distinct controls and synchronized positions', flush=True)
(a.results / 'manifest.json').write_text(json.dumps({'rom_sha256': rom_hash, 'bios_sha1': BIOS_SHA1, 'bios_mode': 'hle' if a.hle else 'real',
                                                    'players': a.players, 'modes': a.modes}, indent=2))
