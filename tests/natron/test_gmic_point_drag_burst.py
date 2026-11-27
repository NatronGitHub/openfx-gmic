# openfx-gmic #176/#223: dragging the handle of a burst point (point(...,_burst=1)) sets the parameter at
# each pen motion, so Natron renders during the drag, and the value ends where the pen is released.
# Local Color Correction's anchor 1 (Position) is a burst point; its Brightness is set to -100 % so that
# moving it changes the image where it goes (a flat grey Source is near white elsewhere).
from gmictest import *

PLUGIN = "eu.gmic.LocalColorCorrection"
node, source = open_gmic(app, PLUGIN)
node.getParam("Brightness_").setValue(-100.0)
wait_idle(120.0)
pen_away(app, node)
x0, y0 = point(node, "Position")
x1, y1 = x0 + 200.0, y0
# off the handle's path, on both sides of the anchor's influence edge, wherever the source puts it
probes = [(x0 + dx, y0 + dy) for dx in (-150.0, 0.0, 100.0, 200.0, 350.0) for dy in (-150.0, -75.0, 75.0)]


def probe_pixels():
    return tuple(pixel(*p) for p in probes)


before = probe_pixels()
print("start %r, probes %r: %r" % ((x0, y0), probes, before))
steps = []


def between(i):
    rendering = NatronGuiTest.isRendering()
    v = point(node, "Position")
    idle = NatronGuiTest.waitIdle(60.0, 0.2)
    check(idle, "the GUI was not idle 60 s after motion %d" % i)
    steps.append((i, rendering, v, idle, probe_pixels()))


drag_point(x0, y0, x1, y1, steps=8, between=between)
wait_idle(120.0)
for s in steps:
    print("motion %d: rendering right after the motion %r, Position %r, idle %r, probes %r" % s)
end = point(node, "Position")
after = probe_pixels()
print("after the release: Position %r, probes %r" % (end, after))
save_viewer("gmic_point_drag_burst")

px = 1.0 / zoom(app)
for i, _, v, _, _ in steps:
    check_close(v[0], x0 + (x1 - x0) * i / 8.0, 2 * px, "Position x after motion %d" % i)
check(any(s[4] != before for s in steps[:-1]),
      "the image did not change during the drag: no render before the release (probes %r)" % ([s[4] for s in steps],))
check_close(end[0], x1, 2 * px, "Position x after the release")
check_close(end[1], y1, 2 * px, "Position y after the release")
