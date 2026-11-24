/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */

#ifndef GMIC_PARAMETER_H
#define GMIC_PARAMETER_H

#include <string>
#include <utility>

namespace gmic_ofx {

// arbitrary bounds :)
constexpr int kMaxLayers = 32;
constexpr int kMaxParams = 256;

// flags of a boolean parameter that enables the point parameter just before it (a G'MIC removable
// point, issue #79): it is not passed to G'MIC, the point is passed as nan,nan when it is off, and
// the OFX glue disables the point (and so its overlay handle) then
constexpr int kParamFlagsPointEnabled = 0x100; // distinct from the ParamType::Text flags (1, 2, 5...)

// flags of a color() parameter (ParamType::Color): with an alpha channel (color(R,G,B,A),
// color(#RRGGBBAA)), an RGBA parameter passing 4 values to G'MIC; without, an RGB parameter passing
// 3, as in G'MIC-Qt
constexpr int kParamFlagsColorRGB = 3;
constexpr int kParamFlagsColorRGBA = 4;

// Layer times (issue #63), defined by the G'MIC plug-in for the filters with layer inputs, used by
// the OFX glue: each layer input (all but Source) has an integer parameter "<clip name> Time
// Offset" (it is fetched at the render time plus this offset), and the first "Layers from Source"
// layer inputs are taken from Source instead of their own clip
constexpr const char* kLayersFromSourceParam = "Layers from Source";
constexpr const char* kTimeOffsetParamSuffix = " Time Offset";

enum class ParamType
{
	Int,
	Bool,
	Select,
	Layer,
	Float,
	Color,
	Point,
	TopicStart,
	TopicEnd,
	Text,
	Separator,
};

enum class DisplayStatus
{
	Normal,
	Disabled,
	Hidden,
};

// an image given to or made by G'MIC: float planar RGBA (RRR... GGG... BBB... AAA...), rows
// top-down, of width x height pixels at (origin_x, origin_y) in pixel coordinates (y up)
struct World
{
	int width = 0;
	int height = 0;
	float* data = nullptr; // not owned, width * height * 4 floats
	int origin_x = 0;
	int origin_y = 0;
};

struct PluginInfo
{
	std::string name;
	std::string identifier;
	std::string category;
	std::string description;
	int major_version = 1;
	int minor_version = 0;
};

struct Parameter
{
	Parameter() = default;
	Parameter(std::string _paramName, std::string _displayName, ParamType _paramType, float _minValue = 0.f, float _maxValue = 1.f,
		float _defaultValue0 = 0.f, float _defaultValue1 = 0.f, float _defaultValue2 = 0.f, float _defaultValue3 = 0.f,
		std::string _text = std::string())
		: paramType(_paramType)
		, paramName(std::move(_paramName))
		, displayName(std::move(_displayName))
		, text(std::move(_text))
		, defaultValue{ _defaultValue0, _defaultValue1, _defaultValue2, _defaultValue3 }
		, minValue(_minValue)
		, maxValue(_maxValue)
	{
		if (displayName.empty()) displayName = paramName;
	}
	ParamType paramType = ParamType::Float;
	std::string paramName;
	std::string displayName;
	std::string text;
	std::string hint; // tooltip (OFX), empty for none
	float defaultValue[4] = {};
	float minValue = 0.f;
	float maxValue = 1.f;
	DisplayStatus displayStatus = DisplayStatus::Normal;
	int flags = 0;
};

// the values of one render: the parameters at the render time, and the images
struct SequenceData
{
	float floatValue[kMaxParams][4] = {};
	std::string textValue[kMaxParams];
	double time = 0;
	// the first frame of the main input's frame range (Output Layer "Frame" in G'MIC), 0 if unknown
	double frameRangeStart = 0;
	World inWorld[kMaxLayers];
	World outWorld;
	bool inputConnected[kMaxLayers] = {};
	// set by pluginProcess: whether the image copied to outWorld had an alpha channel
	bool resultHasAlpha = true;
};

// a plug-in: its G'MIC filter's parameters and the ones the plug-in adds (see pluginSetup)
struct GlobalData
{
	Parameter param[kMaxParams];
	int nofParams = 1;
	int nofInputs = 0; // the ParamType::Layer parameters, i.e. the input clips
	PluginInfo pluginInfo;
	int pluginIndex = 0;

	GlobalData() {
		param[0] = Parameter("input", "Input", ParamType::Layer);
	}
};

} // namespace gmic_ofx

#endif
