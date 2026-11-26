# openfx-gmic #176/#223: the point() parameters of a G'MIC filter have handles in their own colours,
# drawn by the plug-in (SupportExt's OFX::PointsInteract), and no Natron native handle as well.
# Local Color Correction has six points without colour arguments: they take G'MIC-Qt's colormap
# (white, red, green, (64,64,255), cyan, magenta). Anchors 3 to 6 are removable and start removed:
# their Enabled checkboxes are turned on first. Screenshot: $GMIC_TEST_OUT/gmic_point_colours.png.
# A handle is a 5-pixel GL point: its centre has the point's colour (overlays are not affected by the
# viewer's display LUT). A Natron native handle would be drawn in the overlay colour (white) with the
# parameter's name below it: the test looks for that text.
from gmictest import *

PLUGIN = "eu.gmic.LocalColorCorrection"
POINTS = [("Position", (255, 255, 255)), ("Position_2", (255, 0, 0)), ("Position_3", (0, 255, 0)),
          ("Position_4", (64, 64, 255)), ("Position_5", (0, 255, 255)), ("Position_6", (255, 0, 255))]
TOL = 12  # 8-bit levels

node, source = open_gmic(app, PLUGIN)
for name, _ in POINTS[2:]:
    node.getParam(name + "_Enabled").setValue(True)
# Local Color Correction brightens a flat grey Source to near white around anchor 1: darken it there,
# so that the white handle has a background unlike it
node.getParam("Brightness_").setValue(-100.0)
wait_idle(120.0)
pen_away(app, node)
z = zoom(app)
px = 1.0 / z  # one screen pixel in canvas units
print("zoom %r (canvas units per screen pixel %r)" % (z, px))
save_viewer("gmic_point_colours")

failures = []
for name, expected in POINTS:
    x, y = point(node, name)
    centre = pixel(x, y)
    around = [pixel(x + dx * px, y + dy * px) for dx, dy in ((10, 0), (-10, 0), (0, 10), (0, -10))]
    print("%s at (%g, %g): centre %r, expected %r; 10 px around %r" % (name, x, y, centre, expected, around))
    if not colour_close(centre, expected, TOL):
        failures.append("%s: centre %r, expected %r" % (name, centre, expected))
    if any(colour_close(a, expected, TOL) for a in around):
        failures.append("%s: the handle colour %r 10 px from its centre: %r" % (name, expected, around))
    # Natron's native handle: the parameter's name, below the point, from its x
    bg = pixel(x - 30 * px, y - 16 * px)
    text = 0
    for sy in range(8, 26, 3):
        for sx in range(0, 42, 3):
            c = pixel(x + sx * px, y - sy * px)
            if max(abs(a - b) for a, b in zip(c, bg)) > 60:
                text += 1
    print("%s: %d pixels unlike the background %r where Natron's label would be" % (name, text, bg))
    if text > 3:
        failures.append("%s: %d pixels of what looks like Natron's native handle label" % (name, text))

check(not failures, "; ".join(failures))
