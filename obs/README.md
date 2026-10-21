# CorridorKey for OBS Studio

Two paths, pick by need:

## 1. Real-time keying — GPU shader filter (no Python, no ML)

`shaders/corridor_key_pro.effect` is a chroma-key pixel shader that runs
**entirely on the GPU** inside OBS — one pass, ~10 texture taps per pixel.
It replaces OBS's built-in Chroma Key filter with a better algorithm:

- **Brightness-invariant normalized-RGB keying** instead of the YUV yz-plane:
  a dark screen fold sits zero distance from a bright one, so folds key at any
  exposure and gray/white/black subject pixels never key out.
- **9-tap box-filtered key distance** (`denoise` knob): webcam grain stops
  speckling through the matte.
- **CorridorKey's own despill formula** ported from
  `corridorkey/runtime.py:_despill_average_srgb_torch` — spill is redistributed
  into the neighbouring channels, not desaturated, and it is gated to edge
  pixels only, so green clothing on your subject survives untouched.
- Green / blue / red screen axis switch (`key_axis`).

### Install

1. Install the **obs-shaderfilter** plugin:
   <https://obsproject.com/forum/resources/obs-shaderfilter.1736/>
2. Right-click your webcam/source → *Filters* → add **User-defined shader**.
3. Point it at `obs/shaders/corridor_key_pro.effect`.
4. Set `key_color` to the actual screen color sampled from your shot
   (screenshot it, eyedropper it), then tune:

| Control | Meaning | Start value |
|---|---|---|
| `key_axis` | 0=green 1=blue 2=red screen | 0 |
| `similarity` | key radius, normalized-RGB units (~0..1.2) | 0.35 |
| `smoothness` | soft matte width | 0.10 |
| `despill_strength` | CorridorKey despill amount | 0.6 |
| `denoise` | 9-tap distance blend (0 = off) | 1.0 |

Performance: pure pixel shader, zero CPU work, no model weights, no VRAM
budget — it costs a fraction of a millisecond per frame at 1080p on any
dGPU that runs OBS. It is real-time; the ML model below is not.

## 2. Offline full-ML keying → feed the result into OBS

The CorridorKey 2.0 model (this repo's main tool) is **not real-time**:
two-stage inference over Hann windows with ~5 GB of weights. Use it for
recorded segments and bring the alpha cutout into OBS as media:

1. Run `infer.py --outputs alpha fg cutout` — `cutout` is written as
   **ProRes 4444 with alpha**, which OBS reads natively.
2. Add it to OBS as a *Media Source* (not a browser/window source); OBS keeps
   the RGBA alpha. Layer it over whatever background you want.
3. For live segments: record → key offline → play back keyed clips as media
   sources; keep the shader from path 1 for the live camera.

Note: the RVM port in `corridorkey/rvm.py` is encoder-only (the MAE decoder
head was intentionally dropped), so this repo cannot emit a live alpha matte
by itself without upstream's decoder weights — hence the shader above is the
practical real-time route.

## License

The shader reuses CorridorKey's despill math and OBS's built-in filter
structure; it stays under the same [Corridor Key Licence](../LICENSE)
(CC BY-NC-SA 4.0 + additional terms). Keep attribution intact.
