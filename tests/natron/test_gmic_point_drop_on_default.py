# openfx-gmic #176/#223: a point dragged with its handle and dropped exactly on its default stays there.
# A point reset to its default (in the parameters) goes to its G'MIC default within Source, as
# Transform's center does; a drag must not be taken for such a reset.
# Poincaré Disk's Shift (non-burst, default 50 %, 50 %: (480, 270) in the 960x540 project) gets a
# Source twice as large (G'MIC Upscale [Scale2x]): connecting it moves Shift to Source's centre, read
# from Source's region of definition ((960, 0) while that grows downwards from the top edge, #238).
# Shift is then dragged back exactly onto (480, 270) and must stay there. The phases' times are
# printed: the renders are 1920x1080.
# To drop exactly on (480, 270), the last drag is made at zoom 1 around it, from a widget pixel that
# maps to it (pen events have integer widget coordinates; the plug-in rounds the value to the pixel).
import time

from gmictest import *

t0 = time.monotonic()


def phase(what):
    print("%6.1f s: %s" % (time.monotonic() - t0, what))


PLUGIN = "eu.gmic.PoincarDisk"
UPSCALE = "eu.gmic.UpscaleScale2x"
require_gmic(UPSCALE)
gen = make_source(app)
up = app.createNode(UPSCALE)
up.connectInput(0, gen)
node, _ = open_gmic(app, PLUGIN, up)
phase("Poincare Disk on the upscaled Source, viewer fitted")
v = app.getViewer(VIEWER)
if not all(hasattr(v, f) for f in ("setCenter", "setZoom", "canvasToWidget", "widgetToCanvas")):
    NatronGuiTest.skip("PyViewer has no setCenter/setZoom/canvasToWidget/widgetToCanvas")
rod = up.getRegionOfDefinition(1, 0)
d = (FORMAT[1] / 2.0, FORMAT[2] / 2.0)  # the default (50 %, 50 %) in the project
start = point(node, "Shift")
print("Source %r, Shift %r" % ((rod.left(), rod.bottom(), rod.right(), rod.top()), start))
check(abs(rod.right() - rod.left() - 2 * FORMAT[1]) < 1, "%s did not double the width of Source: %r"
      % (UPSCALE, (rod.left(), rod.right())))
check_close(start[0], (rod.left() + rod.right()) / 2, 1, "Shift x at Source's centre")
check_close(start[1], (rod.bottom() + rod.top()) / 2, 1, "Shift y at Source's centre")

# 1: near the default, at the fit zoom
drag_point(start[0], start[1], d[0] + 20.0, d[1] + 20.0, steps=10)
wait_idle(120.0)
near = point(node, "Shift")
phase("first drag")
print("after the first drag: %r" % (near,))
# 2: at zoom 1 around the default, from the widget pixel nearest to it; the widget pixels may fall
# half a canvas pixel off the default (it depends on the widget's size): then move the centre by 0.5


def nearest_pixel():
    """(error, canvas position) of the widget pixel that maps nearest to the default."""
    w = v.canvasToWidget(d[0], d[1])
    best = None
    for wx in (int(w[0]) - 1, int(w[0]), int(w[0]) + 1):
        for wy in (int(w[1]) - 1, int(w[1]), int(w[1]) + 1):
            c = v.widgetToCanvas(wx, wy)
            e = max(abs(c[0] - d[0]), abs(c[1] - d[1]))
            if best is None or e < best[0]:
                best = (e, (c[0], c[1]))
    return best


v.setZoom(1.0)
for ox, oy in ((0.0, 0.0), (0.5, 0.0), (0.0, 0.5), (0.5, 0.5)):
    v.setCenter(d[0] + ox, d[1] + oy)
    wait_idle(120.0)
    best = nearest_pixel()
    if best[0] < 1e-6:
        break
phase("zoom 1 around the default")
print("zoom %r; drop at canvas %r (%g from the default)" % (zoom(app), best[1], best[0]))
check(best[0] < 1e-6, "no widget pixel maps to the default %r: the nearest is at %r" % (d, best[1]))
drag_point(near[0], near[1], best[1][0], best[1][1], steps=5)
wait_idle(120.0)
end = point(node, "Shift")
phase("drop on the default")
print("after the drop on the default: %r" % (end,))
save_viewer("gmic_point_drop_on_default")
check(end != start, "Shift went back to Source's default %r after a drop on its default" % (start,))
check(end == d, "Shift dropped on %r is at %r (expected exactly the default %r)" % (best[1], end, d))
