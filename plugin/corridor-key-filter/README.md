# Corridor Key 2.0 — native OBS Studio filter plugin

Drop-in video filter: once the module sits in OBS's `plugins/` folder,
OBS loads it automatically and **Corridor Key 2.0** appears in every source's
filter list right next to the built-in Chroma Key Filter. No user-configured
shader, no runtime toggles.

## How it works

- `corridor-key-filter.c` registers an `obs_source_info` of type
  `OBS_SOURCE_TYPE_FILTER` (same pattern as OBS's own `chroma-key-filter.c`).
- `data/corridor_key_2.effect` is the GPU HLSL effect: brightness-invariant
  normalized-RGB key distance + 9-tap denoise + CorridorKey despill on the whole
  foreground. One pass on the render GPU, same cost class as the built-in filter.
- **Moving-subject mode (2.2.0)** is the one to use when the plate mask looks
  ragged: it keeps a *running background model* instead of one frozen frame.
  Every frame the model drifts towards the picture, so anything static — the
  green screen, the wall, the shelving, the chair — is absorbed and removed,
  while a person who moves is always ahead of the drift and stays. The chroma
  key still runs on top, so the green screen goes immediately and shadows on it
  are cleaned up too. A subject that stops moving for longer than
  *Keep a still subject for* is absorbed as well (that is what removes the
  static room), so raise that value if you sit still a lot.
- **Static background removal** needs no green screen at all: the filter grabs
  one frame of the empty scene into a "plate" texture (one extra GPU pass, on
  demand) and keys out everything within *plate tolerance* of that stored
  frame. Anything that was not in the plate — a person, a hand, a moving prop —
  stays. Nudge the plate tolerance up when the room light flickers.
- The plate is **never compared against a live sample**. Every plate-mode frame
  copies the input through the same `Copy` pass the capture used, and the
  shader differences the two copies, so backend colour handling cancels out.
  2.1.0 compared the plate against a live sample and silently failed on D3D11
  (Windows) while passing on GL; two rounds of guessing the sRGB convention did
  not fix it, the mirrored copy does. The plugin logs
  `ready (linear sRGB: on|off)` at filter creation for diagnosis.

## Install (prebuilt)

Download your OS package from the
[release](https://github.com/sneakyjoeru/CorridorKey2.0-OBS/releases/tag/corridor-key-2.0-obs)
(built for OBS 31/32, 64-bit), close OBS, then:

1. **Windows** — unzip `corridor-key-filter-2.2.0-windows-x64.zip` into
   `C:\ProgramData\obs-studio\plugins\` (you get
   `...\plugins\corridor-key-filter\bin\64bit\corridor-key-filter.dll`).
2. **Linux** — `mkdir -p ~/.config/obs-studio/plugins && tar -xzf corridor-key-filter-2.2.0-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins/`
   (Flatpak OBS: `~/.var/app/com.obsproject.Studio/config/obs-studio/plugins/`).
3. **macOS** — run `corridor-key-filter-2.2.0-macos-universal.pkg`, or unzip the
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
| Despill strength | CorridorKey despill (0 = off, 1000 = full) on every kept pixel whose key channel exceeds the mean of the other two; skin/grays unchanged | 600 |
| Denoise | 9-tap edge denoise blend | 1.0 |
| Keep only the moving subject | moving-subject mode: adaptive background + chroma, removes everything static | off |
| Motion threshold | how different from the learned background a pixel must be to count as "moving"; raise it until the static background disappears, lower it if parts of you vanish (×1000) | 60 |
| Keep a still subject for | how long a pixel may stay still before the background absorbs it (0.1 s steps) | 2.5 s |
| Remove static background | switch the keyer from a colour to the captured plate | off |
| Plate tolerance | how far a pixel may differ from the plate and still be removed (×1000 of the 0..1 encoded range) | 60 |
| Plate softness | feather of the plate matte (×1000) | 60 |
| Auto-capture delay | seconds between switching plate mode on and the automatic capture; 0 = only the button captures | 5 |
| Capture plate now | grabs the current frame as the plate — be out of shot, or cover the camera | — |
| Clear captured plate | forget the plate (the filter then passes the image through untouched) | — |

### Removing a background with a moving subject (no clean plate needed)

1. Add the filter, tick **Keep only the moving subject**. The green screen is
   gone at once (chroma), the rest of the room fades out over ~a second while
   the model learns it.
2. Move a little so the model separates you from the room. Raise **Motion
   threshold** if noise or a flickering monitor keeps parts of the room alive;
   lower it if a slowly moving arm or a sleeve disappears.
3. **Keep a still subject for** is the trade: the room disappears *because*
   static pixels are absorbed, so a subject that stops moving for longer than
   this is absorbed too. 2–4 s suits most webcams; 8–10 s if you sit still.
4. Pressing **Capture plate now** restarts the model from the current frame.

Uses three small extra GPU passes per frame (one frame copy, one background
update, the key), which is why it costs noticeably more than plain chroma key.

### Removing a static background

1. Lock the camera: fixed position, and exposure/white balance locked if you can.
2. Add the filter, tick **Remove static background**.
3. Leave the shot (or cover the lens) and press **Capture plate now** — or press
   it at any time while nothing but the background is visible. With auto-capture
   on, it fires by itself after the delay.
4. Raise **Plate tolerance** until the background is fully gone; lower it if
   parts of you disappear. **Plate softness** softens the matte edge.

The plate is one still frame, so it only works for a locked-off camera: unsteady
cameras, auto-exposure pumping or daylight drift will show through. Re-capture
the plate whenever the lighting changes. Shadows and reflections of the subject
in the background are *not* in the plate, so they stay — that is the same
roughness as any difference key and usually reads as natural.

## License

Corridor Key 2.0 name kept per upstream licence additional terms; CC BY-NC-SA
4.0 + additional terms ([LICENSE](../../LICENSE)); attribution to Corridor
Digital / Niko Pueringer and the OBS project preserved in file headers.
