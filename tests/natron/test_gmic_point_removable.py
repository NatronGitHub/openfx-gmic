# openfx-gmic #176/#223: a removable point turned off (its Enabled checkbox) has no handle: nothing is
# drawn at its position (its pixel is like the image 10 px away, in any colour), and a drag there
# changes nothing. Local Color Correction's anchor 2
# (Position_2, red) is removable and starts on; anchor 3 (Position_3, green) starts off.
from gmictest import *

PLUGIN = "eu.gmic.LocalColorCorrection"
node, source = open_gmic(app, PLUGIN)
pen_away(app, node)
px = 1.0 / zoom(app)  # one screen pixel in canvas units


def like_background(x, y):
    """Whether the pixel at (x, y) is within 12 levels of the pixels 10 screen pixels around it."""
    c = pixel(x, y)
    around = [pixel(x + dx * px, y + dy * px) for dx, dy in ((10, 0), (-10, 0), (0, 10), (0, -10))]
    print("at (%g, %g): %r; 10 px around: %r" % (x, y, c, around))
    return any(colour_close(c, a, 12) for a in around)


names = ["Position", "Position_2", "Position_3", "Position_4", "Position_5", "Position_6"]
x, y = point(node, "Position_2")
on = pixel(x, y)
g = point(node, "Position_3")
off3 = pixel(*g)
print("Position_2 at (%g, %g) on: %r; Position_3 (off) at %r: %r" % (x, y, on, g, off3))
check(colour_close(on, (255, 0, 0), 12), "Position_2's red handle is not drawn while it is on: %r" % (on,))
check(like_background(*g), "Position_3 is off, but something is drawn at its position: %r" % (off3,))
node.getParam("Position_2_Enabled").setValue(False)
wait_idle(120.0)
off = pixel(x, y)
print("Position_2 off: %r" % (off,))
save_viewer("gmic_point_removable_off")
check(like_background(x, y), "Position_2 is off, but something is still drawn at its position: %r" % (off,))
values = dict((n, point(node, n)) for n in names)
drag_point(x, y, x + 250.0, y + 120.0, steps=10)
wait_idle(120.0)
after = dict((n, point(node, n)) for n in names)
print("after a drag at Position_2's position: %r" % (after,))
check(after == values, "a drag at the position of a point that is off changed %r" %
      ([(n, values[n], after[n]) for n in names if values[n] != after[n]],))
