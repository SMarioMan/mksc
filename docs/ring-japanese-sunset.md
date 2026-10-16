# Japanese Sunset Wilds artwork

The default mod build uses the original US artwork. Japanese Shy Guy artwork
and the course preview are optional:

```sh
python3 tools/ring/build.py --sunset-art japanese
```

Use `--sunset-art original` (the default) to switch back. `build_release.py`
accepts the same option and records it for every variant. Building either
option requires only the US base ROM; the verified Japanese asset delta is
already included. The Japanese ROM is needed only to recreate that delta.
The clean US build retains its original artwork and
SHA-1 `9d327c030c3e2d9007990518594f70c3340ac56f`.

The import is stored as a 4,760-byte BPS asset delta, not as a Japanese ROM.
Its source is the US Sunset Wilds course record and compressed preview. BPS
source, target and patch CRCs are checked whenever assets are generated.

To recreate the delta with the supplied originals:

```sh
python3 tools/ring/restore_sunset_assets.py \
  --us-rom baserom.gba --japanese-rom '/path/to/Mario Kart Advance (Japan).gba'
```

The importer verifies Japanese SHA-1
`88ffde363b05264a99a4d5ada0c80a00196a94d7`. Course ID 26 is read through the
relative course directory at ROM offset `0x258000`. The Japanese preview is at
`0xAD03C`; its palette at `0xACF84` is byte-identical to the US palette.
The imported record is appended to the mod's course data and its directory
offset updated. Internal relative pointers remain intact. Other course records
and the English course name are preserved. A symbolic preview pointer supports
shifted builds.
The importer also checks that all data outside the changed sprite/cell stream
matches the US record, after adjusting its five displaced relative pointers.
Terrain, checkpoints, object placements and gameplay palettes remain identical.

`test_sunset_art.py` checks the linked record and preview against the selected original artwork or verified
asset delta, then selects Sunset Wilds through the native menus with keypad
inputs and compares the complete decompressed preview in VRAM on 2 and 8
consoles using the real BIOS. The general all-course suite covers gameplay.
