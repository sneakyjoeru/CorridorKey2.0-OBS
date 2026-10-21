# CorridorKey for OBS Studio

Everything here feeds OBS Studio's **built-in Chroma Key filter** — no plugins,
no custom shaders. The built-in filter is itself a GPU pass inside OBS, so keying
stays real-time at playback; the ML model runs offline beforehand.

## Workflow: offline ML keying → flat screen → stock Chroma Key filter

1. Run inference with a cutout output:
   `infer.py --input clip.mp4 --output_dir out/ --outputs alpha fg cutout`
   The `cutout_*.mov` is written by the video writer as **ProRes yuva444p10le**
   (straight alpha) — verified in `corridorkey/video_io.py` (`prores_ks`,
   `-pix_fmt yuva444p10le`). `infer.py` also carries `--despill_strength`, so
   edges arrive already despilled.
2. Composite the cutout onto a flat synthetic screen:
   `python obs/greenscreen.py --input out/cutout_xxx.mov --screen green --out out/obs_screen.mp4`
   A flat #00FF00 field is the ideal input for any chroma keyer: uniform color,
   no lighting gradient, no fabric folds.
3. In OBS: add `obs_screen.mp4` as a **Media Source**, right-click → *Filters* →
   add the stock **Chroma Key** filter, set its `key_color` to the exact screen
   color you chose in step 2, then tune `similarity` / `smoothness` / `spill`
   only if edges need it.

Live webcam path: the same stock Chroma Key filter applies directly to the
camera source — nothing from this repo is needed there.

## Why not real-time ML

CorridorKey 2.0 is two-stage DiT inference over Hann windows with ~5 GB of
weights — offline by design. The RVM port in `corridorkey/rvm.py` is encoder-only
(the MAE decoder head was intentionally dropped), so this repo cannot emit live
alpha mattes on its own; record → key offline → play back keyed media instead.

## License

This workflow reuses CorridorKey's despill math and output pipeline; it stays
under the same [Corridor Key Licence](../LICENSE) (CC BY-NC-SA 4.0 + additional
terms). Keep attribution intact.
