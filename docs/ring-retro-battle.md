The ring build restores the four hidden SNES Battle courses as a separate
native selection page. L or R switches sets; the four GBA arenas remain
available. Leaving Battle mode resets the course/set selection normally.

The reference is the supplied SNES Battle Course Restoration v1.0 patch by
BlueYoshi_57EML (27 January 2024). That patch replaces GBA arenas. The build
instead extracts its repaired course records, graphics and thumbnails into
separate generated assets and connects them to the hidden track definitions.
The small original BPS delta is kept in `tools/ring/fixtures`; full ROM-derived
outputs remain in the ignored build directory. BPS source, target and patch
CRCs are verified before generating anything.

Course data uses relative offsets. Removing the reference ROM's unused gap
adjusts only offsets to its appended streams. Menu definitions use linker
symbols, preserving shifted builds. The clean build uses its original data.

Battle starts use the eight authored marker records rather than duplicating
four positions. The C loader points each racer toward the centroid of the
eight positions, avoiding the native four-entry facing table's out-of-bounds
reads. `test_spawns.py` exercises both sets at 2–8 participants and checks
unchanged marker coordinates, distinct positions, headings, terrain and
agreement between consoles.
