# Corridor Key 2.0 — native OBS Studio filter plugin

Drop-in video filter: once the module sits in OBS's `plugins/` folder,
OBS loads it automatically and **Corridor Key 2.0** appears in every source's
filter list right next to the built-in Chroma Key Filter. No user-configured
shader, no runtime toggles.

## How it works

- `corridor-key-filter.c` registers an `obs_source_info` of type
  `OBS_SOURCE_TYPE_FILTER` (same pattern as OBS's own `chroma-key-filter.c`).
- `data/corridor_key_2.effect` is the GPU HLSL effect: brightness-invariant
  normalized-RGB key distance + 9-tap denoise + CorridorKey despill, gated to
  edge pixels. One pass on the render GPU, same cost class as the built-in filter.

## Install (prebuilt)

Download your OS package from the
[release](https://github.com/sneakyjoeru/CorridorKey2.0-OBS/releases/tag/corridor-key-2.0-obs)
(built for OBS 31/32, 64-bit), close OBS, then:

1. **Windows** — unzip `corridor-key-filter-2.0.0-windows-x64.zip` into
   `C:\ProgramData\obs-studio\plugins\` (you get
   `...\plugins\corridor-key-filter\bin\64bit\corridor-key-filter.dll`).
2. **Linux** — `mkdir -p ~/.config/obs-studio/plugins && tar -xzf corridor-key-filter-2.0.0-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins/`
   (Flatpak OBS: `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`).
3. **macOS** — run `corridor-key-filter-2.0.0-macos-universal.pkg`, or unzip the
   `.zip` and copy `corridor-key-filter.plugin` into
   `~/Library/Application Support/obs-studio/plugins/`. The build is ad-hoc
   signed, not notarized: if macOS blocks it, run
   `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/corridor-key-filter.plugin`.
4. Start OBS → right-click a source → **Filters** → **+** → **Corridor Key 2.0**.

## Build from source

Standalone project based on
[obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) — no
obs-studio checkout needed. From this folder:

- **Windows** (VS 2022, CMake ≥ 3.28): `cmake --preset windows-x64 && cmake --build --preset windows-x64 && cmake --install build_x64 --prefix release --config RelWithDebInfo`
- **macOS** (Xcode 16): `cmake --preset macos && cmake --build --preset macos && cmake --install build_macos --prefix release --config RelWithDebInfo`
- **Linux** (Ubuntu 24.04): `sudo add-apt-repository ppa:obsproject/obs-studio && sudo apt install obs-studio libsimde-dev ninja-build cmake`, then
  `cmake --preset ubuntu-x86_64 && cmake --build --preset ubuntu-x86_64 && cmake --install build_x86_64 --prefix release`

Windows/macOS download the OBS sources and obs-deps pinned in `buildspec.json`.
CI: `.github/workflows/obs-plugin.yaml` builds all three on every change.

The module is named `corridor-key-filter` (no dots): OBS cuts a plugin folder
name at its last `.`, so the old `corridor-key-2.0-filter` name never loaded
from the per-user plugin folders.

## Controls

| Control | Meaning | Default |
|---|---|---|
| Key color | eyedropper color of your screen | `#00FF00` |
| Similarity | key radius (normalized-RGB units ×1000) | 350 |
| Smoothness | matte feather (×1000) | 100 |
| Despill strength | CorridorKey despill, edges only | 600 |
| Denoise | 9-tap edge denoise blend | 1.0 |

## License

Corridor Key 2.0 name kept per upstream licence additional terms; CC BY-NC-SA
4.0 + additional terms ([LICENSE](../../LICENSE)); attribution to Corridor
Digital / Niko Pueringer and the OBS project preserved in file headers.
