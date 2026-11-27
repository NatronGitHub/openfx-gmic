# openfx-gmic #176/#223: dragging the handle of a non-burst point (point(...,_burst=0)) sets the parameter
# only at pen up: no render during the drag, one after the release, where the pen was released.
# Poincaré Disk's Shift is a non-burst point (orange); Source is a grid (G'MIC Grid Cartesian on a
# flat generator) so that a shift changes the image: its large dark arcs move. The image is read at a
# 24x24 grid of points off the handle's path: unchanged during the drag, and after the release more
# than 12 levels off at several points (the fine grid averages to grey at the fit zoom, the arcs don't).
from gmictest import *

PLUGIN = "eu.gmic.PoincarDisk"
require_gmic("eu.gmic.GridCartesian")
gen = make_source(app)
grid = app.createNode("eu.gmic.GridCartesian")
grid.connectInput(0, gen)
node, _ = open_gmic(app, PLUGIN, grid)
pen_away(app, node)
x0, y0 = point(node, "Shift")
x1, y1 = x0 + 150.0, y0 + 50.0
px = 1.0 / zoom(app)
probes = grid_probes(grid, 24, [((x0, y0), (x1, y1))], 15 * px)  # Source's region: in the viewer


def probe_pixels():
    return [pixel(*p) for p in probes]


before = probe_pixels()
print("start %r, %d probes" % ((x0, y0), len(probes)))
steps = []


def between(i):
    rendering = NatronGuiTest.isRendering()
    v = point(node, "Shift")
    idle = NatronGuiTest.waitIdle(60.0, 0.2)
    check(idle, "the GUI was not idle 60 s after motion %d" % i)
    steps.append((i, rendering, v, idle, probe_pixels()))


drag_point(x0, y0, x1, y1, steps=8, between=between)
wait_idle(120.0)
for i, rendering, v, idle, pix in steps:
    print("motion %d: rendering right after the motion %r, Shift %r, idle %r, %d probes changed"
          % (i, rendering, v, idle, len(changed(before, pix, 0))))
end = point(node, "Shift")
after = probe_pixels()
moved = changed(before, after, 12)
print("after the release: Shift %r; %d of %d probes more than 12 levels off: %r" % (
    end, len(moved), len(probes), [(probes[k], before[k], after[k]) for k in moved[:10]]))
save_viewer("gmic_point_drag_release")

for i, rendering, v, _, pix in steps:
    check(v == (x0, y0), "Shift changed during the drag (motion %d): %r" % (i, v))
    check(not rendering, "a render ran right after motion %d of a non-burst point" % i)
    check(pix == before, "the image changed during the drag (motion %d) at %d probes" % (i, len(changed(before, pix, 0))))
check_close(end[0], x1, 2 * px, "Shift x after the release")
check_close(end[1], y1, 2 * px, "Shift y after the release")
check(len(moved) >= 5, "the image did not clearly change after the release: %d probes more than 12 levels off"
      % len(moved))
