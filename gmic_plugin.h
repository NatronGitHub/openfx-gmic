/*
 #
 #  File        : gmic_plugin.h
 #
 #  Description : The G'MIC side of the OpenFX plug-in (gmic_plugin.cpp), used by the
 #                OpenFX Support library glue (gmic_ofx.cpp): the filters, their parameters,
 #                and the G'MIC runs
 #
 #  Licenses    : CeCILL-C or CeCILL v2.0, as gmic_plugin.cpp
 #
*/

#ifndef GMIC_PLUGIN_H
#define GMIC_PLUGIN_H

#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gmic_parameter.h"
#include "gmic_libc.h"

namespace OFX {
class ImageEffect;
}

// G'MIC gets values in [0, 255]: the images and the colour parameters (0 to 1 in OFX) are converted
// to and from that range
constexpr float kGmicValueScale = 255.f;

// the parameters that the clip preferences depend on (see pluginSetup and getClipPreferences)
constexpr const char* kAnimateSeedParam = "Animate Random Seed";
constexpr const char* kOutputLayerParam = "Output Layer";
// renders with the filter's preview command (see pluginProcess); the OFX glue enables the
// parameters that only the preview command uses (Parameter::draftModeOnly) when it is on
constexpr const char* kDraftModeParam = "Preview/Draft Mode";
// the first option of the Output Layer parameter, "Merged", its default (issue #207): the results
// composited into one image by G'MIC's gui_merge_layers, which approximates what GIMP shows of the
// separate layers that G'MIC-Qt's Apply gives it
constexpr int kOutputLayerMerged = 0;
// the last option of the Output Layer parameter, "Frame" (issue #65): see pluginProcess
constexpr int kOutputLayerFrame = 11;
// Resize Mode (issue #175): Fixed (Inplace) gives Source's rectangle (a result of another size is
// placed as with Dynamic, then cropped or padded, #238), Dynamic (the default) the result's own size and position, which the region
// of definition declares (see pluginResultGeometry); Downsample 1/N (2 to 5) processes a
// downsampled input and resizes the result to Source's size
constexpr const char* kResizeModeParam = "Resize Mode";
constexpr int kResizeModeFixed = 0;
constexpr int kResizeModeDynamic = 1;

// The number of plug-ins, one per G'MIC filter (the filters are loaded on first use).
int getNofPlugins();

// The identifier, label, grouping and version of plug-in pluginIndex (the description: see
// pluginSetup).
gmic_ofx::PluginInfo getPluginInfo(int pluginIndex);

// The parameters of plug-in pluginIndex, whose info is pluginInfo (getPluginInfo): the filter's
// own, then the layers, layer times and advanced options that the plug-in adds; also makes the
// description of the plug-in. The ParamType::Layer parameters are the input clips, the
// first one Source.
std::unique_ptr<gmic_ofx::GlobalData> pluginSetup(int pluginIndex, const gmic_ofx::PluginInfo& pluginInfo);

// The G'MIC state of an instance: the options of its G'MIC runs, and its entry in the results
// kept for Output Layer "Frame" (released on destruction).
class GmicInstanceData
{
public:
	GmicInstanceData();
	~GmicInstanceData();
	// options points into errorMessage: neither copied nor moved
	GmicInstanceData(const GmicInstanceData&) = delete;
	GmicInstanceData& operator=(const GmicInstanceData&) = delete;

	// the options of the G'MIC runs (gmic_plugin_call)
	gmic_interface_options& options() { return _options; }

private:
	gmic_interface_options _options;
	std::array<char, 256> _errorMessage; // _options.error_message_buffer
};

// An error on the node: a persistent message (cleared by the next successful render, with
// effect.clearPersistentMessage()) if the host has them, else a modal message titled modalId.
// (Defined in gmic_ofx.cpp.)
void showRenderError(OFX::ImageEffect& effect, const std::string& msg, const std::string& modalId = std::string());
// A warning on the node after a successful render: a persistent warning message (cleared, or
// replaced, by the next render), if the host has them; nothing else, since a modal message would
// interrupt every render. (Defined in gmic_ofx.cpp.)
void showRenderWarning(OFX::ImageEffect& effect, const std::string& msg);

// Does the plug-in pass Source through, without running G'MIC? (A filter with no command, e.g. most
// About pages: EffectData::command is empty.) Its effect is then the identity (isIdentity), and
// render copies Source without calling pluginProcess.
bool pluginPassesSourceThrough(const gmic_ofx::GlobalData& globalData);

// Does the filter accept render scales? Yes if its preview factor ends with "+"
// (EffectData::PreviewFactor::Accurate): its author says that its preview is right at any zoom.
// Natron then renders it at its render scale (proxy mode, zoomed-out viewer); the other filters
// refuse render scales other than 1 there, and Natron renders them at full size (see
// refuseRenderScale in gmic_ofx.cpp, issue #179).
bool pluginSupportsRenderScale(const gmic_ofx::GlobalData& globalData);

// Run G'MIC on sequenceData's input images (float planar RGBA, values up to 255), into its
// outWorld buffer, with the parameter values of sequenceData. Polls effect.abort() while G'MIC
// runs, and reports G'MIC's errors on effect. False if the output is not valid (G'MIC failed,
// or the render was aborted).
bool pluginProcess(gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData, GmicInstanceData& instanceData,
	OFX::ImageEffect& effect);

// What pluginRunStatus did (C++ Core Guidelines F.21: the outputs in one struct)
struct GmicStatusRun
{
	// the filter parameters (indices in GlobalData::param) whose G'MIC argument the status changed
	// (for a removable point: the point if its coordinates changed, its checkbox if it was
	// removed or put back), in order; never a push button
	std::vector<int> changed;
	// the new display status of each parameter whose visibility follows the status
	// (Parameter::followsStatus), as (index in GlobalData::param, status), in order, after a
	// successful run (issue #173): G'MIC-Qt's FilterParametersWidget::setVisibilityStates(). Its
	// initial state for a value that the status gives without a visibility, or for every value if
	// the status gives none; empty (no change) if the filter's visibility never changes, or if the
	// status has visibilities for another number of values than the parameters (logged).
	std::vector<std::pair<int, gmic_ofx::DisplayStatus>> visibility;
	std::string error; // G'MIC's message, if the run failed
	bool cancelled = false; // the user cancelled the run (the host's Cancel)
	// why the run changed nothing, for the user: no preview command, or a status with another
	// number of values than the parameters
	std::string warning;
	// the value that the status gives each float parameter that it sets, as a double (sequenceData
	// has a float: the status's "0.3" stays 0.3)
	std::map<int, double> exact;
};

// G'MIC-Qt's run of the filter's preview command after a change of its parameters, for the status
// that the run leaves (MainWindow.cpp in c-koi/gmic-qt: onPreviewImageAvailable(),
// onGUIDynamismRunDone()): runs the preview command with sequenceData's values, as the run made
// before the main command of a preview-first filter (see pluginProcess), on its input images
// scaled down to G'MIC-Qt's preview size (at most 512 pixels), with G'MIC-Qt's _preview_timeout,
// and sets in sequenceData the values that its status gives (FilterParametersWidget::setValues()
// in G'MIC-Qt), and gives the visibilities (GmicStatusRun::visibility). Its own results are
// discarded. Called from the instance changed action, for the press of a push button (issue
// #174, with the button at 1) and after a user edit of a statusOnEdit filter's parameters (issue
// #173): the host's progress starts at once, and the user's Cancel stops the run. Uses its own G'MIC options, so that a render of the
// same instance may run at the same time. Nothing changes if the filter has no preview ("_none_"),
// if there is no Source image, if the run is cancelled, if the status is empty or has another
// number of values than the parameters (logged), or if G'MIC fails.
GmicStatusRun pluginRunStatus(gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData, OFX::ImageEffect& effect);

// kOfxActionPurgeCaches: drop the results kept for Output Layer "Frame", of every instance
void pluginPurgeCaches();

// the Output Layer parameter of an instance changed (any reason), to the option outputLayer
void pluginOutputLayerChanged(GmicInstanceData& instanceData, int outputLayer);

// The size and position of the result that a render outputs, in pixels: its offset from the
// top-left corner of the first input (Source), x to the right and y down as in G'MIC (the pos(x,y)
// in the result's name, as G'MIC-Qt's hosts read it), and its size; only if the run's results
// are a layout, some of them with a non-zero pos (tiles, speech bubbles). Otherwise the results
// have no position of their own (no pos, G'MIC's default pos(0,0), an input's name: e.g. an
// upscale, a frame, a rotation, a crop) and keep Source's bottom-left corner, the origin of OFX
// images, so that a larger one grows up and to the right (issue #238): atSourceBottom, x and y 0.
struct ResultGeometry
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	bool atSourceBottom = false; // no position of its own: at Source's bottom-left corner
	// its bottom row in pixels (y up), for Source's rows [sourceY1, sourceY2); without the margin
	int bottom(int sourceY1, int sourceY2) const
	{
		assert(!atSourceBottom || (x == 0 && y == 0));
		return atSourceBottom ? sourceY1 : sourceY2 - y - height;
	}
	// a transparent margin around it in the region of definition, in pixels (0 or 1 per side:
	// left, top, right, bottom, G'MIC's orientation): a refined prediction that is not an exact
	// fraction may be 1 pixel off (see pluginResultGeometry), its size on the side away from its
	// corner (right, and bottom, or top if atSourceBottom); a result within it fits the output
	int marginLeft = 0;
	int marginTop = 0;
	int marginRight = 0;
	int marginBottom = 0;
	bool hasMargin() const { return marginLeft || marginTop || marginRight || marginBottom; }
};

// Does the output keep the result's size with Resize Mode Fixed and Dynamic (issue #175)? Only on
// a host with multi-resolution (and without the test aid OFX_GMIC_MULTIRES=0): on the others, the
// output keeps Source's size, the result is resized to it as before, and there is no probe
// (pluginResultGeometry).
bool pluginKeepsResultSize();

// Resize Mode Dynamic (issue #175): the geometry of the result that a render with sequenceData's
// values and input sizes gives, for the region of definition, which OFX needs before the render.
// Runs the filter as the render does (pluginProcess), on a worker thread, on small, thin and
// check probes, opaque gradient images (their data is ignored: the region of definition action
// has no images), and predicts the size and position at the real size from affine fits (fitAxis,
// fitPlane in gmic_plugin.cpp); a fit that fails, a size that isn't affine or a probe that fails
// gets one probe at the real size, exact. A Source no larger than the probes would be (by area,
// see kSmallProbesPixels) is probed at its own size. The probes and predictions are kept for
// their command lines and input sizes (for Output Layer "Frame", per frame), so that they run
// again only when these change; a lookup of a probe that is running waits for it. All within
// kPredictionTimeLimit. Empty if the host has no multi-resolution (pluginKeepsResultSize, which
// the test aid OFX_GMIC_MULTIRES can override), if even
// the probe at the real size failed, ran out of memory or past the time limit (logged; a timeout
// is remembered for a while, see timedOutRecently), or if it gave no or an empty result: the
// output then keeps Source's region of definition, and the render that finds a result of another
// size keeps its geometry as the prediction, for the next queries (issue #228).
std::optional<ResultGeometry> pluginResultGeometry(const gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData);

// The prediction that pluginResultGeometry last made (or a render kept) for these values and input
// sizes, if it is still in its cache (never a new prediction: for the render)
std::optional<ResultGeometry> pluginCachedResultGeometry(const gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData);

#endif
