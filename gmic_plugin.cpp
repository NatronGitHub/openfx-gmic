/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */
/*
 #
 #  File        : gmic_plugin.cpp
 #
 #  Description : The sources of an OpenFX plugin which interfaces the
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
// version and description of the plug-ins (the name, grouping and identifier are the filter's)
#define	MAJOR_VERSION		2
#define	MINOR_VERSION		0

#define PLUGIN_DESCRIPTION	"Wrapper for the G'MIC framework (http://gmic.eu) written by Tobias Fleischer (http://www.reduxfx.com) and Frederic Devernay.";

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

// G'MIC needs NaN and infinities (see GMIC_OFX/Makefile): with -ffinite-math-only, the compiler folds
// its NaN tests to constants. The G'MIC sources are compiled with the same options as this file.
#if defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__
#error "-ffinite-math-only (in -ffast-math and -Ofast) breaks G'MIC: add -fno-finite-math-only after it"
#endif
#if defined(_M_FP_FAST)
#error "/fp:fast breaks G'MIC: use /fp:precise"
#endif

#ifdef _WIN32
// CreateThread (the render thread) and GlobalMemoryStatusEx (physicalMemoryBytes), without the
// min/max macros. Before the other headers, so that no using-directive for std can be in effect
// there: the Windows SDK's byte would then be ambiguous with C++17's std::byte (C2872).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "gmic_plugin.h"
#include "gmic_parser.h"
#include "gmic_libc.h"
#include "gmic_plugin_call.h"
#include "RFX_StringUtils.h"
#include "ofxsImageEffect.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using std::string;
using std::vector;
using namespace reduxfx;

#include <sstream>
#include <map>
#include <set>
#include <limits>
#include <memory>
#include <vector>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <fstream>
#include <iterator>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#if defined(cimg_use_fftw3) && !defined(cimg_use_fftw3_singlethread)
#include <fftw3.h>
#endif
#include <system_error>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#endif


// The contents of a file, or an empty string if it can't be read (missing, a directory, unreadable,
// or too large to hold). The size isn't taken from tellg(), which can be -1 or 2^63-1 for a
// directory (ext4 htree); libstdc++ throws ios_base::failure when reading one.
static
string loadStringFromFile(const string& filename)
{
	if (filename.empty()) return string();
	std::ifstream infile(filename.c_str(), std::ios::in | std::ios::binary);
	if (!infile) return string();
	string buffer;
	try {
		buffer.assign(std::istreambuf_iterator<char>(infile), std::istreambuf_iterator<char>());
	} catch (const std::exception&) { // bad_alloc, length_error, ios_base::failure (EISDIR, EIO)
		return string();
	}
	if (infile.bad()) return string();
	return buffer;
}

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
// gmic_ofx.cpp), which only lines up with a result of the input's size. A rotated, framed or
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
// own), and of the built-in definitions of the commands the fix relies on; <original> is "-" for
// a command that the built-in G'MIC no longer defines (but still calls). A block is applied if the
// built-in command is the original one (or, for "-", is not defined) and the callees are
// unchanged, skipped silently if the built-in command already is the fixed one (fixed upstream),
// and otherwise skipped with a warning.
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
			// a callee given without a checksum only has to exist; a command whose original is "-"
			// must not exist
			const bool absent = (k == 0 && expected == "-");
			if (absent ? !def.empty() : (def.empty() || ((k == 0 || !expected.empty()) && expected != found))) {
				ok = false;
				fprintf(stderr, "openfx-gmic: Warning: hot fix for the G'MIC command '%s' not applied: the built-in definition of '%s' %s (checksum %s, the fix is for %s), review it in gmic_hotfixes.gmic\n",
				        command.c_str(), name.c_str(), absent ? "exists" : def.empty() ? "is missing" : "changed", found.c_str(), expected.c_str());
			}
		}
		if (ok) {
			hotfixes += texts[i];
		}
	}
	return hotfixes;
}

namespace {

// The list of the plug-ins (README, "Choosing the plug-ins"; issue #157): its name, in the bundle's
// Resources and in the user's G'MIC configuration directory (get_gmic_rc_path)
constexpr const char* kPluginListFile = "gmic_ofx_plugins.txt";

// The plug-in identifiers that "list" (the contents of a gmic_ofx_plugins.txt) disables. Each line
// is a plug-in identifier, enabled, or disabled if commented out with one or more '#' (and spaces),
// either one optionally followed by a '#' comment; any other line is a comment. If an identifier is
// on several lines, the last one wins. (The identifiers aren't checked: one that is no plug-in, e.g. of a filter
// that G'MIC removed, changes nothing.)
std::set<string> disabledPlugins(const string& list)
{
	constexpr const char* kSpaces = " \t\r";
	std::set<string> disabled;
	std::istringstream in(list.compare(0, 3, "\xEF\xBB\xBF") == 0 ? list.substr(3) : list); // (UTF-8 BOM)
	string line;
	while (std::getline(in, line)) {
		size_t b = line.find_first_not_of(kSpaces);
		const bool commented = b != string::npos && line[b] == '#';
		if (commented) {
			b = line.find_first_not_of(" \t\r#", b); // ("##eu.gmic.X", "# # eu.gmic.X")
		}
		if (b == string::npos) {
			continue;
		}
		string id = line.substr(b, line.find('#', b) - b); // (up to the comment, if any)
		id.erase(id.find_last_not_of(kSpaces) + 1);
		if (id.empty() || id.find_first_of(kSpaces) != string::npos) {
			continue; // a comment
		}
		if (commented) {
			disabled.insert(id);
		} else {
			disabled.erase(id);
		}
	}
	return disabled;
}

// Write the list of the plug-ins pluginData to "path": grouped by menu (in their order within
// each), each with its menu and label in a comment, those in "disabled" commented out. The build
// writes the bundle's list so, with every plug-in enabled (GMIC_OFX/gmic_ofx_plugin_list.cpp).
// It is written to "<path>.tmp", then renamed: false if it could not be written, and then a file
// that was at "path" is kept.
[[nodiscard]] bool writePluginList(const string& path, const vector<EffectData>& pluginData, const std::set<string>& disabled)
{
	vector<const EffectData*> sorted;
	sorted.reserve(pluginData.size());
	for (const EffectData& d : pluginData) {
		sorted.push_back(&d);
	}
	std::stable_sort(sorted.begin(), sorted.end(), [](const EffectData* a, const EffectData* b) { return a->category < b->category; });
	const string tmp = path + ".tmp";
	std::ofstream out(tmp.c_str(), std::ios::out | std::ios::binary);
	out << "# The OpenFX plug-ins of openfx-gmic: one plug-in identifier per line (as saved in projects).\n"
	       "# To disable a plug-in, comment out its line (\"# eu.gmic.AddGrain\"); after another '#', the\n"
	       "# rest of a line is a comment. The plug-ins that this list doesn't mention are enabled; if an\n"
	       "# identifier is on several lines, the last one wins. Copy this file to\n"
	       "# ~/.config/gmic/" << kPluginListFile << " (Windows: %APPDATA%\\gmic\\" << kPluginListFile << ") to edit it.\n"
	       "# After a change, restart the host and CLEAR ITS PLUG-IN CACHE (Natron: Cache > Clear Plug-ins\n"
	       "# Load Cache, or start it once with --clear-openfx-cache): with a stale cache, a host may keep\n"
	       "# the old menus and create the wrong filter for a node. A project that uses a disabled plug-in\n"
	       "# can't load that node. See README, \"Choosing the plug-ins\".\n";
	const string* category = nullptr;
	for (const EffectData* d : sorted) {
		if (!category || *category != d->category) {
			category = &d->category;
			out << "\n# --- " << *category << " ---\n";
		}
		out << (disabled.count(d->uniqueId) ? "# " : "") << d->uniqueId << "  # " << d->category << ": " << d->name << '\n';
	}
	out.close();
	if (!out) {
		std::remove(tmp.c_str());
		return false;
	}
	std::remove(path.c_str()); // (Windows: rename doesn't replace a file)
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// The filters, parsed from the G'MIC stdlib (or a user file) once, on first use.
struct PluginGlobalData
{
	vector<EffectData> pluginData;
	vector<string> pluginContent;
	// The user's filter file (gmic_ofx.gmic or gmic_stdlib.gmic), when the filters come from it:
	// custom commands for every run, so that its filters find the commands it defines, as in
	// G'MIC-Qt. Empty with the built-in stdlib, which G'MIC loads itself.
	string userCommands;
	string hotfixes; // custom commands for every run, see gmicGetHotfixes()
};

PluginGlobalData loadPluginGlobalData()
{
	PluginGlobalData data;
	vector<EffectData>& pluginData = data.pluginData;
	vector<string>& pluginContent = data.pluginContent;
	{
		// The hot fixes are checked against the built-in stdlib, which G'MIC always loads: decompress
		// it once here (~35 ms), even when the filters come from a user file (gmic_ofx.gmic).
		string stdlib;
		{
			const char* lib = gmic_get_stdlib();
			stdlib = string(lib);
			gmic_delete_external((float*)lib);
		}
		data.hotfixes = gmicGetHotfixes(stdlib);
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
		if (!builtinStdlib) {
			data.userCommands = std::move(effectContent);
		}
		// leave out the filters for which leaveOut(filter) is true
		const auto leaveOutFilters = [&pluginData, &pluginContent](const auto& leaveOut) {
			size_t n = 0;
			for (size_t i = 0; i < pluginData.size(); ++i) {
				if (!leaveOut(pluginData[i])) {
					if (n != i) {
						pluginData[n] = std::move(pluginData[i]);
						pluginContent[n] = std::move(pluginContent[i]);
					}
					++n;
				}
			}
			pluginData.resize(n);
			pluginContent.resize(n);
		};
		// Each plug-in has a fixed table of MAX_NOF_PARAMS parameters: the filter's own parameters,
		// up to 4 extra layers and the fixed trailing parameters (see pluginSetup). Leave out the
		// few filters that do not fit (e.g. "Transfer Color [Reduced Colors]" in G'MIC 4.0.7,
		// with several hundred parameters) instead of overflowing the table.
		leaveOutFilters([](const EffectData& d) {
			return (int)d.param.size() + kNofExtraParams + (d.multiLayer ? kNofLayerTimeParams : 0) > MAX_NOF_PARAMS;
		});
		// The plug-ins that the user's list disables are not registered (OFX has no way to only hide
		// them from the menus). The bundle's list enables every plug-in: it isn't read.
		// OFX_GMIC_WRITE_PLUGIN_LIST=<file>: write the list there (the build writes the bundle's).
		const string rcPath = get_gmic_rc_path();
		const std::set<string> disabled = rcPath.empty() ? std::set<string>() : disabledPlugins(loadStringFromFile(rcPath + kPluginListFile));
		const char* const listPath = std::getenv("OFX_GMIC_WRITE_PLUGIN_LIST");
		if (listPath && *listPath && !writePluginList(listPath, pluginData, disabled)) {
			fprintf(stderr, "openfx-gmic: Error: could not write the plug-in list %s\n", listPath);
		}
		if (!disabled.empty()) {
			leaveOutFilters([&disabled](const EffectData& d) { return disabled.count(d.uniqueId) > 0; });
		}
	}
	return data;
}

// Built on first use (OfxGetNumberOfPlugins), not in a global constructor during dlopen, where an
// exception would terminate the host and gmic_get_stdlib() could run before the statics of gmic.cpp
// are initialised (C++ Core Guidelines I.22); a function-local static is initialised once,
// thread-safely. A failure gives zero plug-ins.
const PluginGlobalData& pluginGlobalData()
{
	static const PluginGlobalData data = []() {
		try {
			return loadPluginGlobalData();
		} catch (const std::exception& e) {
			fprintf(stderr, "openfx-gmic: Error: could not load the G'MIC filters: %s\n", e.what());
		} catch (...) {
			fprintf(stderr, "openfx-gmic: Error: could not load the G'MIC filters\n");
		}
		return PluginGlobalData();
	}();
	return data;
}

} // namespace

// the results kept for Output Layer "Frame" (see frameCacheStore)
static void frameCacheRelease(const void* owner);

GmicInstanceData::GmicInstanceData()
	: _options()
	, _errorMessage()
{
	_options.custom_commands = NULL;
	_options.error_message_buffer = _errorMessage.data();
	_options.ignore_stdlib = false;
	_options.p_is_abort = NULL;
	_options.p_progress = NULL;
	_options.output_format = E_FORMAT_FLOAT;
	_options.interleave_output = false;
	_options.no_inplace_processing = false;
}

GmicInstanceData::~GmicInstanceData()
{
	frameCacheRelease(this);
}

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

// The plug-in's description: the filter's notes (with those of its note() parameters), the notes
// on Ignore Alpha and Layer Times, and the plug-in's own.
static string pluginDescription(const EffectData& effectData)
{
	// OFX strings are UTF-8, as G'MIC's: no transliteration on the OFX path
	string d = strRemoveXmlTags(effectData.notes, true);
	strReplace(d, "<", "(");
	strReplace(d, ">", ")");
	return d + ignoreAlphaNote(effectData) + layerTimesNote(effectData) + "\n" + PLUGIN_DESCRIPTION;
}

#if defined(cimg_use_fftw3) && !defined(cimg_use_fftw3_singlethread)
// FFTW is a shared library: other plug-ins of the host process (e.g. openfx-misc's CImg plug-ins)
// share its global planner and thread pool, and each one only locks its own mutex around its
// FFTs. Two fixes make this safe:
// - FFTW's planner is made thread-safe, once per process, at the first plug-in load (FFTW >=
//   3.3.5, in libfftw3_threads; CImg calls fftw_init_threads at library initialisation, calling
//   it again is harmless);
// - CImg never calls fftw_cleanup_threads(). G'MIC 4.0.8 no longer calls it after each FFT, which
//   freed the planner and the threads without the planner lock while another plug-in may be
//   planning or executing an FFT, but still calls it in the destructor of its static FFTW3_static,
//   which patches/cimg-fftw-no-cleanup-at-unload.sh removes
//   (https://github.com/GreycLab/CImg/issues/500): that destructor runs inside the host's
//   dlclose(), and FFTW returns from the cleanup while its detached worker threads are still
//   exiting through FFTW's code, which crashed once the plug-in was unmapped (FFTW linked
//   statically, on Linux, or a shared libfftw3 unloaded with the plug-in). The idle threads and
//   the planner are kept until the process exits.
// fftw_plan_with_nthreads(), called by CImg before each plan, remains a process-wide setting: a
// plug-in may plan with another one's thread count (performance only, results are the same).
static void makeFftwThreadSafe()
{
	static std::once_flag once;
	std::call_once(once, [] { fftw_init_threads(); fftw_make_planner_thread_safe(); });
}
#endif

std::unique_ptr<GlobalData> pluginSetup(int pluginIndex, const PluginInfo& pluginInfo)
{
#if defined(cimg_use_fftw3) && !defined(cimg_use_fftw3_singlethread)
	makeFftwThreadSafe();
#endif
	auto globalData = std::make_unique<GlobalData>();
	GlobalData* const globalDataP = globalData.get();
	globalDataP->pluginIndex = pluginIndex;
	globalDataP->pluginInfo = pluginInfo;

	// a copy: the note() parameters are appended to its notes (the description)
	EffectData effectData = pluginGlobalData().pluginData[globalDataP->pluginIndex];

	globalDataP->param[0] = Parameter(kOfxImageEffectSimpleSourceClipName, "", PT_LAYER);
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
			"Advanced Options", kOutputLayerParam, "Resize Mode", "Ignore Alpha", "Preview/Draft Mode",
			"Global Random Seed", kAnimateSeedParam, "Log Verbosity" };
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
			// erase any occurence of " (%)" in the string, because the displayed parameter will be in pixels
			size_t start_pos = name.find(" (%)");
    		if(start_pos != string::npos) {
				name.erase(start_pos, 4);
			}
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
				flags = (maxValue == "1") ? 1 : 0; // text(1,...): multi-line (the parser stores the flag in maxValue)
			} else if (paramType == "separator") {
				t = PT_SEPARATOR;
			} else if (paramType == "input") {
				t = PT_LAYER;
			} else {
				// a type this plug-in doesn't know (e.g. from a newer G'MIC, or a malformed line in
				// ~/.config/gmic; the parser warns): a hidden float, which passes 0 in its slot, so
				// that the later arguments keep their positions
				t = PT_FLOAT;
				h = true;
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
	globalDataP->param[p].displayStatus = DS_HIDDEN;
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", PT_TOPIC_START);
	globalDataP->param[p].flags = 1;
	++p;
	// "Frame" is added at the end (issue #65): the choice is saved as its index (and label in
	// Natron), so the other options keep their meaning in saved projects
	globalDataP->param[p] = Parameter(kOutputLayerParam, "", PT_SELECT, 0, kOutputLayerFrame, 1, 0, 0, 0, "Merged|Layer 0|Layer -1|Layer -2|Layer -3|Layer -4|Layer -5|Layer -6|Layer -7|Layer -8|Layer -9|Frame");
	globalDataP->param[p].hint = "The image of the G'MIC results to output. Merged: all the results merged into one. "
		"Layer 0, Layer -1...: one result, counted from the top of the stack as in G'MIC-Qt (Layer 0 is the first). "
		"Frame: the result whose number is the frame number minus the first frame of Source, modulo the number of "
		"results, for the filters that make several frames in one run (Fade Layers, Morph Layers, Tiles to Layers, "
		"Spline Spirograph's Number of Output Frames...). The results are kept in memory, so that the next frames "
		"don't run the filter again while its inputs and parameters stay the same (up to 1/8 of the memory, 1 to 4 GiB, "
		"or OFX_GMIC_FRAME_CACHE_MB megabytes). Each run computes all the results before one is picked or kept, so "
		"it needs the memory of all of them whatever that limit (e.g. 512 HD frames take about 17 GB): the filter's "
		"own number of frames sets it. The node's frame range is Source's: for a still image, extend it "
		"upstream to the number of results (e.g. with a FrameRange node).";
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
	if (effectData.command == effectData.preview_command) {
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
	globalDataP->param[p] = Parameter(kAnimateSeedParam, "", PT_BOOL, 0, 1, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Add the frame number to the Global Random Seed, so that the random numbers change "
		"from frame to frame (reproducibility: see Global Random Seed).";
	++p;
	globalDataP->param[p] = Parameter("Log Verbosity", "", PT_SELECT, 0, 4, 0, 0, 0, 0, "Off|Level 1|Level 2|Level 3|");
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", PT_TOPIC_END);
	++p;
	globalDataP->nofParams = p;

	globalDataP->pluginInfo.description = pluginDescription(effectData);
	globalDataP->nofInputs = (int)std::count_if(globalDataP->param, globalDataP->param + p,
		[](const Parameter& param) { return param.paramType == PT_LAYER; });
	return globalData;
}

static
const EffectData&
effectDataOf(const GlobalData* globalDataP)
{
    return pluginGlobalData().pluginData[globalDataP->pluginIndex];
}

// the filter's command line, with its main or preview command
static
std::string
gmicCommand(const SequenceData* sequenceDataP, const GlobalData* globalDataP, bool preview)
{
    const EffectData& effectData = effectDataOf(globalDataP);
    string cmd = preview ? effectData.preview_command : effectData.command;

    if (cmd == "") {
        cmd = PAR_TXT(PARAM_COMMAND);
        strReplace(cmd, "\r", " ");
        strReplace(cmd, "\n", " ");
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
                const int f = globalDataP->param[i].flags;
                if (f == 2 || f == 3 || f == 5) {
                    // file or folder: G'MIC un-escapes \ and \" inside "...", so escape backslashes
                    // (Windows paths, which G'MIC-Qt gets with '/' from Qt) then double quotes
                    strReplace(s, "\\", "\\\\");
                    strReplace(s, "\"", "\\\"");
                } else {
                    // text and value: as G'MIC-Qt (quotedString()), only escape the double quotes
                    // that are not escaped yet (or a text containing one, e.g. G'MIC code, ends the
                    // argument early), and keep the backslashes and newlines for G'MIC to decode:
                    // "\251" is a ©, and a "#" comment in multi-line G'MIC code ends at its line
                    strReplace(s, "\r\n", "\n");
                    strReplace(s, "\r", "\n");
                    string q;
                    bool escaped = false;
                    for (size_t k = 0; k < s.size(); ++k) {
                        if (escaped) {
                            escaped = false;
                        } else if (s[k] == '\\') {
                            escaped = true;
                        } else if (s[k] == '"') {
                            q += '\\';
                        }
                        q += s[k];
                    }
                    // a trailing odd backslash ("C:\temp\") would escape the closing quote and run
                    // into the next argument: double it, so that it stays literal (G'MIC-Qt has
                    // this bug)
                    if (escaped) q += '\\';
                    s.swap(q);
                }
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
    // (the newlines of text parameters are kept: see above)

    return cmd;
}

// Output Layer "Frame" (issue #65): at most that many results (a frame per result, e.g. Spline
// Spirograph makes up to 512 frames, Fade Layers up to 401); the other modes use at most
// MAX_NOF_LAYERS (Layer -9 is the last one that can be picked)
static const int kMaxFrameResults = 1024;
static size_t frameCacheBudget();

static bool outputIsFrame(const SequenceData* sequenceDataP, const GlobalData* globalDataP)
{
	return PAR_VAL(PARAM_OUTPUT) == (float)kOutputLayerFrame;
}

static int maxResultsOf(const SequenceData* sequenceDataP, const GlobalData* globalDataP)
{
	return outputIsFrame(sequenceDataP, globalDataP) ? kMaxFrameResults : MAX_NOF_LAYERS;
}

// _openfx_gmic_frame_select frame,max_results,max_bytes: if there are more than max_results
// results, or they take more than max_bytes, keeps only result [frame mod N], named
// openfx_gmic_frame_selected (Output Layer "Frame": the results are then not kept, see
// pluginProcess)
static const char* const kFrameSelectCommand =
	"\n_openfx_gmic_frame_select :\n"
	"  b=0 repeat $! { b={$b+w#$>*h#$>*d#$>*s#$>*4} }\n"
	"  if $!>$2||$b>$3 k[{(($1%$!)+$!)%$!}] nm openfx_gmic_frame_selected fi\n";

// The whole command line: the filter's command (main or preview), with the variables G'MIC-Qt
// defines, the output layer selection, Ignore Alpha, the Resize Mode, verbosity and seed. Sets the
// custom commands of the options: customCommands (the user's filter file, the filter's #@gui lines,
// the hot fixes for G'MIC bugs, the Ignore Alpha helpers and _openfx_gmic_opaque_resized), which
// must outlive the G'MIC call.
static
std::string
fullCommand(const SequenceData* sequenceDataP, const GlobalData* globalDataP, GmicInstanceData* instanceDataP, unsigned int nofImages, bool isPreview, string& customCommands)
{
	// the user's filter file, if the filters come from one (after the built-in stdlib, which G'MIC
	// loads first: its definitions replace the built-in ones, as in G'MIC-Qt), the filter's #@gui
	// lines, and the hot fixes for G'MIC bugs (gmic_hotfixes.gmic, after the user's file: they
	// still apply), on every run
	const PluginGlobalData& data = pluginGlobalData();
	customCommands = data.userCommands; // (empty with the built-in stdlib)
	if (!customCommands.empty()) {
		customCommands += '\n';
	}
	customCommands += data.pluginContent[globalDataP->pluginIndex] + '\n' + data.hotfixes;
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
	// Output Layer "Frame": bounded by _openfx_gmic_frame_select, at the end (see pluginProcess)
	if (outputIsFrame(sequenceDataP, globalDataP)) {
		customCommands += kFrameSelectCommand;
	} else {
		cmd += " -if {$!>" + intToString(MAX_NOF_LAYERS) + "} -keep[0-" + intToString(MAX_NOF_LAYERS - 1) + "] -fi";
	}

	instanceDataP->options().custom_commands = customCommands.c_str();
	
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
#else
static DWORD WINAPI gmicCallThread(LPVOID arg)
{
	gmicCallRun((GmicCallData*)arg);
	return 0;
}
#endif

// gmic_plugin_call(), aborted when the host aborts the render: then *aborted is set to true, and
// the result must be discarded.
static int gmicCallAbortable(const OFX::ImageEffect& effect, const char* cmd, unsigned int* nofImages, gmic_interface_image* images, unsigned int maxImages, gmic_interface_options* options, bool* aborted)
{
	*aborted = false;
	if (effect.abort()) {
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
	// the same 8 MiB, reserved (committed on demand): std::thread can't set the stack size, and its
	// default is the host executable's reserve (1 MiB with MSVC's linker, 2 MiB with MinGW's)
	HANDLE thread = CreateThread(NULL, (SIZE_T)8 * 1024 * 1024, gmicCallThread, &d,
	                             STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
	started = (thread != NULL);
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
			if (!*aborted && effect.abort()) {
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
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
#endif
		options->p_is_abort = NULL;
		throw;
	}
#ifndef _WIN32
	pthread_join(thread, NULL);
#else
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
#endif
	options->p_is_abort = NULL;
	if (!*aborted && effect.abort()) {
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

// Output Layer "Frame" (issue #65). Filters such as Fade Layers, Morph Layers (Inter-Frames),
// Tiles to Layers, Spline Spirograph (Number of Output Frames), 3D Conversion (Render Multiple
// Frames) or Multiscale Operator (Interpolated Motion) make N frames in one run, which G'MIC-Qt
// gives as N layers (an animation in GIMP). "Frame" outputs result [(t - start) mod N] at time t,
// where start is the first frame of Source's frame range (0 if there is none, or an infinite
// one): on a still input, the timeline plays the N results from the start of Source, then loops.
// The node's frame range stays Source's (N is only known after a run): with a one-frame Source,
// the host's range must be extended upstream (e.g. Natron's FrameRange node).
//
// Every render would run the filter to output one of its N results: the N results are kept, and
// the next frames are copied from them while the key is the same. The key is the whole G'MIC
// command line, which holds every parameter value at the render time (point positions in % of
// Source, the seed, also when animated with the frame), Preview/Draft Mode, Ignore Alpha, the
// Resize Mode, the verbosity and the image size, plus the size and a hash (four independent
// 64-bit lanes) of the pixels of every input image given to G'MIC (after the conversion to float
// and the unpremultiplication: whatever G'MIC sees, which covers the layer inputs, the frames of
// Source given by the layer times, and upstream changes). The render scale and window only change
// the output placement, done when copying. The cache is keyed by this content key, not by
// instance: Natron renders through instance clones, and duplicated nodes get the same results;
// each instance refers to the key of its last Frame render, and an entry that no instance refers
// to any more is dropped (another key, another Output Layer, the instance destroyed). Nothing
// is invalidated explicitly: another key gives another entry. A filter whose key changes from
// frame to frame anyway (a varying input, an animated parameter, Animate Random Seed) runs on
// every frame, as before.
// Memory: the results stay in the buffers G'MIC returned (no copy), at most frameCacheBudget()
// bytes for all the entries (1/8 of the physical memory, 1 to 4 GiB, or OFX_GMIC_FRAME_CACHE_MB
// megabytes; 0 disables the cache); the least recently used entries are dropped to make room. A
// set of results larger than the budget, or of more than kMaxFrameResults images, is reduced by
// G'MIC to the one result needed (_openfx_gmic_frame_select), so that the memory held after a
// run stays bounded: the filter then runs on every frame. The host purging its caches drops all
// the entries. The host serialises the renders of an instance (kOfxImageEffectRenderInstanceSafe);
// the cache, shared by all instances, is behind a mutex, and its frames are reference counted,
// so that dropping an entry while another render copies one of its frames is safe.
// Single flight: a render that misses while another render (a clone, another thread) runs G'MIC
// for the same key waits for that run, then takes its result from the cache; if there is none
// (the run failed or was aborted, or its results were not kept), it runs G'MIC itself.
static unsigned long long physicalMemoryBytes(); // at the end of the file

// OFX_GMIC_FRAME_CACHE_MB: a decimal number >= 0, with '.' as decimal point whatever the locale
// and an optional exponent ("512", "0.5", "2e3"), surrounding whitespace allowed. Anything else
// (a sign, "inf", "nan", hexadecimal, other characters, an overflow) gives the default budget,
// without isnan/isinf (folded with -ffinite-math-only): only this syntax reaches the conversion.
static bool frameCacheParseMB(const char* env, double& mb)
{
	std::string s(env ? env : "");
	const size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
	if (b == std::string::npos) return false;
	s = s.substr(b, e - b + 1);
	size_t i = 0, digits = 0;
	while (i < s.size() && isdigit((unsigned char)s[i])) ++i, ++digits;
	if (i < s.size() && s[i] == '.') ++i;
	while (i < s.size() && isdigit((unsigned char)s[i])) ++i, ++digits;
	if (digits == 0) return false;
	if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
		++i;
		if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
		const size_t first = i;
		while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
		if (i == first) return false;
	}
	if (i != s.size()) return false;
	errno = 0;
	mb = stringToDouble(s); // locale-independent strtod
	if (errno == ERANGE && mb > 1.) return false; // overflow (an underflow gives 0)
	return mb >= 0. && mb <= std::numeric_limits<double>::max();
}

static size_t computeFrameCacheBudget()
{
	const double GiB = 1024. * 1024. * 1024.;
	double bytes = 0.;
	const char* env = getenv("OFX_GMIC_FRAME_CACHE_MB");
	double mb = 0.;
	if (env && frameCacheParseMB(env, mb)) {
		bytes = mb * 1024. * 1024.;
	} else {
		if (env && *env) {
			fprintf(stderr, "openfx-gmic: OFX_GMIC_FRAME_CACHE_MB=\"%s\" is not a number of megabytes >= 0: using the default budget\n", env);
		}
		// leave room for the host's own cache (Natron's RAM cache)
		bytes = (double)physicalMemoryBytes() / 8.;
		bytes = std::min(std::max(bytes, 1. * GiB), 4. * GiB);
	}
	return (size_t)std::min(bytes, (double)(std::numeric_limits<size_t>::max() / 2));
}

static size_t frameCacheBudget()
{
	static const size_t budget = computeFrameCacheBudget();
	return budget;
}

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
	CachedFrames frames;
	size_t bytes;
	unsigned long long lastUse;
};

struct FrameCache
{
	std::mutex mutex;
	std::map<std::string, FrameCacheEntry> entries; // by key
	std::map<const void*, std::string> ownerKey; // the key each instance (GmicInstanceData) refers to
	std::set<std::string> running; // the keys G'MIC is running for (see frameCacheFind)
	std::condition_variable runDone; // a key left running
	std::set<std::string> uncacheable; // keys whose results were not kept (too large, empty): not waited for
	size_t bytes;
	unsigned long long clock;
	FrameCache() : bytes(0), clock(0) {}
};

// Never destroyed: instances may still be destroyed after the static objects at unload.
static FrameCache& frameCache()
{
	static FrameCache* cache = new FrameCache;
	return *cache;
}

static size_t imageBytes(const gmic_interface_image& im)
{
	return (size_t)im.width * im.height * im.depth * im.spectrum * sizeof(float);
}

// With the mutex locked. Moves the frames of an entry to dropped (freed by the caller after
// unlocking), then removes it: the total only changes once nothing can throw.
static void frameCacheDrop(FrameCache& c, std::map<std::string, FrameCacheEntry>::iterator it, std::vector<CachedFrames>& dropped)
{
	dropped.push_back(CachedFrames()); // may throw: nothing changed yet
	dropped.back().swap(it->second.frames);
	c.bytes -= it->second.bytes;
	c.entries.erase(it);
}

// With the mutex locked: drops the entry of key if no instance refers to it.
static void frameCacheDropIfUnused(FrameCache& c, const std::string& key, std::vector<CachedFrames>& dropped)
{
	for (std::map<const void*, std::string>::const_iterator o = c.ownerKey.begin(); o != c.ownerKey.end(); ++o) {
		if (o->second == key) return;
	}
	std::map<std::string, FrameCacheEntry>::iterator it = c.entries.find(key);
	if (it != c.entries.end()) frameCacheDrop(c, it, dropped);
}

// With the mutex locked: the instance now refers to key (its previous entry may be dropped).
static void frameCacheSetOwnerKey(FrameCache& c, const void* owner, const std::string& key, std::vector<CachedFrames>& dropped)
{
	std::map<const void*, std::string>::iterator o = c.ownerKey.find(owner);
	if (o != c.ownerKey.end() && o->second == key) return;
	std::string previous;
	if (o != c.ownerKey.end()) previous.swap(o->second);
	c.ownerKey[owner] = key;
	if (!previous.empty()) frameCacheDropIfUnused(c, previous, dropped);
}

// result [frame mod N] of the entry of key (frame: any integer), or NULL if there is none; the
// instance now refers to key. If another render is running G'MIC for key, waits for it first,
// polling the host's abort (then sets *aborted). On NULL without abort, the caller runs G'MIC; if
// *claimed is set, other renders wait for it: it must call frameCacheRunDone(key) whatever happens.
static std::shared_ptr<const CachedFrame> frameCacheFind(const void* owner, const std::string& key, long long frame,
	const OFX::ImageEffect& effect, bool* claimed, bool* aborted)
{
	std::vector<CachedFrames> dropped; // freed after unlocking
	FrameCache& c = frameCache();
	std::unique_lock<std::mutex> lock(c.mutex);
	frameCacheSetOwnerKey(c, owner, key, dropped);
	for (bool waited = false; ; waited = true) {
		std::map<std::string, FrameCacheEntry>::iterator it = c.entries.find(key);
		if (it != c.entries.end() && !it->second.frames.empty()) {
			it->second.lastUse = ++c.clock;
			const long long n = (long long)it->second.frames.size();
			return it->second.frames[(size_t)(((frame % n) + n) % n)];
		}
		// not kept by the run waited for, or too large to keep anyway: run G'MIC (maybe alongside
		// another render)
		if (waited || c.uncacheable.count(key)) return std::shared_ptr<const CachedFrame>();
		if (c.running.insert(key).second) {
			*claimed = true;
			return std::shared_ptr<const CachedFrame>();
		}
		while (c.running.count(key)) {
			c.runDone.wait_for(lock, std::chrono::milliseconds(50));
			lock.unlock();
			const bool abort = effect.abort();
			lock.lock();
			if (abort) {
				*aborted = true;
				return std::shared_ptr<const CachedFrame>();
			}
		}
	}
}

// the G'MIC run claimed by frameCacheFind for key is over (its results are stored, if kept)
static void frameCacheRunDone(const std::string& key)
{
	FrameCache& c = frameCache();
	std::lock_guard<std::mutex> lock(c.mutex);
	c.running.erase(key);
	c.runDone.notify_all();
}

// the results for key can't be kept (too large, empty): the next renders don't wait for each
// other; true the first time for key (to log it). At most 64 keys are remembered.
static bool frameCacheUncacheable(const std::string& key)
{
	FrameCache& c = frameCache();
	std::lock_guard<std::mutex> lock(c.mutex);
	if (c.uncacheable.size() >= 64 && !c.uncacheable.count(key)) c.uncacheable.clear();
	return c.uncacheable.insert(key).second;
}

// the instance no longer refers to an entry (destroyed, or Output Layer is not Frame)
static void frameCacheRelease(const void* owner)
{
	std::vector<CachedFrames> dropped; // freed after unlocking
	FrameCache& c = frameCache();
	std::lock_guard<std::mutex> lock(c.mutex);
	std::map<const void*, std::string>::iterator o = c.ownerKey.find(owner);
	if (o == c.ownerKey.end()) return;
	std::string key;
	key.swap(o->second);
	c.ownerKey.erase(o);
	frameCacheDropIfUnused(c, key, dropped);
}

// keeps the results images[0..nofImages-1] under key, for the instance: their buffers now belong
// to the cache, and their data pointers are set to NULL. Results that are not usable or don't fit
// in the budget are left to the caller.
static void frameCacheStore(const void* owner, const std::string& key, gmic_interface_image* images, unsigned int nofImages, const std::string& filterName)
{
	size_t bytes = 0;
	for (unsigned int i = 0; i < nofImages; ++i) {
		if (!images[i].data || images[i].spectrum == 0) {
			if (frameCacheUncacheable(key)) fprintf(stderr, "G'MIC: %s: result %u is empty, the results are not kept (Output Layer Frame runs the filter on every frame)\n",
					filterName.c_str(), i);
			return;
		}
		bytes += imageBytes(images[i]);
	}
	if (nofImages == 0 || bytes > frameCacheBudget()) return;
	FrameCacheEntry entry;
	entry.bytes = bytes;
	entry.lastUse = 0;
	entry.frames.reserve(nofImages);
	for (unsigned int i = 0; i < nofImages; ++i) {
		std::shared_ptr<CachedFrame> f = std::make_shared<CachedFrame>();
		entry.frames.push_back(f);
		f->image = images[i]; // owned from here (nothing throws until the end)
		images[i].data = NULL;
	}
	std::vector<CachedFrames> dropped; // freed after unlocking
	FrameCache& c = frameCache();
	std::lock_guard<std::mutex> lock(c.mutex);
	frameCacheSetOwnerKey(c, owner, key, dropped);
	std::map<std::string, FrameCacheEntry>::iterator it = c.entries.find(key);
	if (it != c.entries.end()) frameCacheDrop(c, it, dropped); // stored meanwhile by another instance
	// make room: drop the least recently used entries
	while (c.bytes + bytes > frameCacheBudget() && !c.entries.empty()) {
		std::map<std::string, FrameCacheEntry>::iterator lru = c.entries.begin();
		for (it = c.entries.begin(); it != c.entries.end(); ++it) {
			if (it->second.lastUse < lru->second.lastUse) lru = it;
		}
		frameCacheDrop(c, lru, dropped);
	}
	FrameCacheEntry& stored = c.entries[key]; // may throw: the total is updated after
	stored.frames.swap(entry.frames);
	stored.bytes = bytes;
	stored.lastUse = ++c.clock;
	c.bytes += bytes;
}

// a hash of a buffer, appended to key in hex: four independent 64-bit lanes (xxHash64 rounds),
// each over every fourth 64-bit word
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

// the key of the results: the plug-in, the command line and the input images (see above)
static std::string frameCacheKey(const GlobalData* globalDataP, const std::string& cmd, const SequenceData* sequenceDataP, unsigned int nofImages)
{
	std::string key = intToString(globalDataP->pluginIndex) + "\n" + cmd;
	for (unsigned int i = 0; i < nofImages; ++i) {
		const World& w = sequenceDataP->inWorld[i];
		key += "\n" + intToString(w.width) + "x" + intToString(w.height) + ":";
		appendHash(key, w.data, (size_t)w.width * (size_t)w.height * 4 * sizeof(float));
	}
	return key;
}

// the host purges its caches (kOfxActionPurgeCaches): drop all the entries (Natron renders
// through clones, so the instance purged is not the one that refers to the entries)
void pluginPurgeCaches()
{
	std::vector<CachedFrames> dropped; // freed after unlocking
	FrameCache& c = frameCache();
	std::lock_guard<std::mutex> lock(c.mutex);
	while (!c.entries.empty()) frameCacheDrop(c, c.entries.begin(), dropped);
}

// Output Layer changed (any reason): not Frame, the instance's entry may be dropped now
void pluginOutputLayerChanged(GmicInstanceData& instanceData, int outputLayer)
{
	if (outputLayer != kOutputLayerFrame) {
		frameCacheRelease(&instanceData);
	}
}

// the frame number of Output Layer "Frame": the render time minus the first frame of Source,
// rounded to the nearest frame
static long long frameNumberOf(const SequenceData* sequenceDataP)
{
	return (long long)std::floor(sequenceDataP->time - sequenceDataP->frameRangeStart + 0.5);
}

bool pluginProcess(SequenceData& sequenceData, const GlobalData& globalData, GmicInstanceData& instanceData, OFX::ImageEffect& effect)
{
	// (the names used by the PAR_* and PARAM_* macros)
	SequenceData* const sequenceDataP = &sequenceData;
	const GlobalData* const globalDataP = &globalData;
	GmicInstanceData* const instanceDataP = &instanceData;
	bool ok = true;
	sequenceDataP->resultHasAlpha = true;

	instanceDataP->options().no_inplace_processing = true; // the input buffers are not overwritten

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
			memset(sequenceDataP->outWorld.data, 0, (size_t)sequenceDataP->outWorld.width * (size_t)sequenceDataP->outWorld.height * 4 * sizeof(float));
		}
		effect.clearPersistentMessage();
		return ok;
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
	} resetCustomCommands(instanceDataP->options());
	string cmd = fullCommand(sequenceDataP, globalDataP, instanceDataP, nofImages, preview, customCommands);
	bool aborted = false;
	int result = 0;
	string errmsg;
	// Output Layer "Frame": the results kept from a previous render, if the key is the same
	const long long frame = frameNumberOf(sequenceDataP);
	string cacheKey;
	std::shared_ptr<const CachedFrame> cachedFrame;
	// this render runs G'MIC for cacheKey while other renders wait: release the key on any return
	struct RunClaim {
		const string& key;
		bool claimed;
		explicit RunClaim(const string& k) : key(k), claimed(false) {}
		~RunClaim() { if (claimed) frameCacheRunDone(key); }
	} claim(cacheKey);
	const bool useCache = frameMode && frameCacheBudget() > 0; // OFX_GMIC_FRAME_CACHE_MB=0: no cache
	if (useCache) {
		cacheKey = frameCacheKey(globalDataP, cmd, sequenceDataP, nofImages);
		cachedFrame = frameCacheFind(instanceDataP, cacheKey, frame, effect, &claim.claimed, &aborted);
	} else {
		frameCacheRelease(instanceDataP);
	}
	const bool runGmic = !cachedFrame && !aborted;
	// Frame: at most the results that can be kept (see kFrameSelectCommand), after the key
	const string frameSelect = frameMode ? " -_openfx_gmic_frame_select " + std::to_string(frame) + "," +
		intToString(kMaxFrameResults) + "," + std::to_string((unsigned long long)frameCacheBudget()) : string();
	if (runGmic) {
		cmd += frameSelect;
		result = gmicCallAbortable(effect, cmd.c_str(), &nofImages, images, maxImages, &instanceDataP->options(), &aborted);
		errmsg = string(instanceDataP->options().error_message_buffer);
		// The retry needs unchanged inputs: gmic_plugin_call() works on copies (no_inplace_processing)
		// and leaves images[] unchanged on an error. Not after an abort.
		if (result != 0 && !aborted && !preview && effectData.previewFallback && instanceDataP->options().no_inplace_processing) {
			fprintf(stderr, "G'MIC: %s: the main command failed (%s), rendering with the preview command\n",
					effectData.name.c_str(), errmsg.c_str());
			preview = true;
			cmd = fullCommand(sequenceDataP, globalDataP, instanceDataP, nofImages, preview, customCommands) + frameSelect;
			result = gmicCallAbortable(effect, cmd.c_str(), &nofImages, images, maxImages, &instanceDataP->options(), &aborted);
			if (result != 0 && !aborted) {
				errmsg += " (with the preview command: " + string(instanceDataP->options().error_message_buffer) + ")";
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
		ok = false;
	} else if (result == 0) {
		// Output Layer: 0 is Merged (a single image), k > 0 is "Layer -(k-1)", i.e. image [k-1],
		// counted from the top of the stack like the inputs (Source = [0] = "Layer 0", the active
		// layer in G'MIC-Qt, then the "Layer -1" input = [1]...). Past the last result: the last one.
		// Frame: image [frame mod N] (see frameNumberOf), maybe from the kept results.
		const gmic_interface_image* res = NULL;
		if (cachedFrame) res = &cachedFrame->image;
		// the result selected by G'MIC: too many or too large to keep
		const bool selected = frameMode && nofImages == 1 && strcmp(images[0].name, "openfx_gmic_frame_selected") == 0;
		if (!res && nofImages > 0) {
			int idx = (int)PAR_VAL(PARAM_OUTPUT) - 1;
			if (frameMode) idx = selected ? 0 : (int)(((frame % (long long)nofImages) + nofImages) % nofImages);
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
			float* const dst = out.data;
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
		// Frame: keep the results for the next frames (their buffers then belong to the cache)
		if (result == 0 && useCache && runGmic && !selected) {
			// out of memory: the results not moved to the cache are freed below, as without cache
			try {
				frameCacheStore(instanceDataP, cacheKey, images, nofImages, effectData.name);
			} catch (const std::bad_alloc&) {
			}
		} else if (result == 0 && useCache && selected && frameCacheUncacheable(cacheKey)) {
			fprintf(stderr, "G'MIC: %s: the results are too large to keep (more than %d, or more than %.0f MB): "
					"Output Layer Frame runs the filter on every frame\n", effectData.name.c_str(), kMaxFrameResults,
					(double)frameCacheBudget() / (1024. * 1024.));
		}
		if (claim.claimed) { claim.claimed = false; frameCacheRunDone(cacheKey); } // the waiting renders go on
	}
	if (result != 0 && !aborted) {
		// persistent message on the node, instead of a modal message on every render
		showRenderError(effect, errmsg, "G'MIC Error");
		ok = false;
	}
	else if (!aborted) {
		effect.clearPersistentMessage();
	}

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
	return ok;
}

int getNofPlugins()
{
	return (int)pluginGlobalData().pluginData.size();
}

PluginInfo getPluginInfo(int pluginIndex)
{
	PluginInfo pluginInfo;
	pluginInfo.name = pluginGlobalData().pluginData[pluginIndex].name;
	pluginInfo.identifier = pluginGlobalData().pluginData[pluginIndex].uniqueId;
	pluginInfo.category = pluginGlobalData().pluginData[pluginIndex].category;
	// (the description is made by pluginSetup)
	pluginInfo.major_version = MAJOR_VERSION;
	pluginInfo.minor_version = MINOR_VERSION;
	return pluginInfo;
}

// the physical memory (frameCacheBudget), 0 if unknown
#if defined(_WIN32)
// (windows.h: included at the top)
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif
static unsigned long long physicalMemoryBytes()
{
#if defined(_WIN32)
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	return GlobalMemoryStatusEx(&status) ? (unsigned long long)status.ullTotalPhys : 0ULL;
#elif defined(__APPLE__)
	unsigned long long mem = 0;
	size_t len = sizeof(mem);
	return sysctlbyname("hw.memsize", &mem, &len, NULL, 0) == 0 ? mem : 0ULL;
#else
	const long pages = sysconf(_SC_PHYS_PAGES), pageSize = sysconf(_SC_PAGESIZE);
	return (pages > 0 && pageSize > 0) ? (unsigned long long)pages * (unsigned long long)pageSize : 0ULL;
#endif
}
