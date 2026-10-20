# Docker releases

Build from this repository root. Docker Desktop works on Windows; no host GBA
compiler is required. The image contains the pinned matching compiler and ARM
build tools, but no base ROM, BIOS, or supplied audio patch. Inputs are mounted
read-only when the container runs. The build runs offline in a temporary source
copy and leaves the checkout and input files unchanged.

## Full release

Create `inputs` and an empty `release` directory. Put these files in `inputs`:

* `baserom.gba`: original US ROM, SHA-1
  `9d327c030c3e2d9007990518594f70c3340ac56f`.
* `MusicRestore.ips`: the supplied Music Restore reference patch, SHA-256
  `ebbad9fbb573b38c0bb18a811b7cff5bf2cdf3ab14be030cdb681f9f8772ae42`.

The checked Japanese Sunset Wilds asset delta is already in the repository;
no additional Japanese ROM is needed to build either artwork choice. No BIOS
is needed for building or patch reconstruction; gameplay tests require it.

```sh
docker build -t mksc-release .
mkdir -p release
docker run --rm --network none \
  --mount "type=bind,source=$PWD/inputs,target=/inputs,readonly" \
  --mount "type=bind,source=$PWD/release,target=/out" \
  mksc-release
```

PowerShell:

```powershell
docker build -t mksc-release .
New-Item -ItemType Directory -Force release
docker run --rm --network none `
  --mount "type=bind,source=$($PWD.Path)/inputs,target=/inputs,readonly" `
  --mount "type=bind,source=$($PWD.Path)/release,target=/out" `
  mksc-release
```

On Linux, optionally add `--user "$(id -u):$(id -g)"` before the image name so
output files belong to your user. Add `--jobs 2` after the image name to limit
compiler concurrency. Use a new empty output directory for each release; the
builder rejects nonempty directories to prevent mixing old and new artifacts.

The complete matrix is 18 ROMs:

| ROM family | Variants |
| --- | --- |
| Matching stock game | 1, byte-identical to the US input |
| Ring game | 16: title/direct boot × normal/shared music × original/Japanese Sunset Wilds × original/restored samples |
| Standalone ring homebrew proof of concept | 1 |

Ring names append `-boot`, `-shared`, `-japanese-sunset`, and `-restored-audio`
when selected. Every ring image supports 2–16 consoles: eight racer slots and
automatic map roles beyond eight. There is no standalone map image or map flag.

Each ROM has `.elf`, `.map`, `.sym`, and `.ips` files. All IPS patches use the
same original US base, including the identity stock patch and the optional
homebrew patch (which truncates the base). The homebrew `.gba` also runs directly.

`manifest.json` records each ROM, patch, reconstructed-ROM SHA-256, input
hashes, compiler version, variant options, and file hashes. **Every patch is
applied with two independent decoders and the reconstructed bytes and SHA-256
must match its built ROM.** This includes restored-audio images padded to
16 MiB. A failed build or reconstruction publishes no deliverables.

`SHA256SUMS` covers the exported files. `mksc-patches.zip` contains only patches,
the manifest and patch instructions, suitable for distributing the mod without
ROMs. The archive intentionally excludes ROMs, ELF files and build logs.

## Without the optional restored samples

To build eight ring variants, stock and homebrew using only `baserom.gba`:

```sh
docker run --rm --network none \
  --mount "type=bind,source=$PWD/inputs,target=/inputs,readonly" \
  --mount "type=bind,source=$PWD/release,target=/out" \
  mksc-release --without-restored-audio
```

The manifest explicitly marks this as a partial matrix (`all_variants: false`).
A full release requires the supplied Music Restore IPS; it is never downloaded.

## Network proxies

Only `docker build` uses the network (Debian packages and the pinned public
agbcc revision). Docker's standard `HTTP_PROXY`/`HTTPS_PROXY` build arguments are
supported. For a TLS-inspecting proxy, supply its trusted CA bundle using
`--secret id=network_ca,src=/path/to/ca-bundle.crt`. This is used only for the
compiler fetch and is not copied into the image. TLS and package-signature
verification remain enabled.

## Validation scope

The container validates compilation, the matching stock hash, every supported
variant, and IPS reconstruction. It does not claim gameplay correctness.
Run the BIOS-backed gameplay gates in `tools/ring/verify_release.py` separately.
The IPS format edge cases can also be checked with:

```sh
python3 tools/release/test_ips.py
```
