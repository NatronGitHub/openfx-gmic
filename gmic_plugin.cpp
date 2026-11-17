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
#define	MAJOR_VERSION		2
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
// the last Output Layer option, "Frame" (issue #65): see pluginProcess
#define OUTPUT_LAYER_FRAME 11

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
#include <memory>
#include <vector>
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
// "Advanced Options" group (start and end) and its 7 parameters; for the filters with layer inputs,
// also the "Layer Times" group (start and end), Layers from Source and the 4 time offsets
static const int kNofExtraParams = 1 + 4 + 1 + 2 + 7;
static const int kNofLayerTimeParams = 2 + 1 + 4;

// Ignore Alpha (see fullCommand): _openfx_gmic_alpha_split stores the alpha channel of every image
// (an empty image for images without alpha) in the global variable _openfx_gmic_alpha, and
// _openfx_gmic_alpha_restore gives them back to the gray or RGB results of the same size (a result
// with its own alpha is kept as is), then frees the stored alphas:
// - a single input: every such result gets a copy of its alpha (e.g. Channels to Layers);
// - as many results as inputs: each result gets the alpha of the input at the same position;
// - several inputs and another number of results: only the first result (the active layer's)
//   gets the first alpha, the others can't be matched to an input.
// Results of another size (frames, rotation, crop, resize...) stay without alpha, and are made
// opaque (see kOpaqueResizedCommand).
// The positional case goes from the last result, so that appending (which removes the alpha
// image) does not shift the alphas still to be appended.
static const char* const kIgnoreAlphaCommands =
	"\n_openfx_gmic_alpha_split :\n"
	"  n=$!\n"
	"  repeat $n {\n"
	"    if s#$>==2 +channels[$>] 1 channels[$>] 0\n"
	"    elif s#$>>=4 +channels[$>] 3 channels[$>] 0,2\n"
	"    else i 0 fi\n"
	"  }\n"
	"  store[$n--1] _openfx_gmic_alpha\n"
	"_openfx_gmic_alpha_restore :\n"
	"  n=$! i $_openfx_gmic_alpha _openfx_gmic_alpha=\n"
	"  if $!-$n==1\n"
	"    repeat $n { if w#$>==w#$n&&h#$>==h#$n&&d#$>==d#$n&&(s#$>==1||s#$>==3) a[$>] [$n],c fi }\n"
	"  else\n"
	"    m={$!-$n==$n?$n:min($n,1)}\n"
	"    repeat $m {\n"
	"      k={$m-1-$>} a={$n+$k}\n"
	"      if w#$k==w#$a&&h#$k==h#$a&&d#$k==d#$a&&(s#$k==1||s#$k==3) a[$k,$a] c fi\n"
	"    }\n"
	"  fi\n"
	"  if $!>$n rm[$n--1] fi\n";

// _openfx_gmic_opaque_resized w,h: gives an opaque alpha channel to the gray or RGB images whose
// size is not w,h (the size of the processed main input). Run just before the final resize to the
// input size, which would hide the size change: a result without alpha from a premultiplied input
// gets its colours multiplied by the input's alpha (see multiplyByInputAlphaPlanar in
// RFX_OFX_Utils.h), which only lines up with a result of the input's size. A rotated, framed or
// cropped result would otherwise be darkened by the misaligned input alpha (issue #66).
static const char* const kOpaqueResizedCommand =
	"\n_openfx_gmic_opaque_resized :\n"
	"  repeat $! { if (w#$>!=$1||h#$>!=$2)&&(s#$>==1||s#$>==3) to_a[$>] fi }\n";

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
		bool builtinStdlib = false; // the curated lists of the parser are only checked against it
		string effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_ofx.gmic");
		if (effectContent.empty()) {
			effectContent = loadStringFromFile(get_gmic_rc_path() + "gmic_stdlib.gmic");
		}
		if (effectContent.empty()) {
			effectContent.swap(stdlib);
			builtinStdlib = true;
			if (
				(int)effectContent.find("#@gimp :") < 0 && 
				(int)effectContent.find("#@gmic_plugin :") < 0 && 
				(int)effectContent.find("#@gui :") < 0) {
				effectContent = "";
			}
		} 
		gmic_parse_multi(effectContent, &pluginData, &pluginContent, builtinStdlib);
		// Each plug-in has a fixed table of MAX_NOF_PARAMS parameters: the filter's own parameters,
		// up to 4 extra layers and the fixed trailing parameters (see pluginSetup). Leave out the
		// few filters that do not fit (e.g. "Transfer Color [Reduced Colors]" in G'MIC 4.0.7,
		// with several hundred parameters) instead of overflowing the table.
		for (size_t i = 0; i < pluginData.size(); ) {
			if ((int)pluginData[i].param.size() + kNofExtraParams + (pluginData[i].multiLayer ? kNofLayerTimeParams : 0) > MAX_NOF_PARAMS) {
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

#ifdef OFX_PLUGIN
// the results kept for Output Layer "Frame", per instance (see frameCacheStore)
static void frameCacheErase(const void* owner);
#endif

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
#ifdef OFX_PLUGIN
		frameCacheErase(this);
#endif
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

// Help note on Ignore Alpha for the filters with layer inputs, whose results can't always be
// matched to an input (see kIgnoreAlphaCommands). The parameter's hint covers the general case.
static string ignoreAlphaNote(const EffectData& effectData)
{
	if (!effectData.multiLayer) return string();
	return "\n\nIgnore Alpha: the alpha of each input is put back on the gray or RGB result at the same "
		"position (a result with its own alpha is kept as is). With only Source connected, every result "
		"gets Source's alpha. With several inputs connected, if the filter returns a different number of "
		"images than it got, only the first result gets the first input's alpha. Gray or RGB results "
		"whose size changed (frames, rotation, crop, resize...) are opaque.";
}

// Filters that process their layers as successive frames of a clip (issue #63). Their time offsets
// default to 0 like the other filters' (saved projects don't store parameters that didn't exist,
// and these connected the earlier frames through TimeOffset nodes): their help says how to get the
// earlier frames, and their "Layer Times" group is open.
static bool isTemporalFilter(const EffectData& effectData)
{
	static const char* const commands[] = { "fx_fade_layers", "fx_morph_layers", "fx_rep_z_render",
		"fx_align_layers", "fx_blend_median", "fx_blend_average_all" };
	for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
		if (effectData.command == commands[i]) return true;
	}
	return false;
}

static string layerTimesNote(const EffectData& effectData)
{
	if (!effectData.multiLayer || !isTemporalFilter(effectData)) return string();
	return "\n\nLayer Times: this filter treats its layers as frames. To process the previous frames of "
		"Source, set Layers from Source to the number of frames (Layer -1, Layer -2... then come from "
		"Source at the frames given by their time offsets, -1, -2... by default), or connect other "
		"clips to the layer inputs and set their time offsets.";
}

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
		static const char* const reservedNames[] = { "Layer -1", "Layer -2", "Layer -3", "Layer -4",
			"Layer Times", LAYERS_FROM_SOURCE_PARAM, "Layer -1" TIME_OFFSET_PARAM_SUFFIX,
			"Layer -2" TIME_OFFSET_PARAM_SUFFIX, "Layer -3" TIME_OFFSET_PARAM_SUFFIX,
			"Layer -4" TIME_OFFSET_PARAM_SUFFIX, "Command",
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
			} else if (paramType == "pointenabled") {
				// enables the removable point before it, see PARAM_FLAGS_POINT_ENABLED
				t = PT_BOOL;
				flags = PARAM_FLAGS_POINT_ENABLED;
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
		if (paramType == "pointenabled") {
			globalDataP->param[p].hint = (param.removable < 0) ?
				"Use the point above. Off by default, since G'MIC-Qt starts with this point removed. "
				"When off, the filter gets nan,nan for the point, as for a point removed in G'MIC-Qt." :
				"Use the point above. When off, the filter gets nan,nan for the point, as for a point removed in G'MIC-Qt.";
		}
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
		// Layer times (issue #63). Placed before Command, so that the PARAM_* indices of the
		// trailing parameters don't change. The defaults (no layer from Source, offsets 0) give the
		// behaviour of the plug-ins without these parameters, also for the saved projects, which
		// don't store them. Not animated: the frames needed are given to the host for a render time.
		// Integer parameters with flags 1 are not animated (see describeInContext).
		globalDataP->param[p] = Parameter("Layer Times", "", PT_TOPIC_START);
		globalDataP->param[p].flags = isTemporalFilter(effectData) ? 0 : 1; // closed, except for the filters that use frames
		++p;
		globalDataP->param[p] = Parameter(LAYERS_FROM_SOURCE_PARAM, "", PT_INT, 0, 4, 0, 0, 0, 0, "");
		globalDataP->param[p].flags = 1;
		globalDataP->param[p].hint = "Number of layer inputs taken from Source instead of their own input: "
			"Layer -1 to Layer -N are then Source at the render time plus their time offset, e.g. the "
			"previous frames with offsets -1, -2... (set when N is raised, for offsets still at 0). 0: off.";
		++p;
		for (int k = 1; k <= 4; ++k) {
			globalDataP->param[p] = Parameter("Layer -" + intToString(k) + TIME_OFFSET_PARAM_SUFFIX, "", PT_INT, -1000, 1000, 0, 0, 0, 0, "");
			globalDataP->param[p].flags = 1;
			globalDataP->param[p].hint = "Frame offset of Layer -" + intToString(k) + ": its image is fetched at "
				"the render time plus this offset (-1: the previous frame). Source is always at the render time.";
			++p;
		}
		globalDataP->param[p] = Parameter("Layer Times", "", PT_TOPIC_END);
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
	// "Frame" is added at the end (issue #65): the choice is saved as its index (and label in
	// Natron), so the other options keep their meaning in saved projects
	globalDataP->param[p] = Parameter("Output Layer", "", PT_SELECT, 0, OUTPUT_LAYER_FRAME, 1, 0, 0, 0, "Merged|Layer 0|Layer -1|Layer -2|Layer -3|Layer -4|Layer -5|Layer -6|Layer -7|Layer -8|Layer -9|Frame");
	globalDataP->param[p].hint = "The image of the G'MIC results to output. Merged: all the results merged into one. "
		"Layer 0, Layer -1...: one result, counted from the top of the stack as in G'MIC-Qt (Layer 0 is the first). "
		"Frame: the result whose number is the frame number minus the first frame of Source, modulo the number of "
		"results, for the filters that make several frames in one run (Fade Layers, Morph Layers, Tiles to Layers, "
		"Spline Spirograph's Number of Output Frames...). The results are kept in memory, so that the next frames "
		"don't run the filter again while its inputs and parameters stay the same.";
	++p;
	// "Fixed (Inplace)" (index 0) behaves like "Dynamic": the result is always resized to the input
	// size. Its special path (a per-plug-in in-place flag, set from one instance by a parameter
	// change callback that the OFX glue never called, and skipping the output copy) was removed.
	// The entry is kept so that saved projects keep their option indices and labels.
	globalDataP->param[p] = Parameter("Resize Mode", "", PT_SELECT, 0, 5, 1, 0, 0, 0, "Fixed (Inplace)|Dynamic|Downsample 1/2|Downsample 1/4|Downsample 1/8|Downsample 1/16");
	++p;
	globalDataP->param[p] = Parameter("Ignore Alpha", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Run the filter on the RGB channels only, then give the inputs' alpha back to the results. "
		"Gray or RGB results of another size than the input (frames, rotation, crop, resize...) are opaque.";
	++p;
	globalDataP->param[p] = Parameter("Preview/Draft Mode", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	if (!hasContent || effectData.command == effectData.preview_command) {
		globalDataP->param[p].displayStatus = DS_HIDDEN;
	}
	++p;
	// Reproducibility (issue #44): -srand seeds G'MIC's process-global random generator, which
	// concurrent G'MIC runs share (other instances or frames: the effect is instance-safe; runs are
	// not serialised, by design). Within a run, patches/cimg-omp-rand.sh makes the random numbers
	// of rand, noise and the math parser in OpenMP loops independent of thread scheduling (they
	// depend on the number of threads); patchmatch and G'MIC's parallel command still vary.
	globalDataP->param[p] = Parameter("Global Random Seed", "", PT_INT, 0, 1<<24, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Seed of the random numbers used by the filter. For most filters, results are reproducible "
		"for a given seed and number of OpenMP threads; filters using patch matching or G'MIC's parallel command, "
		"and concurrent renders, can still vary.";
	++p;
	globalDataP->param[p] = Parameter("Animate Random Seed", "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Add the frame number to the Global Random Seed, so that the random numbers change "
		"from frame to frame (reproducibility: see Global Random Seed).";
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
	globalDataP->pluginInfo.description = d + ignoreAlphaNote(effectData) + layerTimesNote(effectData) + "\n" + PLUGIN_DESCRIPTION;

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
        // the filter parameters only: they come first, the layers and their times follow
        const int nofFilterParams = (int)effectData.param.size();
        for (int i = 1; i <= nofFilterParams && i < PARAM_COMMAND; i++) {
            if (PAR_TYPE(i) == PT_BOOL && globalDataP->param[i].flags == PARAM_FLAGS_POINT_ENABLED)
                continue; // not a G'MIC argument: see the point before it
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
                // a removable point that is off: removed, as G'MIC-Qt passes it (issue #79)
                if (i + 1 <= nofFilterParams && i + 1 < PARAM_COMMAND && PAR_TYPE(i + 1) == PT_BOOL &&
                    globalDataP->param[i + 1].flags == PARAM_FLAGS_POINT_ENABLED && PAR_VAL(i + 1) == 0) {
                    cmd += "nan,nan,";
                    continue;
                }
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

// Output Layer "Frame" (issue #65): at most that many results (a frame per result, e.g. Spline
// Spirograph makes up to 512 frames, Fade Layers up to 401); the other modes use at most
// MAX_NOF_LAYERS (Layer -9 is the last one that can be picked)
static const int kMaxFrameResults = 1024;

static bool outputIsFrame(SequenceData* sequenceDataP, GlobalData* globalDataP)
{
	return PAR_VAL(PARAM_OUTPUT) == (float)OUTPUT_LAYER_FRAME;
}

static int maxResultsOf(SequenceData* sequenceDataP, GlobalData* globalDataP)
{
	return outputIsFrame(sequenceDataP, globalDataP) ? kMaxFrameResults : MAX_NOF_LAYERS;
}

// The whole command line: the filter's command (main or preview), with the variables G'MIC-Qt
// defines, the output layer selection, Ignore Alpha, the Resize Mode, verbosity and seed. Sets the
// custom commands of the options: customCommands (the filter's #@gui lines, the hot fixes for G'MIC
// bugs, the Ignore Alpha helpers and _openfx_gmic_opaque_resized), which must outlive the G'MIC call.
static
std::string
fullCommand(SequenceData* sequenceDataP, GlobalData* globalDataP, unsigned int nofImages, bool isPreview, string& customCommands)
{
	MySequenceData* mySequenceDataP = (MySequenceData*)sequenceDataP->customSequenceDataP;
#ifdef AE_PLUGIN
	customCommands = ((MyGlobalData*)globalDataP->customGlobalDataP)->effectContent;
#else
	// the filter's #@gui lines, and the hot fixes for G'MIC bugs (gmic_hotfixes.gmic), on every run
	customCommands = pluginGlobalData.pluginContent[globalDataP->pluginIndex] + '\n' + pluginGlobalData.hotfixes;
#endif
	customCommands += kOpaqueResizedCommand;
	string cmd = gmicCommand(sequenceDataP, globalDataP, isPreview);

	// Ignore Alpha: the alpha channels of all the images are split off and stored, the command runs
	// on all the RGB images at once (multi-input filters see every layer, extra results are kept),
	// then the results of the same size get the alpha back (see kIgnoreAlphaCommands: results of
	// another size are opaque). The command stays at the top level, as with Ignore Alpha off and in
	// G'MIC-Qt: inside a custom command, $1, $*, $#... in text parameters would be substituted, and
	// apply_channels processed the images one by one, keeping a single result for each.
	if (PAR_VAL(PARAM_NOALPHA) > 0.f) {
		customCommands += kIgnoreAlphaCommands;
		cmd = "-_openfx_gmic_alpha_split " + cmd + " -_openfx_gmic_alpha_restore";
	}

	if (PAR_VAL(PARAM_OUTPUT) == 0.f) {
		// G'MIC >= 2.9 renamed gimp_merge_layers to gui_merge_layers ("Unknown command" otherwise)
		cmd += " -gui_merge_layers";
	}

	// images[] has maxResults() entries (gmic_plugin_call() fails with more results): keep at most
	// that many, the first ones (Output Layer "Layer -k" is image [k], see pluginProcess). Some
	// filters return more by default, e.g. Fade Layers, Morph Layers, Tiles to Layers.
	const int maxResults = maxResultsOf(sequenceDataP, globalDataP);
	cmd += " -if {$!>" + intToString(maxResults) + "} -keep[0-" + intToString(maxResults - 1) + "] -fi";

	mySequenceDataP->options.custom_commands = customCommands.c_str();
	
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
	const float dsMode = PAR_VAL(PARAM_RESIZE);
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

	{
		// Resize Mode: "Downsample 1/N" processes a downsampled input; the result is always resized
		// to the input size ("Fixed (Inplace)" and "Dynamic" alike). Before that, the gray or RGB
		// results whose size differs from the processed input get an opaque alpha (see
		// kOpaqueResizedCommand).
		float ds = PAR_VAL(PARAM_RESIZE);
		string processedSize = intToString(sequenceDataP->inWorld[0].width) + "," + intToString(sequenceDataP->inWorld[0].height);
		if (ds >= 2.f) {
			ds = pow(2.f, ds - 1.f);
			const string dsSize = intToString(sequenceDataP->inWorld[0].width / (int)ds) + "," + intToString(sequenceDataP->inWorld[0].height / (int)ds);
			cmd = " -resize " + dsSize + " " + cmd;
			processedSize = dsSize;
		}
		cmd += " -_openfx_gmic_opaque_resized " + processedSize;
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
	unsigned int maxImages;
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
		result = gmic_plugin_call(d->cmd, d->nofImages, d->images, d->maxImages, d->options);
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
static int gmicCallAbortable(OfxImageEffectHandle instance, const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options, bool* aborted)
{
	*aborted = false;
	if (!gEffectHost || !gEffectHost->abort) {
		return gmic_plugin_call(cmd, nofImages, images, maxImages, options);
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
	d.maxImages = maxImages;
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
		return gmic_plugin_call(cmd, nofImages, images, maxImages, options);
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

// Output Layer "Frame" (issue #65). Filters such as Fade Layers, Morph Layers (Inter-Frames),
// Tiles to Layers, Spline Spirograph (Number of Output Frames), 3D Conversion (Render Multiple
// Frames) or Multiscale Operator (Interpolated Motion) make N frames in one run, which G'MIC-Qt
// gives as N layers (an animation in GIMP). "Frame" outputs result [(t - start) mod N] at time t,
// where start is the first frame of Source's frame range (0 if there is none, or an infinite
// one): on a still input, the timeline plays the N results from the start of Source, then loops.
//
// Every render would run the filter to output one of its N results: the N results are kept, per
// instance, and the next frames are copied from them while the key is the same. The key is the
// whole G'MIC command line, which holds every parameter value at the render time (point
// positions in % of Source, the seed, also when animated with the frame), Preview/Draft Mode,
// Ignore Alpha, the Resize Mode, the verbosity and the image size, plus the size and a 256-bit
// hash of the pixels of every input image given to G'MIC (after the conversion to float and the
// unpremultiplication: whatever G'MIC sees, which covers the layer inputs, the frames of Source
// given by the layer times, and upstream changes). The render scale and window only change the
// output placement, done when copying. So nothing is invalidated explicitly: another key
// replaces the results. A filter whose key changes from frame to frame anyway (a varying input,
// an animated parameter, Animate Random Seed) runs on every frame, as before.
// Memory: the results stay in the buffers G'MIC returned (no copy), at most kFrameCacheMaxBytes
// for all the instances together; when the results of an instance don't fit, the least recently
// used ones of other instances are dropped, and results larger than the whole budget are not
// kept. They are freed when the instance is destroyed, when its Output Layer is not Frame, or when
// the host purges its caches. The host serialises the renders of an instance
// (kOfxImageEffectRenderInstanceSafe); the cache, shared by all instances, is behind a mutex, and
// its frames are reference counted, so that dropping the results of an instance while another
// render copies one of them is safe.
#ifdef OFX_PLUGIN
static const size_t kFrameCacheMaxBytes = (size_t)1 << 30; // 1 GiB

// a G'MIC result, owned (freed with gmic_delete_external)
struct CachedFrame
{
	gmic_interface_image image;
	CachedFrame() { memset(&image, 0, sizeof(image)); }
	~CachedFrame() { if (image.data) gmic_delete_external((float*)image.data); }
private:
	CachedFrame(const CachedFrame&);
	CachedFrame& operator=(const CachedFrame&);
};

typedef std::vector<std::shared_ptr<const CachedFrame> > CachedFrames;

struct FrameCacheEntry
{
	std::string key;
	CachedFrames frames;
	size_t bytes;
	unsigned long long lastUse;
};

static std::mutex gFrameCacheMutex;
static std::map<const void*, FrameCacheEntry> gFrameCache; // by instance (its MySequenceData)
static size_t gFrameCacheBytes = 0;
static unsigned long long gFrameCacheClock = 0;

static size_t imageBytes(const gmic_interface_image& im)
{
	return (size_t)im.width * im.height * im.depth * im.spectrum * sizeof(float);
}

// result [frame mod N] of the results of an instance (frame: any integer), or NULL if their key
// is another one
static std::shared_ptr<const CachedFrame> frameCacheFind(const void* owner, const std::string& key, long long frame)
{
	std::lock_guard<std::mutex> lock(gFrameCacheMutex);
	std::map<const void*, FrameCacheEntry>::iterator it = gFrameCache.find(owner);
	if (it == gFrameCache.end() || it->second.key != key || it->second.frames.empty()) {
		return std::shared_ptr<const CachedFrame>();
	}
	it->second.lastUse = ++gFrameCacheClock;
	const long long n = (long long)it->second.frames.size();
	return it->second.frames[(size_t)(((frame % n) + n) % n)];
}

static void frameCacheErase(const void* owner)
{
	CachedFrames dropped; // freed after unlocking
	std::lock_guard<std::mutex> lock(gFrameCacheMutex);
	std::map<const void*, FrameCacheEntry>::iterator it = gFrameCache.find(owner);
	if (it == gFrameCache.end()) return;
	gFrameCacheBytes -= it->second.bytes;
	dropped.swap(it->second.frames);
	gFrameCache.erase(it);
}

// keeps the results images[0..nofImages-1] of an instance, replacing its previous ones: their
// buffers now belong to the cache, and their data pointers are set to NULL (a buffer that is an
// input's, which G'MIC doesn't return with no_inplace_processing, is copied). Results that don't
// fit in the budget are left to the caller.
static void frameCacheStore(const void* owner, const std::string& key, gmic_interface_image* images, unsigned int nofImages, const SequenceData* sequenceDataP)
{
	frameCacheErase(owner);
	size_t bytes = 0;
	for (unsigned int i = 0; i < nofImages; ++i) {
		if (!images[i].data || images[i].spectrum == 0) return; // not a usable result: not kept
		bytes += imageBytes(images[i]);
	}
	if (nofImages == 0 || bytes > kFrameCacheMaxBytes) return;
	FrameCacheEntry entry;
	entry.key = key;
	entry.bytes = bytes;
	entry.frames.reserve(nofImages);
	for (unsigned int i = 0; i < nofImages; ++i) {
		std::shared_ptr<CachedFrame> f = std::make_shared<CachedFrame>();
		f->image = images[i];
		bool isInput = false;
		for (int j = 0; j < MAX_NOF_LAYERS; ++j) {
			if (sequenceDataP->inWorld[j].data && images[i].data == sequenceDataP->inWorld[j].data) isInput = true;
		}
		if (isInput) {
			const size_t n = imageBytes(images[i]) / sizeof(float);
			float* copy = new float[n];
			memcpy(copy, images[i].data, n * sizeof(float));
			f->image.data = copy;
		} else {
			images[i].data = NULL;
		}
		entry.frames.push_back(f);
	}
	std::vector<CachedFrames> dropped; // freed after unlocking
	std::lock_guard<std::mutex> lock(gFrameCacheMutex);
	// make room: drop the least recently used results of the other instances
	while (gFrameCacheBytes + bytes > kFrameCacheMaxBytes && !gFrameCache.empty()) {
		std::map<const void*, FrameCacheEntry>::iterator lru = gFrameCache.begin();
		for (std::map<const void*, FrameCacheEntry>::iterator it = gFrameCache.begin(); it != gFrameCache.end(); ++it) {
			if (it->second.lastUse < lru->second.lastUse) lru = it;
		}
		gFrameCacheBytes -= lru->second.bytes;
		dropped.push_back(CachedFrames());
		dropped.back().swap(lru->second.frames);
		gFrameCache.erase(lru);
	}
	entry.lastUse = ++gFrameCacheClock;
	gFrameCacheBytes += bytes;
	FrameCacheEntry& stored = gFrameCache[owner];
	stored.key.swap(entry.key);
	stored.frames.swap(entry.frames);
	stored.bytes = entry.bytes;
	stored.lastUse = entry.lastUse;
}

// 256-bit hash of a buffer (four xxHash64-style lanes over 64-bit words), appended to key in hex
static void appendHash(std::string& key, const void* data, size_t size)
{
	static const unsigned long long P1 = 11400714785074694791ULL, P2 = 14029467366897019727ULL;
	unsigned long long acc[4] = { P1 + P2, P2, 0, 0 - P1 };
	const unsigned char* p = (const unsigned char*)data;
	size_t i = 0;
	for (; i + 32 <= size; i += 32) {
		for (int l = 0; l < 4; ++l) {
			unsigned long long w;
			memcpy(&w, p + i + 8 * l, 8);
			acc[l] += w * P2;
			acc[l] = (acc[l] << 31) | (acc[l] >> 33);
			acc[l] *= P1;
		}
	}
	for (; i < size; ++i) {
		acc[i & 3] = (acc[i & 3] ^ p[i]) * P1;
	}
	char buf[96];
	snprintf(buf, sizeof(buf), "%016llx%016llx%016llx%016llx", acc[0], acc[1], acc[2], acc[3]);
	key += buf;
}

// the key of the results: the command line and the input images (see above)
static std::string frameCacheKey(const std::string& cmd, const SequenceData* sequenceDataP, unsigned int nofImages)
{
	std::string key = cmd;
	for (unsigned int i = 0; i < nofImages; ++i) {
		const World& w = sequenceDataP->inWorld[i];
		key += "\n" + intToString(w.width) + "x" + intToString(w.height) + "/" + intToString(w.bitDepth) + "/" + intToString(w.rowBytes) + ":";
		appendHash(key, w.data, (size_t)w.height * (size_t)w.rowBytes);
	}
	return key;
}

// the host purges its caches (kOfxActionPurgeCaches): drop the results of the instance
int pluginPurgeCaches(SequenceData* sequenceDataP)
{
	if (sequenceDataP && sequenceDataP->customSequenceDataP) {
		frameCacheErase(sequenceDataP->customSequenceDataP);
	}
	return 0;
}
#endif

// the frame number of Output Layer "Frame": the render time minus the first frame of Source,
// rounded to the nearest frame
static long long frameNumberOf(const SequenceData* sequenceDataP)
{
	return (long long)std::floor(sequenceDataP->time - sequenceDataP->frameRangeStart + 0.5);
}

int pluginProcess(SequenceData* sequenceDataP, GlobalData* globalDataP, ContextData* contextDataP)
{
	int err = 0;
	sequenceDataP->resultHasAlpha = true;

	MySequenceData* mySequenceDataP = (MySequenceData*)sequenceDataP->customSequenceDataP;
	mySequenceDataP->options.no_inplace_processing = true; // the input buffers are not overwritten

	const bool frameMode = outputIsFrame(sequenceDataP, globalDataP);
	// the inputs, then the results (more of them with Output Layer "Frame", see fullCommand)
	std::vector<gmic_interface_image> imageBuffer((size_t)maxResultsOf(sequenceDataP, globalDataP));
	gmic_interface_image* const images = &imageBuffer[0];
	const unsigned int maxImages = (unsigned int)imageBuffer.size();
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
	// the custom commands, set in the options by fullCommand(): must stay valid until the G'MIC calls return
	string customCommands;
	// customCommands goes out of scope: reset the pointer on return, also when G'MIC throws
	struct ResetCustomCommands {
		gmic_interface_options& options;
		explicit ResetCustomCommands(gmic_interface_options& o) : options(o) {}
		~ResetCustomCommands() { options.custom_commands = NULL; }
	} resetCustomCommands(mySequenceDataP->options);
	string cmd = fullCommand(sequenceDataP, globalDataP, nofImages, preview, customCommands);
	bool aborted = false;
	int result = 0;
	string errmsg;
	// Output Layer "Frame": the results kept from a previous render, if the key is the same
	const long long frame = frameNumberOf(sequenceDataP);
#ifdef OFX_PLUGIN
	string cacheKey;
	std::shared_ptr<const CachedFrame> cachedFrame;
	if (frameMode) {
		cacheKey = frameCacheKey(cmd, sequenceDataP, nofImages);
		cachedFrame = frameCacheFind(mySequenceDataP, cacheKey, frame);
	} else {
		frameCacheErase(mySequenceDataP);
	}
	const bool runGmic = !cachedFrame;
#else
	const bool runGmic = true;
#endif
	if (runGmic) {
#ifdef OFX_PLUGIN
		result = gmicCallAbortable(contextDataP->instance, cmd.c_str(), &nofImages, images, maxImages, &mySequenceDataP->options, &aborted);
#else
		result = gmic_plugin_call(cmd.c_str(), &nofImages, images, maxImages, &mySequenceDataP->options);
#endif
		errmsg = string(mySequenceDataP->options.error_message_buffer);
		// The retry needs unchanged inputs: gmic_plugin_call() works on copies (no_inplace_processing)
		// and leaves images[] unchanged on an error. Not after an abort.
		if (result != 0 && !aborted && !preview && effectData.previewFallback && mySequenceDataP->options.no_inplace_processing) {
			fprintf(stderr, "G'MIC: %s: the main command failed (%s), rendering with the preview command\n",
					effectData.name.c_str(), errmsg.c_str());
			preview = true;
			cmd = fullCommand(sequenceDataP, globalDataP, nofImages, preview, customCommands);
#ifdef OFX_PLUGIN
			result = gmicCallAbortable(contextDataP->instance, cmd.c_str(), &nofImages, images, maxImages, &mySequenceDataP->options, &aborted);
#else
			result = gmic_plugin_call(cmd.c_str(), &nofImages, images, maxImages, &mySequenceDataP->options);
#endif
			if (result != 0 && !aborted) {
				errmsg += " (with the preview command: " + string(mySequenceDataP->options.error_message_buffer) + ")";
			}
		}
	}

	// Only the host tells whether the render was aborted: G'MIC's 'quit' command also sets its
	// abort flag, and then G'MIC's result (the images at that point) is used as usual.
	if (aborted) {
		// The host aborted the render: the result is incomplete, and there is no error to report.
		// Fail the render (kOfxStatFailed), so that no host uses or caches the output buffer.
		// Natron checks whether the render was aborted before looking at the status, so this is
		// not reported as a failure there.
		err = PF_Err_INTERNAL_STRUCT_DAMAGED;
	} else if (result == 0) {
		// Output Layer: 0 is Merged (a single image), k > 0 is "Layer -(k-1)", i.e. image [k-1],
		// counted from the top of the stack like the inputs (Source = [0] = "Layer 0", the active
		// layer in G'MIC-Qt, then the "Layer -1" input = [1]...). Past the last result: the last one.
		// Frame: image [frame mod N] (see frameNumberOf), maybe from the kept results.
		const gmic_interface_image* res = NULL;
#ifdef OFX_PLUGIN
		if (cachedFrame) res = &cachedFrame->image;
#endif
		if (!res && nofImages > 0) {
			int idx = (int)PAR_VAL(PARAM_OUTPUT) - 1;
			if (frameMode) idx = (int)(((frame % (long long)nofImages) + nofImages) % nofImages);
			if (idx < 0) idx = 0;
			if ((unsigned)idx >= nofImages) idx = (int)nofImages - 1;
			res = &images[idx];
		}

		if (!res || res->spectrum == 0 || !res->data) {
			result = -1;
			errmsg = "The image produced by G'MIC is empty.";
		} else {
			// Planar output R,G,B,A from 1 (G), 2 (GA), 3 (RGB) or 4+ (RGBA) channels. The G'MIC
			// result is anchored at the first input's rectangle, the output has its own rectangle
			// (both in pixels, rows top-down in the planar buffers): copy the overlap, clear the rest.
			const World& out = sequenceDataP->outWorld;
			const int dw = out.width, dh = out.height;
			const int sw = (int)res->width, sh = (int)res->height;
			// G'MIC anchors images at their top-left corner: a result of another size than the first
			// input keeps the input's left and top edges (pixel y goes up)
			const int sx1 = sequenceDataP->inWorld[0].origin_x;
			const int sy1 = sequenceDataP->inWorld[0].origin_y + sequenceDataP->inWorld[0].height - sh;
			const int dx1 = out.origin_x, dy1 = out.origin_y;
			const size_t dplane = (size_t)dw * (size_t)dh, splane = (size_t)sw * (size_t)sh;
			float* const dst = (float*)out.data;
			const float* const src = (const float*)res->data;
			const unsigned int spectrum = res->spectrum;
			sequenceDataP->resultHasAlpha = (spectrum == 2 || spectrum >= 4);
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
#ifdef OFX_PLUGIN
		// Frame: keep the results for the next frames (their buffers then belong to the cache)
		if (result == 0 && frameMode && runGmic) {
			frameCacheStore(mySequenceDataP, cacheKey, images, nofImages, sequenceDataP);
		}
#endif
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
		if (!images[i].data) continue; // kept by the cache
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
	pluginInfo.description = d + ignoreAlphaNote(pluginGlobalData.pluginData[pluginIndex]) + layerTimesNote(pluginGlobalData.pluginData[pluginIndex]) + "\n\n" + PLUGIN_DESCRIPTION;
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
