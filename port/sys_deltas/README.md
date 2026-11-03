# Slippi Sys deltas

These two VCDIFF patches reconstruct `GameSetup_gui.dat` and `slpCSS.dat` using
files from the player's own GALE01 revision 2 retail disc. The compliance ruling
prohibits shipping these Nintendo-derived whole files from YouGame in production.
The four other whole `.dat` files remain eligible to ship. This spike leaves all
Sys files untouched; packaging must exclude the two prohibited originals later.

Python 3.9 (stdlib only) and `/opt/homebrew/bin/xdelta3` version 3.2.1 are required.
From the repository root:

```sh
python3 port/sys_deltas/make_sys_deltas.py /path/to/GALE01r2.iso
python3 port/sys_deltas/check_sys_deltas.py /path/to/GALE01r2.iso
python3 port/sys_deltas/make_sys_deltas.py /path/to/GALE01r2.iso --compliance-only
python3 -m unittest discover port/sys_deltas
```

Use `--out directory` on the generator and `--dir directory` on the checker to
override the default `port/sys_deltas`. For a determinism check, generate twice
into separate directories and compare both patches and `manifest.json` byte for
byte. Inputs, filenames, source selection, and encoding options are deterministic.

The generator confirms the three Sys copies match, scans every FST file for exact
32-byte-aligned, nonuniform 64-byte target blocks, and greedily selects the source
covering the most additional target bytes (ties by disc path). Coverage counts
the union of matching byte ranges, not overlapping blocks twice. Selection stops
when the best remaining contribution is below one 64-byte block. The generator
then encodes and scans each delta against the whole disc. For every failing target,
it appends every implicated disc file not already selected, in descending hit count
(ties by disc path), and regenerates until there are zero hits. It fails closed if
hits remain after 8 rounds, a hit has no FST source, or no new source can be added.
The manifest records the final source order, starting with the coverage ranking. Encoding uses `-e -f -S none -9`, a source window covering the
entire concatenation, `-a` to disable armor, `-A` to omit application headers, `-n` for no window checksum (the host's
existing decoder reads that field differently; the target SHA-1 is the check), and
`-D` to disable external decompression. Retail sources, concatenations and decoded
targets exist only in automatically cleaned system temporary directories.

Each patch must decode to the target SHA-1 before publication. Both the generator
and checker require **zero hits** in the full-disc any-offset compliance scan:
index every 64-byte window at every byte offset of the delta, skipping windows of
one repeated byte, then stream the **whole disc image** in disc-aligned 32-byte
steps. This finds any stored disc run of at least 96 bytes, regardless of its delta
offset; it does not claim to detect every shorter run. Failures print disc file
paths, file-relative offsets, absolute disc offsets, and delta offsets. ADD bytes
are counted from actual
VCDIFF instructions, including paired opcodes; RUN and COPY bytes are excluded.

`manifest.json` contains `format` (`vcdiff-xdelta3-3.2.1-nosecondary`), `disc`
(`id`, `revision`) and `files`. Each file records `target`, its three `sys` folders,
`target_sha1`, `target_size`, ordered `sources` (`disc` path, `sha1`, `size`),
`delta`, `delta_sha1`, and `delta_size`. No extracted retail bytes are stored.

The engine applier is owned by melee-native. It must validate disc identity,
verify each source SHA-1 and size, concatenate sources **in manifest order**,
verify the patch, apply plain VCDIFF without secondary compression, and check
`target_sha1` and size before use. Patches use the default VCDIFF code table and
xdelta's Adler-32 window checksums; there are no application headers or armor.
Any missing source or hash mismatch fails closed, with no fallback whole file.
