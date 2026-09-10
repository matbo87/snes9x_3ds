# Third-Party Notices

This project depends on third-party libraries/toolchains. Their licenses apply to those components.

## Bundled Core

- **Snes9x core** (in `source/Snes9x/`):
  - License terms: non-commercial Snes9x license
  - Notice/source: `source/Snes9x/copyright.h`

## Submodules

- **rcheevos** (submodule at `source/rcheevos/`):
  - License terms: MIT (c) 2018 RetroAchievements.org
  - Notice/source: `source/rcheevos/LICENSE`
  - Upstream: https://github.com/RetroAchievements/rcheevos
  - Fork: https://github.com/matbo87/rcheevos, branch `snes9x_3ds`, carrying
    3DS-specific changes on top of `v12.4.0`.

## Linked Libraries (build/runtime)

Based on current build flags in `Makefile`:

- `-lcitro3d` (citro3d, devkitPro ecosystem; zlib license) — always built
  locally from `v1.7.1` with `patches/citro3d.patch` applied; the
  patched build is required, a stock library does not link.
- `-lctru` (libctru, devkitPro ecosystem)
- `-lpng` (libpng)
- `-lz` (zlib)
- `-lm` (system math library)

Please consult each upstream project for exact license text/version requirements.

## Toolchain

This project is built with devkitPro / devkitARM tooling. Toolchain binaries and packaged components are licensed by their respective upstream projects.

## Bundled Build Tools

This repository includes prebuilt `makerom` binaries in `makerom/` for developer convenience when packaging `.cia` files.

See:

- `makerom/BINARY_SOURCES.md` for upstream release URLs and checksums
- https://github.com/3DSGuy/Project_CTR for source and licenses of these tools

## Attribution and Compliance

- Keep original copyright/license headers.
- Keep the Snes9x notice when redistributing source/binaries derived from this repository.
- For distribution packages, include this file and `LICENSE.md`.
