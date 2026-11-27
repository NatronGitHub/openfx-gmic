# openfx-gmic #176/#223: dragging the handle of a non-burst point (point(...,_burst=0)) sets the parameter
# only at pen up: no render during the drag, one after the release, where the pen was released.
# Poincaré Disk's Shift is a non-burst point (orange); Source is a grid (G'MIC Grid Cartesian on a
# flat generator) so that a shift changes the image.
from gmictest import *

PLUGIN = "eu.gmic.PoincarDisk"
require_gmic("eu.gmic.GridCartesian")
gen = make_source(app)
grid = app.createNode("eu.gmic.GridCartesian")
grid.connectInput(0, gen)
node, _ = open_gmic(app, PLUGIN, grid)
pen_away(app, node)
x0, y0 = point(node, "Shift")
x1, y1 = x0 + 300.0, y0 + 100.0
probes = [(x0 - 400.0, y0 - 250.0), (x0 + 400.0, y0 - 250.0), (x0 - 300.0, y0 + 300.0), (x0 + 100.0, y0 - 350.0)]
before = [pixel(*p) for p in probes]
print("start %r, probes %r" % ((x0, y0), before))
steps = []


def between(i):
    rendering = NatronGuiTest.isRendering()
    v = point(node, "Shift")
    idle = NatronGuiTest.waitIdle(60.0, 0.2)
    check(idle, "the GUI was not idle 60 s after motion %d" % i)
    steps.append((i, rendering, v, idle, [pixel(*p) for p in probes]))


drag_point(x0, y0, x1, y1, steps=8, between=between)
wait_idle(120.0)
for s in steps:
    print("motion %d: rendering right after the motion %r, Shift %r, idle %r, probes %r" % s)
end = point(node, "Shift")
after = [pixel(*p) for p in probes]
print("after the release: Shift %r, probes %r" % (end, after))
save_viewer("gmic_point_drag_release")

px = 1.0 / zoom(app)
for i, rendering, v, _, pix in steps:
    check(v == (x0, y0), "Shift changed during the drag (motion %d): %r" % (i, v))
    check(not rendering, "a render ran right after motion %d of a non-burst point" % i)
    check(pix == before, "the image changed during the drag (motion %d): %r, before %r" % (i, pix, before))
check_close(end[0], x1, 2 * px, "Shift x after the release")
check_close(end[1], y1, 2 * px, "Shift y after the release")
check(after != before, "the image did not change after the release: %r" % (after,))
