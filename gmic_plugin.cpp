/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */
/*
 #
 #  File        : gmic_plugin.cpp
 #
 #  Description : The sources for a plugin that can be compiled for both 
 #                After Effects and OpenFX hosts and which interface the
 #                G'MIC library     
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

// History:
// 0.3.2:
// - original version from https://github.com/dtschump/gmic-community/tree/master/gmic_plugin
// 1.0.0: (these changes were only tested for the OpenFX plugin)
// - Source input is now always the topmost G'MIC layer, other inputs are below
// - handle separator() as a PT_SEPARATOR parameter
// - handle point(...)
// - handle note(...) as a label parameter (PT_TEXT with flags=4)
// - handle filein(..) and fileout(...)
// - color(...) is now RGBA
// - better handle link(...)
// - correctly handle multiple parameters on the same line, or multi-line parameters of any kind.
// - the G'MIC command-line is only computed when rendering, not at each param change (because converting pixel coords to % for point(...) requires image size)
// - bug fixes
//
// TODO:
// - there may be any number of gmic parameters per line, see for example fx_hearts
// - why doesn't "mineral mosaic" show the note after the separator?
//
// version, name and description for the plugin
#define	MAJOR_VERSION		1
#define	MINOR_VERSION		0
#define	BUILD_VERSION		0

#define PLUGIN_DESCRIPTION	"Wrapper for the G'MIC framework (http://gmic.eu) written by Tobias Fleischer (http://www.reduxfx.com) and Frederic Devernay.";

#ifdef OFX_PLUGIN
#define PLUGIN_NAME	        "GMIC"
#define PLUGIN_CATEGORY		"GMIC"
#define PLUGIN_UNIQUEID		"GMIC"
#else
// AE plugin needs these with spaces to replace them with proper effect names in the gmic_ae_tool
#define PLUGIN_NAME	        "G'MIC Plugin                   "
#define PLUGIN_CATEGORY		"G'MIC                          "
#define PLUGIN_UNIQUEID		"gmic_plugin                    "
#endif

#ifndef OFX_PLUGIN
#ifndef AE_PLUGIN
#define OFX_PLUGIN
#endif
#endif

// the following flags apply to the OFX plugin interface only and are ignored if set in an AE plugin!
#define NO_MULTITHREADED_CONVERSION
#define IMG_FLIP_Y

// the following flags apply to the AE plugin interface only and are ignored if set in an OFX plugin!
#define	_PF_OutFlag2_AE13_5_THREADSAFE 4194304
#define _PF_OutFlag2_SUPPORTS_GET_FLATTENED_SEQUENCE_DATA 8388608
#define FX_OUT_FLAGS PF_OutFlag_NON_PARAM_VARY + PF_OutFlag_DEEP_COLOR_AWARE + PF_OutFlag_I_DO_DIALOG + PF_OutFlag_SEND_UPDATE_PARAMS_UI + PF_OutFlag_USE_OUTPUT_EXTENT + PF_OutFlag_CUSTOM_UI
#define FX_OUT_FLAGS2 PF_OutFlag2_SUPPORTS_SMART_RENDER + PF_OutFlag2_FLOAT_COLOR_AWARE + PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG + _PF_OutFlag2_AE13_5_THREADSAFE + _PF_OutFlag2_SUPPORTS_GET_FLATTENED_SEQUENCE_DATA

#ifndef PIPL

// if this is defined, 8bpc (0-255) input layers will be transformed into 32bpc (0.0-1.0) input layers
// this should in most cases be set, as you then only have to write algorithms for float data
#define CONVERT_INT_TO_FLOAT

// if this is defined, 32bpc (0.0-1.0) input layers will be transformed into 8bpc (0-255) input layers
// this should in most cases NOT be set, as you will lose precision
// #define CONVERT_FLOAT_TO_INT

// if this is defined, pixel data is not stored interleaved internally (RGBARGBARGBA...) but in a planar buffer (RRR... GGG... BBB... AAA...)
// this should in most cases NOT be set, unless you (or a specific library) need the pixel buffers in this format
#define PLANAR_BUFFER

// Layout of the last parameters (see pluginSetup): Command, [Advanced Options group start], Output Layer,
// Resize Mode, Ignore Alpha, Preview/Draft Mode, Global Random Seed, Animate Random Seed, Log Verbosity,
// [group end].
#define PARAM_COMMAND (globalDataP->nofParams - 10)
#define PARAM_OUTPUT (globalDataP->nofParams - 8)
#define PARAM_RESIZE (globalDataP->nofParams - 7)
#define PARAM_NOALPHA (globalDataP->nofParams - 6)
#define PARAM_PREVIEW (globalDataP->nofParams - 5)
#define PARAM_SRAND (globalDataP->nofParams - 4)
#define PARAM_ANIMSEED (globalDataP->nofParams - 3)
#define PARAM_VERBOSITY (globalDataP->nofParams - 2)

#ifdef OFX_PLUGIN
    #include "RFX_OFX_Utils.h"
#else
    #include "RFX_AE_Utils.h"
#endif

#include "gmic_parser.h"
#include "gmic_libc.h"
#include <cmath>

#include "RFX_FileUtils.h"

using namespace reduxfx;

#ifdef OFX_PLUGIN
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#ifdef DEBUG
#include <iostream>
#endif


static
string get_gmic_rc_path()
{
	string path;
	const char *_path_rc = 0;
/*	if (!_path_rc) _path_rc = getenv("GMIC_PATH");
	if (!_path_rc) _path_rc = getenv("GMIC_GIMP_PATH");
	if (!_path_rc) _path_rc = getenv("XDG_CONFIG_HOME"); */
	if (!_path_rc) {
#ifndef _WIN32
		_path_rc = getenv("HOME");
		if (_path_rc) {
			path = string(_path_rc) + "/.config/gmic/";
		}
#else
		_path_rc = getenv("APPDATA");
#endif
	}
	if (_path_rc && path.empty()) {
		path = string(_path_rc) + "/gmic/";
	}
	return path;
}

// parameters added by pluginSetup besides the filter's own: Input, 4 layers, Command, the
// "Advanced Options" group (start and end) and its 7 parameters
static const int kNofExtraParams = 1 + 4 + 1 + 2 + 7;

class PluginGlobalData
{
public:
	vector<EffectData> pluginData;
	vector<string> pluginContent;
	PluginGlobalData() {
		string effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_ofx.gmic");
		if (effectContent.empty()) {
			effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_stdlib.gmic");
		}
		if (effectContent.empty()) {
			const char* lib = gmic_get_stdlib();
			effectContent = string(lib);
			gmic_delete_external((float*)lib);
			if (
				(int)effectContent.find("#@gimp :") < 0 && 
				(int)effectContent.find("#@gmic_plugin :") < 0 && 
				(int)effectContent.find("#@gui :") < 0) {
				effectContent = "";
			}
		} 
		gmic_parse_multi(effectContent, &pluginData, &pluginContent);
		// Each plug-in has a fixed table of MAX_NOF_PARAMS parameters: the filter's own parameters,
		// up to 4 extra layers and the fixed trailing parameters (see pluginSetup). Leave out the
		// few filters that do not fit (e.g. "Transfer Color [Reduced Colors]" in G'MIC 4.0.7,
		// with several hundred parameters) instead of overflowing the table.
		for (size_t i = 0; i < pluginData.size(); ) {
			if ((int)pluginData[i].param.size() + kNofExtraParams > MAX_NOF_PARAMS) {
				pluginData.erase(pluginData.begin() + i);
				pluginContent.erase(pluginContent.begin() + i);
			} else {
				++i;
			}
		}
	};
};

PluginGlobalData pluginGlobalData;

#endif

class MyGlobalData
{
public:
#ifdef AE_PLUGIN
	EffectData effectData;
	string effectContent;
#endif
	MyGlobalData() {};
	~MyGlobalData() {};
};

class MySequenceData
{
public:
	//string command;
	gmic_interface_options options;

	MySequenceData() {
		options.custom_commands = NULL;
		options.error_message_buffer = new char[256];
		options.error_message_buffer[0] = '\0';
		options.ignore_stdlib = false;
		options.p_is_abort = NULL;
		options.p_progress = NULL;
		options.output_format = E_FORMAT_FLOAT;
		options.interleave_output = false;
		options.no_inplace_processing = false;
	};
	~MySequenceData() {
		delete[] options.error_message_buffer;
	};
};

void* createCustomGlobalData() { return new MyGlobalData(); }
void destroyCustomGlobalData(void* customGlobalDataP) { delete (MyGlobalData*)customGlobalDataP; }
void* createCustomSequenceData() { return new MySequenceData(); }
void destroyCustomSequenceData(void* customSequenceDataP) { delete (MySequenceData*)customSequenceDataP; }
void* flattenCustomSequenceData(void* /*customUnflatSequenceDataP*/, int& /*flatSize*/) { return NULL; }
void* unflattenCustomSequenceData(void* /*customSequenceDataP*/, int /*flatSize*/) { return new MySequenceData(); }

int pluginSetdown(GlobalData* /*globalDataP*/, ContextData* /*contextDataP*/) { return 0; }

int pluginSetup(GlobalData* globalDataP, ContextData* /*contextDataP*/)
{
	globalDataP->scale = 255.f;
	globalDataP->buttonName = "Reload";


	EffectData effectData;
	effectData.multiLayer = true;
#ifdef AE_PLUGIN
	MyGlobalData* myGlobalDataP = (MyGlobalData*)globalDataP->customGlobalDataP;
	string filename = globalDataP->pluginFilename;
	int pos = (int)filename.find_last_of(".");
	if (pos > 0) filename = filename.substr(0, pos);
	filename = globalDataP->pluginPath + filename + ".gmic";
	string effectContent = loadStringFromFile(filename);
	effectContent = gmic_parse_single(effectContent, effectData);
	if (myGlobalDataP) {
		myGlobalDataP->effectContent = effectContent;
		myGlobalDataP->effectData = effectData;
	}
	bool hasContent = effectContent != "";
#else
	effectData = pluginGlobalData.pluginData[globalDataP->pluginIndex];
	bool hasContent = true;
#endif	
	
	globalDataP->inplaceProcessing = false;

#ifdef AE_PLUGIN
	globalDataP->param[0] = Parameter("Input", "", PT_LAYER);
#else
	globalDataP->param[0] = Parameter(kOfxImageEffectSimpleSourceClipName, "", PT_LAYER);
#endif
	int p = 1;
	for (int i = 0; i < (int)effectData.param.size(); i++) {
		int t = PT_FLOAT;
		int flags = 0;
		bool h = false;
		float d1 = 0.f;
		float d2 = 0.f;
		float d3 = 0.f;
		float d4 = 0.f;
		const EffectParameter& param = effectData.param[i];
		std::string name = param.name;
		// a filter parameter must not reuse the name of a parameter added below (none does in
		// G'MIC 4.0.7, but a clash would make the host reject the duplicate parameter)
		static const char* const reservedNames[] = { "Layer -1", "Layer -2", "Layer -3", "Layer -4", "Command",
			"Advanced Options", "Output Layer", "Resize Mode", "Ignore Alpha", "Preview/Draft Mode",
			"Global Random Seed", "Animate Random Seed", "Log Verbosity" };
		for (size_t r = 0; r < sizeof(reservedNames) / sizeof(reservedNames[0]); ++r) {
			if (name == reservedNames[r]) {
				name += " (filter)";
				break;
			}
		}
		const std::string& minValue = param.minValue;
		const std::string& maxValue = param.maxValue;
		const std::string& defaultValue = param.defaultValue;
		const std::string& text = param.text;
		const std::string& paramType = param.paramType;
		if (paramType == "color") {
			vector<string> r;
			t = PT_COLOR;
			strSplit(defaultValue, '|', r); // drops empty fields: pad below
			if (r.size() == 1) { r.push_back(r[0]); r.push_back(r[0]); } // gray: color(V)
			while (r.size() < 3) r.push_back("0");
			d1 = (float)atof(r[0].c_str()) / 255.f;
			d2 = (float)atof(r[1].c_str()) / 255.f;
			d3 = (float)atof(r[2].c_str()) / 255.f;
			// color(R,_G,_B,_A): only pass alpha to G'MIC if the filter declares it
			flags = (r.size() > 3 && !r[3].empty()) ? 4 : 3;
			d4 = (flags == 4) ? (float)atof(r[3].c_str()) / 255.f : 1.f;
		} else if (paramType == "point") {
			vector<string> r;
			t = PT_POINT;
			strSplit(defaultValue, '|', r); // drops empty fields: pad below
			while (r.size() < 2) r.push_back("50");
			d1 = (float)atof(r[0].c_str());
			d2 = (float)atof(r[1].c_str());
#ifdef OFX_PLUGIN
			// erase any occurence of " (%)" in the string, because the displayed parameter will be in pixels
			size_t start_pos = name.find(" (%)");
    		if(start_pos != string::npos) {
				name.erase(start_pos, 4);
			}
#endif
		} else {
			d1 = (float)atof(defaultValue.c_str());
			d2 = d1; d3 = d1; d4 = d1;
			if (paramType == "bool" || paramType == "button") {
				t = PT_BOOL;
			} else if (paramType == "choice") {
				t = PT_SELECT;
				// "color" and "point": see above
			} else if (paramType == "value") {
				h = true;
				t = PT_TEXT;
			} else if (paramType == "file" || paramType == "fileout") {
				t = PT_TEXT;
				flags = 2;
			} else if (paramType == "filein") {
				t = PT_TEXT;
				flags = 5;
			} else if (paramType == "float") {
				t = PT_FLOAT;
			} else if (paramType == "folder") {
				t = PT_TEXT;
				flags = 3;
			} else if (paramType == "int") {
				t = PT_INT;
			} else if (paramType == "note" || paramType == "link") {
				t = PT_TEXT;
				string note = strRemoveXmlTags(strTrim(text, " \t\r\n'\""));
				strReplace(note, "\\n", "\n");
				effectData.notes += note + '\n';
				flags = 4;
			} else if (paramType == "text") {
				t = PT_TEXT;
			} else if (paramType == "separator") {
				t = PT_SEPARATOR;
			} else if (paramType == "input") {
				t = PT_LAYER;
			} else {
				assert(false);
			}
		}
		// a value(...) parameter is a hidden constant argument: its value is the default, which
		// the parser stores in defaultValue (it used to be passed as an empty string)
		const std::string& paramText = (paramType == "value") ? defaultValue : text;
		globalDataP->param[p] = Parameter(
		  name, "", t, (float)atof(minValue.c_str()), (float)atof(maxValue.c_str()), d1, d2, d3, d4, paramText);
		globalDataP->param[p].flags = flags;
		if (h) globalDataP->param[p].displayStatus = DS_HIDDEN;
		p++;
	}

//	globalDataP->param[p++] = Parameter("Dummy", PT_FLOAT);

	if (effectData.multiLayer) {
		globalDataP->param[p] = Parameter("Layer -1", "", PT_LAYER);
		++p;
		globalDataP->param[p] = Parameter("Layer -2", "", PT_LAYER);
		++p;
		globalDataP->param[p] = Parameter("Layer -3", "", PT_LAYER);
		++p;
		globalDataP->param[p] = Parameter("Layer -4", "", PT_LAYER);
		++p;
	}	
	globalDataP->param[p] = Parameter("Command", "", PT_TEXT, 0, 0, 0, 0, 0, 0, "-blur 2");
	if (hasContent) {
		globalDataP->param[p].displayStatus = DS_HIDDEN;
	}
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", PT_TOPIC_START);
	globalDataP->param[p].flags = 1;
	++p;
	globalDataP->param[p] = Parameter("Output Layer", "", PT_SELECT, 0, 10, 1, 0, 0, 0, "Merged|Layer 0|Layer -1|Layer -2|Layer -3|Layer -4|Layer -5|Layer -6|Layer -7|Layer -8|Layer -9|");
	++p;
	globalDataP->param[p] = Parameter("Resize Mode", "", PT_SELECT, 0, 5, 1, 0, 0, 0, "Fixed (Inplace)|Dynamic|Downsample 1/2|Downsample 1/4|Downsample 1/8|Downsample 1/16");
	++p;
	globalDataP->param[p] = Parameter("Ignore Alpha", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	++p;
	globalDataP->param[p] = Parameter("Preview/Draft Mode", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	if (!hasContent || effectData.command == effectData.preview_command) {
		globalDataP->param[p].displayStatus = DS_HIDDEN;
	}
	++p;
	globalDataP->param[p] = Parameter("Global Random Seed", "", PT_INT, 0, 1<<24, 0, 0, 0, 0, "");
	++p;
	globalDataP->param[p] = Parameter("Animate Random Seed", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	++p;
	globalDataP->param[p] = Parameter("Log Verbosity", "", PT_SELECT, 0, 4, 0, 0, 0, 0, "Off|Level 1|Level 2|Level 3|");
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", PT_TOPIC_END);
	++p;
	globalDataP->nofParams = p;

	string d = effectData.notes;
/*
	for (int i = 0; i < effectData.notes.size(); i++)
		if (effectData.notes[i] >= 32 && effectData.notes[i] < 128) d += effectData.notes[i];
*/
	d = strRemoveXmlTags(d, true);
	d = strToAscii(d);
	strReplace(d, "<", "(");
	strReplace(d, ">", ")");
//	strReplace(d, "\n", "\\n");
	globalDataP->pluginInfo.description = d + "\n" + PLUGIN_DESCRIPTION;

	return 0;
}

static
const EffectData&
effectDataOf(GlobalData* globalDataP)
{
#ifdef AE_PLUGIN
    return ((MyGlobalData*)globalDataP->customGlobalDataP)->effectData;
#else
    return pluginGlobalData.pluginData[globalDataP->pluginIndex];
#endif
}

// the filter's command line, with its main or preview command
static
std::string
gmicCommand(SequenceData* sequenceDataP, GlobalData* globalDataP, bool preview)
{
    const EffectData& effectData = effectDataOf(globalDataP);
    string cmd = preview ? effectData.preview_command : effectData.command;

    if (cmd == "") {
        cmd = PAR_TXT(PARAM_COMMAND);
    } else {
        cmd = "-" + strTrim(cmd, " \t\r\n") + " ";
        for (int i = 1; i < PARAM_COMMAND; i++) { // filter parameters only
            if (PAR_TYPE(i) == PT_INT || PAR_TYPE(i) == PT_BOOL || PAR_TYPE(i) == PT_SELECT)
                cmd += intToString((int)PAR_VAL(i)) + ",";
            else if (PAR_TYPE(i) == PT_FLOAT)
                cmd += floatToString(PAR_VAL(i)) + ",";
            else if (PAR_TYPE(i) == PT_TEXT) {
                if (globalDataP->param[i].flags == 4) {
                    // label/note
                    continue;
                }
                string s = PAR_TXT(i);
                // G'MIC un-escapes \ and \" inside "...": escape backslashes first (e.g. Windows paths),
                // then double quotes (or a text containing one, e.g. G'MIC code, ends the argument early)
                strReplace(s, "\\", "\\\\");
                strReplace(s, "\"", "\\\"");
                cmd += "\"" + s + "\",";
            } else if (PAR_TYPE(i) == PT_COLOR) {
                cmd +=
                floatToString((255.f * PAR_CH(i, 0))) + "," +
                floatToString((255.f * PAR_CH(i, 1))) + "," +
                floatToString((255.f * PAR_CH(i, 2))) + ",";
                if (globalDataP->param[i].flags == 4) {
                    cmd += floatToString((255.f * PAR_CH(i, 3))) + ",";
                }
            } else if (PAR_TYPE(i) == PT_POINT) {
                cmd +=
                floatToString(PAR_CH(i, 0)) + "," +
                floatToString(PAR_CH(i, 1)) + ",";
            }
        }
        cmd = cmd.substr(0, cmd.size() - 1);
    }

    strReplace(cmd, "\r", " ");
    strReplace(cmd, "\n", " ");

    return cmd;
}

// The whole command line: the filter's command (main or preview), with the variables G'MIC-Qt
// defines, the output layer selection, Ignore Alpha, the Resize Mode, verbosity and seed. Sets the
// custom commands of the options (stored in customCommands, which must outlive the call).
static
std::string
fullCommand(SequenceData* sequenceDataP, GlobalData* globalDataP, unsigned int nofImages, bool isPreview, string& customCommands)
{
	MySequenceData* mySequenceDataP = (MySequenceData*)sequenceDataP->customSequenceDataP;
#ifdef AE_PLUGIN
	mySequenceDataP->options.custom_commands = ((MyGlobalData*)globalDataP->customGlobalDataP)->effectContent.c_str();
#else
	mySequenceDataP->options.custom_commands = pluginGlobalData.pluginContent[globalDataP->pluginIndex].c_str();
#endif
	string cmd = gmicCommand(sequenceDataP, globalDataP, isPreview);

	if (PAR_VAL(PARAM_OUTPUT) == 0.f) {
		// G'MIC >= 2.9 renamed gimp_merge_layers to gui_merge_layers ("Unknown command" otherwise)
		cmd += " -gui_merge_layers";
	}

	// gmic_call() copies every resulting image into images[], which has MAX_NOF_LAYERS entries:
	// keep at most that many (the last ones, since the Output Layer is counted from the end).
	// Some filters return more by default, e.g. Fade Layers, Morph Layers, Tiles to Layers.
	cmd += " -if {$!>" + intToString(MAX_NOF_LAYERS) + "} -keep[-" + intToString(MAX_NOF_LAYERS) + "--1] -fi";

	// Ignore Alpha: apply_channels takes the command as a quoted string, which it un-escapes
	// again, so quoting the command (whose text arguments are already escaped) breaks any text
	// containing a quote or backslash. Define it as a custom command and apply that instead.
	customCommands = mySequenceDataP->options.custom_commands ? mySequenceDataP->options.custom_commands : "";
	if (PAR_VAL(PARAM_NOALPHA) > 0.f) {
		strReplace(cmd, "\n", " ");
		strReplace(cmd, "\r", " ");
		customCommands += "\n_openfx_gmic_cmd :\n  " + cmd + "\n";
		mySequenceDataP->options.custom_commands = customCommands.c_str();
		cmd = "-apply_channels \"_openfx_gmic_cmd\",rgb";
	}
	
	// Variables that G'MIC-Qt defines for the filters (GmicProcessor.cpp, FilterThread.cpp), with
	// the whole image as the preview area. Its size is the largest input size, as G'MIC-Qt's
	// LayersExtentProxy::getExtent() (all the inputs have the size of the first one if the Resize
	// Mode downsamples them).
	// - _preview_x0/y0/x1/y1, defined by G'MIC-Qt for the preview and when applying the filter:
	//   e.g. the preview command of Array [Random] [Jumble by Px], which its main command calls,
	//   crops to it.
	// - _preview_width/height and _preview_area_width/height only for a preview run: main
	//   commands use them to detect a preview (Speech Bubble draws its guides). For a preview,
	//   gui_preview_wh now returns the image size instead of 400x400 for small images.
	// - _host (G'MIC-Qt's host short name) only for a preview run: the stdlib compares it with
	//   "cli" or "paintdotnet", but Array [Random] [Jumble by Px]'s preview tests
	//   '$_host'!='$cli' (always true when _host is defined), so main runs keep it undefined.
	// _persistent is not defined: filters whose main command needs it are rendered with the
	// preview command (see pluginProcess).
	int extentW = 0, extentH = 0;
	const float dsMode = globalDataP->inplaceProcessing ? 0.f : PAR_VAL(PARAM_RESIZE);
	if (dsMode >= 2.f) {
		const int ds = (int)pow(2.f, dsMode - 1.f);
		extentW = sequenceDataP->inWorld[0].width / ds;
		extentH = sequenceDataP->inWorld[0].height / ds;
	} else {
		for (unsigned int i = 0; i < nofImages; ++i) {
			extentW = std::max(extentW, sequenceDataP->inWorld[i].width);
			extentH = std::max(extentH, sequenceDataP->inWorld[i].height);
		}
	}
	const string W = intToString(extentW), H = intToString(extentH);
	string env = "_preview_x0=0 _preview_y0=0 _preview_x1=" + intToString(extentW - 1) + " _preview_y1=" + intToString(extentH - 1) + " ";
	if (isPreview) {
		env += "_host=ofx _preview_width=" + W + " _preview_height=" + H +
			" _preview_area_width=" + W + " _preview_area_height=" + H + " ";
	}
	cmd = env + cmd;

	if (!globalDataP->inplaceProcessing) {
		float ds = PAR_VAL(PARAM_RESIZE);
		if (ds >= 2.f) {
			ds = pow(2.f, ds - 1.f);
			cmd = " -resize " + intToString(sequenceDataP->inWorld[0].width / (int)ds) + "," + intToString(sequenceDataP->inWorld[0].height / (int)ds) + " " + cmd;
		}
		cmd += " -resize " + intToString(sequenceDataP->inWorld[0].width) + "," + intToString(sequenceDataP->inWorld[0].height);
 	}
	int verbosity = (int)PAR_VAL(PARAM_VERBOSITY) - 1;
	int seed = (int)PAR_VAL(PARAM_SRAND);
	bool animated_seed = (int)PAR_VAL(PARAM_ANIMSEED);
	if (animated_seed) {
		seed += (int)sequenceDataP->time;
	}

	cmd = "-v " + intToString(verbosity) + " -srand " + intToString(seed) + " " + cmd;
	// cmd += " -display";
	// set some variables that are defined globally for the GIMP plugin
	// not really needed as the effects should work independently of GIMP
//	cmd = "_input_mode=1 _output_mode=0 _verbosity_mode=0 _preview_mode=0 _preview_size=0 " + cmd;

	return cmd;
}

int pluginParamChange(int index, SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* /*contextDataP*/)
{
	if (index == PARAM_RESIZE) {
		globalDataP->inplaceProcessing = PAR_VAL(index) < 1.f;
	} else {
	}
	return 0;
}

int pluginProcess(SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* contextDataP)
{
	/*
	float* fi = (float*)sequenceDataP->inWorld[0].data;
	float* fo = (float*)sequenceDataP->outWorld.data;
	for (int x = 0; x < sequenceDataP->outWorld.width * sequenceDataP->outWorld.height * 4; x++)
	{
		*fo = *fi * 2.f;//(float)(rand() % 100) / 100.f;
		fi++;
		fo++;
	}
	return 0;
	*/

	int err = 0;

	MySequenceData* mySequenceDataP = (MySequenceData*)sequenceDataP->customSequenceDataP;
	mySequenceDataP->options.no_inplace_processing = !globalDataP->inplaceProcessing;

	gmic_interface_image images[MAX_NOF_LAYERS];
	unsigned int nofImages = 0;
	for (int i = 0; i < globalDataP->nofInputs; i++) {
		if (!sequenceDataP->inWorld[i].data || !sequenceDataP->inputConnected[i]) break;
		nofImages++;
	}
	if (nofImages == 0) {
		// no input image (main input not connected): output transparent black, not uninitialized memory
		if (sequenceDataP->outWorld.data) {
			memset(sequenceDataP->outWorld.data, 0, (size_t)sequenceDataP->outWorld.height * (size_t)sequenceDataP->outWorld.rowBytes);
		}
#ifdef OFX_PLUGIN
		if (gMessageSuiteV2) {
			gMessageSuiteV2->clearPersistentMessage(contextDataP->instance);
		}
#endif
		return err;
	}

	for (unsigned int i = 0; i < nofImages; i++) {
		images[i].width = sequenceDataP->inWorld[i].width;
		images[i].height = sequenceDataP->inWorld[i].height;
		images[i].data = sequenceDataP->inWorld[i].data;
		images[i].spectrum = 4;
		images[i].depth = 1;
		images[i].name[0] = '\0';
		images[i].format = E_FORMAT_FLOAT;
		images[i].is_interleaved = false;
		strncpy(images[i].name, string("input" + intToString(i)).c_str(), sizeof(images[i].name));
	}

	// G'MIC-Qt runs the preview command first, and some main commands need its state: render
	// these with the preview command when their main command fails (EffectData::previewFallback)
	const EffectData& effectData = effectDataOf(globalDataP);
	const bool hasContent = !effectData.command.empty();
	bool preview = hasContent && (effectData.previewOnly || PAR_VAL(PARAM_PREVIEW) > 0);
	string customCommands;
	string cmd = fullCommand(sequenceDataP, globalDataP, nofImages, preview, customCommands);
	int result = gmic_call(cmd.c_str(), &nofImages, &images[0], &mySequenceDataP->options);
	string errmsg = string(mySequenceDataP->options.error_message_buffer);
	// The retry needs unchanged inputs, i.e. gmic_call() working on copies: always the case in the
	// OFX build (inplaceProcessing is only set by pluginParamChange(), which only the AE glue calls).
	if (result != 0 && !preview && effectData.previewFallback && mySequenceDataP->options.no_inplace_processing) {
		fprintf(stderr, "G'MIC: %s: the main command failed (%s), rendering with the preview command\n",
				effectData.name.c_str(), errmsg.c_str());
		preview = true;
		cmd = fullCommand(sequenceDataP, globalDataP, nofImages, preview, customCommands);
		result = gmic_call(cmd.c_str(), &nofImages, &images[0], &mySequenceDataP->options);
		if (result != 0) {
			errmsg += " (with the preview command: " + string(mySequenceDataP->options.error_message_buffer) + ")";
		}
	}

	if (result == 0) {
		if (!globalDataP->inplaceProcessing) {
			int idx = (int)PAR_VAL(PARAM_OUTPUT);
			if (idx > 0) {
				idx = nofImages - idx;
				if (idx < 0) idx = 0;
			}

			if (nofImages == 0 || (unsigned)idx >= nofImages || images[idx].spectrum == 0 || !images[idx].data) {
				result = -1;
				errmsg = "The image produced by G'MIC is empty.";
			} else {
				// Planar output R,G,B,A from 1 (G), 2 (GA), 3 (RGB) or 4+ (RGBA) channels. The G'MIC
				// result is anchored at the first input's rectangle, the output has its own rectangle
				// (both in pixels, rows top-down in the planar buffers): copy the overlap, clear the rest.
				const World& out = sequenceDataP->outWorld;
				const int dw = out.width, dh = out.height;
				const int sw = (int)images[idx].width, sh = (int)images[idx].height;
				// G'MIC anchors images at their top-left corner: a result of another size than the first
				// input keeps the input's left and top edges (pixel y goes up)
				const int sx1 = sequenceDataP->inWorld[0].origin_x;
				const int sy1 = sequenceDataP->inWorld[0].origin_y + sequenceDataP->inWorld[0].height - sh;
				const int dx1 = out.origin_x, dy1 = out.origin_y;
				const size_t dplane = (size_t)dw * (size_t)dh, splane = (size_t)sw * (size_t)sh;
				float* const dst = (float*)out.data;
				const float* const src = (const float*)images[idx].data;
				const unsigned int spectrum = images[idx].spectrum;
				// source plane for each output channel, or NULL for opaque alpha
				const float* plane[4];
				for (int c = 0; c < 3; ++c) plane[c] = src + (spectrum >= 3 ? splane * c : 0);
				plane[3] = (spectrum == 2) ? src + splane : (spectrum >= 4 ? src + splane * 3 : NULL);
				const bool same = (sw == dw && sh == dh && sx1 == dx1 && sy1 == dy1);
				if (!same) {
					memset(dst, 0, dplane * 4 * sizeof(float));
				}
				// overlap, in pixel coordinates (y up)
				const int x1 = std::max(sx1, dx1), x2 = std::min(sx1 + sw, dx1 + dw);
				const int y1 = std::max(sy1, dy1), y2 = std::min(sy1 + sh, dy1 + dh);
				for (int c = 0; c < 4; ++c) {
					if (same) {
						if (plane[c]) memcpy(dst + dplane * c, plane[c], dplane * sizeof(float));
						else std::fill(dst + dplane * c, dst + dplane * (c + 1), 255.f);
						continue;
					}
					for (int y = y1; y < y2; ++y) {
						float* drow = dst + dplane * c + (size_t)(dh - 1 - (y - dy1)) * dw + (x1 - dx1);
						if (plane[c]) {
							const float* srow = plane[c] + (size_t)(sh - 1 - (y - sy1)) * sw + (x1 - sx1);
							memcpy(drow, srow, (size_t)std::max(0, x2 - x1) * sizeof(float));
						} else {
							std::fill(drow, drow + std::max(0, x2 - x1), 255.f);
						}
					}
				}
			}
		}
	}
	if (result != 0) {
#ifdef OFX_PLUGIN
		if (gMessageSuiteV2) {
			// persistent message on the node (cleared by the next successful render), instead of a
			// modal message on every render
			gMessageSuiteV2->setPersistentMessage(contextDataP->instance, kOfxMessageError, "", "%s", errmsg.c_str());
		} else if (gMessageSuite) {
			gMessageSuite->message(contextDataP->instance, kOfxMessageError, "G'MIC Error", "%s", errmsg.c_str());
#ifdef DEBUG
		} else {
			cout << "ERROR: " << errmsg << endl;
#endif
		}
#endif
#ifdef AE_PLUGIN
		errmsg = errmsg.substr(0, 255);
		strcpy(contextDataP->out_data->return_msg, errmsg.c_str());
#endif
		err = PF_Err_INTERNAL_STRUCT_DAMAGED;
	}
#ifdef OFX_PLUGIN
	else if (gMessageSuiteV2) {
		gMessageSuiteV2->clearPersistentMessage(contextDataP->instance);
	}
#endif

	for (unsigned int i = 0; i < nofImages; i++) {
		bool found = false;
		for (int j = 0; j < globalDataP->nofInputs; j++) {
			if (images[i].data == sequenceDataP->inWorld[j].data) {
				found = true;
				break;
			}
		}
		if (!found) gmic_delete_external((float*)images[i].data);
	}
	return err;
}

#ifdef OFX_PLUGIN

int getNofPlugins()
{
	return (int)pluginGlobalData.pluginData.size();
}

void getPluginInfo(int pluginIndex, PluginInfo& pluginInfo)
{
	pluginInfo.name = pluginGlobalData.pluginData[pluginIndex].name;
	pluginInfo.identifier = pluginGlobalData.pluginData[pluginIndex].uniqueId;
	pluginInfo.category = pluginGlobalData.pluginData[pluginIndex].category;
	
	string d = pluginGlobalData.pluginData[pluginIndex].notes;
	//strReplace(d, "\n", "\\n");
	d = strRemoveXmlTags(d, true);
	d = strToAscii(d);
	strReplace(d, "<", "(");
	strReplace(d, ">", ")");
	pluginInfo.description = d + "\n\n" + PLUGIN_DESCRIPTION;
	pluginInfo.major_version = MAJOR_VERSION;
	pluginInfo.minor_version = MINOR_VERSION;
}

#else

extern "C" DllExport PF_Err EntryPointFunc(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* param[], PF_LayerDef* outputP, void* extraP)
{
	if (cmd == PF_Cmd_DO_DIALOG) {
		GlobalData* globalDataP = ((AE_GlobalData*)PF_LOCK_HANDLE(in_data->global_data))->globalDataP;
		pluginSetup(globalDataP, NULL);
		PF_UNLOCK_HANDLE(in_data->global_data);
		return PF_Err_NONE;
	} else {
		return pluginMain(cmd, in_data, out_data, param, outputP, extraP); 
	}
};

#endif

#endif
