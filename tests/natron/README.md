# Natron tests

Tests of the plug-in in Natron, for what openfx's hostDemo can't test (#233):

- `test_gmic_point_*.py`: the point handles of the viewer (#176, #223), run with `Natron --test`
  (synthetic pen and key events, pixels read in the viewer): their colours, burst and non-burst drags,
  undo, a drop on the default value, removable points.
- `headless_gmic_status_visibility.py`: the parameter visibilities and values that a preview command's
  status sets after an edit (#173, #224), run with `NatronRenderer -t`, on the filter of
  `headless_gmic_status_visibility.gmic` (given to the test as its user filter file).

`gmictest.py` has the helpers of the GUI tests. They also use Natron's `Tests/gui/natrontest.py` and
`natronheadless.py` (see Natron's `Documentation/source/devel/guitesting.rst`), which `run_tests.sh`
fetches at the Natron commit under test, `NATRON_COMMIT`.

## Running

```sh
NATRON_BIN_DIR=/path/to/natron GMIC_PLUGIN_DIR=/path/to/OFX/Plugins OFX_PLUGIN_PATH=/path/to/openfx-misc \
  NATRON_COMMIT=<Natron commit> tests/natron/run_tests.sh [test.py ...]
```

- `NATRON_BIN_DIR`: the directory of the `Natron` and `NatronRenderer` launchers, e.g. the extracted
  `Natron-RB-2.6-Linux-x86_64.tar.xz` of Natron's `natron_testing` pre-release (remove its bundled
  `Plugins/OFX/Natron/GMIC.ofx.bundle`) or a Natron build with symbolic links to them.
- `GMIC_PLUGIN_DIR`: the directory holding the `GMIC.ofx.bundle` under test.
- `OFX_PLUGIN_PATH`: a generator for the tests' source, openfx-misc's Constant (bundled in the Natron
  archive) or Natron's test generator (`Tests/OfxTestPlugin`); without one, the tests are skipped.
- `NATRON_TEST_WRAPPER`: a command to run Natron and NatronRenderer with (e.g. a debugger script).
- `OUT` (default `./natron-tests`): one directory per test, with its log (`log.txt`), screenshots and
  user filter file; Natron's caches go there too.

Each test prints PASS, FAIL or SKIP. The GUI tests need a display with OpenGL: on Linux, run
`run_tests.sh` under `xvfb-run -a -s "-screen 0 1920x1200x24"` (Mesa llvmpipe works). Without
`NatronRenderer`, the script exits with status 77 (skipped); without a display, the GUI tests are
skipped. A missing G'MIC plug-in fails the tests.

CI runs them weekly and on demand (`gh workflow run ci.yml -f only=natron`), in the `natron` job of
`.github/workflows/ci.yml`.

## Writing tests

- The pen events have integer widget coordinates: for an exact drop, set the viewer's zoom to 1 and
  centre it on the target (`setCenter`), as `test_gmic_point_drop_on_default.py` does.
- `Double2DParam.getDefaultValue()` returns normalised values for the point parameters: compare with
  canvas coordinates computed from the project's format.
- To write an image, select WritePFM (`fr.inria.openfx.WritePFM`) rather than a Write node: for
  `.pfm`, Write picks WriteOIIO, which rotates the rows of PFM images over about 64 MiB
  (https://github.com/NatronGitHub/openfx-io/issues/476). Or keep the outputs small.
