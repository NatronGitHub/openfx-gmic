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
#include <memory>
#include <string>
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
// the last option of the Output Layer parameter, "Frame" (issue #65): see pluginProcess
constexpr int kOutputLayerFrame = 11;

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
	std::string error; // G'MIC's message, if the run failed
};

// G'MIC-Qt's run of the filter's preview command after a change of its parameters, for the status
// that the run leaves (MainWindow.cpp in c-koi/gmic-qt: onPreviewImageAvailable(),
// onGUIDynamismRunDone()): runs the preview command with sequenceData's values, as the run made
// before the main command of a preview-first filter (see pluginProcess), on its input images
// scaled down to G'MIC-Qt's preview size (at most 512 pixels), with G'MIC-Qt's _preview_timeout,
// and sets in sequenceData the values that its status gives (FilterParametersWidget::setValues()
// in G'MIC-Qt). Its own results are discarded. For the press of a push button (issue #174),
// called from the instance changed action with the button at 1: the host's progress starts at
// once, and the user's Cancel stops the run. Uses its own G'MIC options, so that a render of the
// same instance may run at the same time. Nothing changes if the filter has no preview ("_none_"),
// if there is no Source image, if the run is cancelled, if the status is empty or has another
// number of values than the parameters (logged), or if G'MIC fails.
GmicStatusRun pluginRunStatus(gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData, OFX::ImageEffect& effect);

// kOfxActionPurgeCaches: drop the results kept for Output Layer "Frame", of every instance
void pluginPurgeCaches();

// the Output Layer parameter of an instance changed (any reason), to the option outputLayer
void pluginOutputLayerChanged(GmicInstanceData& instanceData, int outputLayer);

#endif
