# Optional restored audio samples

The default build keeps the original samples and mixer rate. To use the
supplied **MarioKartSuperCircuit (Music Restore).ips**, run:

```sh
python3 tools/ring/build.py --music shared --boot-ring \
  --music-restoration-ips '/path/to/MarioKartSuperCircuit (Music Restore).ips'
```

`build_release.py` accepts the same option for all four variants and the
shifted relocation fixture. It records the patch SHA-256 in `variants.json`.
Omitting the option restores the original audio assets on the next build.
The IPS and reference ROM are local inputs and are not committed.

The importer requires the supplied patch's SHA-256
`ebbad9fbb573b38c0bb18a811b7cff5bf2cdf3ab14be030cdb681f9f8772ae42`
and the matching US base ROM. It imports 37 instrument/bank objects and one
changed native waveform, plus the appended PCM sample data. It retains all
378 original data relocations; the 89 distinct appended sample references
use a linker-owned sample base. No fixed `0x08400000` addresses survive.
This matters because ring code already occupies part of that historical range.

The patch also changes rendering instructions and graphics. Those changes
are excluded from the audio option. The mixer retains the native 10512 Hz output rate, master volume and
six-channel budget. The reference's 21024 Hz mode caused eight-player Battle
to drop to roughly 65% speed in testing, so that unrelated rate change is
excluded. Restored source samples and instrument assignments remain exact;
this option does not promise the reference patch's doubled output bandwidth.
Performance validation remains separate from asset validation.
Normal and shared sound routing remain independent options.

Check the linked audio against the reference, including a shifted build:

```sh
python3 tools/ring/test_restored_audio.py \
  --patch '/path/to/MarioKartSuperCircuit (Music Restore).ips' \
  --rom mksc-ring.gba --elf mksc-ring.elf
```

The clean US build must still produce SHA-1
`9d327c030c3e2d9007990518594f70c3340ac56f`.
