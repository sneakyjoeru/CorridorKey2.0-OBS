# Corridor Key 2.0 — native OBS Studio filter plugin

Drop-in video filter: once the built module sits in OBS's `plugins/` folder,
OBS loads it automatically and **Corridor Key 2.0** appears in every source's
filter list right next to the built-in Chroma Key Filter. No user-configured
shader, no runtime toggles.

## How it works

- `corridor-key-filter.c` registers an `obs_source_info` of type
  `OBS_SOURCE_TYPE_FILTER` (same pattern as OBS's own `chroma-key-filter.c`).
- `data/corridor_key_2.effect` is the GPU HLSL effect: brightness-invariant
  normalized-RGB key distance + 9-tap denoise + CorridorKey despill, gated to
  edge pixels. One pass on the render GPU, same cost class as the built-in filter.

## Build (all three OSes)

OBS plugins are native modules ABI-matched to OBS itself, so build inside a
clone of [obs-studio](https://github.com/obsproject/obs-studio):

1. Clone OBS: `git clone https://github.com/obsproject/obs-studio.git` (CMake ≥ 3.28,
   Qt6, FFmpeg dev libs per the upstream BUILD instructions for your OS).
2. Copy this folder into `obs-studio/plugins/corridor-key-2.0-filter/`.
3. In `plugins/CMakeLists.txt` add one line next to the other filters:
   `add_core_module(corridor-key-2.0-filter)`
4. Configure + build with OBS's documented commands per OS:
   - **Windows**: CMake + MSVS 2022 x64 project (`cmake -A win64-x64-release …`).
   - **Linux**: `cmake -B build … && cmake --build build` → `corridor-key-2.0-filter.so`.
   - **macOS**: OBS macOS build instructions → `.dylib`.
5. Install: copy the built module (and keep `data/` alongside as OBS installs
   it) into OBS's plugins directory:
   - Windows: `C:\Program Files\obs-studio\plugins\`
   - Linux: `~/.config/obs-studio/plugins/`
   - macOS: `/Applications/obs-studio.app/Contents/Resources/plugins/` (or the
     user dir under `.config` equivalent).
   Then launch OBS — the filter is registered at startup, no further setup.

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
