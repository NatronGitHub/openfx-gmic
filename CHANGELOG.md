# Changelog

Changes in openfx-gmic since Natron 2.5.0 (tag `Natron-2.5.0`). Natron 2.6 policy: plug-ins may change their output when the underlying library changes (here G'MIC 2.8 → 4.0.7); users who need the old results can keep using Natron 2.5. Changes to the plug-in's own parameters or semantics bump the plug-in's major version.

## For Natron 2.6

### G'MIC 4.0.7 (was 2.8.4) — #3, #26
- The filters now come from G'MIC 4.0.7 and its community filters: 1205 plug-ins (the filter set, names, parameters and results of many filters changed with G'MIC itself).
- Bugs in the G'MIC 4.0.7 sources and filters are fixed at build time until G'MIC releases the fixes, and reported upstream (#58, #67): a crash of the host on macOS when a G'MIC error happens inside an OpenMP loop (Smooth [Patch-PCA], #41), and broken filters (Z-Time, Exposure Fusion Weight Map, Non-Convolution Edge Extraction, Colorize [Photographs], Morphology Painting, Random Pattern Courtepointe; #53).

### Behaviour changes (saved projects may render differently)
- **Plug-in version 2.0** (#57): "Output Layer" now counts from the top of the G'MIC image stack (Source = Layer 0, the "Layer -1" input = Layer -1…), as in G'MIC-Qt. Before, it counted from the end. Affected: filters that return several images (e.g. Channels to Layers now gives R instead of B by default; Fade Layers, Morph Layers, Tiles to Layers give the first result instead of the last), and filters with layer inputs connected. "Merged" and single-result filters are unchanged. Natron loads 1.x nodes with 2.0 and lists them in its error log.
- **Ignore Alpha** (#57): the filter now processes all layers at once (multi-input filters work), and `$1`, `$*`… in text parameters are kept as typed. Results whose size changed come out without alpha.
- **Premultiplied inputs** (#54) are unpremultiplied before G'MIC and the result premultiplied back (G'MIC works on straight colours): fewer dark fringes on semi-transparent areas.
- **Point parameters** (#54) take the pixel aspect ratio into account (anamorphic footage).
- **Render scale** (#54): on Natron, zoomed-out viewers and proxy mode now render at full resolution and downscale, so pixel-sized parameters (radii, sizes) give the same result at every zoom level (slower when zoomed out).
- **Filters relying on G'MIC-Qt's preview** (#60): Tupper's Self-Referential Formula, Shear, Array [Random Jumble] by Px, Color Harmonies, Bit-Plane Shuffler and others now render.
- **Locale** (#59): parameter defaults and numbers in G'MIC commands no longer depend on the system locale.

### New
- Renders can be aborted while G'MIC runs (#46).
- The licence texts (COPYING, CeCILL-C, CeCILL) are installed in the bundle's Resources.

### Fixes
- Partial renders (zoomed-in viewer, render window) and images whose bounds differ from the output (#2, #40).
- Wrong parameters or output for many filters: value(), file defaults, choices, colours with alpha, 8-bit flip, gray+alpha (#17, #7, #39, #20).
- Crashes: more than 32 output images, aborted runs (#37, #56).

### Build
- Built with C++17 (C++20 for Qt6 builds of Natron) (#33); CI on Linux, macOS (Apple clang, MacPorts) and Windows (MSYS2 UCRT64) (#23, #25, #28, #35, #38).
- CI artifacts are self-contained (bundled libraries) (#62).
