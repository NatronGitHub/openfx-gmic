# openfx-gmic #173/#224, headless (NatronRenderer -t): after an edit of a filter whose preview command
# sets a status, the plug-in applies the status: values and, on Natron, visibilities.
# The filter is "User File Status Edit" (as in .github/workflows/user_filters.gmic), loaded from the
# user filter file $HOME/.config/gmic/gmic_ofx.gmic, which run_tests.sh fills from
# headless_gmic_status_visibility.gmic. Amount starts hidden ("_0") and Echo disabled ("_1"); Mode On
# shows both ("_2") and sets Tail to 7; Mode Off goes back to the initial state, the values stay.
import os

from NatronEngine import natron
from natronheadless import Skipped, check, run

PLUGIN = "eu.gmic.UserFileStatusEdit"
SOURCES = ("net.natron.test.Generator", "net.sf.openfx.ConstantPlugin")


def state(node):
    """{name: (visible, enabled, value)} of the filter's parameters."""
    return dict((n, (node.getParam(n).getIsVisible(), node.getParam(n).getIsEnabled(0), node.getParam(n).getValue()))
                for n in ("Mode", "Amount", "Echo", "Tail"))


def main(app):
    ids = natron.getPluginIDs()
    check(PLUGIN in ids, "%s not found: GMIC.ofx.bundle not on OFX_PLUGIN_PATH (%s), or the user filter file "
          "not loaded (HOME=%s)" % (PLUGIN, os.environ.get("OFX_PLUGIN_PATH", ""), os.environ.get("HOME")))
    sources = [p for p in SOURCES if p in ids]
    if not sources:
        raise Skipped("no generator among %r" % (SOURCES,))
    gen = app.createNode(sources[0])
    node = app.createNode(PLUGIN)
    check(node is not None, "createNode(%s) failed" % PLUGIN)
    node.connectInput(0, gen)
    s0 = state(node)
    print("initial (visible, enabled, value): %r" % (s0,))
    check(not s0["Amount"][0], "Amount is visible at first")
    check(not s0["Echo"][1], "Echo is enabled at first")
    node.getParam("Mode").setValue(1)
    s1 = state(node)
    print("Mode On: %r" % (s1,))
    check(s1["Amount"][0] and s1["Echo"][1], "Mode On: Amount hidden or Echo disabled: %r" % (s1,))
    check(s1["Tail"][2] == 7, "Mode On: Tail %r, expected 7 from the status" % (s1["Tail"][2],))
    node.getParam("Mode").setValue(0)
    s2 = state(node)
    print("Mode Off: %r" % (s2,))
    check(not s2["Amount"][0] and not s2["Echo"][1], "Mode Off: not back to the initial state: %r" % (s2,))
    check(s2["Tail"][2] == 7, "Mode Off: Tail %r, expected 7 (values stay)" % (s2["Tail"][2],))


run(main)
