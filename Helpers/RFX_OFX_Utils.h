/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */
/*
 #
 #  File        : RFX_OFX_Utils.h
 #
 #  Description : A self-contained header file with helper functions to make    
 #                using the OpenFX SDK/API a bit easier to use    
 #
 #  Copyright   : Tobias Fleischer / reduxFX Productions (http://www.reduxfx.com)
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
 #  As a counterpart to the access to the source code and  rights to copy,
 #  modify and redistribute granted by the license, users are provided only
 #  with a limited warranty  and the software's author,  the holder of the
 #  economic rights,  and the successive licensors  have only  limited
 #  liability.
 #
 #  In this respect, the user's attention is drawn to the risks associated
 #  with loading,  using,  modifying and/or developing or reproducing the
 #  software by the user in light of its specific status of free software,
 #  that may mean  that it is complicated to manipulate,  and  that  also
 #  therefore means  that it is reserved for developers  and  experienced
 #  professionals having in-depth computer knowledge. Users are therefore
 #  encouraged to load and test the software's suitability as regards their
 #  requirements in conditions enabling the security of their systems and/or
 #  data to be ensured and,  more generally, to use and operate it in the
 #  same conditions as regards security.
 #
 #  The fact that you are presently reading this means that you have had
 #  knowledge of the CeCILL and CeCILL-C licenses and that you accept its terms.
 #
*/

#ifndef RFX_OFX_UTILS_H
#define RFX_OFX_UTILS_H

#include <stdexcept>
#include <new>
#include <cstring>
#include <cstddef>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxUtilities.H"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnPublicOfxExtensions.h"
#endif

#ifndef NO_MULTITHREADED_CONVERSION
#include <thread>
#endif


// #include "../../../Helpers/TLog.h"

#include <string>
#include "RFX_Utils.h"
#include "RFX_Parameter.h"
#include "RFX_StringUtils.h"
using namespace std;
using namespace reduxfx;

#ifdef ABOUT_DIALOG
#include "TMsgDlg.h"
#endif

#undef OFX_64
#undef OFX_32

#if defined(_M_AMD64) || defined(_AMD64_) || defined(_M_IA64) || defined(_IA64_) || defined(_WIN64) || defined(__x86_64__) || defined(__LP64__)
#define OFX_64
#undef OFX_32
#else
#define OFX_32
#undef OFX_64
#endif

#define PF_Err_NONE 0
#define PF_Err_INTERNAL_STRUCT_DAMAGED 512
#define A_long long


struct ContextData
{
    int pluginIndex;
	OfxImageEffectHandle instance;
	OfxPropertySetHandle inArgs;
	OfxPropertySetHandle outArgs;
};

extern void* createCustomGlobalData();
extern void destroyCustomGlobalData(void* customGlobalDataP);
extern void* createCustomSequenceData();
extern void destroyCustomSequenceData(void* customSequenceDataP);
extern void* flattenCustomSequenceData(void* customUnflatSequenceDataP, int& flatSize);
extern void* unflattenCustomSequenceData(void* customFlatSequenceDataP, int flatSize);

extern int pluginParamChange(int index, SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* contextDataP);
extern int pluginSetup(GlobalData* globalDataP, ContextData* contextDataP);
extern int pluginSetdown(GlobalData* globalDataP, ContextData* contextDataP);
extern int pluginProcess(SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* contextDataP);

extern int getNofPlugins();
extern void getPluginInfo(int pluginIndex, PluginInfo& pluginInfo);

#if defined __APPLE__ || defined __linux__ || defined __FreeBSD__
#define EXPORT __attribute__((visibility("default")))
#elif defined _WIN32
#define EXPORT OfxExport
#else
#error Not building on your operating system quite yet
#endif

template <class T> inline T Maximum(T a, T b) {return a > b ? a : b;}
template <class T> inline T Minimum(T a, T b) {return a < b ? a : b;}

const float MAX_CHAN8 = 255.f;
const float I_MAX_CHAN8 = 1.f / 255.f;
const float MAX_CHAN16 = 32768.f;
const float I_MAX_CHAN16 = 1.f / 32768.f;

#ifdef PLANAR_BUFFER
const int stepWidth = 1;
#else
const int stepWidth = 4;
#endif

inline void setContextData(ContextData &cd, OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs, int pluginIndex)
{
	cd.instance = instance;
	cd.inArgs = inArgs;
	cd.outArgs = outArgs;
    cd.pluginIndex = pluginIndex;
}

// pointers64 to various bits of the host
OfxHost					*gHost;
OfxImageEffectSuiteV1	*gEffectHost = 0;
OfxPropertySuiteV1		*gPropHost = 0;
OfxParameterSuiteV1		*gParamHost = 0;
OfxMemorySuiteV1		*gMemoryHost = 0;
OfxMultiThreadSuiteV1	*gThreadHost = 0;
OfxMessageSuiteV1		*gMessageSuite = 0;
OfxMessageSuiteV2		*gMessageSuiteV2 = 0; // persistent messages, if the host supports them
OfxInteractSuiteV1		*gInteractHost = 0;

// some flags about the host's behaviour
int gHostIsNatron = false;

// GlobalData* globalDataP = NULL;
vector<OfxPlugin> gPlugins;
vector<GlobalData> globalData;

// private instance data type
struct MyInstanceData {
	bool isGeneralEffect;
	SequenceData* sequenceDataP;
	OfxParamHandle param[MAX_NOF_PARAMS];
	OfxImageClipHandle input[MAX_NOF_LAYERS];
	OfxImageClipHandle output;
	OfxParamHandle seqDataH;
};

// Convenience wrapper to get private data 
static MyInstanceData* getMyInstanceData(OfxImageEffectHandle effect)
{
	// get the property handle for the plugin
	OfxPropertySetHandle effectProps;
	gEffectHost->getPropertySet(effect, &effectProps);

	// get my data pointer out of that
	MyInstanceData* myData = NULL;
	gPropHost->propGetPointer(effectProps, kOfxPropInstanceData, 0, (void **)&myData);
	return myData;
}

static OfxStatus onLoad(int pluginIndex)
{
	ContextData contextData;
	setContextData(contextData, NULL, NULL, NULL, pluginIndex);
	globalData[pluginIndex].customGlobalDataP = createCustomGlobalData();
	pluginSetup(&globalData[pluginIndex], NULL);
	return kOfxStatOK;
}

static OfxStatus onUnload(int pluginIndex)
{
	ContextData contextData;
	setContextData(contextData, NULL, NULL, NULL, pluginIndex);
	pluginSetdown(&globalData[pluginIndex], NULL);
	destroyCustomGlobalData(globalData[pluginIndex].customGlobalDataP);
	return kOfxStatOK;
}

static OfxStatus syncPrivateData(int /*pluginIndex*/, OfxImageEffectHandle effect)
{
	MyInstanceData* myData = getMyInstanceData(effect);
	if (myData && myData->sequenceDataP && myData->sequenceDataP->customSequenceDataP) {
		int flatSize = 0;
		char* flatCustomSequenceDataP = (char*)flattenCustomSequenceData(myData->sequenceDataP->customSequenceDataP, flatSize);
		string s = bufferToHexString((unsigned char*)flatCustomSequenceDataP, flatSize);
		gParamHost->paramSetValue(myData->seqDataH, s.c_str());
		delete flatCustomSequenceDataP;
	}
	return kOfxStatOK;
}

// instance construction
static OfxStatus createInstance(int pluginIndex, OfxImageEffectHandle effect)
{
	// get a pointer to the effect properties
	OfxPropertySetHandle effectProps;
	gEffectHost->getPropertySet(effect, &effectProps);

	// get a pointer to the effect's parameter set
	OfxParamSetHandle paramSet;
	gEffectHost->getParamSet(effect, &paramSet);

	// make private instance data
	MyInstanceData* myData = new MyInstanceData;
	myData->sequenceDataP = new SequenceData();
	for (int i = 0; i < globalData[pluginIndex].nofParams; i++) {
		myData->sequenceDataP->textValue[i] = globalData[pluginIndex].param[i].text;
		for (int j = 0; j < 4; j++) {
			myData->sequenceDataP->floatValue[i][j] = globalData[pluginIndex].param[i].defaultValue[j];
		}
	}
	myData->sequenceDataP->contextP = (void*)effect;

	gParamHost->paramGetHandle(paramSet, "SequenceData", &(myData->seqDataH), 0);

	char* flatCustomSequenceDataP = NULL;
	gParamHost->paramGetValue(myData->seqDataH, &flatCustomSequenceDataP);
	if (flatCustomSequenceDataP) {
		string s = string(flatCustomSequenceDataP);
		unsigned char* buf = NULL;
		if (s != "") {
			buf = new unsigned char[(int)s.size() / 2];
			hexStringToBuffer(s, buf);
		}
		if (buf) {
			myData->sequenceDataP->customSequenceDataP = unflattenCustomSequenceData(buf, (int)s.size() / 2);
			delete [] buf;
		} else {
			myData->sequenceDataP->customSequenceDataP = createCustomSequenceData();
		}
	} else {
		myData->sequenceDataP->customSequenceDataP = createCustomSequenceData();
	}
		
	const char *context = 0;

	// is this instance a general effect ?
	gPropHost->propGetString(effectProps, kOfxImageEffectPropContext, 0, &context);
	myData->isGeneralEffect = context && (strcmp(context, kOfxImageEffectContextGeneral) == 0);

	myData->param[0] = NULL;
	int l = 0;
	for (int i = 0; i < globalData[pluginIndex].nofParams; i++) {
		if (globalData[pluginIndex].param[i].paramType == PT_LAYER) {
			gEffectHost->clipGetHandle(effect, globalData[pluginIndex].param[i].paramName.c_str(), &(myData->input[l]), 0);
			l++;
		}
		gParamHost->paramGetHandle(paramSet, globalData[pluginIndex].param[i].paramName.c_str(), &(myData->param[i]), 0);
	}
	gEffectHost->clipGetHandle(effect, "Output", &myData->output, 0);
	
	// set my private instance data
	gPropHost->propSetPointer(effectProps, kOfxPropInstanceData, 0, (void*)myData);

	return kOfxStatOK;
}

// instance destruction
static OfxStatus destroyInstance(int /*pluginIndex*/, OfxImageEffectHandle effect)
{
	// get my instance data
	MyInstanceData* myData = getMyInstanceData(effect);
	if (myData && myData->sequenceDataP) {
		if (myData->sequenceDataP->customSequenceDataP) {
			destroyCustomSequenceData(myData->sequenceDataP->customSequenceDataP);
			myData->sequenceDataP->customSequenceDataP = NULL;
		}
		delete (SequenceData*)myData->sequenceDataP;
		myData->sequenceDataP = NULL;
	}

	// and delete it
	if (myData) delete myData;
	return kOfxStatOK;
}

// dstRect is in pixels; point parameters are in canonical coordinates: pixel x = canonical x *
// renderScale.x / par, pixel y = canonical y * renderScale.y (par: the pixel aspect ratio of the
// image dstRect belongs to)
static void getAllParamData(int pluginIndex, const OfxRectI& dstRect, double par, MyInstanceData* myData, OfxTime t, const OfxPointD& renderScale)
{
	for (int i = 0; i < globalData[pluginIndex].nofParams; i++) {
		if (globalData[pluginIndex].param[i].paramType == PT_FLOAT) {
			double v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			myData->sequenceDataP->floatValue[i][0] = (float)v;
		} else if (globalData[pluginIndex].param[i].paramType == PT_INT) {
			int v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			myData->sequenceDataP->floatValue[i][0] = (float)v;
		} else if (globalData[pluginIndex].param[i].paramType == PT_COLOR) {
			double myR, myG, myB, myA;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &myR, &myG, &myB, &myA);
			myData->sequenceDataP->floatValue[i][0] = (float)myR;
			myData->sequenceDataP->floatValue[i][1] = (float)myG;
			myData->sequenceDataP->floatValue[i][2] = (float)myB;
			myData->sequenceDataP->floatValue[i][3] = (float)myA;
		} else if (globalData[pluginIndex].param[i].paramType == PT_POINT) {
			// point parameter is in % (percent) in gmic: no need to scale
			double myX, myY;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &myX, &myY);
			// percent of the image, from its top-left corner (G'MIC's y goes down)
			myData->sequenceDataP->floatValue[i][0] = (float)((myX * renderScale.x / par - dstRect.x1) * 100. / (dstRect.x2 - dstRect.x1));
			myData->sequenceDataP->floatValue[i][1] = 100.f - (float)((myY * renderScale.y - dstRect.y1) * 100. / (dstRect.y2 - dstRect.y1));
		} else if (globalData[pluginIndex].param[i].paramType == PT_ANGLE) {
			double v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			if (globalData[pluginIndex].param[i].flags > 0)
				myData->sequenceDataP->floatValue[i][0] = -M_PI * (float)v / 180.0f;
			else
				myData->sequenceDataP->floatValue[i][0] = (float)v;
		} else if (globalData[pluginIndex].param[i].paramType == PT_SELECT) {
			int v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			myData->sequenceDataP->floatValue[i][0] = (float)v;
		} else if (globalData[pluginIndex].param[i].paramType == PT_BOOL) {
			int v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			myData->sequenceDataP->floatValue[i][0] = (float)v;
		} else if (globalData[pluginIndex].param[i].paramType == PT_TEXT) {
			char* v;
			gParamHost->paramGetValueAtTime(myData->param[i], t, &v);
			myData->sequenceDataP->textValue[i] = v;
		}
	}
}



static
bool
renderScaleIsOne(OfxPropertySetHandle inArgs)
{
    double renderScale[2] = {1., 1.};
    OfxStatus stat = gPropHost->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, renderScale);
    if (stat == kOfxStatOK && (renderScale[0] != 1. || renderScale[1] != 1.)) {
        return false;
    }
    return true;
}

// Render scale (issue #49). G'MIC parameters carry no unit, so pixel-sized ones (radii, sizes,
// amplitudes in pixels) can't be scaled with the render scale, and a zoomed-out viewer or a proxy
// render doesn't match the full-resolution render. On Natron, the actions fail for render scales other
// than 1: Natron then marks the effect as not supporting render scale (OfxEffectInstance tries the
// region of definition at scale 0.5) and renders it at full resolution, then downscales the result
// (renderFullScaleThenDownscale in EffectInstance::renderRoI). Other hosts (Resolve, Nuke, Vegas) don't
// fall back to full resolution, and the render would fail: there, render scales are accepted as
// before, with unscaled pixel-sized parameters. Natron is recognized by kNatronOfxHostIsNatron, not by
// kOfxPropName, which Natron can set to another host's name for compatibility.
static
bool
hostRendersFullScaleWithoutRenderScale()
{
    int isNatron = 0;
#ifdef OFX_EXTENSIONS_NATRON
    if (gHost && gPropHost) {
        gPropHost->propGetInt(gHost->host, kNatronOfxHostIsNatron, 0, &isNatron);
    }
#endif
    return isNatron != 0;
}

// the same rule for every action that has a render scale (region of definition, regions of interest,
// isIdentity, render): true if the action must fail
static
bool
renderScaleRefused(OfxPropertySetHandle inArgs)
{
    return hostRendersFullScaleWithoutRenderScale() && !renderScaleIsOne(inArgs);
}

// tells the host what region we are capable of filling
static
OfxStatus getSpatialRoD(int /*pluginIndex*/, OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    if (renderScaleRefused(inArgs)) {
        return kOfxStatFailed;
    }
	// retrieve any instance data associated with this effect
	MyInstanceData *myData = getMyInstanceData(effect);

	OfxTime time;
	gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);

	// my RoD is the same as my input's
	OfxRectD rod;
	gEffectHost->clipGetRegionOfDefinition(myData->input[0], time, &rod);

	// set the rod in the out args
	gPropHost->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, &rod.x1);
	return kOfxStatOK;
}

// tells the host how much of the input we need to fill the given window
static
OfxStatus getSpatialRoI(int pluginIndex, OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    if (renderScaleRefused(inArgs)) {
		// GMIC does not support render scale
        return kOfxStatFailed;
    }
	// G'MIC processes whole images: ask for the full region of definition of every input, not just
	// the part under the render window (the default), which gave a cropped input when the viewer
	// shows part of the image (issue #2)
	MyInstanceData *myData = getMyInstanceData(effect);
	OfxTime time = 0.;
	gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);
	int i = 0;
	for (int p = 0; p < globalData[pluginIndex].nofParams && i < MAX_NOF_LAYERS; p++) {
		if (globalData[pluginIndex].param[p].paramType != PT_LAYER) continue;
		OfxRectD rod;
		if (myData->input[i] && gEffectHost->clipGetRegionOfDefinition(myData->input[i], time, &rod) == kOfxStatOK) {
			const std::string roiName = std::string("OfxImageClipPropRoI_") + globalData[pluginIndex].param[p].paramName;
			gPropHost->propSetDoubleN(outArgs, roiName.c_str(), 4, &rod.x1);
		}
		++i;
	}
	return kOfxStatOK;
}

// Tells the host how many frames we can fill, only called in the general context.
// This is actually redundant as this is the default behaviour, but for illustrative
// purposes.
static
OfxStatus getTemporalDomain(int /*pluginIndex*/, OfxImageEffectHandle effect, OfxPropertySetHandle /*inArgs*/, OfxPropertySetHandle outArgs)
{
	MyInstanceData *myData = getMyInstanceData(effect);
	double sourceRange[2];
	
	// get the frame range of the source clip
	OfxPropertySetHandle props; gEffectHost->clipGetPropertySet(myData->input[0], &props);
	gPropHost->propGetDoubleN(props, kOfxImageEffectPropFrameRange, 2, sourceRange);

	// set it on the out args
	gPropHost->propSetDoubleN(outArgs, kOfxImageEffectPropFrameRange, 2, sourceRange);
	
	return kOfxStatOK;
}

// true if a clip or image property set is flagged premultiplied
static bool propsArePremultiplied(OfxPropertySetHandle props)
{
	const char* premult = NULL;
	if (props) {
		gPropHost->propGetString(props, kOfxImageEffectPropPreMultiplication, 0, &premult);
	}
	return premult && strcmp(premult, kOfxImagePreMultiplied) == 0;
}

// Premultiplication (issue #43). G'MIC filters work on straight (unpremultiplied) colours: premultiplied
// inputs are unpremultiplied before G'MIC. The output is declared premultiplied (and the result is
// premultiplied back) only if the main input is premultiplied. Otherwise it is declared
// unpremultiplied: the result has straight colours, and for an input flagged opaque whose alpha isn't 1
// (e.g. a Read with "Output Premult" set to Opaque) multiplying by that alpha would darken the image;
// declaring the output opaque would be wrong as soon as the result has an alpha other than 1, which
// the plug-in can't know before rendering.
static bool mainInputIsPremultiplied(MyInstanceData* myData)
{
	OfxPropertySetHandle props = NULL;
	if (!myData || !myData->input[0] || gEffectHost->clipGetPropertySet(myData->input[0], &props) != kOfxStatOK) {
		return false;
	}
	return propsArePremultiplied(props);
}

// Set our clip preferences
static OfxStatus getClipPreferences(int pluginIndex, OfxImageEffectHandle effect, OfxPropertySetHandle /*inArgs*/, OfxPropertySetHandle outArgs)
{
	// retrieve any instance data associated with this effect
	MyInstanceData *myData = getMyInstanceData(effect);
	
	// get the component type and bit depth of our main input
	int	bitDepth;
	bool isRGBA;
	ofxuClipGetFormat(myData->input[0], bitDepth, isRGBA, true); // get the unmapped clip component

	// get the strings used to label the various bit depths
	// the output clip only supports RGBA (render writes 4 channels), even for an Alpha input
	(void)isRGBA;
	const char *componentStr = kOfxImageComponentRGBA;

	// set out output to be the same same as the input, component and bitdepth
	gPropHost->propSetString(outArgs, "OfxImageClipPropComponents_Output", 0, componentStr);
	gPropHost->propSetString(outArgs, kOfxImageEffectPropPreMultiplication, 0, mainInputIsPremultiplied(myData) ? kOfxImagePreMultiplied : kOfxImageUnPreMultiplied);
	// The output depth is not set: the plug-in declares kOfxImageEffectPropSupportsMultipleClipDepths=0,
	// so the host gives all clips the same depth (and a spec-conformant host refuses the write).
	int v = 0;
	GlobalData* globalDataP = &globalData[pluginIndex];
	gParamHost->paramGetValue(myData->param[PARAM_ANIMSEED], &v);
	if (v) {
		gPropHost->propSetInt(outArgs, kOfxImageEffectFrameVarying, 0, 1);
	}

	return kOfxStatOK;
}

// are the settings of the effect performing an identity operation
static OfxStatus isIdentity(int /*pluginIndex*/, OfxImageEffectHandle /*effect*/, OfxPropertySetHandle inArgs, OfxPropertySetHandle /*outArgs*/)
{
    if (renderScaleRefused(inArgs)) {
        return kOfxStatFailed;
    }
	return kOfxStatReplyDefault;
}

#ifdef ABOUT_DIALOG
static void showAboutDialog(int pluginIndex, MyInstanceData* /*myData*/)
{
	try
	{
		// normal about dialog
		string sDescription = globalData[pluginIndex].pluginInfo.description;
		strReplace(sDescription, "\\r", "\r");
		strReplace(sDescription, "\\n", "\n");
		sDescription = sDescription.substr(0, 201);
		string sName = string(PLUGIN_NAME);
		char msg[1024];
		sprintf(msg, "%s %d.%d\r%s", sName.c_str(), MAJOR_VERSION, MINOR_VERSION, sDescription.c_str());
		CTMsgDlg dlg(400, 150);
		dlg.ShowDialog(sName, string(msg), "", "", "OK", false, "");
	}
	catch(...)
	{
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////
// function called when the instance has been changed by anything
static OfxStatus instanceChanged(int pluginIndex, OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
	ContextData contextData;
	setContextData(contextData, instance, inArgs, outArgs, pluginIndex);

	// see why it changed
	const char *changeReason;
	gPropHost->propGetString(inArgs, kOfxPropChangeReason, 0, &changeReason);

	// we are only interested in user edits
	if (strcmp(changeReason, kOfxChangeUserEdited) != 0) return kOfxStatReplyDefault;

	// fetch the type of the object that changed
	const char *typeChanged;
	gPropHost->propGetString(inArgs, kOfxPropType, 0, &typeChanged);

	// was it a clip or a param?
	// bool isClip = strcmp(typeChanged, kOfxTypeClip) == 0;
	bool isParam = strcmp(typeChanged, kOfxTypeParameter) == 0;

	// get the name of the thing that changed
	const char *objChanged;
	gPropHost->propGetString(inArgs, kOfxPropName, 0, &objChanged);

	// retrieve any instance data associated with this effect
	MyInstanceData* myData = getMyInstanceData(instance);
	
	if (isParam) {
		if (strcmp(objChanged, "SequenceData") == 0) {
			char* flatCustomSequenceDataP = NULL;
			gParamHost->paramGetValue(myData->seqDataH, &flatCustomSequenceDataP);
			if (myData->sequenceDataP->customSequenceDataP) {
				destroyCustomSequenceData(myData->sequenceDataP->customSequenceDataP);
				myData->sequenceDataP->customSequenceDataP = NULL;
			}

			string s = string(flatCustomSequenceDataP);
			unsigned char* buf = NULL;
			if (s != "") {
				buf = new unsigned char[(int)s.size() / 2];
				hexStringToBuffer(s, buf);
			}
			if (buf) {
				myData->sequenceDataP->customSequenceDataP = unflattenCustomSequenceData(buf, (int)s.size() / 2);
				delete [] buf;
			} else {
				myData->sequenceDataP->customSequenceDataP = createCustomSequenceData();
			}
		}
		OfxParamSetHandle paramSet;
		gEffectHost->getParamSet(instance, &paramSet);
		OfxParamHandle param; 
		OfxPropertySetHandle props;
		gParamHost->paramGetHandle(paramSet, objChanged, &param, &props);
		const char* paramType;
		gPropHost->propGetString(props, kOfxParamPropType, 0, &paramType);

#ifdef ABOUT_DIALOG
		if (strcmp(objChanged, "About") == 0) {
			showAboutDialog(pluginIndex, myData);
		}
#endif
	}
	
	// don't trap any 
	return kOfxStatReplyDefault;
}

#define NOF_CONVERT_THREADS 8

class ConvertData
{
public:
	void* srcData;
	int srcRowBytes;
	void* dstData;
	int dstRowBytes;
	int width;
	int height;
	float scale;
	
	ConvertData(void* _srcData = NULL, int _srcRowBytes = 0, void* _dstData = NULL, int _dstRowBytes = 0, int _width = 0, int _height = 0, float _scale = 1.f)
	{
		srcData = _srcData;
		dstData = _dstData;
		srcRowBytes = _srcRowBytes;
		dstRowBytes = _dstRowBytes;
		width = _width;
		height = _height;
		scale = _scale;
	};
};

#if defined(PLANAR_BUFFER) && !defined(NO_MULTITHREADED_CONVERSION)
static
void convert_RGBA8P_to_RGBA8(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		unsigned char* in_pixR = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 0));
		unsigned char* in_pixG = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 1));
		unsigned char* in_pixB = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 2));
		unsigned char* in_pixA = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 3));
#ifdef IMG_FLIP_Y
		unsigned char* out_pix = (unsigned char*)((char*)data.dstData + ((ptrdiff_t)(data.height-1-y) * data.dstRowBytes));
#else
		unsigned char* out_pix = (unsigned char*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
#endif
		for (int x = 0; x < data.width; x++) {
			*out_pix++ = *in_pixR; in_pixR += stepWidth;
			*out_pix++ = *in_pixG; in_pixG += stepWidth;
			*out_pix++ = *in_pixB; in_pixB += stepWidth;
			*out_pix++ = *in_pixA; in_pixA += stepWidth;
		}
	}
}

static
void convert_RGBA8P_to_RGBA32(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		unsigned char* in_pixR = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 0));
		unsigned char* in_pixG = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 1));
		unsigned char* in_pixB = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 2));
		unsigned char* in_pixA = (unsigned char*)((char*)data.srcData + sizeof(unsigned char) * data.width * (y + data.height * 3));
#ifdef IMG_FLIP_Y
		float* out_pix = (float*)((char*)data.dstData + ((ptrdiff_t)(data.height-1-y) * data.dstRowBytes));
#else
		float* out_pix = (float*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
#endif
		for (int x = 0; x < data.width; x++) {
			*out_pix++ = ((float)(*in_pixR) * I_MAX_CHAN8 * data.scale); in_pixR += stepWidth;
			*out_pix++ = ((float)(*in_pixG) * I_MAX_CHAN8 * data.scale); in_pixG += stepWidth;
			*out_pix++ = ((float)(*in_pixB) * I_MAX_CHAN8 * data.scale); in_pixB += stepWidth;
			*out_pix++ = ((float)(*in_pixA) * I_MAX_CHAN8 * data.scale); in_pixA += stepWidth;
		}
	}
}

static
void convert_RGBA8_to_RGBA8P(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
#ifdef IMG_FLIP_Y
		unsigned char* in_pix = (unsigned char*)((char*)data.srcData + ((ptrdiff_t)(data.height-1-y) * data.srcRowBytes));
#else
		unsigned char* in_pix = (unsigned char*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
#endif
		unsigned char* out_pixR = (unsigned char*)((char*)data.dstData + sizeof(unsigned char) * data.width * (y + data.height * 0));
		unsigned char* out_pixG = (unsigned char*)((char*)data.dstData + sizeof(unsigned char) * data.width * (y + data.height * 1));
		unsigned char* out_pixB = (unsigned char*)((char*)data.dstData + sizeof(unsigned char) * data.width * (y + data.height * 2));
		unsigned char* out_pixA = (unsigned char*)((char*)data.dstData + sizeof(unsigned char) * data.width * (y + data.height * 3));
		for (int x = 0; x < data.width; x++) {
			*out_pixR = *in_pix++; out_pixR += stepWidth;
			*out_pixG = *in_pix++; out_pixG += stepWidth;
			*out_pixB = *in_pix++; out_pixB += stepWidth;
			*out_pixA = *in_pix++; out_pixA += stepWidth;
		}
	}
}
#endif // PLANAR_BUFFER

static
void convert_RGBA32P_to_RGBA32(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		float* in_pixR = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 0));
		float* in_pixG = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 1));
		float* in_pixB = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 2));
		float* in_pixA = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 3));
#ifdef IMG_FLIP_Y
		float* out_pix = (float*)((char*)data.dstData + ((ptrdiff_t)(data.height-1-y) * data.dstRowBytes));
#else
		float* out_pix = (float*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
#endif
		for (int x = 0; x < data.width; x++) {
			*out_pix++ = *in_pixR * data.scale; in_pixR += stepWidth;
			*out_pix++ = *in_pixG * data.scale; in_pixG += stepWidth;
			*out_pix++ = *in_pixB * data.scale; in_pixB += stepWidth;
			*out_pix++ = *in_pixA * data.scale; in_pixA += stepWidth;
		}
	}
}

static
void convert_RGBA32P_to_RGBA8(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		float* in_pixR = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 0));
		float* in_pixG = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 1));
		float* in_pixB = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 2));
		float* in_pixA = (float*)((char*)data.srcData + sizeof(float) * data.width * (y + data.height * 3));
#ifdef IMG_FLIP_Y
		unsigned char* out_pix = (unsigned char*)((char*)data.dstData + ((ptrdiff_t)(data.height-1-y) * data.dstRowBytes));
#else
		unsigned char* out_pix = (unsigned char*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
#endif
		for (int x = 0; x < data.width; x++) {
			*out_pix++ = (unsigned char)(saturate(*in_pixR * data.scale) * MAX_CHAN8 + 0.5f); in_pixR += stepWidth;
			*out_pix++ = (unsigned char)(saturate(*in_pixG * data.scale) * MAX_CHAN8 + 0.5f); in_pixG += stepWidth;
			*out_pix++ = (unsigned char)(saturate(*in_pixB * data.scale) * MAX_CHAN8 + 0.5f); in_pixB += stepWidth;
			*out_pix++ = (unsigned char)(saturate(*in_pixA * data.scale) * MAX_CHAN8 + 0.5f); in_pixA += stepWidth;
		}
	}
}

static
void convert_RGBA32_to_RGBA32P(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
#ifdef IMG_FLIP_Y
		float* in_pix = (float*)((char*)data.srcData + ((ptrdiff_t)(data.height-1-y) * data.srcRowBytes));
#else
		float* in_pix = (float*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
#endif
		float* out_pixR = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 0));
		float* out_pixG = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 1));
		float* out_pixB = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 2));
		float* out_pixA = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 3));
		for (int x = 0; x < data.width; x++) {
			*out_pixR = (*in_pix++) * data.scale; out_pixR += stepWidth;
			*out_pixG = (*in_pix++) * data.scale; out_pixG += stepWidth;
			*out_pixB = (*in_pix++) * data.scale; out_pixB += stepWidth;
			*out_pixA = (*in_pix++) * data.scale; out_pixA += stepWidth;
		}
	}
}

#ifndef PLANAR_BUFFER
static
void convert_RGBA8_to_RGBA32(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		unsigned char* in_pix = (unsigned char*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
		float* out_pixR = (float*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
		float* out_pixG = out_pixR + 1;
		float* out_pixB = out_pixR + 2;
		float* out_pixA = out_pixR + 3;
		for (int x = 0; x < data.width; x++) {
			*out_pixR = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixR += stepWidth;
			*out_pixG = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixG += stepWidth;
			*out_pixB = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixB += stepWidth;
			*out_pixA = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixA += stepWidth;
		}
	}
}
#endif // !PLANAR_BUFFER

static
void convert_RGBA8_to_RGBA32P(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		// rows in the same order as convert_RGBA32_to_RGBA32P (and as the output conversions):
		// without IMG_FLIP_Y here, 8-bit images came out upside down
#ifdef IMG_FLIP_Y
		unsigned char* in_pix = (unsigned char*)((char*)data.srcData + ((ptrdiff_t)(data.height-1-y) * data.srcRowBytes));
#else
		unsigned char* in_pix = (unsigned char*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
#endif
		float* out_pixR = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 0));
		float* out_pixG = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 1));
		float* out_pixB = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 2));
		float* out_pixA = (float*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 3));
		for (int x = 0; x < data.width; x++) {
			*out_pixR = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixR += stepWidth;
			*out_pixG = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixG += stepWidth;
			*out_pixB = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixB += stepWidth;
			*out_pixA = ((float)(*in_pix++) * I_MAX_CHAN8 * data.scale); out_pixA += stepWidth;
		}
	}
}

#ifndef PLANAR_BUFFER
static
void convert_RGBA32_to_RGBA8(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		float* in_pix = (float*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
		unsigned char* out_pixR = (unsigned char*)((char*)data.dstData + ((ptrdiff_t)y * data.dstRowBytes));
		unsigned char* out_pixG = out_pixR + 1;
		unsigned char* out_pixB = out_pixR + 2;
		unsigned char* out_pixA = out_pixR + 3;
		for (int x = 0; x < data.width; x++) {
			*out_pixR = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixR += stepWidth;
			*out_pixG = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixG += stepWidth;
			*out_pixB = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixB += stepWidth;
			*out_pixA = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixA += stepWidth;
		}
	}
}
#endif // !PLANAR_BUFFER

#if defined(PLANAR_BUFFER) && !defined(NO_MULTITHREADED_CONVERSION)
static
void convert_RGBA32_to_RGBA8P(ConvertData& data, const int startLine, const int endLine)
{
	for (int y = startLine; y < endLine; y++) {
		float* in_pix = (float*)((char*)data.srcData + ((ptrdiff_t)y * data.srcRowBytes));
		unsigned char* out_pixR = (unsigned char*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 0));
		unsigned char* out_pixG = (unsigned char*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 1));
		unsigned char* out_pixB = (unsigned char*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 2));
		unsigned char* out_pixA = (unsigned char*)((char*)data.dstData + sizeof(float) * data.width * (y + data.height * 3));
		for (int x = 0; x < data.width; x++) {
			*out_pixR = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixR += stepWidth;
			*out_pixG = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixG += stepWidth;
			*out_pixB = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixB += stepWidth;
			*out_pixA = (unsigned char)(saturate(*in_pix++ * data.scale) * MAX_CHAN8 + 0.5f); out_pixA += stepWidth;
		}
	}
}
#endif

#if !defined(CONVERT_INT_TO_FLOAT) || !defined(PLANAR_BUFFER)
#error "the premultiplication handling in render() expects float planar buffers (CONVERT_INT_TO_FLOAT and PLANAR_BUFFER)"
#endif

// The buffers below are float planar RGBA (RRR... GGG... BBB... AAA...), with values up to maxValue.

// alpha at or below this fraction of maxValue is treated as transparent when unpremultiplying
#define kUnpremultiplyMinAlpha 1e-5f

// Unpremultiply, in place. Pixels with an alpha at or below kUnpremultiplyMinAlpha * maxValue (or NaN)
// keep their colour: dividing by a tiny alpha would give huge values that a filter (e.g. a blur) would
// spread over their neighbours. Premultiplying back multiplies them by that alpha, so a premultiplied
// pixel with alpha 0 and colour > 0 (additive, e.g. a glow) loses its colour in the round trip.
static void unpremultiplyPlanar(float* data, int width, int height, float maxValue)
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

// Premultiply, in place. The factor alpha / maxValue is clamped to [0,1] (NaN gives 0), so that a
// G'MIC alpha below 0 or above maxValue can't produce colours outside [0, alpha].
static void premultiplyPlanar(float* data, int width, int height, float maxValue)
{
	if (!data || width <= 0 || height <= 0) return;
	const size_t n = (size_t)width * (size_t)height;
	float* r = data;
	float* g = data + n;
	float* b = data + 2 * n;
	const float* a = data + 3 * n;
	for (size_t k = 0; k < n; ++k) {
		float f = a[k] / maxValue;
		if (!(f > 0.f)) f = 0.f; // also NaN
		else if (f > 1.f) f = 1.f;
		r[k] *= f;
		g[k] *= f;
		b[k] *= f;
	}
}

// A result without alpha (1 or 3 channels) from a premultiplied main input: with straight colours it
// would be opaque everywhere, with hard bright edges where the input was transparent. Before the
// premultiplication handling, G'MIC got the premultiplied colours, i.e. the input composited over
// black, and the result looked composited over black too. To keep that look, the colours are
// multiplied by the main input's alpha and the output alpha stays opaque. Only done when the result
// has the main input's size (G'MIC anchors it at the input's rectangle): out is the output buffer,
// in the main input's buffer, both in pixel coordinates with rows top-down.
static void multiplyByInputAlphaPlanar(const World& out, const World& in, float maxValue)
{
	if (!out.data || !in.data) return;
	const int x1 = std::max(out.origin_x, in.origin_x), x2 = std::min(out.origin_x + out.width, in.origin_x + in.width);
	const int y1 = std::max(out.origin_y, in.origin_y), y2 = std::min(out.origin_y + out.height, in.origin_y + in.height);
	const size_t dplane = (size_t)out.width * (size_t)out.height, splane = (size_t)in.width * (size_t)in.height;
	float* const dst = (float*)out.data;
	const float* const srcA = (const float*)in.data + 3 * splane;
	for (int y = y1; y < y2; ++y) {
		const size_t drow = (size_t)(out.height - 1 - (y - out.origin_y)) * out.width;
		const size_t srow = (size_t)(in.height - 1 - (y - in.origin_y)) * in.width;
		for (int x = x1; x < x2; ++x) {
			float f = srcA[srow + (x - in.origin_x)] / maxValue;
			if (!(f > 0.f)) f = 0.f;
			else if (f > 1.f) f = 1.f;
			const size_t k = drow + (x - out.origin_x);
			dst[k] *= f;
			dst[dplane + k] *= f;
			dst[2 * dplane + k] *= f;
		}
	}
}

// the process code that the host sees
static OfxStatus render(int pluginIndex, OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    if (renderScaleRefused(inArgs)) {
        return kOfxStatFailed;
    }
	ContextData contextData;
	setContextData(contextData, instance, inArgs, outArgs, pluginIndex);

	// get the time from the inArgs (the render window is not used: tiles are not supported, and
	// the whole output image is rendered)
	OfxTime time;
	OfxStatus status = kOfxStatOK;

	gPropHost->propGetDouble(inArgs, kOfxPropTime, 0, &time);

	// retrieve any instance data associated with this effect
	MyInstanceData *myData = getMyInstanceData(instance);

	OfxPropertySetHandle outputImg = NULL;
	OfxPropertySetHandle sourceImg[MAX_NOF_LAYERS] = { 0 };
	bool customInputData[MAX_NOF_LAYERS] = { 0 };
	bool customOutputData = false;
	for (int i = 0; i < globalData[pluginIndex].nofInputs; i++) {
		sourceImg[i] = NULL;
		customInputData[i] = false;
	}

	bool dstIsAlpha = false;
	void* dstData = NULL;

#ifndef NO_MULTITHREADED_CONVERSION
	thread t[NOF_CONVERT_THREADS];
#endif

	try
	{
		// get the output image
		int dstRowBytes = 0, dstBitDepth = 0;
		OfxRectI dstRect = {0, 0, 0, 0};
		outputImg = ofxuGetImage(myData->output, time, dstRowBytes, dstBitDepth, dstIsAlpha, dstRect, dstData);

		if (outputImg == NULL) throw OfxuNoImageException();
		float scale = globalData[pluginIndex].scale;

		OfxPointD renderScale = {1., 1.};
		gPropHost->propGetDoubleN(inArgs, kOfxImageEffectPropRenderScale, 2, &renderScale.x);

		myData->sequenceDataP->time = time;
		myData->sequenceDataP->downsample_x = (float)renderScale.x;
		myData->sequenceDataP->downsample_y = (float)renderScale.y;
		
		// get the input image(s); the parameters are fetched afterwards, since point parameters are
		// relative to the first input's rectangle (the image G'MIC processes), not the output's
		OfxRectI firstSrcRect = dstRect;
		double firstSrcPAR = 1.;
		bool firstSrcPremultiplied = false;
		gPropHost->propGetDouble(outputImg, kOfxImagePropPixelAspectRatio, 0, &firstSrcPAR);
		int i = 0;
		for (int p = 0; p < globalData[pluginIndex].nofParams; p++) {
			if (globalData[pluginIndex].param[p].paramType != PT_LAYER) continue;
			int srcRowBytes = 0, srcBitDepth = 0;
			OfxRectI srcRect = {0, 0, 0, 0};
			bool srcIsAlpha = false;
			void *srcData = NULL;

			myData->sequenceDataP->inputConnected[i] = false;
			if (ofxuIsClipConnected(instance, globalData[pluginIndex].param[p].paramName.c_str())) {
				myData->sequenceDataP->inputConnected[i] = true;
				// every input at the render time: a per-layer time offset was read from floatValue[i],
				// which holds the value of the i-th filter parameter, not a layer setting
				const double tofs = time;
				sourceImg[i] = ofxuGetImage(myData->input[i], tofs, srcRowBytes, srcBitDepth, srcIsAlpha, srcRect, srcData);
			}
			if (sourceImg[i] == NULL) {
				myData->sequenceDataP->inputConnected[i] = false;
				if (i == 0) {
					throw OfxuNoImageException();
				} else {
					myData->sequenceDataP->inWorld[i].data = NULL;
					break;
				}
			}

			if (i == 0 && srcRect.x2 > srcRect.x1 && srcRect.y2 > srcRect.y1) {
				firstSrcRect = srcRect; // not an empty rectangle (point conversion divides by its size)
				gPropHost->propGetDouble(sourceImg[i], kOfxImagePropPixelAspectRatio, 0, &firstSrcPAR);
			}
			myData->sequenceDataP->inWorld[i].origin_x = srcRect.x1;
			myData->sequenceDataP->inWorld[i].origin_y = srcRect.y1;
			myData->sequenceDataP->inWorld[i].width = (srcRect.x2 - srcRect.x1);
			myData->sequenceDataP->inWorld[i].height = (srcRect.y2 - srcRect.y1);
			myData->sequenceDataP->inWorld[i].bitDepth = srcBitDepth;
			myData->sequenceDataP->inWorld[i].pixelFormat = PF_RGBA;
			myData->sequenceDataP->inWorld[i].data = srcData;
			myData->sequenceDataP->inWorld[i].rowBytes = srcRowBytes;


#if defined(CONVERT_INT_TO_FLOAT)
			if (srcBitDepth == 8) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourF);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 32;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
	
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
#ifdef PLANAR_BUFFER
					t[j] = thread(convert_RGBA8_to_RGBA32P, data, startLine, endLine);
#else
					t[j] = thread(convert_RGBA8_to_RGBA32, data, startLine, endLine);
#endif
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else

#ifdef PLANAR_BUFFER
				convert_RGBA8_to_RGBA32P(data, 0, myData->sequenceDataP->inWorld[i].height);
#else
				convert_RGBA8_to_RGBA32(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif

#endif
				customInputData[i] = true;
#ifdef PLANAR_BUFFER
			} else if (srcBitDepth == 32) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourF);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 32;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
					t[j] = thread(convert_RGBA32_to_RGBA32P, data, startLine, endLine);
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else				
				convert_RGBA32_to_RGBA32P(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif
				customInputData[i] = true;
#endif			
			}



#elif defined(CONVERT_FLOAT_TO_INT)
			if (srcBitDepth == 32) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourB);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 8;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
#ifdef PLANAR_BUFFER
					t[j] = thread(convert_RGBA32_to_RGBA8P, data, startLine, endLine);
#else
					t[j] = thread(convert_RGBA32_to_RGBA8, data, startLine, endLine);
#endif
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else

#ifdef PLANAR_BUFFER
				convert_RGBA32_to_RGBA8P(data, 0, myData->sequenceDataP->inWorld[i].height);
#else
				convert_RGBA32_to_RGBA8(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif

#endif
				customInputData[i] = true;
#ifdef PLANAR_BUFFER
			} else if (srcBitDepth == 8) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourB);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 8;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
					t[j] = thread(convert_RGBA8_to_RGBA8P, data, startLine, endLine);
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else				
				convert_RGBA8_to_RGBA8P(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif
				customInputData[i] = true;
#endif
			}
#elif defined(PLANAR_BUFFER)
			if (srcBitDepth == 8) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourB);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 8;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
					t[j] = thread(convert_RGBA8_to_RGBA8P, data, startLine, endLine);
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else				
				convert_RGBA8_to_RGBA8P(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif
			} else if (srcBitDepth == 32) {
				myData->sequenceDataP->inWorld[i].rowBytes = myData->sequenceDataP->inWorld[i].width * sizeof(OfxRGBAColourF);
				myData->sequenceDataP->inWorld[i].data = new unsigned char[(size_t)myData->sequenceDataP->inWorld[i].height * (size_t)myData->sequenceDataP->inWorld[i].rowBytes];
				myData->sequenceDataP->inWorld[i].bitDepth = 32;
				ConvertData data(srcData, srcRowBytes, myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].rowBytes, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
#ifndef NO_MULTITHREADED_CONVERSION
				int nofLines = (int)(floor((float)myData->sequenceDataP->inWorld[i].height / (float)NOF_CONVERT_THREADS) + 1.f);
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
					int startLine = j * nofLines;
					int endLine = min((j + 1) * nofLines, myData->sequenceDataP->inWorld[i].height);
					t[j] = thread(convert_RGBA32_to_RGBA32P, data, startLine, endLine);
				}
				for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else				
				convert_RGBA32_to_RGBA32P(data, 0, myData->sequenceDataP->inWorld[i].height);
#endif
			}
			customInputData[i] = true;

#endif
			// G'MIC works on straight colours: unpremultiply premultiplied inputs (the converted buffer
			// is float planar for both 8-bit and float images, so this also covers 8-bit inputs)
			if (propsArePremultiplied(sourceImg[i]) && customInputData[i] && myData->sequenceDataP->inWorld[i].bitDepth == 32) {
				if (i == 0) firstSrcPremultiplied = true;
				unpremultiplyPlanar((float*)myData->sequenceDataP->inWorld[i].data, myData->sequenceDataP->inWorld[i].width, myData->sequenceDataP->inWorld[i].height, scale);
			}
			i++;
		}

		if (!(firstSrcPAR > 0.)) firstSrcPAR = 1.;
		getAllParamData(pluginIndex, firstSrcRect, firstSrcPAR, myData, time, renderScale);

		myData->sequenceDataP->outWorld.pixelFormat = PF_RGBA;
		myData->sequenceDataP->outWorld.origin_x = dstRect.x1;
		myData->sequenceDataP->outWorld.origin_y = dstRect.y1;
		myData->sequenceDataP->outWorld.width = (dstRect.x2 - dstRect.x1);
		myData->sequenceDataP->outWorld.height = (dstRect.y2 - dstRect.y1);
		myData->sequenceDataP->outWorld.rowBytes = dstRowBytes;
		myData->sequenceDataP->outWorld.bitDepth = dstBitDepth;
		myData->sequenceDataP->outWorld.data = dstData;

#if defined(CONVERT_INT_TO_FLOAT)
		if (dstBitDepth == 8) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourF);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
			customOutputData = true;
#ifdef PLANAR_BUFFER
		} else if (dstBitDepth == 32) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourF);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
			customOutputData = true;
#endif
		}
#elif defined(CONVERT_FLOAT_TO_INT)
		if (dstBitDepth == 32) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourB);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
			customOutputData = true;
#ifdef PLANAR_BUFFER
		} else if (dstBitDepth == 8) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourB);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
			customOutputData = true;
#endif
		}
#elif defined(PLANAR_BUFFER)
		if (dstBitDepth == 8) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourB);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
		} else if (dstBitDepth == 32) {
			myData->sequenceDataP->outWorld.rowBytes = myData->sequenceDataP->outWorld.width * sizeof(OfxRGBAColourF);
			myData->sequenceDataP->outWorld.data = new unsigned char[(size_t)myData->sequenceDataP->outWorld.height * (size_t)myData->sequenceDataP->outWorld.rowBytes];
		}
		customOutputData = true;
#endif

		// here we call the actual processing function of the plugin! If G'MIC failed, the output
		// buffer holds no valid image: fail the render instead of converting garbage
		if (pluginProcess(myData->sequenceDataP, &globalData[pluginIndex], &contextData) != 0) {
			throw OfxuStatusException(kOfxStatFailed);
		}

		// the G'MIC result has straight colours, converted here before the conversion to the output depth
		if (customOutputData) {
			const SequenceData* sd = myData->sequenceDataP;
			if (firstSrcPremultiplied && !sd->resultHasAlpha &&
				sd->resultWidth == sd->inWorld[0].width && sd->resultHeight == sd->inWorld[0].height) {
				// no alpha in the result: keep the look of the input composited over black (see
				// multiplyByInputAlphaPlanar)
				multiplyByInputAlphaPlanar(sd->outWorld, sd->inWorld[0], scale);
			}
			// premultiply if the output image is premultiplied: the host's value, normally the one
			// declared in getClipPreferences
			if (propsArePremultiplied(outputImg)) {
				premultiplyPlanar((float*)sd->outWorld.data, sd->outWorld.width, sd->outWorld.height, scale);
			}
		}

#if defined(CONVERT_INT_TO_FLOAT)
		if (dstBitDepth == 8) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
#ifdef PLANAR_BUFFER
				t[j] = thread(convert_RGBA32P_to_RGBA8, data, startLine, endLine);
#else
				t[j] = thread(convert_RGBA32_to_RGBA8, data, startLine, endLine);
#endif
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else

#ifdef PLANAR_BUFFER
			convert_RGBA32P_to_RGBA8(data, 0, myData->sequenceDataP->outWorld.height);
#else
			convert_RGBA32_to_RGBA8(data, 0, myData->sequenceDataP->outWorld.height);
#endif

#endif

#ifdef PLANAR_BUFFER
		} else if (dstBitDepth == 32) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
				t[j] = thread(convert_RGBA32P_to_RGBA32, data, startLine, endLine);
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else
			convert_RGBA32P_to_RGBA32(data, 0, myData->sequenceDataP->outWorld.height);
#endif
#endif
		}


#elif defined(CONVERT_FLOAT_TO_INT)
		if (dstBitDepth == 32) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
#ifdef PLANAR_BUFFER
				t[j] = thread(convert_RGBA8P_to_RGBA32, data, startLine, endLine);
#else
				t[j] = thread(convert_RGBA8_to_RGBA32, data, startLine, endLine);
#endif
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else

#ifdef PLANAR_BUFFER
			convert_RGBA8P_to_RGBA32(data, 0, myData->sequenceDataP->outWorld.height);
#else
			convert_RGBA8_to_RGBA32(data, 0, myData->sequenceDataP->outWorld.height);
#endif

#endif

#ifdef PLANAR_BUFFER
		} else if (dstBitDepth == 8) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
				t[j] = thread(convert_RGBA8P_to_RGBA8, data, startLine, endLine);
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else
			convert_RGBA8P_to_RGBA8(data, 0, myData->sequenceDataP->outWorld.height);
#endif
#endif

		}


#elif defined(PLANAR_BUFFER)
		if (dstBitDepth == 8) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
				t[j] = thread(convert_RGBA8P_to_RGBA8, data, startLine, endLine);
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else
			convert_RGBA8P_to_RGBA8(data, 0, myData->sequenceDataP->outWorld.height);
#endif
		} else if (dstBitDepth == 32) {
			ConvertData data(myData->sequenceDataP->outWorld.data, myData->sequenceDataP->outWorld.rowBytes, dstData, dstRowBytes, myData->sequenceDataP->outWorld.width, myData->sequenceDataP->outWorld.height, 1.f / scale);
#ifndef NO_MULTITHREADED_CONVERSION
			int nofLines = (int)(floor((float)myData->sequenceDataP->outWorld.height / (float)NOF_CONVERT_THREADS) + 1.f);
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
				int startLine = j * nofLines;
				int endLine = min((j + 1) * nofLines, myData->sequenceDataP->outWorld.height);
				t[j] = thread(convert_RGBA32P_to_RGBA32, data, startLine, endLine);
			}
			for (int j = 0; j < NOF_CONVERT_THREADS; ++j) t[j].join();
#else
			convert_RGBA32P_to_RGBA32(data, 0, myData->sequenceDataP->outWorld.height);
#endif
		}
#endif

	} catch(OfxuNoImageException &ex) {
		// if we were interrupted, the failed fetch is fine, just return kOfxStatOK
		// otherwise, something weird happened
		if (!gEffectHost->abort(instance)) {
			// status = kOfxStatFailed;
		}
	} catch(OfxuStatusException &ex) {
		status = ex.status();
	} catch(std::bad_alloc&) {
		status = kOfxStatErrMemory;
	} catch(...) {
		// e.g. a std::exception from the image conversion: fail this render, but still release
		// the host images and the converted buffers below
		status = kOfxStatFailed;
	}

#ifndef NO_MULTITHREADED_CONVERSION
	// if an exception interrupted a conversion, wait for the threads already started before
	// freeing the buffers they use (and destroying a joinable std::thread would terminate)
	for (int j = 0; j < NOF_CONVERT_THREADS; ++j) {
		if (t[j].joinable()) t[j].join();
	}
#endif

	// release the data pointers
	for (int i = 0; i < globalData[pluginIndex].nofInputs; i++) {
		if (sourceImg[i]) {
			gEffectHost->clipReleaseImage(sourceImg[i]);
		}
		if (customInputData[i]) {
			delete [] (unsigned char*)myData->sequenceDataP->inWorld[i].data;
			myData->sequenceDataP->inWorld[i].data = NULL;
		}
	}
	if (customOutputData) {
		delete [] (unsigned char*)myData->sequenceDataP->outWorld.data;
		myData->sequenceDataP->outWorld.data = NULL;
	}
	if (outputImg) gEffectHost->clipReleaseImage(outputImg);
	
	return status;
}

static void paramAddFloat(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, double minValue, double maxValue, double defaultValue, int precision, int dflags)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeDouble, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	if (dflags > 0) gPropHost->propSetString(props, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeScale);
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, defaultValue);
	gPropHost->propSetDouble(props, kOfxParamPropMin, 0, minValue);
	gPropHost->propSetDouble(props, kOfxParamPropMax, 0, maxValue);
	gPropHost->propSetDouble(props, kOfxParamPropDisplayMin, 0, minValue);
	gPropHost->propSetDouble(props, kOfxParamPropDisplayMax, 0, maxValue);
	gPropHost->propSetDouble(props, kOfxParamPropIncrement, 0, 0.01);
	gPropHost->propSetDouble(props, kOfxParamPropDigits, 0, precision);	
}

static void paramAddColor(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, double red, double green, double blue, double alpha)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeRGBA, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, red);
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, green);
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 2, blue);
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 3, alpha);
}

static void paramAddText(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, const char* defaultText, int textType)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeString, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	if (textType == 0) { // single-line
		gPropHost->propSetString(props, kOfxParamPropDefault, 0, defaultText);
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
	} else if (textType == 1) { // multi-line
		gPropHost->propSetString(props, kOfxParamPropDefault, 0, defaultText);
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsMultiLine);
	} else if (textType == 2) { // file / fileout
		gPropHost->propSetString(props, kOfxParamPropDefault, 0, defaultText);
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsFilePath);
		gPropHost->propSetInt(props, kOfxParamPropStringFilePathExists, 0, 0);
	} else if (textType == 3) { // folder
		gPropHost->propSetString(props, kOfxParamPropDefault, 0, defaultText);
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsDirectoryPath);
	} else if (textType == 4) { // label
		std::string text = strTrim(strRemoveXmlTags(defaultText, true), " \t\r\n'\"");
		gPropHost->propSetString(props, kOfxParamPropDefault, 0, text.c_str());
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsLabel);
		gPropHost->propSetString(props, kOfxParamPropHint, 0, text.c_str()); // for long labels
	} else if (textType == 5) { // filein
		gPropHost->propSetString(props, kOfxParamPropStringMode, 0, kOfxParamStringIsFilePath);
		gPropHost->propSetInt(props, kOfxParamPropStringFilePathExists, 0, 1);
	}
}

static void paramAddInt(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, int minValue, int maxValue, int defaultValue)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeInteger, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue);
	gPropHost->propSetInt(props, kOfxParamPropMin, 0, minValue);
	gPropHost->propSetInt(props, kOfxParamPropMax, 0, maxValue);
	gPropHost->propSetInt(props, kOfxParamPropDisplayMin, 0, minValue);
	gPropHost->propSetInt(props, kOfxParamPropDisplayMax, 0, maxValue);
}

static void paramAddPoint(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, double defaultX, double defaultY)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeDouble2D, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, defaultX);
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 1, defaultY);
	gPropHost->propSetString(props, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeXYAbsolute);
	gPropHost->propSetString(props, kOfxParamPropDefaultCoordinateSystem, 0, kOfxParamCoordinatesNormalised);
	gPropHost->propSetInt(props, kOfxParamPropUseHostOverlayHandle, 0, 1);
}

static void paramAddAngle(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, double defaultValue)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeDouble, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetDouble(props, kOfxParamPropDefault, 0, defaultValue);
	gPropHost->propSetString(props, kOfxParamPropDoubleType, 0, kOfxParamDoubleTypeAngle);
}

static void paramAddBool(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, int defaultValue)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeBoolean, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue);
    gPropHost->propSetInt(props, kOfxParamPropAnimates, 0, 0);
}

static void paramAddChoice(OfxParamSetHandle effectParams, OfxPropertySetHandle& props, const char* paramName, int maxValue, int defaultValue, vector<string> choices)
{
	OfxStatus stat = gParamHost->paramDefine(effectParams, kOfxParamTypeChoice, paramName, &props);
	if (stat != kOfxStatOK) {
		throw OfxuStatusException(stat);
	}
	gPropHost->propSetInt(props, kOfxParamPropDefault, 0, defaultValue);
	for (int i = 0; i < min(maxValue, (int)choices.size()); i++) gPropHost->propSetString(props, kOfxParamPropChoiceOption, i, choices[i].c_str());
    gPropHost->propSetInt(props, kOfxParamPropAnimates, 0, false);
}

// describe the plugin in context
static OfxStatus describeInContext(int pluginIndex, OfxImageEffectHandle effect, OfxPropertySetHandle inArgs)
{
	// get the context from the inArgs handle
	const char *context = NULL;
	gPropHost->propGetString(inArgs, kOfxImageEffectPropContext, 0, &context);
	// bool isGeneralContext = strcmp(context, kOfxImageEffectContextGeneral) == 0;

	OfxPropertySetHandle props = NULL;
	// define the single output clip in both contexts
	gEffectHost->clipDefine(effect, "Output", &props);

	// set the component types we can handle on our output
	gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

	// clip properties only (thread safety, frame threading, multi-resolution and multiple clip
	// PARs/depths are effect properties, set in describe())
	gPropHost->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);

	// fetch the parameter set from the effect
	OfxParamSetHandle paramSet;
	gEffectHost->getParamSet(effect, &paramSet);

	{
	gParamHost->paramDefine(paramSet, kOfxParamTypeCustom, "SequenceData", &props);
	gPropHost->propSetInt(props, kOfxParamPropSecret, 0, 1);
	}
	
#ifdef ABOUT_DIALOG
	{
#ifndef ABOUT_DIALOG_CAPTION
#define ABOUT_DIALOG_CAPTION "About"
#endif
	gParamHost->paramDefine(paramSet, kOfxParamTypePushButton, ABOUT_DIALOG_CAPTION, &props);
	}
#endif

	string group;
	int l = 0;
	for (int i = 0; i < globalData[pluginIndex].nofParams; i++) {
		const reduxfx::Parameter& param = globalData[pluginIndex].param[i];
		float mmin = param.minValue;
		float mmax = param.maxValue;
		
		if (param.paramType == PT_FLOAT) {
			paramAddFloat(paramSet, props,
				param.paramName.c_str(), // param
				mmin, //valid min
				mmax, //valid max
				param.defaultValue[0], //default
				2, //precision
				param.flags //display flags
				);
		} else if (param.paramType == PT_COLOR) {
			paramAddColor(
				paramSet, props,
				param.paramName.c_str(), // param
				param.defaultValue[0], 
				param.defaultValue[1], 
				param.defaultValue[2],
				param.defaultValue[3]
				); 
		} else if (param.paramType == PT_TEXT) {
			std::string txt = param.text;
			bool multiline = false;
			strReplace(txt, "\\r", "\r");
			strReplace(txt, "\\n", "\n");
			multiline |= (txt.find_first_of("\r\n") != string::npos);
			int flags = param.flags;
			// force the param to be multiline if there are newlines in the default value
			if (multiline && flags == 0) {
				flags = 1;
			}
			paramAddText(
				paramSet, props,
				param.paramName.c_str(), // param
				txt.c_str(), flags);
		} else if (param.paramType == PT_INT) {
			paramAddInt(
				paramSet, props,
				param.paramName.c_str(), // param
				(int)mmin, //valid min
				(int)mmax, //valid max
				(int)param.defaultValue[0] //default
				);
		} else if (param.paramType == PT_POINT) {
			// point parameter is in % (percent) in gmic: no need to scale
			paramAddPoint(
				paramSet, props,
				param.paramName.c_str(), // param
				param.defaultValue[0]/100., //default
				1. - param.defaultValue[1]/100. //default: G'MIC's y goes down, OFX normalised y goes up
			);
		} else if (param.paramType == PT_BOOL) {
			paramAddBool(
				paramSet, props,
				param.paramName.c_str(), // param
				(int)param.defaultValue[0] //default
			);
		} else if (param.paramType == PT_SELECT) {	
			string t = param.text;
			// t = strReplace(t, "|-|", "|(-|");
			vector<string> choices;
			strSplit(t, '|', choices);
			paramAddChoice(
				paramSet, props,
				param.paramName.c_str(), // param
				(int)param.maxValue+1, //max
				(int)(min(param.maxValue,param.defaultValue[0])), //default
				choices // choices
			);
		} else if (param.paramType == PT_ANGLE) {	
			paramAddAngle(
				paramSet, props,
				param.paramName.c_str(), // param
				param.defaultValue[0] //default
			);
		} else if (param.paramType == PT_SEPARATOR) {
			// props still contains the properties for the previous parameter
#ifdef OFX_EXTENSIONS_NUKE
			if (props) {
				gPropHost->propSetInt(props, kOfxParamPropLayoutHint, 0, kOfxParamPropLayoutHintDivider);
			}
#endif
			continue;
		} else if (param.paramType == PT_LAYER) {
			gEffectHost->clipDefine(effect, param.paramName.c_str(), &props);
			if (i > 0) {
				gPropHost->propSetInt(props, kOfxImageClipPropOptional, 0, 1);
			}
			gPropHost->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
			gPropHost->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
			l++;
			continue;
		} else if (param.paramType == PT_TOPIC_START) {	
			gParamHost->paramDefine(paramSet, kOfxParamTypeGroup, param.paramName.c_str(), &props);
			if (param.flags>0) gPropHost->propSetInt(props, kOfxParamPropGroupOpen, 0, 0);
			gPropHost->propSetInt(props, kOfxParamPropAnimates, 0, 0);
			gPropHost->propSetInt(props, kOfxParamPropGroupOpen, 0, param.flags>0?0:1);
			group = param.paramName;
		} else if (param.paramType == PT_TOPIC_END) {	
			group = "";
			continue;
		} else {
			assert(false);
			continue;
		}

		if (param.displayStatus == DS_DISABLED) {
			gPropHost->propSetInt(props, kOfxParamPropEnabled, 0, 0);
		} else if (param.displayStatus == DS_HIDDEN) {
			gPropHost->propSetInt(props, kOfxParamPropSecret, 0, 1);
		}
		gPropHost->propSetString(props, kOfxParamPropScriptName, 0, param.paramName.c_str());
		gPropHost->propSetString(props, kOfxPropLabel, 0, param.displayName.c_str());
		if (!param.hint.empty()) {
			gPropHost->propSetString(props, kOfxParamPropHint, 0, param.hint.c_str());
		}

		if (group != "" && param.paramType != PT_TOPIC_START) 
			gPropHost->propSetString(props, kOfxParamPropParent, 0, group.c_str());
	}
	if (globalData[pluginIndex].nofInputs == 0) {
		globalData[pluginIndex].nofInputs = l;
	}

	return kOfxStatOK;
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
// - All characters within 0x01..0x1f and 0x7F..0x9F, except \t \b \r (0x09 0x0A 0X0D) are forbidden
//   in label and grouping, because these may be used to build the GUI before loading the plugin.
//   They may appear in the description, although it is not recommended.
// - The '<' and '&' characters generate bad XML in all cases
// - The '>' and '\'' characters are tolerated by the expat parser, although the XML is not valid
static string validateXMLString(std::string const & s)
{
    string r;
    for (size_t i=0;i<s.size();i++) {
        // The are exactly five characters which must be escaped
        // http://www.w3.org/TR/xml/#syntax
        switch (s[i]) {
            case '\t':
            case '\n':
            case '\r':
            case '"':
            case '\'':
            case '>':
                break;
            case '<':
                break;
            case '&':
                break;
            default: {
                unsigned char c = (unsigned char)(s[i]);
                if ( !((0x01 <= c && c <= 0x1f) || (0x7F <= c && c <= 0x9F)) ) {
                    r += c;
                }
            } break;
        }
    }
    return r;
}

////////////////////////////////////////////////////////////////////////////////
// the plugin's description routine
static OfxStatus describe(int pluginIndex, OfxImageEffectHandle effect)
{
	// record a few host features
#ifdef OFX_EXTENSIONS_NATRON
	gPropHost->propGetInt(gHost->host, kNatronOfxHostIsNatron, 0, &gHostIsNatron);
#endif


	// get the property handle for the plugin
	OfxPropertySetHandle effectProps;
	gEffectHost->getPropertySet(effect, &effectProps);

	// We can render both fields in a fielded images in one hit if there is no animation
	// So set the flag that allows us to do this
	gPropHost->propSetInt(effectProps, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);

	// say we cannot support multiple pixel depths
	gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
	// nor multiple clip pixel aspect ratios (the default, set explicitly)
	gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0);
	
	// set the bit depths the plugin can handle
	gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
	gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthByte);

	// set some labels and the group it belongs to
	if (gHostIsNatron) {
		gPropHost->propSetString(effectProps, kOfxPropLabel, 0, globalData[pluginIndex].pluginInfo.name.c_str());
		gPropHost->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, globalData[pluginIndex].pluginInfo.category.c_str());
		gPropHost->propSetString(effectProps, kOfxPropPluginDescription, 0, globalData[pluginIndex].pluginInfo.description.c_str());
	} else {
		// workaround for bugs in the OpenFX plugin cache, e.g. in Nuke
		gPropHost->propSetString(effectProps, kOfxPropLabel, 0, validateXMLString(globalData[pluginIndex].pluginInfo.name).c_str());
		gPropHost->propSetString(effectProps, kOfxImageEffectPluginPropGrouping, 0, validateXMLString(globalData[pluginIndex].pluginInfo.category).c_str());
		gPropHost->propSetString(effectProps, kOfxPropPluginDescription, 0, validateXMLString(globalData[pluginIndex].pluginInfo.description).c_str());
	}

	// define the contexts we can be used in
	gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
	gPropHost->propSetString(effectProps, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);

	gPropHost->propSetString(effectProps, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderInstanceSafe);
	gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsTiles, 0, 0);
	gPropHost->propSetInt(effectProps, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
	gPropHost->propSetInt(effectProps, kOfxImageEffectPropSupportsMultiResolution, 0, 0);

	const char *path = NULL;
	gPropHost->propGetString(effectProps, kOfxPluginPropFilePath, 0, &path);

	if (path) {
		string p = string(path);
	#ifdef _WIN32
		int iPos = (int)p.find_last_of("/\\");
	#else
		int iPos = (int)p.find_last_of("/");
	#endif
		if (iPos >= 0) {
			globalData[pluginIndex].pluginPath = p.substr(0, iPos);
			globalData[pluginIndex].pluginFilename = p.substr(iPos + 1);
		} else {
			globalData[pluginIndex].pluginPath = p;
			globalData[pluginIndex].pluginFilename = p;
		}
	}

    gPropHost->propSetString(effectProps, kOfxImageEffectPropClipPreferencesSlaveParam, 0, "Animate Random Seed");

	return kOfxStatOK;
}


////////////////////////////////////////////////////////////////////////////////
// The main function
static OfxStatus pluginMain(int pluginIndex, const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
// LOG << "main: " << pluginIndex << " " << action;
	try {
	// cast to appropriate type
	OfxImageEffectHandle effect = (OfxImageEffectHandle)handle;

	if (strcmp(action, kOfxActionDescribe) == 0) {
		return describe(pluginIndex, effect);
	}
	else if (strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
		return describeInContext(pluginIndex, effect, inArgs);
	}
	else if (strcmp(action, kOfxActionLoad) == 0) {
		return onLoad(pluginIndex);
	}
	else if (strcmp(action, kOfxActionUnload) == 0) {
		return onUnload(pluginIndex);
	}
	else if (strcmp(action, kOfxActionCreateInstance) == 0) {
		return createInstance(pluginIndex, effect);
	} 
	else if (strcmp(action, kOfxActionDestroyInstance) == 0) {
		return destroyInstance(pluginIndex, effect);
	} 
	else if (strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
		return isIdentity(pluginIndex, effect, inArgs, outArgs);
	}		
	else if (strcmp(action, kOfxImageEffectActionRender) == 0) {
		return render(pluginIndex, effect, inArgs, outArgs);
	}		
	else if (strcmp(action, kOfxImageEffectActionGetRegionOfDefinition) == 0) {
		return getSpatialRoD(pluginIndex, effect, inArgs, outArgs);
	}
	else if (strcmp(action, kOfxActionSyncPrivateData) == 0) {
		return syncPrivateData(pluginIndex, effect);
	}	
	else if (strcmp(action, kOfxImageEffectActionGetRegionsOfInterest) == 0) {
		return getSpatialRoI(pluginIndex, effect, inArgs, outArgs);
	}	
	else if (strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
		return getClipPreferences(pluginIndex, effect, inArgs, outArgs);
	}	
	else if (strcmp(action, kOfxActionInstanceChanged) == 0) {
		return instanceChanged(pluginIndex, effect, inArgs, outArgs);
	}	
	else if (strcmp(action, kOfxImageEffectActionGetTimeDomain) == 0) {
		return getTemporalDomain(pluginIndex, effect, inArgs, outArgs);
	}	
	} catch (std::bad_alloc) {
		// catch memory
		//std::cout << "OFX Plugin Memory error." << std::endl;
		return kOfxStatErrMemory;
	} catch (const std::exception& e) {
		// standard exceptions
		//std::cout << "OFX Plugin error: " << e.what() << std::endl;
		return kOfxStatErrUnknown;
	} catch (int err) {
		// ho hum, gone wrong somehow
		return err;
	} catch (...) {
		// everything else
		//std::cout << "OFX Plugin error" << std::endl;
		return kOfxStatErrUnknown;
	}

	// other actions to take the default value
	return kOfxStatReplyDefault;
}

// function to set the host structure
static void setHostFunc(OfxHost *hostStruct)
{
	gHost = hostStruct;
	OfxStatus stat = ofxuFetchHostSuites();
	if (stat == kOfxStatOK) {
		gMessageSuiteV2 = (OfxMessageSuiteV2*)gHost->fetchSuite(gHost->host, kOfxMessageSuite, 2);
	}
}

////////////////////////////////////////////////////////////////////////////////
// the plugin struct 

// this little macro/template trickery was originally found in Alexandre Gauthier-Foichat's OFX G'MIC plugin
template<int nth>
static OfxStatus pluginMainNth(const char *action, const void *handle, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    return pluginMain(nth, action, handle, inArgs, outArgs);
}
#define NTHFUNC(nth) case nth: return pluginMainNth<nth>;
#define NTHFUNC10(nth) NTHFUNC(nth); NTHFUNC(nth+1); NTHFUNC(nth+2); NTHFUNC(nth+3); NTHFUNC(nth+4); NTHFUNC(nth+5); NTHFUNC(nth+6); NTHFUNC(nth+7); NTHFUNC(nth+8); NTHFUNC(nth+9)
#define NTHFUNC100(nth) NTHFUNC10(nth); NTHFUNC10(nth+10); NTHFUNC10(nth+20); NTHFUNC10(nth+30); NTHFUNC10(nth+40); NTHFUNC10(nth+50); NTHFUNC10(nth+60); NTHFUNC10(nth+70); NTHFUNC10(nth+80); NTHFUNC10(nth+90)
#define NTHFUNC1000(nth) NTHFUNC100(nth); NTHFUNC100(nth+100); NTHFUNC100(nth+200); NTHFUNC100(nth+300); NTHFUNC100(nth+400); NTHFUNC100(nth+500); NTHFUNC100(nth+600); NTHFUNC100(nth+700); NTHFUNC100(nth+800); NTHFUNC100(nth+900)
static const int kMaxNofPlugins = 2000; // number of NTHFUNC entries below
static OfxPluginEntryPoint* pluginMainNthFunc(int nth)
{
    switch (nth) {
        NTHFUNC1000(0);
        NTHFUNC1000(1000); // G'MIC 4 has more than 1000 filters
    }
    return 0;
}
#undef NTHFUNC

static
void initPlugins()
{
	int n = getNofPlugins();
	if (n > kMaxNofPlugins) n = kMaxNofPlugins; // no entry point beyond pluginMainNthFunc's table
	gPlugins.resize(n);
	globalData.resize(n);
	for (int i = 0; i < n; i++) {
		getPluginInfo(i, globalData[i].pluginInfo);
		globalData[i].pluginIndex = i;
		gPlugins[i].pluginApi = kOfxImageEffectPluginApi;
		gPlugins[i].apiVersion = 1;
		gPlugins[i].pluginIdentifier = globalData[i].pluginInfo.identifier.c_str();
		gPlugins[i].pluginVersionMajor = globalData[i].pluginInfo.major_version;
		gPlugins[i].pluginVersionMinor = globalData[i].pluginInfo.minor_version;
		gPlugins[i].setHost = setHostFunc;
		gPlugins[i].mainEntry = pluginMainNthFunc(i);
	}
}

// the two mandated functions
EXPORT OfxPlugin* OfxGetPlugin(int nth)
{
	if ((int)gPlugins.size() == 0) {
		initPlugins();
	}
	if (nth < (int)gPlugins.size()) return &gPlugins[nth];
	return NULL;
}
 
EXPORT int OfxGetNumberOfPlugins(void)
{
	initPlugins();
	int n = (int)gPlugins.size();
	return n;
} 

#endif

