/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */
/*
 #
 #  File        : gmic_ofx.cpp
 #
 #  Description : The OpenFX side of the G'MIC plug-in, on the OpenFX C++ Support library:
 #                one plug-in per G'MIC filter (gmic_plugin.cpp), their descriptors, instances
 #                and actions
 #
 #  Copyright   : Tobias Fleischer / reduxFX Productions (http://www.reduxfx.com), and the
 #                openfx-gmic contributors (this file replaces Helpers/RFX_OFX_Utils.h)
 #
 #  Licenses        : This file is 'dual-licensed', you have to choose one
 #                    of the two licenses below to apply.
 #
 #                    CeCILL-C
 #                    The CeCILL-C license is close to the GNU LGPL.
 #                    ( http://www.cecill.info/licences/Licence_CeCILL-C_V1-en.html )
 #
 #                or  CeCILL v2.0
 #                    The CeCILL license is compatible with the GNU GPL.
 #                    ( http://www.cecill.info/licences/Licence_CeCILL_V2-en.html )
 #
 #  This software is governed either by the CeCILL or the CeCILL-C license
 #  under French law and abiding by the rules of distribution of free software.
 #  You can  use, modify and or redistribute the software under the terms of
 #  the CeCILL or CeCILL-C licenses as circulated by CEA, CNRS and INRIA
 #  at the following URL: "http://www.cecill.info".
 #
 #  The fact that you are presently reading this means that you have had
 #  knowledge of the CeCILL and CeCILL-C licenses and that you accept its terms.
 #
*/

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <locale>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "ofxsImageEffect.h"

#include "gmic_plugin.h"
#include "gmic_log.h"
#include "gmic_parameter.h"
#include "gmic_string.h"

using gmic_ofx::DisplayStatus;
using gmic_ofx::GlobalData;
using gmic_ofx::kLayersFromSourceParam;
using gmic_ofx::kMaxLayers;
using gmic_ofx::kParamFlagsColorRGBA;
using gmic_ofx::kParamFlagsPointEnabled;
using gmic_ofx::kTimeOffsetParamSuffix;
using gmic_ofx::Parameter;
using gmic_ofx::ParamType;
using gmic_ofx::PluginInfo;
using gmic_ofx::SequenceData;
using gmic_ofx::World;

namespace {

constexpr float kMaxChan8 = 255.f;
constexpr float kInvMaxChan8 = 1.f / 255.f;

// hidden string parameter: the point parameters that the plug-in moved within Source, and where to
// (see pointsMoveToSource), so that this survives saving and loading the project
constexpr const char* kPointsFromSourceParam = "PointsFromSource";

// G'MIC 4 has more than 1000 filters: entry points for up to that many plug-ins (see
// pluginMainEntry)
constexpr std::size_t kMaxNofPlugins = 2000;

// The value of f(), or nothing if the host call it makes fails, as the raw C API's callers
// handled a failed status (e.g. the host's default region of definition).
template <class F>
auto hostQuery(F f) -> std::optional<decltype(f())>
{
	try {
		return f();
	} catch (const OFX::Exception::Suite&) {
	} catch (const OFX::Exception::PropertyUnknownToHost&) {
	} catch (const OFX::Exception::PropertyValueIllegalToHost&) {
	}
	return std::nullopt;
}

// f(), best-effort: a failed host call is ignored, as the raw C API's callers ignored the status.
// True if it succeeded.
template <class F>
bool hostTry(F f)
{
	return hostQuery([&f] { f(); return true; }).has_value();
}

bool hostIsNatron()
{
#ifdef OFX_EXTENSIONS_NATRON
	return OFX::getImageEffectHostDescription()->isNatron;
#else
	return false;
#endif
}

// Check the effect descriptor for potentially bad XML in the OFX Plugin cache.
//
// The function that wrote string properties to the OFX plugin cache used to
// escape only que quote (") character. As a result, if one of the string
// properties in the Image Effect descriptor contains special characters,
// the host may be unable to read the plugin cache, giving an "xml error 4"
// (error 4 is XML_ERROR_INVALID_TOKEN).
//
// The plugin label and grouping may not contain any of these bad characters.
// The plugin description may contain characters that do not corrupt the XML,
// because kOfxActionDescribe is usually called at plugin loading.
//
// Version with the bug:
// https://github.com/ofxa/openfx/blob/34ff595a2918e9e4065603d7fc4d500ae9efc421/HostSupport/include/ofxhXml.h
// Version without the bug:
// https://github.com/ofxa/openfx/blob/64ba52fff61894759fbf3942b92659b02b2b17cd/HostSupport/include/ofxhXml.h
//
// With the old version:
// - All characters within U+0001..U+001F and U+007F..U+009F, except \t \b \r (0x09 0x0A 0X0D) are forbidden
//   in label and grouping, because these may be used to build the GUI before loading the plugin.
//   They may appear in the description, although it is not recommended.
// - The '<' and '&' characters generate bad XML in all cases
// - The '>' and '\'' characters are tolerated by the expat parser, although the XML is not valid
// The Support library only checks these strings (in debug builds): this removes the characters.
std::string validateXMLString(const std::string& s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); i++) {
		// The are exactly five characters which must be escaped
		// http://www.w3.org/TR/xml/#syntax
		switch (s[i]) {
		case '\t':
		case '\n':
		case '\r':
		case '"':
		case '\'':
		case '>':
		case '<':
		case '&':
			break;
		default: {
			// UTF-8 (OFX strings are): the C1 controls U+0080..U+009F are the two bytes C2 80..C2 9F;
			// the bytes 0x80..0x9F of other characters (e.g. "’", E2 80 99) must stay
			const unsigned char c = (unsigned char)(s[i]);
			const unsigned char next = (i + 1 < s.size()) ? (unsigned char)(s[i + 1]) : 0;
			if (c == 0xC2 && 0x80 <= next && next <= 0x9F) {
				++i;
			} else if (!((0x01 <= c && c <= 0x1f) || c == 0x7F)) {
				r += c;
			}
		} break;
		}
	}
	return r;
}

// does the plug-in have layer times? (see the LayerTimes)
bool hasLayerTimes(const GlobalData& gd)
{
	return std::any_of(gd.param, gd.param + gd.nofParams, [](const Parameter& p) {
		return p.paramType == ParamType::Int && p.paramName == kLayersFromSourceParam;
	});
}

// does the plug-in have point parameters? (see pointsMoveToSource)
bool hasPoints(const GlobalData& gd)
{
	return std::any_of(gd.param, gd.param + gd.nofParams, [](const Parameter& p) { return p.paramType == ParamType::Point; });
}

bool isEmpty(const OfxRectD& r)
{
	return !(r.x2 > r.x1 && r.y2 > r.y1);
}

// the frame range of a clip, if the host gives it
std::optional<OfxRangeD> frameRangeOf(const OFX::Clip& clip)
{
	return hostQuery([&clip] { return clip.getFrameRange(); });
}

// the default of a point parameter in normalised coordinates, as declared to the host (G'MIC gives
// it in percent of the image, its y going down; OFX normalised y goes up)
OfxPointD pointNormalisedDefault(const Parameter& param)
{
	return { param.defaultValue[0] / 100., 1. - param.defaultValue[1] / 100. };
}

// whether a color() parameter has an alpha channel (see kParamFlagsColorRGBA): declared as an RGBA
// parameter; the others are RGB, as in G'MIC-Qt
bool colorHasAlpha(const Parameter& param)
{
	return param.flags == kParamFlagsColorRGBA;
}

// The increment and number of digits of a float() slider, as G'MIC-Qt derives them from its range
// (FloatParameter::addTo(), CustomDoubleSpinBox): a hundredth of the range, and 5 decimals minus the
// number of integer digits of the bound that has the most, with at least 2 decimals.
struct FloatSliderSteps
{
	double increment;
	int digits;
};

// the number of digits of |value| rounded to an integer, as G'MIC-Qt's
// CustomDoubleSpinBox::integerPartDigitCount() (QString::number(value, 'f', 0) without its '-')
int integerPartDigitCount(float value)
{
	std::array<char, 64> text {}; // FLT_MAX has 39 digits
	const int n = std::snprintf(text.data(), text.size(), "%.0f", std::fabs(static_cast<double>(value)));
	// the guard can't trigger (at most 39 digits, or "inf"/"nan"); it only bounds a truncated count
	return (n > 0) ? std::min(n, static_cast<int>(text.size()) - 1) : 1;
}

// value with 6 significant digits: a float difference divided by 100 carries the float's noise
// (0.1f / 100 is 0.0010000000149...), which Natron's spin box would show at full precision
double roundToSignificantDigits(double value)
{
	std::ostringstream text;
	text.imbue(std::locale::classic());
	text.precision(6);
	text << value;
	return gmic_ofx::stringToDouble(text.str());
}

FloatSliderSteps floatSliderSteps(float minValue, float maxValue)
{
	constexpr int kMaxDigits = 5; // CustomDoubleSpinBox::MAX_DIGITS
	const double increment = roundToSignificantDigits(static_cast<double>(maxValue - minValue) / 100.);
	// an empty, reversed or non-finite (overflowing) range: 0.01, as before
	return { (std::isfinite(increment) && increment > 0.) ? increment : 0.01,
		std::max(2, kMaxDigits - std::max(integerPartDigitCount(minValue), integerPartDigitCount(maxValue))) };
}

// ---------------------------------------------------------------------------------------------
// Pixel conversions. The G'MIC buffers are float planar RGBA (RRR... GGG... BBB... AAA...), rows
// top-down; the OFX images are interleaved RGBA, rows bottom-up (rowBytes may be negative).

// a planar value (in [0, 1] for 8-bit pixels) as a Pixel: 8-bit values are rounded, clamped, and
// NaN gives 0 (a float-to-integer conversion of NaN or of a value out of range is undefined)
template <class Pixel>
Pixel toPixel(float v);

template <>
float toPixel<float>(float v)
{
	return v;
}

template <>
unsigned char toPixel<unsigned char>(float v)
{
	return !(v > 0.f) ? 0 : v >= 1.f ? 255 : static_cast<unsigned char>(v * kMaxChan8 + 0.5f);
}

// the interleaved RGBA image src, of Pixels, to the planar buffer dst (its bounds' size), multiplied
// by factor. Row y of the planar buffer (top-down) is row bounds.y2 - 1 - y of the image.
template <class Pixel>
void convertToPlanar(const OFX::Image& src, float* dst, float factor)
{
	const OfxRectI& b = src.getBounds();
	const int width = b.x2 - b.x1, height = b.y2 - b.y1;
	const size_t plane = static_cast<size_t>(width) * static_cast<size_t>(height);
	for (int y = 0; y < height; y++) {
		const Pixel* in = static_cast<const Pixel*>(src.getPixelAddress(b.x1, b.y2 - 1 - y));
		float* out = dst + static_cast<size_t>(width) * y;
		for (int x = 0; x < width; x++) {
			for (size_t c = 0; c < 4; ++c) {
				out[plane * c + x] = static_cast<float>(*in++) * factor;
			}
		}
	}
}

// The interleaved RGBA image src, of SrcPixels, copied into dst, of DstPixels, where their bounds
// overlap (8-bit values are in [0, 1] as floats); transparent black elsewhere.
template <class SrcPixel, class DstPixel>
void copyImage(const OFX::Image& src, OFX::Image& dst)
{
	const float factor = std::is_same<SrcPixel, unsigned char>::value ? kInvMaxChan8 : 1.f;
	const OfxRectI& s = src.getBounds();
	const OfxRectI& d = dst.getBounds();
	const int x1 = std::max(s.x1, d.x1), x2 = std::min(s.x2, d.x2);
	for (int y = d.y1; y < d.y2; ++y) {
		DstPixel* out = static_cast<DstPixel*>(dst.getPixelAddress(d.x1, y));
		for (int x = d.x1; x < d.x2; ++x) {
			const SrcPixel* in = (y >= s.y1 && y < s.y2 && x >= x1 && x < x2)
				? static_cast<const SrcPixel*>(src.getPixelAddress(x, y)) : nullptr;
			for (size_t c = 0; c < 4; ++c) {
				*out++ = in ? toPixel<DstPixel>(static_cast<float>(in[c]) * factor) : DstPixel();
			}
		}
	}
}

// copyImage for the bit depths of src and dst (8-bit or float)
void copyImage(const OFX::Image& src, OFX::Image& dst)
{
	const bool src8 = src.getPixelDepth() == OFX::eBitDepthUByte, dst8 = dst.getPixelDepth() == OFX::eBitDepthUByte;
	if (src8 && dst8) copyImage<unsigned char, unsigned char>(src, dst);
	else if (src8) copyImage<unsigned char, float>(src, dst);
	else if (dst8) copyImage<float, unsigned char>(src, dst);
	else copyImage<float, float>(src, dst);
}

// the planar buffer src (the bounds' size of dst), multiplied by factor, to the interleaved RGBA
// image dst, of Pixels
template <class Pixel>
void convertFromPlanar(const float* src, OFX::Image& dst, float factor)
{
	const OfxRectI& b = dst.getBounds();
	const int width = b.x2 - b.x1, height = b.y2 - b.y1;
	const size_t plane = static_cast<size_t>(width) * static_cast<size_t>(height);
	for (int y = 0; y < height; y++) {
		const float* in = src + static_cast<size_t>(width) * y;
		Pixel* out = static_cast<Pixel*>(dst.getPixelAddress(b.x1, b.y2 - 1 - y));
		for (int x = 0; x < width; x++) {
			for (size_t c = 0; c < 4; ++c) {
				*out++ = toPixel<Pixel>(in[plane * c + x] * factor);
			}
		}
	}
}

// the factor alpha / maxValue, clamped to [0,1] (NaN gives 0), so that a G'MIC alpha below 0 or
// above maxValue can't produce colours outside [0, alpha]
float alphaFactor(float a, float maxValue)
{
	const float f = a / maxValue;
	return !(f > 0.f) ? 0.f : f > 1.f ? 1.f : f; // also NaN
}

// alpha at or below this fraction of maxValue is treated as transparent when unpremultiplying
constexpr float kUnpremultiplyMinAlpha = 1e-5f;

// Unpremultiply, in place. Pixels with an alpha at or below kUnpremultiplyMinAlpha * maxValue (or NaN)
// keep their colour: dividing by a tiny alpha would give huge values that a filter (e.g. a blur) would
// spread over their neighbours. Premultiplying back multiplies them by that alpha, so a premultiplied
// pixel with alpha 0 and colour > 0 (additive, e.g. a glow) loses its colour in the round trip.
void unpremultiplyPlanar(float* data, int width, int height, float maxValue)
{
	if (!data || width <= 0 || height <= 0) return;
	const size_t n = (size_t)width * (size_t)height;
	float* r = data;
	float* g = data + n;
	float* b = data + 2 * n;
	const float* a = data + 3 * n;
	const float minAlpha = kUnpremultiplyMinAlpha * maxValue;
	for (size_t k = 0; k < n; ++k) {
		if (!(a[k] > minAlpha)) continue;
		const float f = maxValue / a[k];
		r[k] *= f;
		g[k] *= f;
		b[k] *= f;
	}
}

// Premultiply, in place, by alphaFactor.
void premultiplyPlanar(float* data, int width, int height, float maxValue)
{
	if (!data || width <= 0 || height <= 0) return;
	const size_t n = (size_t)width * (size_t)height;
	float* r = data;
	float* g = data + n;
	float* b = data + 2 * n;
	const float* a = data + 3 * n;
	for (size_t k = 0; k < n; ++k) {
		const float f = alphaFactor(a[k], maxValue);
		r[k] *= f;
		g[k] *= f;
		b[k] *= f;
	}
}

// A result without alpha (1 or 3 channels) from a premultiplied main input: with straight colours it
// would be opaque everywhere, with hard bright edges where the input was transparent. Before the
// premultiplication handling, G'MIC got the premultiplied colours, i.e. the input composited over
// black, and the result looked composited over black too. To keep that look, the colours are
// multiplied by the main input's alpha and the output alpha stays opaque. Only gray or RGB results
// that have the processed input's size come here without alpha: the others get an opaque alpha at
// the end of the G'MIC command, since the input alpha wouldn't line up with them (see
// kOpaqueResizedCommand in gmic_plugin.cpp). out is the
// output buffer, in the main input's buffer, both in pixel coordinates with rows top-down.
void multiplyByInputAlphaPlanar(const World& out, const World& in, float maxValue)
{
	if (!out.data || !in.data) return;
	const int x1 = std::max(out.origin_x, in.origin_x), x2 = std::min(out.origin_x + out.width, in.origin_x + in.width);
	const int y1 = std::max(out.origin_y, in.origin_y), y2 = std::min(out.origin_y + out.height, in.origin_y + in.height);
	const size_t dplane = (size_t)out.width * (size_t)out.height, splane = (size_t)in.width * (size_t)in.height;
	float* const dst = out.data;
	const float* const srcA = in.data + 3 * splane;
	for (int y = y1; y < y2; ++y) {
		const size_t drow = (size_t)(out.height - 1 - (y - out.origin_y)) * out.width;
		const size_t srow = (size_t)(in.height - 1 - (y - in.origin_y)) * in.width;
		for (int x = x1; x < x2; ++x) {
			const float f = alphaFactor(srcA[srow + (x - in.origin_x)], maxValue);
			const size_t k = drow + (x - out.origin_x);
			dst[k] *= f;
			dst[dplane + k] *= f;
			dst[2 * dplane + k] *= f;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Points (issues #73 and #79)

// relative tolerance when comparing point values (canonical coordinates, absolute near 0)
constexpr double kPointValueTolerance = 1e-6;

bool pointValueIs(double a, double b)
{
	return std::fabs(a - b) <= kPointValueTolerance * std::max(1., std::fabs(b));
}

bool pointValueIs(const OfxPointD& a, double x, double y)
{
	return pointValueIs(x, a.x) && pointValueIs(y, a.y);
}

std::string pointValueToString(double v)
{
	std::ostringstream ss;
	ss.imbue(std::locale::classic());
	ss.precision(17);
	ss << v;
	return ss.str();
}

// a number written by pointValueToString (other text, e.g. in a hand-edited project, would read
// as 0), refused if it overflows (strtod gives an infinity and sets errno: no std::isfinite, which
// -ffinite-math-only folds to true)
std::optional<double> pointValueParse(const std::string& s)
{
	if (s.empty() || s.find_first_not_of("0123456789+-.eE") != std::string::npos) return std::nullopt;
	errno = 0;
	const double v = gmic_ofx::stringToDouble(s);
	if (errno == ERANGE && std::fabs(v) > 1.) return std::nullopt;
	return v;
}

// Layer times (issue #63): the plug-in may define, for its layer inputs (all but the first one,
// Source), the integer parameters kLayersFromSourceParam and "<clip name>"
// kTimeOffsetParamSuffix (gmic_parameter.h). Layer input i is fetched at the render time plus its
// offset; the first "Layers from Source" layer inputs are then taken from Source instead of their
// own clip, so that a single clip gives a window of frames. Without these parameters, every input
// is fetched at the render time.
struct LayerTimes
{
	int nofInputs = 0;
	std::array<bool, kMaxLayers> fromSource{}; // per input: taken from Source (input 0)
	std::array<int, kMaxLayers> offset{}; // per input, 0 for Source
	// the input whose clip input i is fetched from (the time: see inputTime)
	int clipIndex(int i) const { return fromSource[i] ? 0 : i; }
};

// a parameter of the plug-in, of the type of its GlobalData entry (none for the layers, groups and
// separators)
using ParamPtr = std::variant<std::monostate, OFX::DoubleParam*, OFX::IntParam*, OFX::BooleanParam*, OFX::ChoiceParam*,
	OFX::RGBParam*, OFX::RGBAParam*, OFX::Double2DParam*, OFX::StringParam*>;

// ---------------------------------------------------------------------------------------------
// The effect instance

class GMICEffect final : public OFX::ImageEffect
{
public:
	GMICEffect(OfxImageEffectHandle handle, const GlobalData& globalData);

private:
	void render(const OFX::RenderArguments& args) override;
	bool isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime
#ifdef OFX_EXTENSIONS_NUKE
		, int& view, std::string& plane
#endif
		) override;
	bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments& args, OfxRectD& rod) override;
	void getRegionsOfInterest(const OFX::RegionsOfInterestArguments& args, OFX::RegionOfInterestSetter& rois) override;
	void getFramesNeeded(const OFX::FramesNeededArguments& args, OFX::FramesNeededSetter& frames) override;
	bool getTimeDomain(OfxRangeD& range) override;
	void getClipPreferences(OFX::ClipPreferencesSetter& clipPreferences) override;
	void changedParam(const OFX::InstanceChangedArgs& args, const std::string& paramName) override;
	void changedClip(const OFX::InstanceChangedArgs& args, const std::string& clipName) override;
	void purgeCaches() override { pluginPurgeCaches(); }
	// throws kOfxStatFailed for a render scale that the action must refuse
	void refuseRenderScale(const OfxPointD& renderScale) const;

	// the render fails (a missing image): unless the host aborted it, then the output is discarded
	void failUnlessAborted() const;
	void fetchParamValues(SequenceData& sd, const OfxRectI& rect, double par, double t, const OfxPointD& renderScale) const;
	LayerTimes layerTimesAt(double t) const;
	double inputTime(const LayerTimes& lt, int i, double t) const;
	// the layer input clips' parameter names, in order (the first one is Source)
	const std::string& inputName(int i) const { return _gd.param[_inputParam[i]].paramName; }

	bool pointIsDefault(const Parameter& param, double x, double y) const;
	std::optional<OfxRectD> sourceRoD(double time) const;
	std::optional<OfxRectD> resultRoD(const OfxRectD& sourceRod, double time, const OfxPointD& renderScale) const;
	void pointsFromSourceRead();
	void pointsFromSourceWrite();
	void pointsMoveToSource(double time, int onlyParam);
	bool isPointCheckbox(int i) const;
	bool mayBeEnabled(int i, bool draft) const;
	std::optional<bool> draftModeValue() const;
	void pointUpdateEnabled(int checkbox, bool draft);
	void pointsUpdateEnabled(int onlyParam);
	void draftModeUpdateEnabled();

	const GlobalData& _gd;
	OFX::Clip* _output;
	std::vector<OFX::Clip*> _inputs; // the layer inputs, Source first
	std::vector<int> _inputParam; // the index in _gd.param of each layer input
	std::vector<ParamPtr> _params; // per _gd.param
	OFX::ChoiceParam* _outputLayer;
	OFX::BooleanParam* _animateSeed;
	OFX::BooleanParam* _draftMode;
	OFX::ChoiceParam* _resizeMode;
	// layer times (see layerTimesAt), NULL for plug-ins without them
	OFX::IntParam* _layersFromSource = nullptr;
	int _lastLayersFromSource = 0; // its value at the previous change (see changedParam)
	std::array<OFX::IntParam*, kMaxLayers> _timeOffset{}; // per input, NULL for Source
	// point parameters: the value the plug-in gave each within Source, if any (see
	// pointsMoveToSource), also kept in the hidden parameter kPointsFromSourceParam
	OFX::StringParam* _pointsFromSource = nullptr;
	std::vector<std::optional<OfxPointD>> _pointFromSource; // per _gd.param
	GmicInstanceData _gmic;
};

GMICEffect::GMICEffect(OfxImageEffectHandle handle, const GlobalData& globalData)
	: OFX::ImageEffect(handle)
	, _gd(globalData)
	, _output(fetchClip(kOfxImageEffectOutputClipName))
	, _params((size_t)globalData.nofParams)
	, _outputLayer(fetchChoiceParam(kOutputLayerParam))
	, _animateSeed(fetchBooleanParam(kAnimateSeedParam))
	, _draftMode(fetchBooleanParam(kDraftModeParam))
	, _resizeMode(fetchChoiceParam(kResizeModeParam))
	, _pointFromSource((size_t)globalData.nofParams)
{
	for (int i = 0; i < _gd.nofParams; i++) {
		const Parameter& param = _gd.param[i];
		const std::string& name = param.paramName;
		switch (param.paramType) {
		case ParamType::Layer:
			_inputs.push_back(fetchClip(name));
			_inputParam.push_back(i);
			break;
		case ParamType::Float:
			_params[i] = fetchDoubleParam(name);
			break;
		case ParamType::Int:
			_params[i] = fetchIntParam(name);
			break;
		case ParamType::Bool:
			_params[i] = fetchBooleanParam(name);
			break;
		case ParamType::Select:
			_params[i] = fetchChoiceParam(name);
			break;
		case ParamType::Color:
			if (colorHasAlpha(param)) {
				_params[i] = fetchRGBAParam(name);
			} else {
				_params[i] = fetchRGBParam(name);
			}
			break;
		case ParamType::Point:
			_params[i] = fetchDouble2DParam(name);
			break;
		case ParamType::Text:
			_params[i] = fetchStringParam(name);
			break;
		default:
			break;
		}
	}
	if (hasPoints(_gd)) {
		_pointsFromSource = fetchStringParam(kPointsFromSourceParam);
	}
	if (hasLayerTimes(_gd)) {
		_layersFromSource = fetchIntParam(kLayersFromSourceParam);
		_layersFromSource->getValue(_lastLayersFromSource);
		for (size_t l = 1; l < _inputs.size() && l < kMaxLayers; l++) {
			_timeOffset[l] = fetchIntParam(inputName((int)l) + kTimeOffsetParamSuffix);
		}
	}
	pointsUpdateEnabled(-1);
	draftModeUpdateEnabled();
}

void GMICEffect::failUnlessAborted() const
{
	// no output image, or no Source image (the main input, not optional): the output was not
	// written. If we were interrupted, the failed fetch is fine, just return kOfxStatOK (the
	// host discards an aborted render); otherwise fail, so that the host doesn't use (and
	// cache) an unwritten output (as in the OFX Programming Guide's examples).
	if (!abort()) {
		OFX::throwSuiteStatusException(kOfxStatFailed);
	}
}

// Render scale (issues #49 and #179). G'MIC parameters carry no unit, so pixel-sized ones (radii,
// sizes, amplitudes in pixels) can't be scaled with the render scale, and a zoomed-out viewer or a
// proxy render wouldn't match the full-resolution render.
// - Natron, filters whose preview factor ends with "+" (pluginSupportsRenderScale: G'MIC's "the
//   preview is accurate at any zoom"): every render scale is accepted, and the inputs come at
//   that scale, as in G'MIC-Qt's zoomed preview.
// - Natron, the other filters: the actions fail for render scales other than 1. Natron then marks
//   the effect as not supporting render scale (OfxEffectInstance tries the region of definition at
//   scale 0.5), renders it at full resolution, and downscales the result
//   (renderFullScaleThenDownscale in EffectInstance::renderRoI).
// - Other hosts (Resolve, Nuke, Vegas): every render scale is accepted, as before, with unscaled
//   pixel-sized parameters. They don't fall back to full resolution: a refusal would fail the
//   render.
// Natron is recognized by kNatronOfxHostIsNatron, not by kOfxPropName, which Natron can set to
// another host's name for compatibility. The same rule for every action that has a render scale
// (region of definition, regions of interest, isIdentity, render): throws kOfxStatFailed if the
// action must fail.
void GMICEffect::refuseRenderScale(const OfxPointD& renderScale) const
{
	const bool scaled = renderScale.x != 1. || renderScale.y != 1.;
	if (scaled && hostIsNatron() && !pluginSupportsRenderScale(_gd)) {
		OFX::throwSuiteStatusException(kOfxStatFailed);
	}
}

// dstRect is in pixels; point parameters are in canonical coordinates: pixel x = canonical x *
// renderScale.x / par, pixel y = canonical y * renderScale.y (par: the pixel aspect ratio of the
// image dstRect belongs to)
void GMICEffect::fetchParamValues(SequenceData& sd, const OfxRectI& dstRect, double par, double t, const OfxPointD& renderScale) const
{
	for (int i = 0; i < _gd.nofParams; i++) {
		const Parameter& param = _gd.param[i];
		float* const value = sd.floatValue[i];
		switch (param.paramType) {
		case ParamType::Float:
			value[0] = static_cast<float>(std::get<OFX::DoubleParam*>(_params[i])->getValueAtTime(t));
			break;
		case ParamType::Int:
			value[0] = static_cast<float>(std::get<OFX::IntParam*>(_params[i])->getValueAtTime(t));
			break;
		case ParamType::Color:
			if (colorHasAlpha(param)) {
				const OfxRGBAColourD c = std::get<OFX::RGBAParam*>(_params[i])->getValueAtTime(t);
				value[0] = static_cast<float>(c.r);
				value[1] = static_cast<float>(c.g);
				value[2] = static_cast<float>(c.b);
				value[3] = static_cast<float>(c.a);
			} else {
				const OfxRGBColourD c = std::get<OFX::RGBParam*>(_params[i])->getValueAtTime(t);
				value[0] = static_cast<float>(c.r);
				value[1] = static_cast<float>(c.g);
				value[2] = static_cast<float>(c.b);
				value[3] = 1.f; // not passed to G'MIC
			}
			break;
		case ParamType::Point: {
			// percent of the image, from its top-left corner (G'MIC's y goes down); an empty dstRect
			// (no input, empty output) counts as 1 pixel, so as not to divide by 0
			const OfxPointD p = std::get<OFX::Double2DParam*>(_params[i])->getValueAtTime(t);
			const int w = std::max(1, dstRect.x2 - dstRect.x1), h = std::max(1, dstRect.y2 - dstRect.y1);
			value[0] = static_cast<float>((p.x * renderScale.x / par - dstRect.x1) * 100. / w);
			value[1] = 100.f - static_cast<float>((p.y * renderScale.y - dstRect.y1) * 100. / h);
		} break;
		case ParamType::Select:
			value[0] = static_cast<float>(std::get<OFX::ChoiceParam*>(_params[i])->getValueAtTime(t));
			break;
		case ParamType::Bool:
			value[0] = std::get<OFX::BooleanParam*>(_params[i])->getValueAtTime(t) ? 1.f : 0.f;
			break;
		case ParamType::Text:
			std::get<OFX::StringParam*>(_params[i])->getValueAtTime(t, sd.textValue[i]);
			break;
		default:
			break;
		}
	}
}

// the layer times of an instance at time t: offsets 0 and every input from its own clip if the
// plug-in has no layer times
LayerTimes GMICEffect::layerTimesAt(double t) const
{
	LayerTimes lt;
	lt.nofInputs = std::min(_gd.nofInputs, kMaxLayers);
	int fromSource = 0;
	if (_layersFromSource) {
		_layersFromSource->getValueAtTime(t, fromSource);
	}
	int k = 0; // rank of the layer input with a time offset
	for (int i = 1; i < lt.nofInputs; i++) {
		if (_timeOffset[i]) {
			_timeOffset[i]->getValueAtTime(t, lt.offset[i]);
			lt.fromSource[i] = (++k <= fromSource);
		}
	}
	return lt;
}

// The time input i is fetched at (getFramesNeeded, getRegionsOfInterest and render): the render
// time plus its offset, clamped to the frame range of the clip it comes from (its own clip or
// Source), e.g. the first frames of a window before the start of Source. With an offset of 0, the
// render time as before the layer times (the host decides what an input outside its range gives).
double GMICEffect::inputTime(const LayerTimes& lt, int i, double t) const
{
	if (lt.offset[i] == 0) return t;
	double ft = t + lt.offset[i];
	const OFX::Clip* clip = _inputs[lt.clipIndex(i)];
	if (const std::optional<OfxRangeD> range = frameRangeOf(*clip);
		range && range->min <= range->max) {
		ft = std::max(range->min, std::min(ft, range->max));
	}
	return ft;
}

// tells the host what region we are capable of filling: Source's (the host's default if it can't
// be read), or with Resize Mode Dynamic, the result's (see resultRoD)
bool GMICEffect::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments& args, OfxRectD& rod)
{
	refuseRenderScale(args.renderScale);
	OFX::Clip* source = _inputs[0];
	const std::optional<OfxRectD> sourceRod = hostQuery([&] { return source->getRegionOfDefinition(args.time); });
	if (!sourceRod) return false;
	rod = *sourceRod;
	// (a host without multi-resolution keeps the output at Source's size: no probe, see
	// pluginKeepsResultSize)
	if (pluginKeepsResultSize() && !pluginPassesSourceThrough(_gd) && source->isConnected() && !isEmpty(*sourceRod) &&
		_resizeMode->getValueAtTime(args.time) == kResizeModeDynamic) {
		try {
			if (const std::optional<OfxRectD> result = resultRoD(*sourceRod, args.time, args.renderScale)) {
				rod = *result;
			}
		} catch (const std::bad_alloc&) {
			gmicLog("G'MIC: %s: not enough memory to measure the result's size: the output keeps Source's size\n",
			        _gd.pluginInfo.name.c_str());
		}
	}
	return true;
}

// the pixels of a canonical rectangle, as hosts compute the bounds of images (Natron's
// RectD::toPixelEnclosing, hostDemo's canonicalToPixelEnclosing)
OfxRectI toPixelEnclosing(const OfxRectD& r, const OfxPointD& renderScale, double par)
{
	return OfxRectI{ (int)std::floor(r.x1 * renderScale.x / par), (int)std::floor(r.y1 * renderScale.y),
		(int)std::ceil(r.x2 * renderScale.x / par), (int)std::ceil(r.y2 * renderScale.y) };
}

// The canonical coordinate of a pixel edge p (x: par is the pixel aspect ratio and scale the
// render scale x; y: par 1 and the render scale y), such that the host's rounding of the region of
// definition to pixels (toPixelEnclosing: floor(c * scale / par) for the low edges, ceil for the
// high ones, as Natron's RectD::toPixelEnclosing) gives p back. Exact when it does (e.g. an edge at
// 0 stays 0, which Natron's connection test compares exactly); else moved inwards by 1e-6 pixel,
// the same in both axes.
double canonicalEdge(int p, double par, double scale, bool high)
{
	const double c = p * par / scale;
	const double back = high ? std::ceil(c * scale / par) : std::floor(c * scale / par);
	if (back == p) return c;
	constexpr double kInwards = 1e-6; // pixel
	return (high ? p - kInwards : p + kInwards) * par / scale;
}

// Resize Mode Dynamic (issue #175): the region of definition of the result, from its geometry
// (pluginResultGeometry, which predicts it from small probes of the inputs' regions of
// definition, at the render time, with the layer times). Empty if it is Source's (a result of
// Source's size at its top-left corner: Source's region of definition, exactly as before), or if
// it is unknown (G'MIC failed, or a size that isn't affine).
// At the action's render scale (issue #179: the filters whose preview factor ends with "+" in
// Natron, every filter elsewhere): the inputs' pixels and the point percentages at that scale,
// as the render gets them, so the probes' command lines and cache keys are the render's at that
// scale. (Natron's other filters refuse a render scale here, before any probe.) The pixel rectangle is the result's, its top-left corner at
// Source's moved by the result's pos(x,y), in canonical coordinates (see canonicalEdge).
std::optional<OfxRectD> GMICEffect::resultRoD(const OfxRectD& sourceRod, double time, const OfxPointD& renderScale) const
{
	// the values of the render at this time (see render), the inputs without images
	SequenceData values;
	SequenceData* const sd = &values;
	for (int i = 0; i < _gd.nofParams; i++) {
		sd->textValue[i] = _gd.param[i].text;
		std::copy(_gd.param[i].defaultValue, _gd.param[i].defaultValue + 4, sd->floatValue[i]);
	}
	sd->time = time;
	if (const std::optional<OfxRangeD> range = frameRangeOf(*_inputs[0]);
		range && range->min <= range->max && std::fabs(range->min) < 1e9) {
		sd->frameRangeStart = range->min;
	}
	double par = hostQuery([this] { return _inputs[0]->getPixelAspectRatio(); }).value_or(1.);
	if (!(par > 0.)) par = 1.;
	const OfxRectI sourcePixels = toPixelEnclosing(sourceRod, renderScale, par);
	const LayerTimes lt = layerTimesAt(time);
	for (int i = 0; i < lt.nofInputs; i++) {
		const int c = lt.clipIndex(i);
		OFX::Clip* clip = _inputs[c];
		if (!clip->isConnected()) break;
		const double t = inputTime(lt, i, time);
		const std::optional<OfxRectD> inRod = i == 0 ? std::optional<OfxRectD>(sourceRod) : hostQuery([&] { return clip->getRegionOfDefinition(t); });
		if (!inRod || isEmpty(*inRod)) break;
		const OfxRectI r = toPixelEnclosing(*inRod, renderScale, par);
		World& in = sd->inWorld[i];
		in.name = inputName(c);
		in.origin_x = r.x1;
		in.origin_y = r.y1;
		in.width = r.x2 - r.x1;
		in.height = r.y2 - r.y1;
		sd->inputConnected[i] = true;
	}
	fetchParamValues(values, sourcePixels, par, time, renderScale);
	const std::optional<ResultGeometry> g = pluginResultGeometry(values, _gd);
	const int sw = sourcePixels.x2 - sourcePixels.x1, sh = sourcePixels.y2 - sourcePixels.y1;
	if (!g || (!g->hasMargin() && g->x == 0 && g->y == 0 && g->width == sw && g->height == sh)) return std::nullopt;
	// pixels (y up): the result's top edge is Source's, moved down by pos y; then its margin
	const int rx1 = sourcePixels.x1 + g->x, ry2 = sourcePixels.y2 - g->y;
	const int x1 = rx1 - g->marginLeft, y2 = ry2 + g->marginTop;
	const int x2 = rx1 + g->width + g->marginRight, y1 = ry2 - g->height - g->marginBottom;
	return OfxRectD{ canonicalEdge(x1, par, renderScale.x, false), canonicalEdge(y1, 1., renderScale.y, false),
		canonicalEdge(x2, par, renderScale.x, true), canonicalEdge(y2, 1., renderScale.y, true) };
}

// tells the host how much of the input we need to fill the given window
void GMICEffect::getRegionsOfInterest(const OFX::RegionsOfInterestArguments& args, OFX::RegionOfInterestSetter& rois)
{
	refuseRenderScale(args.renderScale);
	// G'MIC processes whole images: ask for the full region of definition of every input, not just
	// the part under the render window (the default), which gave a cropped input when the viewer
	// shows part of the image (issue #2)
	// With layer times, the region of definition of each frame fetched (see render): the union of
	// them for Source, whose frames may also be layers.
	const LayerTimes lt = layerTimesAt(args.time);
	const int n = std::min((int)_inputs.size(), kMaxLayers);
	std::vector<std::optional<OfxRectD>> roi((size_t)n);
	for (int i = 0; i < n; ++i) {
		const int c = lt.clipIndex(i);
		OFX::Clip* clip = _inputs[c];
		const double t = inputTime(lt, i, args.time);
		const std::optional<OfxRectD> rod = hostQuery([&] { return clip->getRegionOfDefinition(t); });
		if (!rod) continue;
		if (!roi[c] || isEmpty(*roi[c])) {
			roi[c] = *rod;
		} else if (!isEmpty(*rod)) { // (an empty rectangle, e.g. at 0,0, would extend the union)
			roi[c]->x1 = std::min(roi[c]->x1, rod->x1);
			roi[c]->y1 = std::min(roi[c]->y1, rod->y1);
			roi[c]->x2 = std::max(roi[c]->x2, rod->x2);
			roi[c]->y2 = std::max(roi[c]->y2, rod->y2);
		}
	}
	for (int c = 0; c < n; ++c) {
		if (lt.fromSource[c]) {
			// a layer clip not used (its layer comes from Source): an empty region, so that the host
			// doesn't render it (e.g. Natron skips the inputs with an empty RoI)
			roi[c] = OfxRectD{ 0., 0., 0., 0. };
		}
		if (roi[c]) {
			rois.setRegionOfInterest(*_inputs[c], *roi[c]);
		}
	}
}

// the frames needed from each input clip (only with layer times, which declare temporal clip
// access): every frame fetched by render, i.e. the render time for Source, and the time plus the
// offset of each layer input from its own clip or from Source
void GMICEffect::getFramesNeeded(const OFX::FramesNeededArguments& args, OFX::FramesNeededSetter& frames)
{
	if (!_layersFromSource) {
		return; // the host's default
	}
	const LayerTimes lt = layerTimesAt(args.time);
	const int n = std::min((int)_inputs.size(), kMaxLayers);
	std::vector<std::vector<double>> times((size_t)n);
	for (int i = 0; i < n; ++i) {
		std::vector<double>& f = times[lt.clipIndex(i)];
		const double t = inputTime(lt, i, args.time);
		if (std::find(f.begin(), f.end(), t) == f.end()) {
			f.push_back(t);
		}
	}
	for (int c = 0; c < n; ++c) {
		// a clip whose frames all come from Source: the render time (the default; an empty range
		// can't be given), it isn't fetched and its RoI is empty (getRegionsOfInterest)
		if (times[c].empty()) times[c].push_back(args.time);
		std::sort(times[c].begin(), times[c].end());
		for (const double t : times[c]) {
			frames.setFramesNeeded(*_inputs[c], OfxRangeD{ t, t });
		}
	}
}

// the frame range of Source (the host's default if it can't be read); only called in the general
// context
bool GMICEffect::getTimeDomain(OfxRangeD& range)
{
	const OFX::Clip* source = _inputs[0];
	const std::optional<OfxRangeD> sourceRange = frameRangeOf(*source);
	if (!sourceRange) return false;
	range = *sourceRange;
	return true;
}

// Premultiplication (issue #43). G'MIC filters work on straight (unpremultiplied) colours: premultiplied
// inputs are unpremultiplied before G'MIC. The output is declared premultiplied (and the result is
// premultiplied back) only if the main input is premultiplied. Otherwise it is declared
// unpremultiplied: the result has straight colours, and for an input flagged opaque whose alpha isn't 1
// (e.g. a Read with "Output Premult" set to Opaque) multiplying by that alpha would darken the image;
// declaring the output opaque would be wrong as soon as the result has an alpha other than 1, which
// the plug-in can't know before rendering.
void GMICEffect::getClipPreferences(OFX::ClipPreferencesSetter& clipPreferences)
{
	// the output clip only supports RGBA (render writes 4 channels), even for an Alpha input.
	// The output depth is not set: the plug-in doesn't support multiple clip depths, so the host
	// gives all clips the same depth (and a spec-conformant host refuses the write).
	clipPreferences.setClipComponents(*_output, OFX::ePixelComponentRGBA);
	const OFX::Clip* source = _inputs[0];
	const bool premultiplied = hostQuery([source] { return source->getPreMultiplication(); }) == OFX::eImagePreMultiplied;
	clipPreferences.setOutputPremultiplication(premultiplied ? OFX::eImagePreMultiplied : OFX::eImageUnPreMultiplied);
	// Output Layer "Frame": the output changes with the frame, also for still inputs. Not set for
	// the layer times: a host keys the images by time when an input varies (inputs fetched at
	// other frames), and a frame varying output would re-render still inputs every frame.
	if (_animateSeed->getValue() || _outputLayer->getValue() == kOutputLayerFrame) {
		clipPreferences.setOutputFrameVarying(true);
	}
}

// are the settings of the effect performing an identity operation
bool GMICEffect::isIdentity(const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime
#ifdef OFX_EXTENSIONS_NUKE
	, int& /*view*/, std::string& /*plane*/
#endif
	)
{
	// a filter with no command (most About pages): Source, as G'MIC-Qt leaves the image unchanged
	if (pluginPassesSourceThrough(_gd)) {
		identityClip = _inputs[0];
		identityTime = args.time;
		return true;
	}
	refuseRenderScale(args.renderScale);
	return false;
}

// relative to Source (the parameters below are set from here only: parameters cannot be set in
// render, and should only be set in the instance changed action or in interacts)
void GMICEffect::changedParam(const OFX::InstanceChangedArgs& args, const std::string& paramName)
{
	// Layers from Source raised by the user: give the layers it adds whose offset is still 0 the
	// previous frames (Layer -k at -k), a frame window instead of copies of the current frame. Not
	// when it is lowered or changed otherwise (undo, redo, reset, project load, a script): its value is
	// tracked on every change. Natron reports some internal edits as user edits.
	if (_layersFromSource && paramName == kLayersFromSourceParam) {
		const int previous = _lastLayersFromSource;
		_layersFromSource->getValue(_lastLayersFromSource);
		if (args.reason == OFX::eChangeUserEdit && _lastLayersFromSource > previous) {
			const LayerTimes lt = layerTimesAt(args.time);
			int k = 0;
			for (int i = 1; i < lt.nofInputs; i++) {
				if (!_timeOffset[i]) continue;
				++k;
				if (k > previous && lt.fromSource[i] && lt.offset[i] == 0) {
					_timeOffset[i]->setValue(-k);
				}
			}
		}
	}

	// Preview/Draft Mode changed in any way: the parameters of the preview command only
	if (paramName == kDraftModeParam) {
		draftModeUpdateEnabled();
	}

	// Output Layer changed in any way: no longer Frame, the results kept for it can go
	if (paramName == kOutputLayerParam) {
		pluginOutputLayerChanged(_gmic, _outputLayer->getValue());
	}

	// a removable point's checkbox, changed in any way (user edit, undo, a script...), and a point
	// changed by the user
	for (int i = 1; i < _gd.nofParams; i++) {
		const Parameter& param = _gd.param[i];
		if (param.paramName != paramName) continue;
		if (isPointCheckbox(i)) {
			pointsUpdateEnabled(i);
		}
		break;
	}

	// we are only interested in user edits below
	if (args.reason != OFX::eChangeUserEdit) return;

	// a point reset to its default by the user: within Source too (pointsMoveToSource sets
	// parameters: the instance changed actions this causes are plug-in edits, ignored here)
	for (int i = 0; i < _gd.nofParams; i++) {
		const Parameter& param = _gd.param[i];
		if (param.paramType != ParamType::Point || param.paramName != paramName) continue;
		OFX::Double2DParam* point = std::get<OFX::Double2DParam*>(_params[i]);
		const std::optional<OfxPointD> v = hostQuery([point] { return point->getValue(); });
		if (!v) break;
		if (pointIsDefault(param, v->x, v->y)) {
			pointsMoveToSource(args.time, i);
		} else {
			// moved elsewhere by the user: forget the value the plug-in gave it
			pointsFromSourceRead();
			if (_pointFromSource[i] && !pointValueIs(*_pointFromSource[i], v->x, v->y)) {
				_pointFromSource[i].reset();
				pointsFromSourceWrite();
			}
		}
		break;
	}
}

// Source connected or changed by the user: point defaults within it
void GMICEffect::changedClip(const OFX::InstanceChangedArgs& args, const std::string& clipName)
{
	if (args.reason == OFX::eChangeUserEdit && clipName == inputName(0)) {
		pointsMoveToSource(args.time, -1);
	}
}

// Point defaults (issue #73). They are declared in normalised coordinates, which the host converts
// with the project size, and the render converts points to percent of the first input, Source
// (fetchParamValues), so that they match the overlay handles: with a Source smaller than the project,
// or offset, the defaults fell outside it. So, when the user connects or changes the Source clip
// (instance changed action: parameters cannot be set in render, and should only be set in the
// instance changed action or in interacts), each point parameter still at its default is moved to its G'MIC default within
// Source's region of definition, as openfx-misc's Transform does with its center; so is a point that
// the user resets to its default (or sets to it). Points don't follow Source when its size changes
// otherwise (e.g. a Reformat upstream edited), as with openfx-misc. A point is at its default if it
// is not animated and has the default converted with the project size and offset, with the size only
// (Natron ignores the offset), or unconverted (hosts without a project size); or the value the
// plug-in gave it at the previous change of Source, kept in kPointsFromSourceParam. Points the user
// moved elsewhere stay where they are; an undo that brings a point back to its default is a user edit
// too, and moves it again.
bool GMICEffect::pointIsDefault(const Parameter& param, double x, double y) const
{
	const OfxPointD n = pointNormalisedDefault(param);
	const std::optional<OfxPointD> size = hostQuery([this] { return getProjectSize(); });
	if (!size || !(size->x > 0. && size->y > 0.)) {
		// no project size: the host can't have converted the default (a user point at these
		// coordinates, a fraction of a pixel, would be taken for the default otherwise)
		return pointValueIs(x, n.x) && pointValueIs(y, n.y);
	}
	const OfxPointD offset = hostQuery([this] { return getProjectOffset(); }).value_or(OfxPointD{ 0., 0. });
	return (pointValueIs(x, n.x * size->x + offset.x) && pointValueIs(y, n.y * size->y + offset.y)) ||
		(pointValueIs(x, n.x * size->x) && pointValueIs(y, n.y * size->y));
}

// Source's region of definition, if it is connected, finite and not empty
std::optional<OfxRectD> GMICEffect::sourceRoD(double time) const
{
	OFX::Clip* source = _inputs[0];
	if (!source->isConnected()) return std::nullopt;
	const std::optional<OfxRectD> rod = hostQuery([&] { return source->getRegionOfDefinition(time); });
	if (rod && rod->x2 > rod->x1 && rod->y2 > rod->y1 && rod->x1 > kOfxFlagInfiniteMin && rod->y1 > kOfxFlagInfiniteMin &&
		rod->x2 < kOfxFlagInfiniteMax && rod->y2 < kOfxFlagInfiniteMax) {
		return rod;
	}
	return std::nullopt;
}

// kPointsFromSourceParam: "<parameter name>=<x>,<y>;" for each point the plug-in moved
void GMICEffect::pointsFromSourceRead()
{
	std::fill(_pointFromSource.begin(), _pointFromSource.end(), std::nullopt);
	if (!_pointsFromSource) return;
	const std::optional<std::string> value = hostQuery([this] { return _pointsFromSource->getValue(); });
	if (!value) return;
	const std::string& str = *value;
	std::string::size_type b = 0;
	while (b < str.size()) {
		std::string::size_type e = str.find(';', b);
		if (e == std::string::npos) e = str.size();
		const std::string item = str.substr(b, e - b);
		b = e + 1;
		const std::string::size_type eq = item.rfind('='), comma = item.rfind(',');
		if (eq == std::string::npos || comma == std::string::npos || comma < eq) continue;
		const std::string name = item.substr(0, eq);
		for (int i = 0; i < _gd.nofParams; i++) {
			if (_gd.param[i].paramType == ParamType::Point && _gd.param[i].paramName == name) {
				const std::optional<double> px = pointValueParse(item.substr(eq + 1, comma - eq - 1));
				const std::optional<double> py = pointValueParse(item.substr(comma + 1));
				if (px && py) {
					_pointFromSource[i] = OfxPointD{ *px, *py };
				}
			}
		}
	}
}

void GMICEffect::pointsFromSourceWrite()
{
	if (!_pointsFromSource) return;
	std::string str;
	for (int i = 0; i < _gd.nofParams; i++) {
		if (_gd.param[i].paramType != ParamType::Point || !_pointFromSource[i]) continue;
		str += _gd.param[i].paramName + "=" + pointValueToString(_pointFromSource[i]->x) + "," +
			pointValueToString(_pointFromSource[i]->y) + ";";
	}
	if (hostQuery([this] { return _pointsFromSource->getValue(); }) == str) return;
	_pointsFromSource->setValue(str);
}

// Move the point parameters at their default (and, if onlyParam >= 0, only that one) within Source,
// in one edit block. Called from the instance changed action only.
void GMICEffect::pointsMoveToSource(double time, int onlyParam)
{
	const std::optional<OfxRectD> rod = sourceRoD(time);
	if (!rod) return;
	pointsFromSourceRead(); // the parameter may have been loaded, or undone
	// one undo step, begun at the first change only; best-effort: without an undo block if the host
	// refuses it
	EditBlock editBlock(*this, "Point defaults on Source", EditBlock::eBeginOnChange, EditBlock::eHostErrorsIgnore);
	bool moved = false;
	for (int i = 0; i < _gd.nofParams; i++) {
		const Parameter& param = _gd.param[i];
		if (param.paramType != ParamType::Point || (onlyParam >= 0 && i != onlyParam)) continue;
		OFX::Double2DParam* point = std::get<OFX::Double2DParam*>(_params[i]);
		const std::optional<unsigned int> keys = hostQuery([point] { return point->getNumKeys(); });
		if (keys && *keys > 0) continue; // (not all hosts count keys)
		const std::optional<OfxPointD> v = hostQuery([point] { return point->getValue(); });
		if (!v) continue;
		const bool atDefault = pointIsDefault(param, v->x, v->y) ||
			(_pointFromSource[i] && pointValueIs(*_pointFromSource[i], v->x, v->y));
		if (!atDefault) continue;
		const OfxPointD n = pointNormalisedDefault(param);
		const OfxPointD s = { rod->x1 + n.x * (rod->x2 - rod->x1), rod->y1 + n.y * (rod->y2 - rod->y1) };
		_pointFromSource[i] = s;
		editBlock.open();
		moved = true;
		if (!pointValueIs(s, v->x, v->y)) point->setValue(s.x, s.y);
	}
	if (moved) pointsFromSourceWrite();
}

// param i is the checkbox of a removable point (issue #79): the boolean parameter right after the
// point, see kParamFlagsPointEnabled
bool GMICEffect::isPointCheckbox(int i) const
{
	return i > 0 && i < _gd.nofParams && _gd.param[i].paramType == ParamType::Bool && _gd.param[i].flags == kParamFlagsPointEnabled &&
		_gd.param[i - 1].paramType == ParamType::Point;
}

// whether a parameter may be enabled at all: not disabled in its descriptor
// (DisplayStatus::Disabled), and, for the parameters of the preview command only
// (Parameter::draftModeOnly), Preview/Draft Mode on (draft, its value)
bool GMICEffect::mayBeEnabled(int i, bool draft) const
{
	const Parameter& param = _gd.param[i];
	return param.displayStatus != DisplayStatus::Disabled && (!param.draftModeOnly || draft);
}

// Preview/Draft Mode, at the current time (none if the host fails)
std::optional<bool> GMICEffect::draftModeValue() const
{
	return hostQuery([this] { return _draftMode->getValue(); });
}

// The point of the removable point checkbox i is enabled, with its overlay handle, only when the
// checkbox is on (and mayBeEnabled).
void GMICEffect::pointUpdateEnabled(int checkbox, bool draft)
{
	OFX::BooleanParam* const box = std::get<OFX::BooleanParam*>(_params[checkbox]);
	const std::optional<bool> on = hostQuery([box] { return box->getValue(); });
	if (!on) return;
	OFX::Double2DParam* const point = std::get<OFX::Double2DParam*>(_params[checkbox - 1]);
	const bool enabled = *on && mayBeEnabled(checkbox - 1, draft);
	hostTry([point, enabled] { point->setEnabled(enabled); }); // (also called from createInstance)
}

// The removable points (see pointUpdateEnabled). onlyParam >= 0: only for that checkbox.
void GMICEffect::pointsUpdateEnabled(int onlyParam)
{
	const std::optional<bool> draft = draftModeValue();
	if (!draft) return;
	for (int i = 1; i < _gd.nofParams; i++) {
		if (isPointCheckbox(i) && (onlyParam < 0 || i == onlyParam)) pointUpdateEnabled(i, *draft);
	}
}

// The parameters that only the filter's preview command uses (G'MIC's split preview, issue #184,
// Parameter::draftModeOnly) are enabled only when Preview/Draft Mode is on (and mayBeEnabled).
// Their values are passed to G'MIC either way, so renders don't depend on whether they are
// enabled. Updated when the instance is created and when Preview/Draft Mode changes, from its
// value at that time: an animated Preview/Draft Mode, or one driven by an expression, doesn't
// update them when the time or the expression's inputs change.
void GMICEffect::draftModeUpdateEnabled()
{
	const std::optional<bool> draft = draftModeValue();
	if (!draft) return;
	for (int i = 1; i < _gd.nofParams; i++) {
		if (!_gd.param[i].draftModeOnly) continue;
		if (isPointCheckbox(i + 1)) { // a removable point: also its checkbox
			pointUpdateEnabled(i + 1, *draft);
			continue;
		}
		const bool enabled = mayBeEnabled(i, *draft);
		// (markSplitPreviewParams: a choice or a point)
		if (OFX::ChoiceParam* const* choice = std::get_if<OFX::ChoiceParam*>(&_params[i])) {
			OFX::ChoiceParam* const c = *choice;
			hostTry([c, enabled] { c->setEnabled(enabled); });
		} else if (OFX::Double2DParam* const* point = std::get_if<OFX::Double2DParam*>(&_params[i])) {
			OFX::Double2DParam* const pt = *point;
			hostTry([pt, enabled] { pt->setEnabled(enabled); });
		}
	}
}

// the image is the RGBA float or byte image that the plug-in declares
void checkImageFormat(const OFX::Image& image)
{
	if (image.getPixelComponents() != OFX::ePixelComponentRGBA ||
		(image.getPixelDepth() != OFX::eBitDepthUByte && image.getPixelDepth() != OFX::eBitDepthFloat)) {
		OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
	}
}

// the process code that the host sees
void GMICEffect::render(const OFX::RenderArguments& args)
{
	refuseRenderScale(args.renderScale);
	// the render window is not used: tiles are not supported, and the whole output image is rendered
	const double time = args.time;
	const std::unique_ptr<OFX::Image> outputImg(_output->fetchImage(time));
	if (!outputImg) {
		failUnlessAborted();
		return;
	}
	checkImageFormat(*outputImg);
	// a filter with no command, for hosts that render an identity anyway: Source as it is, without
	// G'MIC (nor the unpremultiplication and conversion for it)
	if (pluginPassesSourceThrough(_gd)) {
		const std::unique_ptr<OFX::Image> sourceImg(_inputs[0]->isConnected() ? _inputs[0]->fetchImage(time) : nullptr);
		if (!sourceImg) {
			failUnlessAborted();
			return;
		}
		checkImageFormat(*sourceImg);
		copyImage(*sourceImg, *outputImg);
		clearPersistentMessage();
		return;
	}
	const OfxRectI dstRect = outputImg->getBounds();
	const float scale = kGmicValueScale;

	// the values of this render, their defaults for the parameters without a value
	SequenceData sd;
	for (int i = 0; i < _gd.nofParams; i++) {
		sd.textValue[i] = _gd.param[i].text;
		std::copy(_gd.param[i].defaultValue, _gd.param[i].defaultValue + 4, sd.floatValue[i]);
	}
	sd.time = time;
	// Output Layer "Frame" counts frames from the start of Source (0 if none, or infinite)
	{
		const OFX::Clip* source = _inputs[0];
		const std::optional<OfxRangeD> range = frameRangeOf(*source);
		if (range && range->min <= range->max && std::fabs(range->min) < 1e9) {
			sd.frameRangeStart = range->min;
		}
	}

	// get the input image(s); the parameters are fetched afterwards, since point parameters are
	// relative to the first input's rectangle (the image G'MIC processes), not the output's (their
	// defaults are moved within Source when it is connected, see pointsMoveToSource)
	OfxRectI firstSrcRect = dstRect;
	double firstSrcPAR = outputImg->getPixelAspectRatio();
	bool firstSrcPremultiplied = false;
	const LayerTimes layerTimes = layerTimesAt(time);
	const int nofInputs = std::min((int)_inputs.size(), kMaxLayers);
	// the host images and their float planar copies, given to G'MIC (released on return)
	std::vector<std::unique_ptr<OFX::Image>> sourceImg((size_t)nofInputs);
	std::vector<std::vector<float>> sourceBuffer((size_t)nofInputs);
	for (int i = 0; i < nofInputs; i++) {
		// layer times: the input may be another frame, of its clip or of Source (see
		// getFramesNeeded). Point parameters stay relative to Source at the render time.
		const int c = layerTimes.clipIndex(i);
		OFX::Clip* clip = _inputs[c];
		const std::string& clipName = inputName(c);
		const double inTime = inputTime(layerTimes, i, time);
		sd.inputConnected[i] = false;
		const bool connected = clip->isConnected();
		if (connected) {
			sd.inputConnected[i] = true;
			sourceImg[i].reset(clip->fetchImage(inTime));
		}
		if (!sourceImg[i] && connected && layerTimes.offset[i] != 0 && !abort()) {
			// No image for a layer at another frame (within the clip's frame range): fail rather
			// than drop it and the following layers (as for an unconnected layer, the stack stops
			// at the first missing one), or skip it and shift the next frames into its place:
			// the filters that use frames depend on their order.
			char msg[512];
			snprintf(msg, sizeof(msg), "%s: no image from %s at frame %g", inputName(i).c_str(), clipName.c_str(), inTime);
			showRenderError(*this, msg);
			OFX::throwSuiteStatusException(kOfxStatFailed);
		}
		if (!sourceImg[i]) {
			sd.inputConnected[i] = false;
			if (i == 0) {
				// the render fails: say why, unless the host aborted it
				if (!abort()) {
					char msg[512];
					snprintf(msg, sizeof(msg), "No image from %s at frame %g", clipName.c_str(), inTime);
					showRenderError(*this, msg);
				}
				failUnlessAborted();
				return;
			}
			sd.inWorld[i].data = nullptr;
			break;
		}
		const OFX::Image& img = *sourceImg[i];
		checkImageFormat(img);
		const OfxRectI srcRect = img.getBounds();
		if (i == 0 && srcRect.x2 > srcRect.x1 && srcRect.y2 > srcRect.y1) {
			firstSrcRect = srcRect; // not an empty rectangle (point conversion divides by its size)
			firstSrcPAR = img.getPixelAspectRatio();
		}
		World& in = sd.inWorld[i];
		in.name = clipName;
		in.origin_x = srcRect.x1;
		in.origin_y = srcRect.y1;
		in.width = srcRect.x2 - srcRect.x1;
		in.height = srcRect.y2 - srcRect.y1;
		std::vector<float>& buffer = sourceBuffer[i];
		buffer.resize((size_t)in.width * (size_t)in.height * 4);
		in.data = buffer.data();
		if (img.getPixelDepth() == OFX::eBitDepthUByte) {
			convertToPlanar<unsigned char>(img, buffer.data(), kInvMaxChan8 * scale);
		} else {
			convertToPlanar<float>(img, buffer.data(), scale);
		}
		// G'MIC works on straight colours: unpremultiply premultiplied inputs (the converted buffer
		// is float planar for both 8-bit and float images, so this also covers 8-bit inputs)
		if (img.getPreMultiplication() == OFX::eImagePreMultiplied) {
			if (i == 0) firstSrcPremultiplied = true;
			unpremultiplyPlanar(buffer.data(), in.width, in.height, scale);
		}
	}

	if (!(firstSrcPAR > 0.)) firstSrcPAR = 1.;
	fetchParamValues(sd, firstSrcRect, firstSrcPAR, time, args.renderScale);

	World& out = sd.outWorld;
	out.origin_x = dstRect.x1;
	out.origin_y = dstRect.y1;
	out.width = dstRect.x2 - dstRect.x1;
	out.height = dstRect.y2 - dstRect.y1;
	std::vector<float> outBuffer((size_t)out.width * (size_t)out.height * 4);
	out.data = outBuffer.data();

	// here we call the actual processing function of the plugin! If G'MIC failed, the output
	// buffer holds no valid image: fail the render instead of converting garbage
	if (!pluginProcess(sd, _gd, _gmic, *this)) {
		OFX::throwSuiteStatusException(kOfxStatFailed);
	}

	// the G'MIC result has straight colours, converted here before the conversion to the output depth
	if (firstSrcPremultiplied && !sd.resultHasAlpha) {
		// no alpha in the result: keep the look of the input composited over black (see
		// multiplyByInputAlphaPlanar)
		multiplyByInputAlphaPlanar(out, sd.inWorld[0], scale);
	}
	// premultiply if the output image is premultiplied: the host's value, normally the one
	// declared in getClipPreferences
	if (outputImg->getPreMultiplication() == OFX::eImagePreMultiplied) {
		premultiplyPlanar(outBuffer.data(), out.width, out.height, scale);
	}

	if (outputImg->getPixelDepth() == OFX::eBitDepthUByte) {
		convertFromPlanar<unsigned char>(outBuffer.data(), *outputImg, 1.f / scale);
	} else {
		convertFromPlanar<float>(outBuffer.data(), *outputImg, 1.f / scale);
	}
}

// ---------------------------------------------------------------------------------------------
// The plug-ins

class GMICPluginFactory final : public OFX::PluginFactory
{
public:
	GMICPluginFactory(int pluginIndex, OfxPluginEntryPoint* mainEntry);

	void load() override;
	void unload() override { _globalData.reset(); }
	void describe(OFX::ImageEffectDescriptor& desc) override;
	void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context) override;
	OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) override;
	const std::string& getID() const override { return _info.identifier; }
	const std::string& getUID() const override { return _uid; }
	unsigned int getMajorVersion() const override { return (unsigned int)_info.major_version; }
	unsigned int getMinorVersion() const override { return (unsigned int)_info.minor_version; }
	OfxPluginEntryPoint* getMainEntry() override { return _mainEntry; }

private:
	// the parameters, from kOfxActionLoad to kOfxActionUnload
	const GlobalData& globalData() const;

	const int _pluginIndex;
	PluginInfo _info;
	std::string _uid; // the key of the Support library's plug-in map
	OfxPluginEntryPoint* const _mainEntry;
	std::unique_ptr<GlobalData> _globalData;
};

GMICPluginFactory::GMICPluginFactory(int pluginIndex, OfxPluginEntryPoint* mainEntry)
	: _pluginIndex(pluginIndex)
	, _info(getPluginInfo(pluginIndex))
	// the key of the Support library's plug-in map: any string unique to the plug-in
	, _uid(_info.identifier + "@" + std::to_string(_info.major_version) + "." + std::to_string(_info.minor_version))
	, _mainEntry(mainEntry)
{
}

void GMICPluginFactory::load()
{
	_globalData = pluginSetup(_pluginIndex, _info);
}

const GlobalData& GMICPluginFactory::globalData() const
{
	if (!_globalData) {
		OFX::throwSuiteStatusException(kOfxStatFailed); // not loaded
	}
	return *_globalData;
}

// the plugin's description routine
void GMICPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
	const GlobalData& gd = globalData();
	// We can render both fields in a fielded images in one hit if there is no animation
	// So set the flag that allows us to do this
	desc.setRenderTwiceAlways(false);
	// say we cannot support multiple pixel depths
	desc.setSupportsMultipleClipDepths(false);
	// nor multiple clip pixel aspect ratios (the default, set explicitly)
	desc.setSupportsMultipleClipPARs(false);
	// set the bit depths the plugin can handle
	desc.addSupportedBitDepth(OFX::eBitDepthFloat);
	desc.addSupportedBitDepth(OFX::eBitDepthUByte);

	// set some labels and the group it belongs to
	const PluginInfo& info = gd.pluginInfo;
	if (hostIsNatron()) {
		desc.setLabel(info.name);
		desc.setPluginGrouping(info.category);
		desc.setPluginDescription(info.description);
	} else {
		// workaround for bugs in the OpenFX plugin cache, e.g. in Nuke
		desc.setLabel(validateXMLString(info.name));
		desc.setPluginGrouping(validateXMLString(info.category));
		desc.setPluginDescription(validateXMLString(info.description));
	}

	// define the contexts we can be used in
	desc.addSupportedContext(OFX::eContextFilter);
	desc.addSupportedContext(OFX::eContextGeneral);

	desc.setRenderThreadSafety(OFX::eRenderInstanceSafe);
	desc.setSupportsTiles(false);
	desc.setHostFrameThreading(false);
	// (issue #175) inputs of any size and position, and an output with the result's own (Resize
	// Mode Dynamic): Natron refuses to connect inputs whose regions of definition differ, or don't
	// start at 0,0, to an effect without multi-resolution (checkCanConnectNoMultiRes)
	desc.setSupportsMultiResolution(true);

	// the parameters the clip preferences depend on, by name (they are defined in describeInContext).
	// Not with addClipPreferencesSlaveParam in describeInContext: hosts copy this descriptor's
	// properties into the in-context ones (HostSupport does), where it would list them twice.
	OFX::PropertySet& props = desc.getPropertySet();
	props.propSetString(kOfxImageEffectPropClipPreferencesSlaveParam, kAnimateSeedParam, 0);
	props.propSetString(kOfxImageEffectPropClipPreferencesSlaveParam, kOutputLayerParam, 1);

	if (hasLayerTimes(gd)) {
		// layer times: inputs at other frames (getFramesNeeded)
		desc.setTemporalClipAccess(true);
	}
}

// describe the plugin in context
void GMICPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum /*context*/)
{
	const GlobalData& gd = globalData();
	const bool layerTimes = hasLayerTimes(gd);

	// define the single output clip in both contexts
	{
		OFX::ClipDescriptor* output = desc.defineClip(kOfxImageEffectOutputClipName);
		// set the component types we can handle on our output
		output->addSupportedComponent(OFX::ePixelComponentRGBA);
		// clip properties only (thread safety, frame threading, multi-resolution and multiple clip
		// PARs/depths are effect properties, set in describe())
		output->setSupportsTiles(false);
	}

	// the last parameter defined: a separator is a layout hint on it
	OFX::ParamDescriptor* previous = nullptr;
	// unused (always empty), kept so that the projects that saved it still load
	{
		OFX::CustomParamDescriptor* sequenceData = desc.defineCustomParam("SequenceData");
		sequenceData->setIsSecret(true);
		previous = sequenceData;
	}
	if (hasPoints(gd)) {
		OFX::StringParamDescriptor* points = desc.defineStringParam(kPointsFromSourceParam);
		points->setIsSecret(true);
		points->setIsPersistent(true);
		points->setAnimates(false);
		points->setEvaluateOnChange(false);
		points->setDefault("");
		previous = points;
	}

	OFX::GroupParamDescriptor* group = nullptr;
	for (int i = 0; i < gd.nofParams; i++) {
		const Parameter& param = gd.param[i];
		const std::string& name = param.paramName;
		OFX::ParamDescriptor* paramDesc = nullptr;
		switch (param.paramType) {
		case ParamType::Float: {
			OFX::DoubleParamDescriptor* p = desc.defineDoubleParam(name);
			if (param.flags > 0) p->setDoubleType(OFX::eDoubleTypeScale);
			p->setDefault(param.defaultValue[0]);
			p->setRange(param.minValue, param.maxValue);
			p->setDisplayRange(param.minValue, param.maxValue);
			const FloatSliderSteps steps = floatSliderSteps(param.minValue, param.maxValue);
			p->setIncrement(steps.increment);
			p->setDigits(steps.digits);
			paramDesc = p;
		} break;
		case ParamType::Color:
			// Natron loads the RGB colour of the projects saved with the RGBA one (of plug-in versions
			// that declared all colours RGBA) and drops the alpha, which G'MIC never got
			if (colorHasAlpha(param)) {
				OFX::RGBAParamDescriptor* p = desc.defineRGBAParam(name);
				p->setDefault(param.defaultValue[0], param.defaultValue[1], param.defaultValue[2], param.defaultValue[3]);
				paramDesc = p;
			} else {
				OFX::RGBParamDescriptor* p = desc.defineRGBParam(name);
				p->setDefault(param.defaultValue[0], param.defaultValue[1], param.defaultValue[2]);
				paramDesc = p;
			}
			break;
		case ParamType::Text: {
			std::string txt = param.text;
			// as G'MIC-Qt, the parser already unescaped the defaults of text() and value(), and
			// file()/folder() defaults are taken as they are: decoding "\n" again would turn the
			// "\\n" of text("C:\\new") into a newline. Only labels (notes) need it.
			if (param.flags == 4) {
				gmic_ofx::strReplace(txt, "\\r", "\r");
				gmic_ofx::strReplace(txt, "\\n", "\n");
			}
			// force the param to be multiline if there are newlines in the default value
			int textType = param.flags;
			if (textType == 0 && txt.find_first_of("\r\n") != std::string::npos) {
				textType = 1;
			}
			if (textType == 4) {
				txt = gmic_ofx::strTrim(gmic_ofx::strRemoveXmlTags(txt, true), " \t\r\n'\"");
			}
			OFX::StringParamDescriptor* p = desc.defineStringParam(name);
			if (textType >= 0 && textType <= 4) {
				p->setDefault(txt); // (filein has no default)
			}
			switch (textType) {
			case 0: // single-line
				p->setStringType(OFX::eStringTypeSingleLine);
				break;
			case 1: // multi-line
				p->setStringType(OFX::eStringTypeMultiLine);
				break;
			case 2: // file / fileout
				p->setStringType(OFX::eStringTypeFilePath);
				p->setFilePathExists(false);
				break;
			case 3: // folder
				p->setStringType(OFX::eStringTypeDirectoryPath);
				break;
			case 4: // label
				p->setStringType(OFX::eStringTypeLabel);
				p->setHint(txt); // for long labels
				break;
			case 5: // filein
				p->setStringType(OFX::eStringTypeFilePath);
				p->setFilePathExists(true);
				break;
			default:
				break;
			}
			paramDesc = p;
		} break;
		case ParamType::Int: {
			OFX::IntParamDescriptor* p = desc.defineIntParam(name);
			p->setDefault((int)param.defaultValue[0]);
			p->setRange((int)param.minValue, (int)param.maxValue);
			p->setDisplayRange((int)param.minValue, (int)param.maxValue);
			if (param.flags == 1) {
				p->setAnimates(false);
			}
			paramDesc = p;
		} break;
		case ParamType::Point: {
			// point parameter is in % (percent) in gmic: the default is normalised
			const OfxPointD n = pointNormalisedDefault(param);
			OFX::Double2DParamDescriptor* p = desc.defineDouble2DParam(name);
			p->setDefault(n.x, n.y);
			p->setDoubleType(OFX::eDoubleTypeXYAbsolute);
			p->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
			p->setUseHostNativeOverlayHandle(true);
			paramDesc = p;
		} break;
		case ParamType::Bool: {
			OFX::BooleanParamDescriptor* p = desc.defineBooleanParam(name);
			p->setDefault((int)param.defaultValue[0] != 0);
			p->setAnimates(false);
			paramDesc = p;
		} break;
		case ParamType::Select: {
			const std::vector<std::string> choices = gmic_ofx::strSplit(param.text, '|');
			OFX::ChoiceParamDescriptor* p = desc.defineChoiceParam(name);
			const int nofOptions = std::min((int)param.maxValue + 1, (int)choices.size());
			for (int k = 0; k < nofOptions; k++) {
				p->appendOption(choices[k]);
			}
			p->setDefault((int)std::min(param.maxValue, param.defaultValue[0]));
			p->setAnimates(false);
			paramDesc = p;
		} break;
		case ParamType::Separator:
#ifdef OFX_EXTENSIONS_NUKE
			if (previous) {
				previous->setLayoutHint(OFX::eLayoutHintDivider);
			}
#endif
			continue;
		case ParamType::Layer: {
			OFX::ClipDescriptor* clip = desc.defineClip(name);
			if (i > 0) {
				clip->setOptional(true);
			}
			clip->addSupportedComponent(OFX::ePixelComponentRGBA);
			clip->setSupportsTiles(false);
			if (layerTimes) {
				// layer times: frames other than the render time (see getFramesNeeded)
				clip->setTemporalClipAccess(true);
			}
			previous = nullptr; // (a clip has no layout hint)
			continue;
		}
		case ParamType::TopicStart: {
			OFX::GroupParamDescriptor* p = desc.defineGroupParam(name);
			p->setOpen(param.flags <= 0);
			group = p;
			paramDesc = p;
		} break;
		case ParamType::TopicEnd:
			group = nullptr;
			continue;
		default:
			continue;
		}

		if (param.displayStatus == DisplayStatus::Disabled) {
			paramDesc->setEnabled(false);
		} else if (param.displayStatus == DisplayStatus::Hidden) {
			paramDesc->setIsSecret(true);
		}
		paramDesc->setScriptName(name);
		paramDesc->setLabel(param.displayName);
		if (!param.hint.empty()) {
			paramDesc->setHint(param.hint);
		}
		if (group && param.paramType != ParamType::TopicStart) {
			paramDesc->setParent(*group);
		}
		previous = paramDesc;
	}
}

OFX::ImageEffect* GMICPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
	// owned by the Support library, which deletes it on kOfxActionDestroyInstance
	return new GMICEffect(handle, globalData());
}

// The factories, one per G'MIC filter, made on the first OfxGetNumberOfPlugins (see
// registerGmicPlugins) and kept until the binary is unloaded: the host may keep the
// identifiers, and the Support library the factories.
std::vector<std::unique_ptr<GMICPluginFactory>>& factories()
{
	static std::vector<std::unique_ptr<GMICPluginFactory>> f;
	return f;
}

// The Support library calls a plug-in's actions through its main entry point, by the key of its
// factory: one entry point per plug-in index (this idea was originally found in Alexandre
// Gauthier-Foichat's OFX G'MIC plugin).
template <std::size_t N>
OfxStatus pluginMainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	return OFX::Private::mainEntryStr(action, handle, inArgs, outArgs, factories()[N]->getUID().c_str());
}

template <std::size_t... N>
constexpr std::array<OfxPluginEntryPoint*, sizeof...(N)> makeMainEntries(std::index_sequence<N...>)
{
	return { { &pluginMainEntry<N>... } };
}

constexpr std::array<OfxPluginEntryPoint*, kMaxNofPlugins> kMainEntries = makeMainEntries(std::make_index_sequence<kMaxNofPlugins>());

// Lazy registration (lazyRegistration, below): the Support library calls this once, from the first
// OfxGetNumberOfPlugins or OfxGetPlugin, and keeps the list. The filters are loaded then, not in a
// static constructor at dlopen (see pluginGlobalData). If it throws, the host gets no plug-ins (the
// Support library catches the exception, but only logs it in debug builds): say why (gmicLog: on stderr or in the log file).
void registerGmicPlugins(OFX::PluginFactoryArray& ids)
{
	try {
		// no entry point beyond kMainEntries
		const size_t nofFilters = static_cast<size_t>(std::max(0, getNofPlugins()));
		const size_t n = std::min(nofFilters, kMaxNofPlugins);
		if (nofFilters > n) {
			gmicLog("openfx-gmic: Warning: %zu G'MIC filters, only the first %zu are plug-ins (kMaxNofPlugins in gmic_ofx.cpp)\n",
				nofFilters, n);
		}
		factories().reserve(n);
		ids.reserve(ids.size() + n);
		for (size_t i = 0; i < n; i++) {
			factories().push_back(std::make_unique<GMICPluginFactory>(static_cast<int>(i), kMainEntries[i]));
			ids.push_back(factories().back().get());
		}
	} catch (const std::exception& e) {
		gmicLog("openfx-gmic: plug-in registration failed: %s\n", e.what());
		throw; // the Support library gives the host no plug-ins
	} catch (...) {
		gmicLog("openfx-gmic: plug-in registration failed: unknown exception\n");
		throw;
	}
}

// the plug-ins are the G'MIC filters, only known once they are loaded: not static factory objects
// registered at dlopen
const OFX::PluginFactories lazyRegistration(&registerGmicPlugins);

} // namespace

void showRenderError(OFX::ImageEffect& effect, const std::string& msg, const std::string& modalId)
{
	// (the Support library checks that the host has the message suites)
	if (effect.setPersistentMessage(OFX::Message::eMessageError, "", msg) == OFX::Message::eMessageReplyFailed) {
		effect.sendMessage(OFX::Message::eMessageError, modalId, msg);
	}
}

void showRenderWarning(OFX::ImageEffect& effect, const std::string& msg)
{
	effect.setPersistentMessage(OFX::Message::eMessageWarning, "", msg);
}
