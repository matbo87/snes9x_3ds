# Third-Party Notices

This project depends on third-party libraries/toolchains. Their licenses apply to those components.

## Bundled Core

- **Snes9x core** (in `source/Snes9x/`):
  - License terms: non-commercial Snes9x license
  - Notice/source: `source/Snes9x/copyright.h`

## Linked Libraries (build/runtime)

Based on current build flags in `Makefile`:

- `-lcitro3d` (citro3d, devkitPro ecosystem)
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
