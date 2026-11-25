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
#include <optional>
#include <string>

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
// Resize Mode (issue #175): Fixed (Inplace) gives Source's rectangle (a result of another size is
// cropped or padded), Dynamic (the default) the result's own size and position, which the region
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

// kOfxActionPurgeCaches: drop the results kept for Output Layer "Frame", of every instance
void pluginPurgeCaches();

// the Output Layer parameter of an instance changed (any reason), to the option outputLayer
void pluginOutputLayerChanged(GmicInstanceData& instanceData, int outputLayer);

// The size and position of the result that a render outputs, in pixels: its offset from the
// top-left corner of the first input (Source), x to the right and y down as in G'MIC (the pos(x,y)
// in the result's name, as G'MIC-Qt's hosts read it), and its size.
struct ResultGeometry
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
};

// Does the output keep the result's size with Resize Mode Fixed and Dynamic (issue #175)? Only on
// a host with multi-resolution: on the others, the output keeps Source's size, the result is
// resized to it as before, and there is no probe (pluginResultGeometry).
bool pluginKeepsResultSize();

// Resize Mode Dynamic (issue #175): the geometry of the result that a render with sequenceData's
// values and input sizes gives, for the region of definition, which OFX needs before the render.
// Runs the filter as the render does (pluginProcess), on a worker thread, on three small opaque
// gradient images (their data is ignored: the region of definition action has no images), and
// predicts the size and position at the real size from an affine fit (see fitAffine); a Source of
// at most 96 pixels is probed at its own size. Each probe's geometries are kept for its command
// line and input sizes (for Output Layer "Frame", every frame's), so that they run again only
// when these change; a lookup of a probe that is running waits for it. Empty if the host has no
// multi-resolution (pluginKeepsResultSize), if the size isn't affine, or if G'MIC failed, gave no
// or an empty result, ran out of memory or past its time limit (logged): the output then keeps
// Source's region of definition.
std::optional<ResultGeometry> pluginResultGeometry(const gmic_ofx::SequenceData& sequenceData, const gmic_ofx::GlobalData& globalData);

#endif
