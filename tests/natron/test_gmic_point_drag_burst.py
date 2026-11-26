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
x1, y1 = x0 + 400.0, y0
probe = (x1, y0 - 150.0)  # in the anchor's influence at the end of the drag, off the handle's path
print("start %r, probe %r: %r" % ((x0, y0), probe, pixel(*probe)))
before = pixel(*probe)
steps = []


def between(i):
    rendering = NatronGuiTest.isRendering()
    v = point(node, "Position")
    idle = NatronGuiTest.waitIdle(60.0, 0.2)
    steps.append((i, rendering, v, idle, pixel(*probe)))


drag_point(x0, y0, x1, y1, steps=8, between=between)
wait_idle(120.0)
for s in steps:
    print("motion %d: rendering right after the motion %r, Position %r, idle %r, probe %r" % s)
end = point(node, "Position")
after = pixel(*probe)
print("after the release: Position %r, probe %r" % (end, after))
save_viewer("gmic_point_drag_burst")

px = 1.0 / zoom(app)
moved = [s for s in steps if abs(s[2][0] - x0) > 2 * px]
check(len(moved) >= 6, "the value changed at %d of 8 motions only (burst point): %r" % (len(moved), [s[2] for s in steps]))
for i, _, v, _, _ in steps:
    check_close(v[0], x0 + (x1 - x0) * i / 8.0, 2 * px, "Position x after motion %d" % i)
check(len(set(s[4] for s in steps[:-1])) > 1 or steps[-2][4] != before,
      "the image did not change during the drag: no render before the release (probe %r)" % ([s[4] for s in steps],))
check_close(end[0], x1, 2 * px, "Position x after the release")
check_close(end[1], y1, 2 * px, "Position y after the release")
