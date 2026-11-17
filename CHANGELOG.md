# Changelog

Changes in openfx-gmic since Natron 2.5.0 (tag `Natron-2.5.0`). Natron 2.6 policy: plug-ins may change their output when their underlying library changes (here G'MIC 2.8.4 → 4.0.7); users who need the old results can keep using Natron 2.5. Changes to the plug-in's own parameters or semantics bump the plug-in's major version. Natron's release notes also list these changes.

## For Natron 2.6

### G'MIC 4.0.7 (was 2.8.4) — #3, #26
- The filters now come from G'MIC 4.0.7 and its community filters: 1193 plug-ins (was 360; 808 in COMMERCIAL builds). Filters that make no sense in an OpenFX host (batch converters, filters that write files, interactive tools) or that upstream disabled as broken are hidden (#64). Many filters' names, parameters and results changed with G'MIC itself.
- The build downloads only the G'MIC 4.0.7 release tarball (sha256-pinned); gmic-community is no longer fetched separately.
- Natron no longer quits on macOS when a G'MIC error happens inside an OpenMP loop, e.g. Smooth [Patch-PCA] on a flat image (#41, #56).
- Bugs in G'MIC 4.0.7 filters are patched in the plug-in until G'MIC releases the fixes (reported upstream): Z-Time, Exposure Fusion Weight Map, Non-Convolution Edge Extraction, Colorize [Photographs], Smooth [Patch-PCA] (#58, part of #53); Jpr Line Edges (#71), Gleam, Heart Tone, Star Tone (gmic-community #301, #302, #305, #77); Softlight and Contrast FFT on images with alpha, Mc Flou, Smooth Sketch, Perspective Streak (gmic-community #306, #307, #309, #311), Linify (gmic #223). Hidden because upstream disabled them as broken: Layer Manipulation, Montage Multi Rows, Halftone Shapes (gmic-community #303–#305), Beauty Retouch (gmic-community #308), Color Region, Prime Surface, Reverse Engineer Color Curve; also hidden: Intarsia, which writes files on every render.
- Smooth [Patch-PCA] no longer returns NaN on flat areas: CImg's eigen fix is applied until G'MIC includes it (#71, #77).

### Behaviour changes (saved projects may render differently)
- **Plug-in version 2.0: Output Layer** (#57). "Output Layer" now counts from the top of the G'MIC image stack (Source = Layer 0, the "Layer -1" input = Layer -1…), as in G'MIC-Qt; before, it counted from the end. Affects filters that return several images (e.g. Channels to Layers now shows R instead of B by default; Fade Layers, Morph Layers, Tiles to Layers show the first result instead of the last) and filters with layer inputs connected. "Merged" and filters with a single result are unchanged. Natron loads nodes saved with version 1.x using 2.0 and lists them in its error log.
- **Layer inputs for more filters** (#22): filters that work on several layers (Stylize, Align Layers, Morph Layers, Pixel Sort, Brushify…) now have Layer -1…-4 inputs; layer inputs are read at the current frame (some filters, e.g. Deconvolve, read them at another frame before) (#40).
- **Nodes with Ignore Alpha on** (#57): the filter now processes all layers at once (multi-input filters work), and `$1`, `$*`… in text parameters are kept as typed. Gray or RGB results whose size changed (frames, rotation, crop…) come out opaque (#66).
- **Premultiplied inputs** (#54) with semi-transparent pixels are unpremultiplied before G'MIC and the result premultiplied back (G'MIC works on straight colours): fewer dark fringes. Pixels with alpha 0 but non-zero colour (additive) lose their colour. Gray or RGB results of another size than the input (frames, rotation, crop…) come out opaque, instead of having their colours multiplied by the misaligned input alpha; this includes uniform upscales, whose semi-transparent edges now stay unpremultiplied (#66).
- **Point parameters**: default positions are no longer mirrored vertically, points are relative to the first input and don't move when zooming or panning (#40); with non-square pixels they now land on the right pixel (e.g. x 192 → 180 at a pixel aspect ratio of 768/720) (#54).
- **Zoomed-out viewer and proxy mode on Natron** (#54) now render at full resolution and downscale, so pixel-sized parameters (radii, sizes) give the same result at every zoom level (slower when zoomed out). Other hosts are unchanged.
- **Filters relying on G'MIC-Qt's preview** (#60) now render: Tupper's Self-Referential Formula, Shear, Array [Random Jumble] by Px, Color Harmonies, Bit-Plane Shuffler, Lavander Binary Map, and others of the same kind.
- **Removable points** (#79): each point that G'MIC-Qt lets you remove now has an "<point> Enabled" checkbox after it. When the checkbox is off, the filter gets the point as removed (nan,nan) and its handle is disabled. Points that G'MIC-Qt shows removed at first are off by default, so saved projects with these filters now render without them, also where the user had moved them: e.g. Colorful Blobs (blobs 4–12), Gradient [Poles] (points #4–#7), Heightfield Warp (light), Symmetrize (point 2), Warp [RBF], Fluffy Cloud, Symmetric 2D Shape, 3D Mesh, ABN Filigrees and Hitomezashi. Turn a checkbox on to get the point back.
- **Colorful Blobs** (#79): the Radius points now set the blobs' radii. Before, they were ignored outside G'MIC-Qt (hot fix until G'MIC fixes it).
- **G'MIC errors** (#39) now fail the render with a message on the node, instead of producing garbage.
- **Output** (#37) is always RGBA, also for an alpha-only input.
- **Locale** (#59): parameter defaults and numbers in G'MIC commands no longer depend on the system locale (under e.g. a French locale, defaults, seeds and sizes could be wrong). The "Fixed (Inplace)" Resize Mode behaves like "Dynamic" (as it always did in practice).

### New
- Renders can be aborted while G'MIC runs (#46, #56).

### Fixes
- Renders where the input and output image bounds differ (e.g. partial render windows) no longer fail with "The image produced by G'MIC has the wrong size" (#40; likely fixes #2, waiting for a retest).
- Wrong parameters or output for many filters: value(), file defaults, choices, colours with alpha, 8-bit flip, gray+alpha (#7, #17, #20, #39).
- Point parameter defaults land inside Source when it is smaller than the project or offset (e.g. Solve Maze failed with "Invalid argument", Spiral Distortion was shifted): when the user connects or changes Source, points still at their default (or reset to it) are set within its region of definition; they don't follow later size changes upstream, e.g. an edited Reformat (#73).
- Crashes: more than 32 output images (#37); a render aborted by the user can no longer overflow the output buffer (#56); Spiral Distortion on inputs smaller than the project (CImg fix applied until G'MIC includes it, #76).

### Build
- Built with C++17 (C++20 for Qt6 builds of Natron) (#33); CI on Linux, macOS (Apple clang, MacPorts) and Windows (MSYS2 UCRT64) (#23, #25, #28, #35, #38).
- CI artifacts are self-contained (bundled libraries) (#62).
