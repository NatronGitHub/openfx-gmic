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
#include "gmic_plugin_call.h"
#include <cmath>

#include "RFX_FileUtils.h"

using namespace reduxfx;

#ifdef OFX_PLUGIN
#include <sstream>
#include <map>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <system_error>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#endif
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

// Hot fixes for bugs in the G'MIC standard library (gmic_hotfixes.gmic, embedded by the build as
// the null-terminated array gmic_hotfixes[]): corrected command definitions, passed to G'MIC as
// custom commands, which replace the built-in ones. Angle brackets: the header generated by CMake
// in the build directory must be found before a stale one left in the sources by the Makefile.
#include <gmic_hotfixes.h>

static inline bool gmicIsBlank(char c) { return (unsigned char)c <= ' '; } // as cimg::is_blank

// Get the definitions of the commands that are keys of defs from a G'MIC command file, as G'MIC
// reads them (see gmic::add_commands): comments, blank lines and leading and trailing blanks
// removed, each line followed by '\n'. A later definition replaces an earlier one.
static void gmicGetDefinitions(const string& content, std::map<string, string>& defs)
{
	string* def = NULL;
	const size_t len = content.size();
	for (size_t pos = 0; pos < len; ) {
		size_t b = pos, e = content.find('\n', pos);
		if (e == string::npos) e = len;
		pos = e + 1;
		if (content[b] == '#') continue;
		for (size_t i = b + 1; i < e; ++i) {
			if (content[i] == '#' && gmicIsBlank(content[i - 1])) { e = i - 1; break; }
		}
		while (e > b && gmicIsBlank(content[e - 1])) --e;
		while (b < e && gmicIsBlank(content[b])) ++b;
		if (b == e) continue;
		// a command definition is '[+]name :', where the name has at most 255 characters
		// [a-zA-Z0-9_] and does not start with a digit, and ':' is not followed by '='
		size_t n = b + (content[b] == '+' ? 1 : 0), ne = n;
		while (ne < e && ne - n < 255 && (isalnum((unsigned char)content[ne]) || content[ne] == '_')) ++ne;
		size_t c = ne;
		while (c < e && gmicIsBlank(content[c])) ++c;
		if (ne > n && !isdigit((unsigned char)content[n]) && c < e && content[c] == ':' && (c + 1 == e || content[c + 1] != '=')) {
			std::map<string, string>::iterator it = defs.find(content.substr(b, ne - b));
			def = (it == defs.end()) ? NULL : &it->second;
			if (def) def->clear();
		}
		if (def) {
			def->append(content, b, e - b);
			*def += '\n';
		}
	}
}

// FNV-1a 64-bit hash of a definition, as 16 hexadecimal digits
static string gmicChecksum(const string& def)
{
	unsigned long long h = 14695981039346656037ULL;
	for (size_t j = 0; j < def.size(); ++j) {
		h = (h ^ (unsigned char)def[j]) * 1099511628211ULL;
	}
	char s[17];
	snprintf(s, sizeof(s), "%016llx", h);
	return s;
}

// The hot fixes that apply to the G'MIC standard library stdlib. Each block of gmic_hotfixes.gmic
// starts with '#@hotfix <command> <original> <fixed> [<callee>:<checksum>...]': the FNV-1a 64-bit
// hashes of the built-in definition the fix was made from, of the fixed definition (the block's
// own), and of the built-in definitions of the commands the fix relies on. A block is applied if
// the built-in command is the original one and the callees are unchanged, skipped silently if the
// built-in command already is the fixed one (fixed upstream), and otherwise skipped with a warning.
static string gmicGetHotfixes(const string& stdlib)
{
	static const string marker = "#@hotfix ";
	vector<vector<std::pair<string, string> > > checks; // per block: (command, checksum)
	vector<string> fixed, texts; // per block: checksum of the fixed definition, block text
	std::istringstream ss(string((const char*)gmic_hotfixes));
	string line;
	while (getline(ss, line)) {
		if (line.compare(0, marker.size(), marker) == 0) {
			std::istringstream ls(line.substr(marker.size()));
			vector<std::pair<string, string> > check;
			string name, checksum, fixedChecksum, callee;
			ls >> name >> checksum >> fixedChecksum;
			check.push_back(std::make_pair(name, checksum));
			while (ls >> callee) {
				const string::size_type colon = callee.find(':');
				check.push_back(std::make_pair(callee.substr(0, colon), colon == string::npos ? string() : callee.substr(colon + 1)));
			}
			checks.push_back(check);
			fixed.push_back(fixedChecksum);
			texts.push_back(string());
		}
		if (!texts.empty()) {
			texts.back() += line + '\n';
		}
	}
	std::map<string, string> defs;
	for (size_t i = 0; i < checks.size(); ++i) {
		for (size_t k = 0; k < checks[i].size(); ++k) {
			defs[checks[i][k].first];
		}
	}
	gmicGetDefinitions(stdlib, defs);
	string hotfixes;
	for (size_t i = 0; i < checks.size(); ++i) {
		const string& command = checks[i][0].first;
		std::map<string, string> own;
		own[command];
		gmicGetDefinitions(texts[i], own);
		const string ownChecksum = gmicChecksum(own[command]);
		if (own[command].empty() || ownChecksum != fixed[i]) {
			fprintf(stderr, "openfx-gmic: Warning: hot fix for the G'MIC command '%s' not applied: the fixed definition has checksum %s, but its '#@hotfix' line says '%s', update it in gmic_hotfixes.gmic\n",
			        command.c_str(), ownChecksum.c_str(), fixed[i].c_str());
			continue;
		}
		if (!defs[command].empty() && gmicChecksum(defs[command]) == ownChecksum) {
			continue; // already fixed in the built-in G'MIC (then the callees do not matter)
		}
		bool ok = true;
		for (size_t k = 0; k < checks[i].size(); ++k) {
			const string& name = checks[i][k].first;
			const string& expected = checks[i][k].second;
			const string& def = defs[name];
			const string found = gmicChecksum(def);
			// a callee given without a checksum only has to exist
			if (def.empty() || ((k == 0 || !expected.empty()) && expected != found)) {
				ok = false;
				fprintf(stderr, "openfx-gmic: Warning: hot fix for the G'MIC command '%s' not applied: the built-in definition of '%s' %s (checksum %s, the fix is for %s), review it in gmic_hotfixes.gmic\n",
				        command.c_str(), name.c_str(), def.empty() ? "is missing" : "changed", found.c_str(), expected.c_str());
			}
		}
		if (ok) {
			hotfixes += texts[i];
		}
	}
	return hotfixes;
}

class PluginGlobalData
{
public:
	vector<EffectData> pluginData;
	vector<string> pluginContent;
	string hotfixes; // custom commands for every run, see gmicGetHotfixes()
	PluginGlobalData() {
		// The hot fixes are checked against the built-in stdlib, which G'MIC always loads: decompress
		// it once here (~35 ms), even when the filters come from a user file (gmic_ofx.gmic).
		string stdlib;
		{
			const char* lib = gmic_get_stdlib();
			stdlib = string(lib);
			gmic_delete_external((float*)lib);
		}
		hotfixes = gmicGetHotfixes(stdlib);
		string effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_ofx.gmic");
		if (effectContent.empty()) {
			effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_stdlib.gmic");
		}
		if (effectContent.empty()) {
			effectContent.swap(stdlib);
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
			d1 = (float)stringToDouble(r[0]) / 255.f;
			d2 = (float)stringToDouble(r[1]) / 255.f;
			d3 = (float)stringToDouble(r[2]) / 255.f;
			// color(R,_G,_B,_A): only pass alpha to G'MIC if the filter declares it
			flags = (r.size() > 3 && !r[3].empty()) ? 4 : 3;
			d4 = (flags == 4) ? (float)stringToDouble(r[3]) / 255.f : 1.f;
		} else if (paramType == "point") {
			vector<string> r;
			t = PT_POINT;
			strSplit(defaultValue, '|', r); // drops empty fields: pad below
			while (r.size() < 2) r.push_back("50");
			d1 = (float)stringToDouble(r[0]);
			d2 = (float)stringToDouble(r[1]);
#ifdef OFX_PLUGIN
			// erase any occurence of " (%)" in the string, because the displayed parameter will be in pixels
			size_t start_pos = name.find(" (%)");
    		if(start_pos != string::npos) {
				name.erase(start_pos, 4);
			}
#endif
		} else {
			d1 = (float)stringToDouble(defaultValue);
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
		  name, "", t, (float)stringToDouble(minValue), (float)stringToDouble(maxValue), d1, d2, d3, d4, paramText);
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
	// "Fixed (Inplace)" (index 0) behaves like "Dynamic": the result is always resized to the input
	// size. Its special path (a per-plug-in in-place flag, set from one instance by a parameter
	// change callback that the OFX glue never called, and skipping the output copy) was removed.
	// The entry is kept so that saved projects keep their option indices and labels.
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
std::string
gmicCommand(SequenceData* sequenceDataP, GlobalData* globalDataP)
{
#ifdef AE_PLUGIN
    MyGlobalData* myGlobalDataP = (MyGlobalData*)globalDataP->customGlobalDataP;
    string cmd = PAR_VAL(PARAM_PREVIEW) > 0 ? myGlobalDataP->effectData.preview_command:myGlobalDataP->effectData.command;
#else
    string cmd = PAR_VAL(PARAM_PREVIEW) > 0 ? pluginGlobalData.pluginData[globalDataP->pluginIndex].preview_command:pluginGlobalData.pluginData[globalDataP->pluginIndex].command;
#endif

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

// parameter change callback of the host glue (only the AE glue calls it): nothing to do, see
// "Resize Mode" in pluginSetup
int pluginParamChange(int /*index*/, SequenceData* /*sequenceDataP*/, GlobalData* /*globalDataP*/, ContextData* /*contextDataP*/)
{
	return 0;
}

#ifdef OFX_PLUGIN
// Render abort (issue #46): G'MIC polls a flag (p_is_abort), in its interpreter and in CImg's
// OpenMP loops. The host's abort() must be called from the render thread (Natron reads
// thread-local render state), and G'MIC only returns when it is done. So run G'MIC in a worker
// thread, and poll abort() from the render thread while it runs: when the host aborts the render,
// set G'MIC's flag, and G'MIC stops at its next check.
struct GmicCallData
{
	const char* cmd;
	unsigned int* nofImages;
	gmic_interface_image* images;
	gmic_interface_options* options;
	int result;
	std::exception_ptr exception; // an exception from G'MIC, rethrown in the render thread
	std::mutex mutex;
	std::condition_variable cond;
	bool done;
};

static void gmicCallRun(GmicCallData* d)
{
	int result = -1;
	try {
		result = gmic_plugin_call(d->cmd, d->nofImages, d->images, MAX_NOF_LAYERS, d->options);
	} catch (...) {
		d->exception = std::current_exception();
	}
	std::lock_guard<std::mutex> lock(d->mutex);
	d->result = result;
	d->done = true;
	d->cond.notify_one();
}

#ifndef _WIN32
static void* gmicCallThread(void* arg)
{
	gmicCallRun((GmicCallData*)arg);
	return NULL;
}
#endif

// gmic_plugin_call(), aborted when the host aborts the render: then *aborted is set to true, and
// the result must be discarded.
static int gmicCallAbortable(OfxImageEffectHandle instance, const char* cmd, unsigned int* nofImages, gmic_interface_image* images, gmic_interface_options* options, bool* aborted)
{
	*aborted = false;
	if (!gEffectHost || !gEffectHost->abort) {
		return gmic_plugin_call(cmd, nofImages, images, MAX_NOF_LAYERS, options);
	}
	if (gEffectHost->abort(instance)) {
		*aborted = true;
		return -1;
	}
	// G'MIC's abort flag. It is a plain bool, read by G'MIC's threads while the render thread sets
	// it: that is upstream's design (the API takes a bool*, polled in G'MIC's loops), and gmic-qt
	// shares its flag across threads the same way. Note that CImg's OpenMP loops find the flag
	// through gmic::current_is_abort(), which looks up the interpreter by thread: a thread that is
	// not an interpreter's (e.g. an OpenMP worker evaluating the math abort()) gets a static flag
	// shared by the whole process, which stays set (upstream behavior, not patched here).
	bool isAbort = false;
	GmicCallData d;
	d.cmd = cmd;
	d.nofImages = nofImages;
	d.images = images;
	d.options = options;
	d.result = -1;
	d.done = false;
	options->p_is_abort = &isAbort;

	bool started = false;
#ifndef _WIN32
	// a large stack, like G'MIC's own threads (the default size of a secondary thread is 512 KiB
	// on macOS, and the interpreter recurses)
	pthread_t thread;
	pthread_attr_t attr;
	if (pthread_attr_init(&attr) == 0) {
		pthread_attr_setstacksize(&attr, (size_t)8 * 1024 * 1024);
		started = (pthread_create(&thread, &attr, gmicCallThread, &d) == 0);
		pthread_attr_destroy(&attr);
	}
#else
	std::thread thread;
	try {
		thread = std::thread(gmicCallRun, &d);
		started = true;
	} catch (const std::system_error&) {
	}
#endif
	if (!started) {
		// no thread: run G'MIC here, without abort
		options->p_is_abort = NULL;
		return gmic_plugin_call(cmd, nofImages, images, MAX_NOF_LAYERS, options);
	}

	// the worker uses d, isAbort and the images: always join it before returning
	try {
		std::unique_lock<std::mutex> lock(d.mutex);
		while (!d.cond.wait_for(lock, std::chrono::milliseconds(50), [&d] { return d.done; })) {
			lock.unlock();
			if (!*aborted && gEffectHost->abort(instance)) {
				*aborted = true;
			}
			if (*aborted) {
				// set it on every poll: G'MIC resets the flag when its interpreter starts
				isAbort = true;
			}
			lock.lock();
		}
	} catch (...) {
		// waiting failed (e.g. std::system_error from the mutex): stop G'MIC, wait for it, and fail
		isAbort = true;
#ifndef _WIN32
		pthread_join(thread, NULL);
#else
		thread.join();
#endif
		options->p_is_abort = NULL;
		throw;
	}
#ifndef _WIN32
	pthread_join(thread, NULL);
#else
	thread.join();
#endif
	options->p_is_abort = NULL;
	if (!*aborted && gEffectHost->abort(instance)) {
		*aborted = true;
	}
	if (*aborted) {
		return -1;
	}
	if (d.exception) {
		std::rethrow_exception(d.exception);
	}
	return d.result;
}
#endif

int pluginProcess(SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* contextDataP)
{
	int err = 0;
	sequenceDataP->resultHasAlpha = true;
	sequenceDataP->resultWidth = sequenceDataP->resultHeight = 0;

	MySequenceData* mySequenceDataP = (MySequenceData*)sequenceDataP->customSequenceDataP;
	mySequenceDataP->options.no_inplace_processing = true; // the input buffers are not overwritten
	// custom commands, set in options.custom_commands below (it must stay valid until gmic_plugin_call() returns)
#ifdef AE_PLUGIN
	string customCommands = ((MyGlobalData*)globalDataP->customGlobalDataP)->effectContent;
#else
	// the filter's #@gui lines, and the hot fixes for G'MIC bugs (gmic_hotfixes.gmic)
	string customCommands = pluginGlobalData.pluginContent[globalDataP->pluginIndex] + '\n' + pluginGlobalData.hotfixes;
#endif

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

	string cmd = gmicCommand(sequenceDataP, globalDataP);

	if (PAR_VAL(PARAM_OUTPUT) == 0.f) {
		// G'MIC >= 2.9 renamed gimp_merge_layers to gui_merge_layers ("Unknown command" otherwise)
		cmd += " -gui_merge_layers";
	}

	// images[] has MAX_NOF_LAYERS entries (gmic_plugin_call() fails with more results):
	// keep at most that many (the last ones, since the Output Layer is counted from the end).
	// Some filters return more by default, e.g. Fade Layers, Morph Layers, Tiles to Layers.
	cmd += " -if {$!>" + intToString(MAX_NOF_LAYERS) + "} -keep[-" + intToString(MAX_NOF_LAYERS) + "--1] -fi";

	// Ignore Alpha: apply_channels takes the command as a quoted string, which it un-escapes
	// again, so quoting the command (whose text arguments are already escaped) breaks any text
	// containing a quote or backslash. Define it as a custom command and apply that instead.
	if (PAR_VAL(PARAM_NOALPHA) > 0.f) {
		strReplace(cmd, "\n", " ");
		strReplace(cmd, "\r", " ");
		customCommands += "\n_openfx_gmic_cmd :\n  " + cmd + "\n";
		cmd = "-apply_channels \"_openfx_gmic_cmd\",rgb";
	}
	mySequenceDataP->options.custom_commands = customCommands.c_str();
	// customCommands goes out of scope: reset the pointer on return, also when G'MIC throws
	struct ResetCustomCommands {
		gmic_interface_options& options;
		~ResetCustomCommands() { options.custom_commands = NULL; }
	} resetCustomCommands = { mySequenceDataP->options };
	
	{
		// Resize Mode: "Downsample 1/N" processes a downsampled input; the result is always resized
		// to the input size ("Fixed (Inplace)" and "Dynamic" alike)
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

	bool aborted = false;
#ifdef OFX_PLUGIN
	int result = gmicCallAbortable(contextDataP->instance, cmd.c_str(), &nofImages, &images[0], &mySequenceDataP->options, &aborted);
#else
	int result = gmic_plugin_call(cmd.c_str(), &nofImages, &images[0], MAX_NOF_LAYERS, &mySequenceDataP->options);
#endif
	string errmsg = string(mySequenceDataP->options.error_message_buffer);

	// Only the host tells whether the render was aborted: G'MIC's 'quit' command also sets its
	// abort flag, and then G'MIC's result (the images at that point) is used as usual.
	if (aborted) {
		// The host aborted the render: the result is incomplete, and there is no error to report.
		// Fail the render (kOfxStatFailed), so that no host uses or caches the output buffer.
		// Natron checks whether the render was aborted before looking at the status, so this is
		// not reported as a failure there.
		err = PF_Err_INTERNAL_STRUCT_DAMAGED;
	} else if (result == 0) {
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
			sequenceDataP->resultHasAlpha = (spectrum == 2 || spectrum >= 4);
			sequenceDataP->resultWidth = sw;
			sequenceDataP->resultHeight = sh;
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
	if (result != 0 && !aborted) {
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
	else if (!aborted && gMessageSuiteV2) {
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
