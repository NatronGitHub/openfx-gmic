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

#include "RFX_Parameter.h"
#include "gmic_libc.h"

namespace OFX {
class ImageEffect;
}

// the parameters that the clip preferences depend on (see pluginSetup and getClipPreferences)
constexpr const char* kAnimateSeedParam = "Animate Random Seed";
constexpr const char* kOutputLayerParam = "Output Layer";
// the last option of the Output Layer parameter, "Frame" (issue #65): see pluginProcess
constexpr int kOutputLayerFrame = 11;

// The number of plug-ins, one per G'MIC filter (the filters are loaded on first use).
int getNofPlugins();

// The identifier, label, grouping and version of plug-in pluginIndex (the description: see
// pluginSetup).
reduxfx::PluginInfo getPluginInfo(int pluginIndex);

// The parameters of plug-in pluginIndex, whose info is pluginInfo (getPluginInfo): the filter's
// own, then the layers, layer times and advanced options that the plug-in adds; also makes the
// description of the plug-in. The PT_LAYER parameters are the input clips, the
// first one Source.
std::unique_ptr<reduxfx::GlobalData> pluginSetup(int pluginIndex, const reduxfx::PluginInfo& pluginInfo);

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

// Run G'MIC on sequenceData's input images (float planar RGBA, values up to 255), into its
// outWorld buffer, with the parameter values of sequenceData. Polls effect.abort() while G'MIC
// runs, and reports G'MIC's errors on effect. False if the output is not valid (G'MIC failed,
// or the render was aborted).
bool pluginProcess(reduxfx::SequenceData& sequenceData, const reduxfx::GlobalData& globalData, GmicInstanceData& instanceData,
	OFX::ImageEffect& effect);

// kOfxActionPurgeCaches: drop the results kept for Output Layer "Frame", of every instance
void pluginPurgeCaches();

// the Output Layer parameter of an instance changed (any reason), to the option outputLayer
void pluginOutputLayerChanged(GmicInstanceData& instanceData, int outputLayer);

#endif
