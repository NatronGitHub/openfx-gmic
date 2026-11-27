# Helpers for openfx-gmic's Natron GUI tests (Natron --test test_gmic_*.py), on top of Natron's
# natrontest.py (Tests/gui/natrontest.py, which run_tests.sh fetches at the Natron commit under test).
# The tests need GMIC.ofx.bundle and a generator on OFX_PLUGIN_PATH: openfx-misc's Constant (as in CI)
# or Natron's test generator (net.natron.test.Generator, Tests/OfxTestPlugin). A missing generator
# skips a test; a missing G'MIC plug-in fails it.
import os

import NatronGuiTest
from natrontest import *

VIEWER = "Viewer1"
# a generator for Source, in order of preference: Constant, which CI uses, first
SOURCES = ("net.sf.openfx.ConstantPlugin", GENERATOR)
OUT_DIR = os.environ.get("GMIC_TEST_OUT", "/tmp")


def make_source(app):
    """A flat grey (0.5) generator node of the project's format; skip the test if there is none."""
    ids = natron.getPluginIDs()
    for pid in SOURCES:
        if pid in ids:
            node = app.createNode(pid)
            check(node is not None, "createNode(%s) failed" % pid)
            if pid != GENERATOR:  # Constant is black by default; the test generator is grey
                node.getParam("color").set(0.5, 0.5, 0.5, 1.0)
            return node
    NatronGuiTest.skip("no generator plug-in among %s (OFX_PLUGIN_PATH=%s)" % (SOURCES, os.environ.get("OFX_PLUGIN_PATH", "")))


def require_gmic(plugin_id):
    """Fail (not skip) if the G'MIC plug-in plugin_id is not loaded: the plug-in is what is tested."""
    check(plugin_id in natron.getPluginIDs(), "G'MIC plug-in %s not found (OFX_PLUGIN_PATH=%s)"
          % (plugin_id, os.environ.get("OFX_PLUGIN_PATH", "")))


def open_gmic(app, plugin_id, source=None):
    """Create plugin_id fed by a generator, view it, open its panel (which shows its overlay), fit the viewer.

    Returns (node, source)."""
    require_gmic(plugin_id)
    if source is None:
        source = make_source(app)
    node = app.createNode(plugin_id)
    check(node is not None, "createNode(%s) failed" % plugin_id)
    node.connectInput(0, source)
    viewer_node(app).connectInput(0, node)
    app.clearSelection()
    app.selectNode(node, True)
    NatronGuiTest.sendKey("NodeGraph", "Return")  # opens the settings panel: the overlay is shown
    wait_idle(120.0)
    NatronGuiTest.sendKey(VIEWER, "F")  # fit_viewer(), with a longer wait: renders are slow in CI
    wait_idle(120.0)
    return node, source


def zoom(app):
    """Screen pixels per canvas unit in the viewer (PyViewer.getZoom); skip the test without it."""
    v = app.getViewer(VIEWER)
    check(v is not None, "no viewer %s" % VIEWER)
    if not hasattr(v, "getZoom"):
        NatronGuiTest.skip("PyViewer has no getZoom")
    return v.getZoom()


def point(node, name):
    p = node.getParam(name)
    check(p is not None, "no parameter %s" % name)
    return (p.getValue(0), p.getValue(1))


def pen_away(app, node):
    """Move the pen (no button) to a corner of the image, so that no handle is hovered (drawn green)."""
    rod = node.getRegionOfDefinition(1, 0)
    NatronGuiTest.penEvent(VIEWER, "move", rod.left() + 2.0, rod.bottom() + 2.0, "none")
    wait_idle()


def pixel(x, y):
    return NatronGuiTest.getDisplayedPixel(VIEWER, x, y)[:3]


def colour_close(c, expected, tol):
    return all(abs(a - b) <= tol for a, b in zip(c, expected))


def drag_point(x0, y0, x1, y1, steps=10, between=None):
    """Drag from (x0, y0) to (x1, y1) with penEvent; between(i) is called after each motion."""
    NatronGuiTest.penEvent(VIEWER, "move", x0, y0, "none")
    NatronGuiTest.penEvent(VIEWER, "press", x0, y0, "left")
    for i in range(1, steps + 1):
        t = float(i) / steps
        NatronGuiTest.penEvent(VIEWER, "move", x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, "none")
        if between is not None:
            between(i)
    NatronGuiTest.penEvent(VIEWER, "release", x1, y1, "left")


def save_viewer(name):
    """Save what the viewer displays (its framebuffer, overlays included) to OUT_DIR/name.png; returns the path."""
    try:
        from PySide2 import QtWidgets
    except ImportError:
        from PySide6 import QtWidgets
    best = None
    for w in QtWidgets.QApplication.allWidgets():
        if w.isVisible() and hasattr(w, "grabFramebuffer") and w.metaObject().className() == "Natron::ViewerGL":
            if best is None or w.width() * w.height() > best.width() * best.height():
                best = w
    if best is None:
        for w in QtWidgets.QApplication.allWidgets():
            if w.isVisible() and hasattr(w, "grabFramebuffer"):
                if best is None or w.width() * w.height() > best.width() * best.height():
                    best = w
    if best is None:
        print("save_viewer: no viewer widget")
        return None
    path = os.path.join(OUT_DIR, name + ".png")
    best.grabFramebuffer().save(path)
    print("screenshot %s (%s)" % (path, best.metaObject().className()))
    return path
