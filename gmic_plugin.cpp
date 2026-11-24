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
// - handle separator() as a ParamType::Separator parameter
// - handle point(...)
// - handle note(...) as a label parameter (ParamType::Text with flags=4)
// - handle filein(..) and fileout(...)
// - color(...) is now RGBA
// - better handle link(...)
// - correctly handle multiple parameters on the same line, or multi-line parameters of any kind.
// - the G'MIC command-line is only computed when rendering, not at each param change (because converting pixel coords to % for point(...) requires image size)
// - bug fixes
// 2.0.0: (OpenFX plug-in)
// - color(...) without alpha is RGB, as in G'MIC-Qt (issue #178)
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
#include "gmic_log.h"
#include "gmic_string.h"
#include "ofxsImageEffect.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using std::string;
using std::vector;
using namespace gmic_ofx;

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
#include <atomic>
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

// The hot fixes that apply to the commands that G'MIC runs: those of the standard library stdlib,
// replaced by those that userCommands defines again (the user's filter file userFile, passed as
// custom commands, see PluginGlobalData::userCommands; empty with the built-in filters). Each
// block of gmic_hotfixes.gmic starts with '#@hotfix <command> <original> <fixed>
// [<callee>:<checksum>...]': the FNV-1a 64-bit hashes of the built-in definition the fix was made
// from, of the fixed definition (the block's own), and of the built-in definitions of the commands
// the fix relies on; <original> is "-" for a command that the built-in G'MIC no longer defines
// (but still calls). A block is applied if the command is the original one (or, for "-", is not
// defined) and the callees are unchanged, and skipped silently if the command already is the fixed
// one (fixed upstream). Otherwise it is skipped: with a note if the user's file defines the
// command or a callee another way (its definition is kept: a copy of the built-in definition
// still gets the fix), else with a warning for the developers.
static string gmicGetHotfixes(const string& stdlib, const string& userCommands, const string& userFile)
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
	std::map<string, string> userDefs;
	for (const auto& d : defs) {
		userDefs[d.first];
	}
	gmicGetDefinitions(userCommands, userDefs);
	for (const auto& d : userDefs) {
		if (!d.second.empty()) {
			defs[d.first] = d.second; // (G'MIC loads the user's file after the built-in stdlib)
		}
	}
	string hotfixes;
	for (size_t i = 0; i < checks.size(); ++i) {
		const string& command = checks[i][0].first;
		std::map<string, string> own;
		own[command];
		gmicGetDefinitions(texts[i], own);
		const string ownChecksum = gmicChecksum(own[command]);
		if (own[command].empty() || ownChecksum != fixed[i]) {
			gmicLog("openfx-gmic: Warning: hot fix for the G'MIC command '%s' not applied: the fixed definition has checksum %s, but its '#@hotfix' line says '%s', update it in gmic_hotfixes.gmic\n",
			        command.c_str(), ownChecksum.c_str(), fixed[i].c_str());
			continue;
		}
		if (!defs[command].empty() && gmicChecksum(defs[command]) == ownChecksum) {
			continue; // already fixed in G'MIC (then the callees do not matter)
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
				if (!userDefs[name].empty()) {
					if (k == 0) {
						gmicLog("openfx-gmic: '%s' is defined by %s: its definition is kept\n", name.c_str(), userFile.c_str());
					} else {
						gmicLog("openfx-gmic: '%s', which the plug-in's fix of '%s' calls, is defined by %s: the definition of '%s' is kept\n",
						        name.c_str(), command.c_str(), userFile.c_str(), command.c_str());
					}
					continue;
				}
				gmicLog("openfx-gmic: Warning: hot fix for the G'MIC command '%s' not applied: the built-in definition of '%s' %s (checksum %s, the fix is for %s), review it in gmic_hotfixes.gmic\n",
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
// the log file (gmicOpenLog): in the user's G'MIC configuration directory, if the user made it
constexpr const char* kLogFile = "gmic_ofx_log";

// "text" as UTF-8 if it is UTF-16 with a byte-order mark (FF FE: little-endian, FE FF: big-endian;
// e.g. Windows PowerShell 5's "> file"), else unchanged. An unpaired surrogate becomes U+FFFD, an odd
// last byte is dropped.
string utf16ToUtf8(const string& text)
{
	const bool le = text.compare(0, 2, "\xFF\xFE") == 0;
	if (!le && text.compare(0, 2, "\xFE\xFF") != 0) {
		return text;
	}
	const auto unit = [&text, le](size_t i) {
		const unsigned lo = static_cast<unsigned char>(text[i + (le ? 0 : 1)]);
		const unsigned hi = static_cast<unsigned char>(text[i + (le ? 1 : 0)]);
		return hi << 8 | lo;
	};
	string utf8;
	const auto put = [&utf8](unsigned byte) { utf8 += static_cast<char>(byte); };
	utf8.reserve(text.size() / 2);
	for (size_t i = 2; i + 1 < text.size(); i += 2) {
		unsigned c = unit(i);
		if (c >= 0xD800 && c < 0xDC00 && i + 3 < text.size() && unit(i + 2) >= 0xDC00 && unit(i + 2) < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (unit(i + 2) - 0xDC00);
			i += 2;
		} else if (c >= 0xD800 && c < 0xE000) {
			c = 0xFFFD;
		}
		if (c < 0x80) {
			put(c);
		} else if (c < 0x800) {
			put(0xC0 | c >> 6);
			put(0x80 | (c & 0x3F));
		} else if (c < 0x10000) {
			put(0xE0 | c >> 12);
			put(0x80 | (c >> 6 & 0x3F));
			put(0x80 | (c & 0x3F));
		} else {
			put(0xF0 | c >> 18);
			put(0x80 | (c >> 12 & 0x3F));
			put(0x80 | (c >> 6 & 0x3F));
			put(0x80 | (c & 0x3F));
		}
	}
	return utf8;
}

// The plug-in identifiers that "list" (the contents of a gmic_ofx_plugins.txt: UTF-8, ASCII, or
// UTF-16 with a byte-order mark; LF, CRLF or CR line endings) disables. Each line
// is a plug-in identifier, enabled, or disabled if commented out with one or more '#' (and spaces),
// either one optionally followed by a '#' comment; any other line is a comment. If an identifier is
// on several lines, the last one wins. (The identifiers aren't checked here: one that is no plug-in,
// e.g. of a filter that G'MIC removed, changes nothing; see pluginListSummary.)
std::set<string> disabledPlugins(const string& list)
{
	constexpr const char* kSpaces = " \t";
	std::set<string> disabled;
	string text = utf16ToUtf8(list);
	if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) { // (UTF-8 BOM)
		text.erase(0, 3);
	}
	// (a CRLF gives an empty line between its CR and LF, which is skipped as any empty line)
	for (size_t start = 0; start < text.size();) {
		const size_t end = std::min(text.find_first_of("\r\n", start), text.size());
		const string line = text.substr(start, end - start);
		start = end + 1;
		size_t b = line.find_first_not_of(kSpaces);
		const bool commented = b != string::npos && line[b] == '#';
		if (commented) {
			b = line.find_first_not_of(" \t#", b); // ("##eu.gmic.X", "# # eu.gmic.X")
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

// The line that reports a user's list at "path" that disables "disabled": how many of the plug-ins
// "pluginIds" it disables, and the first 10, in sorted order, of its disabled identifiers that are
// none of them (a typo, or a filter that G'MIC removed or renamed).
string pluginListSummary(const string& path, const std::set<string>& disabled, const std::set<string>& pluginIds)
{
	constexpr size_t kMaxUnknown = 10;
	size_t nDisabled = 0;
	size_t nUnknown = 0;
	string unknown;
	for (const string& id : disabled) {
		if (pluginIds.count(id)) {
			++nDisabled;
		} else if (++nUnknown <= kMaxUnknown) {
			unknown += (nUnknown == 1 ? "; unknown identifiers: " : ", ") + id;
		}
	}
	if (nUnknown > kMaxUnknown) {
		unknown += ", ... (" + std::to_string(nUnknown) + " in all)";
	}
	return "openfx-gmic: " + path + ": " + std::to_string(nDisabled) + (nDisabled == 1 ? " plug-in" : " plug-ins") + " disabled" + unknown;
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
	// The filter file (gmic_ofx.gmic, gmic_stdlib.gmic or G'MIC's update file), when the filters
	// come from one: custom commands for every run, so that its filters find the commands it
	// defines, as in G'MIC-Qt. Empty with the built-in stdlib, which G'MIC loads itself.
	string userCommands;
	string hotfixes; // custom commands for every run, see gmicGetHotfixes()
};

// A filter file: its path, for messages, and its contents.
struct FilterFile
{
	string path;
	string content;
};

// G'MIC's filter update file of this G'MIC version (update408.gmic for G'MIC 4.0.8), in G'MIC's
// own folder (gmicRcPath()), where G'MIC-Qt and G'MIC's 'update' command download it as text (the
// plug-in never downloads it). Read only if OFX_GMIC_UPDATE_FILE is 1, since G'MIC-Qt rewrites it
// on its own and a host's stale plug-in cache would then swap filters (README, "Environment"),
// and never in COMMERCIAL builds, since it brings back the filters under the GPL. Its path and
// contents, or an empty content if it is not read, missing, has no filter (G'MIC-Qt leaves an
// empty file after a failed download), or is still CImg-compressed, which only G'MIC could read.
static FilterFile loadUpdateFile()
{
	const char* const optIn = std::getenv("OFX_GMIC_UPDATE_FILE");
	if (!optIn) return FilterFile();
	if (string(optIn) != "1") {
		gmicLog("openfx-gmic: OFX_GMIC_UPDATE_FILE=\"%s\" is not 1: G'MIC's update file is not read\n", optIn);
		return FilterFile();
	}
#ifdef OFX_GMIC_COMMERCIAL
	gmicLog("openfx-gmic: OFX_GMIC_UPDATE_FILE ignored: the update file has GPL filters, which COMMERCIAL builds leave out\n");
	return FilterFile();
#else
	const string rcPath = gmicRcPath();
	if (rcPath.empty()) return FilterFile();
	FilterFile file{rcPath + gmicUpdateFileName(), string()};
	file.content = loadStringFromFile(file.path);
	static const string kCImgPrefix = "1 uint8 "; // a serialized CImg (G'MIC-Qt's Updater::isCImgCompressed)
	if (file.content.compare(0, kCImgPrefix.size(), kCImgPrefix) == 0) {
		gmicLog("openfx-gmic: Warning: %s is compressed, the plug-in only reads it as text: ignored\n", file.path.c_str());
		return FilterFile();
	}
	if (file.content.find("#@gui") == string::npos) return FilterFile(); // (as G'MIC-Qt)
	return file;
#endif
}

// The filter file the filters come from, the first of these that the plug-in can use (README,
// "Filters"): ~/.config/gmic/gmic_ofx.gmic or gmic_stdlib.gmic (in rcPath, from
// get_gmic_rc_path()) if not empty, then G'MIC's update file (loadUpdateFile). An empty content
// if none: the filters then come from the built-in stdlib.
static FilterFile loadFilterFile(const string& rcPath)
{
	FilterFile file{rcPath + "gmic_ofx.gmic", string()};
	file.content = loadStringFromFile(file.path);
	if (file.content.empty()) {
		file.path = rcPath + "gmic_stdlib.gmic";
		file.content = loadStringFromFile(file.path);
	}
	if (file.content.empty()) {
		file = loadUpdateFile();
	}
	return file;
}

PluginGlobalData loadPluginGlobalData()
{
	PluginGlobalData data;
	vector<EffectData>& pluginData = data.pluginData;
	vector<string>& pluginContent = data.pluginContent;
	{
		// The hot fixes are checked against the built-in stdlib, which G'MIC always loads, and the
		// filter file's commands, which replace its own: decompress it once here (~35 ms), even
		// when the filters come from a file.
		string stdlib;
		{
			const char* lib = gmic_get_stdlib();
			stdlib = string(lib);
			gmic_delete_external((float*)lib);
		}
		bool builtinStdlib = false; // the curated lists of the parser are only checked against it
		const string rcPath = get_gmic_rc_path();
		// before the first message (also G'MIC's: the hot fixes, the renders)
		// (not when the build writes the bundle's plug-in list, OFX_GMIC_WRITE_PLUGIN_LIST: its
		// messages stay on stderr, in the build log)
		const char* const writeList = std::getenv("OFX_GMIC_WRITE_PLUGIN_LIST");
		if (!rcPath.empty() && !(writeList && *writeList)) gmicOpenLog(rcPath + kLogFile);
		FilterFile file = loadFilterFile(rcPath);
		string effectContent = std::move(file.content);
		if (!effectContent.empty()) {
			data.hotfixes = gmicGetHotfixes(stdlib, effectContent, file.path);
		} else {
			data.hotfixes = gmicGetHotfixes(stdlib, string(), string());
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
		// Each plug-in has a fixed table of kMaxParams parameters: the filter's own parameters,
		// up to 4 extra layers and the fixed trailing parameters (see pluginSetup). Leave out the
		// few filters that do not fit (e.g. "Transfer Color [Reduced Colors]" in G'MIC 4.0.7,
		// with several hundred parameters) instead of overflowing the table.
		leaveOutFilters([](const EffectData& d) {
			return (int)d.param.size() + kNofExtraParams + (d.multiLayer ? kNofLayerTimeParams : 0) > kMaxParams;
		});
		// The plug-ins that the user's list disables are not registered (OFX has no way to only hide
		// them from the menus). The bundle's list enables every plug-in: it isn't read.
		// OFX_GMIC_WRITE_PLUGIN_LIST=<file>: write the list there (the build writes the bundle's).
		const string userList = rcPath.empty() ? string() : loadStringFromFile(rcPath + kPluginListFile);
		const std::set<string> disabled = disabledPlugins(userList);
		if (!userList.empty()) { // (on stderr or in the log file: Natron's GUI shows neither)
			std::set<string> pluginIds;
			for (const EffectData& d : pluginData) {
				pluginIds.insert(d.uniqueId);
			}
			gmicLog("%s\n", pluginListSummary(rcPath + kPluginListFile, disabled, pluginIds).c_str());
		}
		const char* const listPath = std::getenv("OFX_GMIC_WRITE_PLUGIN_LIST");
		if (listPath && *listPath && !writePluginList(listPath, pluginData, disabled)) {
			gmicLog("openfx-gmic: Error: could not write the plug-in list %s\n", listPath);
		}
		if (!disabled.empty()) {
			leaveOutFilters([&disabled](const EffectData& d) { return disabled.count(d.uniqueId) > 0; });
		}
		// the plug-ins that run their preview command first (#206), one line each. CI's describe
		// test (.github/workflows/describe_plugins.sh) reads these lines: keep their text the same
		for (const EffectData& d : pluginData) {
			if (d.previewFirst) gmicLog("openfx-gmic: previewFirst %s\n", d.uniqueId.c_str());
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
			gmicLog("openfx-gmic: Error: could not load the G'MIC filters: %s\n", e.what());
		} catch (...) {
			gmicLog("openfx-gmic: Error: could not load the G'MIC filters\n");
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

	globalDataP->param[0] = Parameter(kOfxImageEffectSimpleSourceClipName, "", ParamType::Layer);
	int p = 1;
	for (int i = 0; i < (int)effectData.param.size(); i++) {
		ParamType t = ParamType::Float;
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
		static const string reservedNames[] = { "Layer -1", "Layer -2", "Layer -3", "Layer -4",
			"Layer Times", kLayersFromSourceParam, string("Layer -1") + kTimeOffsetParamSuffix,
			string("Layer -2") + kTimeOffsetParamSuffix, string("Layer -3") + kTimeOffsetParamSuffix,
			string("Layer -4") + kTimeOffsetParamSuffix, "Command",
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
			t = ParamType::Color;
			vector<string> r = strSplit(defaultValue, '|'); // drops empty fields: pad below
			if (r.size() == 1) { r.push_back(r[0]); r.push_back(r[0]); } // gray: color(V)
			while (r.size() < 3) r.push_back("0");
			d1 = (float)stringToDouble(r[0]) / kGmicValueScale;
			d2 = (float)stringToDouble(r[1]) / kGmicValueScale;
			d3 = (float)stringToDouble(r[2]) / kGmicValueScale;
			// color(R,_G,_B,_A): only pass alpha to G'MIC if the filter declares it
			flags = (r.size() > 3 && !r[3].empty()) ? kParamFlagsColorRGBA : kParamFlagsColorRGB;
			d4 = (flags == kParamFlagsColorRGBA) ? (float)stringToDouble(r[3]) / kGmicValueScale : 1.f;
		} else if (paramType == "point") {
			t = ParamType::Point;
			vector<string> r = strSplit(defaultValue, '|'); // drops empty fields: pad below
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
				t = ParamType::Bool;
			} else if (paramType == "pointenabled") {
				// enables the removable point before it, see kParamFlagsPointEnabled
				t = ParamType::Bool;
				flags = kParamFlagsPointEnabled;
			} else if (paramType == "choice") {
				t = ParamType::Select;
				// "color" and "point": see above
			} else if (paramType == "value") {
				h = true;
				t = ParamType::Text;
			} else if (paramType == "file" || paramType == "fileout") {
				t = ParamType::Text;
				flags = 2;
			} else if (paramType == "filein") {
				t = ParamType::Text;
				flags = 5;
			} else if (paramType == "float") {
				t = ParamType::Float;
			} else if (paramType == "folder") {
				t = ParamType::Text;
				flags = 3;
			} else if (paramType == "int") {
				t = ParamType::Int;
			} else if (paramType == "note" || paramType == "link") {
				t = ParamType::Text;
				string note = strRemoveXmlTags(strTrim(text, " \t\r\n'\""));
				strReplace(note, "\\n", "\n");
				effectData.notes += note + '\n';
				flags = 4;
			} else if (paramType == "text") {
				t = ParamType::Text;
				flags = (maxValue == "1") ? 1 : 0; // text(1,...): multi-line (the parser stores the flag in maxValue)
			} else if (paramType == "separator") {
				t = ParamType::Separator;
			} else if (paramType == "input") {
				t = ParamType::Layer;
			} else {
				// a type this plug-in doesn't know (e.g. from a newer G'MIC, or a malformed line in
				// ~/.config/gmic; the parser warns): a hidden float, which passes 0 in its slot, so
				// that the later arguments keep their positions
				t = ParamType::Float;
				h = true;
			}
		}
		// a value(...) parameter is a hidden constant argument: its value is the default, which
		// the parser stores in defaultValue (it used to be passed as an empty string)
		const std::string& paramText = (paramType == "value") ? defaultValue : text;
		globalDataP->param[p] = Parameter(
		  name, "", t, (float)stringToDouble(minValue), (float)stringToDouble(maxValue), d1, d2, d3, d4, paramText);
		globalDataP->param[p].flags = flags;
		if (h) globalDataP->param[p].displayStatus = DisplayStatus::Hidden;
		if (paramType == "pointenabled") {
			globalDataP->param[p].hint = (param.removable < 0) ?
				"Use the point above. Off by default, since G'MIC-Qt starts with this point removed. "
				"When off, the filter gets nan,nan for the point, as for a point removed in G'MIC-Qt." :
				"Use the point above. When off, the filter gets nan,nan for the point, as for a point removed in G'MIC-Qt.";
		}
		p++;
	}

//	globalDataP->param[p++] = Parameter("Dummy", ParamType::Float);

	if (effectData.multiLayer) {
		globalDataP->param[p] = Parameter("Layer -1", "", ParamType::Layer);
		++p;
		globalDataP->param[p] = Parameter("Layer -2", "", ParamType::Layer);
		++p;
		globalDataP->param[p] = Parameter("Layer -3", "", ParamType::Layer);
		++p;
		globalDataP->param[p] = Parameter("Layer -4", "", ParamType::Layer);
		++p;
		// Layer times (issue #63). Placed before Command, so that the PARAM_* indices of the
		// trailing parameters don't change. The defaults (no layer from Source, offsets 0) give the
		// behaviour of the plug-ins without these parameters, also for the saved projects, which
		// don't store them. Not animated: the frames needed are given to the host for a render time.
		// Integer parameters with flags 1 are not animated (see describeInContext).
		globalDataP->param[p] = Parameter("Layer Times", "", ParamType::TopicStart);
		globalDataP->param[p].flags = isTemporalFilter(effectData) ? 0 : 1; // closed, except for the filters that use frames
		++p;
		globalDataP->param[p] = Parameter(kLayersFromSourceParam, "", ParamType::Int, 0, 4, 0, 0, 0, 0, "");
		globalDataP->param[p].flags = 1;
		globalDataP->param[p].hint = "Number of layer inputs taken from Source instead of their own input: "
			"Layer -1 to Layer -N are then Source at the render time plus their time offset, e.g. the "
			"previous frames with offsets -1, -2... (set when N is raised, for offsets still at 0). 0: off.";
		++p;
		for (int k = 1; k <= 4; ++k) {
			globalDataP->param[p] = Parameter("Layer -" + std::to_string(k) + kTimeOffsetParamSuffix, "", ParamType::Int, -1000, 1000, 0, 0, 0, 0, "");
			globalDataP->param[p].flags = 1;
			globalDataP->param[p].hint = "Frame offset of Layer -" + std::to_string(k) + ": its image is fetched at "
				"the render time plus this offset (-1: the previous frame). Source is always at the render time.";
			++p;
		}
		globalDataP->param[p] = Parameter("Layer Times", "", ParamType::TopicEnd);
		++p;
	}
	globalDataP->param[p] = Parameter("Command", "", ParamType::Text, 0, 0, 0, 0, 0, 0, "-blur 2");
	globalDataP->param[p].displayStatus = DisplayStatus::Hidden;
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", ParamType::TopicStart);
	globalDataP->param[p].flags = 1;
	++p;
	// "Frame" is added at the end (issue #65): the choice is saved as its index (and label in
	// Natron), so the other options keep their meaning in saved projects
	globalDataP->param[p] = Parameter(kOutputLayerParam, "", ParamType::Select, 0, kOutputLayerFrame, 1, 0, 0, 0, "Merged|Layer 0|Layer -1|Layer -2|Layer -3|Layer -4|Layer -5|Layer -6|Layer -7|Layer -8|Layer -9|Frame");
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
	globalDataP->param[p] = Parameter("Resize Mode", "", ParamType::Select, 0, 5, 1, 0, 0, 0, "Fixed (Inplace)|Dynamic|Downsample 1/2|Downsample 1/4|Downsample 1/8|Downsample 1/16");
	++p;
	globalDataP->param[p] = Parameter("Ignore Alpha", "", ParamType::Bool, 0, 1, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Run the filter on the RGB channels only, then give the inputs' alpha back to the results. "
		"Gray or RGB results of another size than the input (frames, rotation, crop, resize...) are opaque.";
	++p;
	globalDataP->param[p] = Parameter("Preview/Draft Mode", "", ParamType::Bool, 0, 1, 0, 0, 0, 0, "");
	if (effectData.command == effectData.preview_command) {
		globalDataP->param[p].displayStatus = DisplayStatus::Hidden;
	}
	++p;
	// Reproducibility (issue #44): -srand seeds G'MIC's process-global random generator, which
	// concurrent G'MIC runs share (other instances or frames: the effect is instance-safe; runs are
	// not serialised, by design). Within a run, patches/cimg-omp-rand.sh makes the random numbers
	// of rand, noise and the math parser in OpenMP loops independent of thread scheduling (they
	// depend on the number of threads); patchmatch and G'MIC's parallel command still vary.
	globalDataP->param[p] = Parameter("Global Random Seed", "", ParamType::Int, 0, 1<<24, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Seed of the random numbers used by the filter. For most filters, results are reproducible "
		"for a given seed and number of OpenMP threads; filters using patch matching or G'MIC's parallel command, "
		"and concurrent renders, can still vary. The number of threads depends on the host and on the renders "
		"running at once: set the OMP_NUM_THREADS environment variable for a fixed one.";
	++p;
	globalDataP->param[p] = Parameter(kAnimateSeedParam, "", ParamType::Bool, 0, 1, 0, 0, 0, 0, "");
	globalDataP->param[p].hint = "Add the frame number to the Global Random Seed, so that the random numbers change "
		"from frame to frame (reproducibility: see Global Random Seed).";
	++p;
	globalDataP->param[p] = Parameter("Log Verbosity", "", ParamType::Select, 0, 4, 0, 0, 0, 0, "Off|Level 1|Level 2|Level 3|");
	++p;
	globalDataP->param[p] = Parameter("Advanced Options", "", ParamType::TopicEnd);
	++p;
	globalDataP->nofParams = p;

	globalDataP->pluginInfo.description = pluginDescription(effectData);
	globalDataP->nofInputs = (int)std::count_if(globalDataP->param, globalDataP->param + p,
		[](const Parameter& param) { return param.paramType == ParamType::Layer; });
	return globalData;
}

static
const EffectData&
effectDataOf(const GlobalData* globalDataP)
{
    return pluginGlobalData().pluginData[globalDataP->pluginIndex];
}

// Is filter parameter i passed to G'MIC (one argument, of one or more values)? Not the notes and
// links, separators, and the checkboxes of removable points (see kParamFlagsPointEnabled): the
// parameters that G'MIC-Qt passes (AbstractParameter::isActualParameter()).
static bool isGmicArgument(const GlobalData* globalDataP, int i)
{
	switch (globalDataP->param[i].paramType) {
	case ParamType::Int:
	case ParamType::Select:
	case ParamType::Float:
	case ParamType::Color:
	case ParamType::Point:
		return true;
	case ParamType::Bool:
		return globalDataP->param[i].flags != kParamFlagsPointEnabled;
	case ParamType::Text:
		return globalDataP->param[i].flags != 4; // note or link
	default:
		return false;
	}
}

// A decimal number of a status value, as G'MIC-Qt reads it (QString::toFloat(), toInt()), '.' as
// decimal point whatever the locale, blanks around it allowed. Only this syntax reaches the
// conversion: no "inf" or "nan" (without isnan/isinf, folded with -ffinite-math-only), no
// hexadecimal, no overflow. integer: digits only, with an optional sign, within int.
static bool statusNumber(const string& text, bool integer, double& v)
{
	const size_t b = text.find_first_not_of(" \t\r\n"), e = text.find_last_not_of(" \t\r\n");
	if (b == string::npos) return false;
	const string s = text.substr(b, e - b + 1);
	size_t i = 0, digits = 0;
	if (s[i] == '+' || s[i] == '-') ++i;
	while (i < s.size() && isdigit((unsigned char)s[i])) ++i, ++digits;
	if (!integer) {
		if (i < s.size() && s[i] == '.') ++i;
		while (i < s.size() && isdigit((unsigned char)s[i])) ++i, ++digits;
	}
	if (digits == 0) return false;
	if (!integer && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
		++i;
		if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
		const size_t first = i;
		while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
		if (i == first) return false;
	}
	if (i != s.size()) return false;
	errno = 0;
	v = stringToDouble(s); // locale-independent strtod
	if (errno == ERANGE && std::fabs(v) > 1.) return false; // overflow (an underflow gives 0)
	if (integer) {
		return v >= (double)std::numeric_limits<int>::min() && v <= (double)std::numeric_limits<int>::max();
	}
	return std::fabs(v) <= (double)std::numeric_limits<float>::max();
}

// the fields of a status value between its commas, as QString::split(",") gives them (empty ones
// included, not trimmed)
static vector<string> statusFields(const string& value)
{
	vector<string> fields(1);
	for (const char c : value) {
		if (c == ',') fields.emplace_back();
		else fields.back() += c;
	}
	return fields;
}

// The G'MIC argument of text, file, folder and value() parameter i (flags: see pluginSetup), quoted
static string quotedArgument(const GlobalData* globalDataP, int i, string s)
{
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
	return "\"" + s + "\"";
}

// The G'MIC argument of filter parameter i (isGmicArgument), from its value at the render time.
// value: if not null, the value that the preview's status gives it (see gmicCommand), applied as
// G'MIC-Qt's setValue() of the parameter applies it (FilterParameters/*Parameter.cpp): a value
// that it can't read leaves the parameter's own; an int or float is clamped to the parameter's
// range, as by G'MIC-Qt's spin box; a choice outside its options, or a color that is not 3 or 4
// values, is ignored.
static string gmicArgument(const SequenceData* sequenceDataP, const GlobalData* globalDataP, int i, const string* value)
{
	const Parameter& par = globalDataP->param[i];
	double v = 0.;
	switch (par.paramType) {
	case ParamType::Int:
		if (value && statusNumber(*value, true, v)) {
			return std::to_string((int)std::min(std::max(v, (double)par.minValue), (double)par.maxValue));
		}
		return std::to_string((int)sequenceDataP->floatValue[i][0]);
	case ParamType::Select:
		if (value && statusNumber(*value, true, v) && v >= 0. && v <= (double)par.maxValue) {
			return std::to_string((int)v);
		}
		return std::to_string((int)sequenceDataP->floatValue[i][0]);
	case ParamType::Bool:
		// BoolParameter::setValue(): true only for "1" ("true", "2"... are false)
		if (value) return (*value == "1") ? "1" : "0";
		return std::to_string((int)sequenceDataP->floatValue[i][0]);
	case ParamType::Float:
		if (value && statusNumber(*value, false, v)) {
			return floatToString((float)std::min(std::max(v, (double)par.minValue), (double)par.maxValue));
		}
		return floatToString(sequenceDataP->floatValue[i][0]);
	case ParamType::Text:
		return quotedArgument(globalDataP, i, value ? *value : sequenceDataP->textValue[i]);
	case ParamType::Color: {
		const bool alpha = par.flags == kParamFlagsColorRGBA;
		const vector<string> c = value ? statusFields(*value) : vector<string>();
		if (value && (c.size() == 3 || c.size() == 4)) {
			// QColor(r, g, b[, a]): 0 for a value that is not an integer, then each in 0..255; an
			// alpha that is not given is opaque
			int rgba[4] = { 0, 0, 0, 255 };
			for (size_t k = 0; k < c.size() && k < (alpha ? 4u : 3u); ++k) {
				rgba[k] = statusNumber(c[k], true, v) ? (int)std::min(std::max(v, 0.), 255.) : 0;
			}
			string arg = std::to_string(rgba[0]) + "," + std::to_string(rgba[1]) + "," + std::to_string(rgba[2]);
			if (alpha) arg += "," + std::to_string(rgba[3]);
			return arg;
		}
		string arg = floatToString(kGmicValueScale * sequenceDataP->floatValue[i][0]) + "," +
			floatToString(kGmicValueScale * sequenceDataP->floatValue[i][1]) + "," +
			floatToString(kGmicValueScale * sequenceDataP->floatValue[i][2]);
		if (alpha) arg += "," + floatToString(kGmicValueScale * sequenceDataP->floatValue[i][3]);
		return arg;
	}
	case ParamType::Point: {
		// a removable point that is off: removed, as G'MIC-Qt passes it (issue #79)
		const bool removable = i + 1 < PARAM_COMMAND && globalDataP->param[i + 1].paramType == ParamType::Bool &&
			globalDataP->param[i + 1].flags == kParamFlagsPointEnabled;
		bool removed = removable && sequenceDataP->floatValue[i + 1][0] == 0;
		float xy[2] = { sequenceDataP->floatValue[i][0], sequenceDataP->floatValue[i][1] };
		const vector<string> c = value ? statusFields(*value) : vector<string>();
		if (value && c.size() == 2) {
			// each coordinate that is a number (not "nan") moves the point; "nan,nan" removes a
			// removable point, anything else puts it back
			bool nan[2];
			for (int k = 0; k < 2; ++k) {
				nan[k] = strLowercase(c[k]) == "nan";
				if (!nan[k] && statusNumber(c[k], false, v)) xy[k] = (float)v;
			}
			removed = removable && nan[0] && nan[1];
		}
		if (removed) return "nan,nan";
		return floatToString(xy[0]) + "," + floatToString(xy[1]);
	}
	default:
		return string();
	}
}

// the filter's command line, with its main or preview command. status: if not null, the values
// of the status of a run of the preview command before this one (see pluginProcess), which are
// given to the arguments as G'MIC-Qt's Apply gets them from its preview (MainWindow.cpp,
// FilterParametersWidget::setValues(): only if there is one value per argument). Visibilities
// are ignored.
static
std::string
gmicCommand(const SequenceData* sequenceDataP, const GlobalData* globalDataP, bool preview, const std::vector<std::string>* status = nullptr)
{
    const EffectData& effectData = effectDataOf(globalDataP);
    string cmd = preview ? effectData.preview_command : effectData.command;

    if (cmd == "") {
        cmd = sequenceDataP->textValue[PARAM_COMMAND];
        strReplace(cmd, "\r", " ");
        strReplace(cmd, "\n", " ");
    } else {
        cmd = "-" + strTrim(cmd, " \t\r\n") + " ";
        // the filter parameters only: they come first, the layers and their times follow
        const int nofFilterParams = std::min((int)effectData.param.size(), PARAM_COMMAND - 1);
        vector<int> args;
        for (int i = 1; i <= nofFilterParams; i++) {
            if (isGmicArgument(globalDataP, i)) args.push_back(i);
        }
        const bool fromStatus = status && !status->empty() && status->size() == args.size();
        if (status && !status->empty() && !fromStatus) {
            // (render_plugins.py fails a render whose log has "the preview's status has": keep it)
            gmicLog("G'MIC: %s: the preview's status has %u values, for %u parameters: ignored\n",
                    effectData.name.c_str(), (unsigned int)status->size(), (unsigned int)args.size());
        }
        for (size_t a = 0; a < args.size(); ++a) {
            cmd += gmicArgument(sequenceDataP, globalDataP, args[a], fromStatus ? &(*status)[a] : nullptr) + ",";
        }
        cmd = cmd.substr(0, cmd.size() - 1);
    }
    // (the newlines of text parameters are kept: see above)

    return cmd;
}

// Output Layer "Frame" (issue #65): at most that many results (a frame per result, e.g. Spline
// Spirograph makes up to 512 frames, Fade Layers up to 401); the other modes use at most
// kMaxLayers (Layer -9 is the last one that can be picked)
static const int kMaxFrameResults = 1024;
static size_t frameCacheBudget();

static bool outputIsFrame(const SequenceData* sequenceDataP, const GlobalData* globalDataP)
{
	return sequenceDataP->floatValue[PARAM_OUTPUT][0] == (float)kOutputLayerFrame;
}

static int maxResultsOf(const SequenceData* sequenceDataP, const GlobalData* globalDataP)
{
	return outputIsFrame(sequenceDataP, globalDataP) ? kMaxFrameResults : kMaxLayers;
}

// _openfx_gmic_frame_select frame,max_results,max_bytes: if there are more than max_results
// results, or they take more than max_bytes, keeps only result [frame mod N], named
// openfx_gmic_frame_selected (Output Layer "Frame": the results are then not kept, see
// pluginProcess)
static const char* const kFrameSelectCommand =
	"\n_openfx_gmic_frame_select :\n"
	"  b=0 repeat $! { b={$b+w#$>*h#$>*d#$>*s#$>*4} }\n"
	"  if $!>$2||$b>$3 k[{(($1%$!)+$!)%$!}] nm openfx_gmic_frame_selected fi\n";

// in the log line of a failed preview-first run (see pluginProcess), which
// .github/workflows/render_plugins.py (PREVIEW_FIRST_FAILED) looks for: keep them the same
static const char* const kPreviewFirstFailed = "run before the main command, failed";

// The G'MIC run that a command line is for
enum class GmicRun
{
	kMain, // a render with the main command
	kPreview, // a render with the preview command (Preview/Draft Mode, or a "_none_" main command)
	// the run of the preview command before the main command (see pluginProcess), for the
	// _persistent and the status that it leaves: its results are discarded
	kPreviewState,
};

// The whole command line: the filter's command (main or preview), with the variables G'MIC-Qt
// defines, the output layer selection, Ignore Alpha, the Resize Mode, verbosity and seed. Sets the
// custom commands of the options: customCommands (the filter file, the filter's #@gui lines, the
// hot fixes for G'MIC bugs, the Ignore Alpha helpers and _openfx_gmic_opaque_resized), which
// must outlive the G'MIC call, and are the same for every GmicRun. status: see gmicCommand.
// GmicRun::kPreviewState: the preview command sees the same images and variables as with
// kPreview, but nothing runs after it, so that the status is the filter's own, as in G'MIC-Qt,
// which runs nothing else (the commands that merge, keep, restore the alpha of or resize the
// results could change it: gui_merge_layers sets it).
static
std::string
fullCommand(const SequenceData* sequenceDataP, const GlobalData* globalDataP, GmicInstanceData* instanceDataP, unsigned int nofImages, GmicRun run, string& customCommands,
            const std::vector<std::string>* status = nullptr)
{
	// the filter file, if the filters come from one (a user file or G'MIC's update file; after the
	// built-in stdlib, which G'MIC loads first: its definitions replace the built-in ones, as in
	// G'MIC-Qt), the filter's #@gui lines, and the hot fixes for G'MIC bugs (gmic_hotfixes.gmic,
	// after the file: those that still fit its definitions, see gmicGetHotfixes), on every run
	const PluginGlobalData& data = pluginGlobalData();
	// (a newline after the user's file ends its last line, if the file doesn't, so that the next
	// definition starts on a line of its own; a '\' at the end of the file's last command line
	// still continues it into the next definition, as in G'MIC-Qt, since G'MIC skips the blank and
	// comment lines in between. The filter's #@gui lines are comments for G'MIC, which the file
	// already holds)
	static const string kNoLines;
	const string& filterLines = data.userCommands.empty() ? data.pluginContent[globalDataP->pluginIndex] : kNoLines;
	customCommands.clear();
	// (and the few helper commands added below)
	customCommands.reserve(data.userCommands.size() + filterLines.size() + data.hotfixes.size() + 4096);
	customCommands += data.userCommands;
	if (!data.userCommands.empty()) {
		customCommands += '\n';
	}
	customCommands += filterLines;
	customCommands += '\n';
	customCommands += data.hotfixes;
	customCommands += kOpaqueResizedCommand;
	const bool isPreview = run != GmicRun::kMain;
	string cmd = gmicCommand(sequenceDataP, globalDataP, isPreview, status);

	// Ignore Alpha: the alpha channels of all the images are split off and stored, the command runs
	// on all the RGB images at once (multi-input filters see every layer, extra results are kept),
	// then the results of the same size get the alpha back (see kIgnoreAlphaCommands: results of
	// another size are opaque). The command stays at the top level, as with Ignore Alpha off and in
	// G'MIC-Qt: inside a custom command, $1, $*, $#... in text parameters would be substituted, and
	// apply_channels processed the images one by one, keeping a single result for each.
	const bool render = run != GmicRun::kPreviewState;
	if (sequenceDataP->floatValue[PARAM_NOALPHA][0] > 0.f) {
		customCommands += kIgnoreAlphaCommands;
		cmd = "-_openfx_gmic_alpha_split " + cmd + (render ? " -_openfx_gmic_alpha_restore" : "");
	}

	if (render && sequenceDataP->floatValue[PARAM_OUTPUT][0] == 0.f) {
		// G'MIC >= 2.9 renamed gimp_merge_layers to gui_merge_layers ("Unknown command" otherwise)
		cmd += " -gui_merge_layers";
	}

	// images[] has maxResults() entries (gmic_plugin_call() fails with more results): keep at most
	// that many, the first ones (Output Layer "Layer -k" is image [k], see pluginProcess). Some
	// filters return more by default, e.g. Fade Layers, Morph Layers, Tiles to Layers.
	// Output Layer "Frame": bounded by _openfx_gmic_frame_select, at the end (see pluginProcess)
	if (outputIsFrame(sequenceDataP, globalDataP)) {
		customCommands += kFrameSelectCommand;
	} else if (render) {
		cmd += " -if {$!>" + std::to_string(kMaxLayers) + "} -keep[0-" + std::to_string(kMaxLayers - 1) + "] -fi";
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
	// _persistent is not in the command line: the filters that need it run their preview command
	// first, which sets it for the main command (see pluginProcess).
	int extentW = 0, extentH = 0;
	const float dsMode = sequenceDataP->floatValue[PARAM_RESIZE][0];
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
	const string W = std::to_string(extentW), H = std::to_string(extentH);
	string env = "_preview_x0=0 _preview_y0=0 _preview_x1=" + std::to_string(extentW - 1) + " _preview_y1=" + std::to_string(extentH - 1) + " ";
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
		float ds = sequenceDataP->floatValue[PARAM_RESIZE][0];
		string processedSize = std::to_string(sequenceDataP->inWorld[0].width) + "," + std::to_string(sequenceDataP->inWorld[0].height);
		if (ds >= 2.f) {
			ds = pow(2.f, ds - 1.f);
			const string dsSize = std::to_string(sequenceDataP->inWorld[0].width / (int)ds) + "," + std::to_string(sequenceDataP->inWorld[0].height / (int)ds);
			cmd = " -resize " + dsSize + " " + cmd;
			processedSize = dsSize;
		}
		if (render) {
			cmd += " -_openfx_gmic_opaque_resized " + processedSize;
			cmd += " -resize " + std::to_string(sequenceDataP->inWorld[0].width) + "," + std::to_string(sequenceDataP->inWorld[0].height);
		}
	}
	int verbosity = (int)sequenceDataP->floatValue[PARAM_VERBOSITY][0] - 1;
	int seed = (int)sequenceDataP->floatValue[PARAM_SRAND][0];
	bool animated_seed = (int)sequenceDataP->floatValue[PARAM_ANIMSEED][0];
	if (animated_seed) {
		seed += (int)sequenceDataP->time;
	}

	cmd = "-v " + std::to_string(verbosity) + " -srand " + std::to_string(seed) + " " + cmd;
	// cmd += " -display";
	// set some variables that are defined globally for the GIMP plugin
	// not really needed as the effects should work independently of GIMP
//	cmd = "_input_mode=1 _output_mode=0 _verbosity_mode=0 _preview_mode=0 _preview_size=0 " + cmd;

	return cmd;
}

// G'MIC's thread count for one render (issue #188), so that the renders running at once don't each
// start one OpenMP thread per CPU: N = max(the host's count, fair share), at most the number of
// hardware threads.
// - The host's count: its multithread suite (multiThreadNumCPUs; Natron: the threads that its
//   other renders leave free, or 1 when its multithreading is off).
// - The fair share: the hardware threads divided by the G'MIC renders running in this process
//   (RunningRender, this one included). It is a floor because the host's count can be too low:
//   the plug-in is instance-safe, so Natron runs one G'MIC render per node at a time
//   (EffectInstance::renderRoI's lock on the node's render instances), and its other frame
//   threads, waiting on that lock, count as busy: during sequence renders and playback, its
//   count is often 1.
// N is given to G'MIC by setting its variable _cpus at the start of the command: G'MIC
// (gmic::set_variable()) then calls omp_set_num_threads(N) in the interpreter's thread, the
// worker thread of gmicCallAbortable(), and the stdlib commands that split their work use
// $_cpus. With libgomp and libomp (OpenMP >= 3.0: the thread count is a per-thread setting),
// only this render's worker thread is affected (a new thread for each run; MSVC's vcomp doesn't
// document the scope, and the fallback without a worker thread sets the host's render thread).
// Limit: G'MIC's 'parallel' command (also used by the stdlib's apply_parallel commands) creates
// its interpreters with new gmic(*this), whose initialisation sets _cpus back to all the CPUs and
// calls omp_set_num_threads() on the interpreter's thread: after a filter's first 'parallel', the
// render uses all the CPUs again, as before this change.
// No prefix, so that G'MIC uses all the CPUs as before:
// - if the number of hardware threads is unknown (hardware_concurrency() is 0);
// - if OMP_NUM_THREADS is set: it stays a fixed thread count (G'MIC then ignores _cpus for
//   OpenMP, and $_cpus stays G'MIC's CPU count);
// - if the host's multithread suite call fails. Support's getNumCPUs() returns 1 then, which
//   can't be told apart from a host's 1, so the suite is called directly. (Support fetches the
//   suite as mandatory when the plug-in loads: it is never missing in practice.)
// Results with random numbers depend on the thread count (see the Global Random Seed hint), which
// may change from one render to the next: it is not part of the Output Layer "Frame" cache key.
static std::atomic<int> gRunningRenders(0); // the G'MIC renders running in this process

// Counts a G'MIC render in gRunningRenders for its lifetime.
class RunningRender
{
public:
	RunningRender() noexcept { gRunningRenders.fetch_add(1); }
	~RunningRender() { gRunningRenders.fetch_sub(1); }
	RunningRender(const RunningRender&) = delete;
	RunningRender& operator=(const RunningRender&) = delete;
	RunningRender(RunningRender&&) = delete;
	RunningRender& operator=(RunningRender&&) = delete;
};

// Call with a RunningRender for this render alive (see above).
static string gmicThreadsPrefix()
{
	static const bool ompEnv = std::getenv("OMP_NUM_THREADS") != NULL; // read once
	if (ompEnv) return string();
	static const OfxMultiThreadSuiteV1* const suite =
		static_cast<const OfxMultiThreadSuiteV1*>(OFX::fetchSuite(kOfxMultiThreadSuite, 1, true));
	unsigned int hostCount = 0;
	if (!suite || !suite->multiThreadNumCPUs || suite->multiThreadNumCPUs(&hostCount) != kOfxStatOK) {
		return string();
	}
	const unsigned int concurrency = std::thread::hardware_concurrency();
	if (concurrency == 0) return string(); // unknown: no fair share, and no upper bound
	// at most INT_MAX: G'MIC reads _cpus with %d
	const unsigned int hardware = std::min(concurrency, (unsigned int)std::numeric_limits<int>::max());
	const unsigned int renders = (unsigned int)std::max(1, gRunningRenders.load());
	const unsigned int fairShare = std::max(1u, hardware / renders);
	const unsigned int n = std::min(std::max(hostCount, fairShare), hardware);
	return "_cpus=" + std::to_string(n) + " ";
}

// Render abort (issue #46): G'MIC polls a flag (p_is_abort), in its interpreter and in CImg's
// OpenMP loops. The host's abort() must be called from the render thread (Natron reads
// thread-local render state), and G'MIC only returns when it is done. So run G'MIC in a worker
// thread, and poll abort() from the render thread while it runs: when the host aborts the render,
// set G'MIC's flag, and G'MIC stops at its next check.
struct GmicCallData
{
	GmicCall call;
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
		result = gmic_plugin_call(d->call);
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

// Render progress (issue #177): G'MIC's progress index (p_progress) is set by the filters that
// use the 'progress' command, from 0 to 100; it is -1 (unknown) when G'MIC starts, and most
// filters never set it. The render thread passes it to the host's progress suite from its abort
// poll (see gmicCallAbortable): the host's progress calls, like abort(), come from the render
// thread. The host's progress starts at the first known value, so that filters without progress
// show none, and ends when the render's G'MIC runs are done (one HostProgress for them: a filter
// that runs its preview command first shows it in the first half), also on an exception. The
// progress is cosmetic: if a progress call throws, the render goes on without progress.
// The result of progressUpdate() (false: the user cancelled) is ignored, only abort() stops a
// render: Natron's progress panel returns false to a progressUpdate() from a render thread until
// its main thread has created the task that progressStart() asked for, which would abort the
// render at its first update.
class HostProgress
{
public:
	HostProgress(OFX::ImageEffect& effect, const string& message) : _effect(effect), _message(message) {}
	~HostProgress() noexcept
	{
		if (!_started) return;
		try {
			_effect.progressEnd();
		} catch (...) {
		}
	}
	HostProgress(const HostProgress&) = delete;
	HostProgress& operator=(const HostProgress&) = delete;
	HostProgress(HostProgress&&) = delete;
	HostProgress& operator=(HostProgress&&) = delete;

	// The part of the host's progress that the next G'MIC runs take: G'MIC's 0 to 100 are then
	// start to start + size (0 to 1 by default, for a render that runs G'MIC once)
	void setRange(double start, double size) noexcept
	{
		_start = start;
		_size = size;
	}

	// gmicProgress: G'MIC's progress index, from 0 to 100, or negative (unknown)
	void update(float gmicProgress) noexcept
	{
		if (_failed || !(gmicProgress >= 0.f)) return; // unknown (-1), or NaN
		const double t = _start + _size * (std::min(gmicProgress, 100.f) / 100.);
		try {
			if (!_started) {
				_effect.progressStart(_message);
				_started = true;
			}
			if (t != _last) {
				(void)_effect.progressUpdate(t);
				_last = t;
			}
		} catch (...) {
			_failed = true; // no more progress for this render (progressEnd() is still called if started)
		}
	}

private:
	OFX::ImageEffect& _effect;
	string _message;
	bool _started = false;
	bool _failed = false;
	double _last = -1.; // the last value given to the host
	double _start = 0.;
	double _size = 1.;
};

// gmic_plugin_call(), aborted when the host aborts the render: then *aborted is set to true, and
// the result must be discarded. G'MIC's progress goes to the host's progress bar, through
// hostProgress (one for all the G'MIC runs of a render).
static int gmicCallAbortable(OFX::ImageEffect& effect, HostProgress& hostProgress, const GmicCall& call, bool* aborted)
{
	gmic_interface_options* const options = call.options;
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
	// G'MIC's progress index: a plain float, written by the main interpreter (its 'progress'
	// command), on the worker thread, while the render thread reads it: the same upstream design
	// as the abort flag (issue #136; gmic-qt's ProgressInfoWidget reads its FilterThread's float
	// the same way). A stale value only delays the progress bar. The interpreters of 'parallel'
	// are copies with their own private progress (gmic::gmic(const gmic&)): their progress is
	// never seen.
	float progress = -1.f;
	GmicCallData d;
	d.call = call;
	d.result = -1;
	d.done = false;
	options->p_is_abort = &isAbort;
	options->p_progress = &progress;

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
		// no thread: run G'MIC here, without abort or progress
		options->p_is_abort = NULL;
		options->p_progress = NULL;
		return gmic_plugin_call(call);
	}

	// the worker uses d, isAbort, progress and the images: always join it before returning
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
			hostProgress.update(progress);
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
		options->p_progress = NULL;
		throw;
	}
#ifndef _WIN32
	pthread_join(thread, NULL);
#else
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
#endif
	options->p_is_abort = NULL;
	options->p_progress = NULL;
	if (!*aborted && effect.abort()) {
		*aborted = true;
	}
	if (*aborted) {
		return -1;
	}
	hostProgress.update(progress); // G'MIC's final value (the worker is joined: no race)
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
			gmicLog("openfx-gmic: OFX_GMIC_FRAME_CACHE_MB=\"%s\" is not a number of megabytes >= 0: using the default budget\n", env);
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
	// the warning of the run that made the frames (a failed preview-first run), shown again for
	// the frames taken from the cache; empty for none
	std::string warning;
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

// result [frame mod N] of the entry of key (frame: any integer), or NULL if there is none (then
// with the entry's warning in *warning); the instance now refers to key. If another render is running G'MIC for key, waits for it first,
// polling the host's abort (then sets *aborted). On NULL without abort, the caller runs G'MIC; if
// *claimed is set, other renders wait for it: it must call frameCacheRunDone(key) whatever happens.
static std::shared_ptr<const CachedFrame> frameCacheFind(const void* owner, const std::string& key, long long frame,
	const OFX::ImageEffect& effect, bool* claimed, bool* aborted, std::string* warning)
{
	std::vector<CachedFrames> dropped; // freed after unlocking
	FrameCache& c = frameCache();
	std::unique_lock<std::mutex> lock(c.mutex);
	frameCacheSetOwnerKey(c, owner, key, dropped);
	for (bool waited = false; ; waited = true) {
		std::map<std::string, FrameCacheEntry>::iterator it = c.entries.find(key);
		if (it != c.entries.end() && !it->second.frames.empty()) {
			it->second.lastUse = ++c.clock;
			*warning = it->second.warning;
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

// the warning when the results of filterName are too large to keep (see frameCacheStore)
static std::string frameCacheTooLargeWarning(const std::string& filterName)
{
	return "G'MIC: " + filterName + ": the results are too large to keep (more than " + std::to_string(kMaxFrameResults) +
		", or more than " + std::to_string((frameCacheBudget() + 512 * 1024) / (1024 * 1024)) +
		" MB): Output Layer Frame runs the filter on every frame";
}

// the results for key can't be kept: warning is logged (once per key) and returned
static std::string frameCacheNotKept(const std::string& key, const std::string& warning)
{
	if (frameCacheUncacheable(key)) gmicLog("%s\n", warning.c_str());
	return warning;
}

// keeps the results images[0..nofImages-1] under key, for the instance, with the run's warning
// (FrameCacheEntry::warning): their buffers now belong to the cache, and their data pointers are
// set to NULL. Results that are not usable or don't fit
// in the budget are left to the caller. Returns a warning for the user if they are not kept (a
// result is empty, or they are larger than the budget), else an empty string.
static std::string frameCacheStore(const void* owner, const std::string& key, gmic_interface_image* images, unsigned int nofImages, const std::string& filterName,
                                   const std::string& runWarning)
{
	size_t bytes = 0;
	for (unsigned int i = 0; i < nofImages; ++i) {
		if (!images[i].data || images[i].spectrum == 0) {
			return frameCacheNotKept(key, "G'MIC: " + filterName + ": result " + std::to_string(i) +
				" is empty, the results are not kept (Output Layer Frame runs the filter on every frame)");
		}
		bytes += imageBytes(images[i]);
	}
	if (nofImages == 0) return std::string();
	if (bytes > frameCacheBudget()) return frameCacheNotKept(key, frameCacheTooLargeWarning(filterName));
	FrameCacheEntry entry;
	entry.bytes = bytes;
	entry.lastUse = 0;
	entry.warning = runWarning; // (before the results are moved: may throw)
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
	stored.warning.swap(entry.warning);
	c.bytes += bytes;
	return std::string();
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
	std::string key = std::to_string(globalDataP->pluginIndex) + "\n" + cmd;
	for (unsigned int i = 0; i < nofImages; ++i) {
		const World& w = sequenceDataP->inWorld[i];
		key += "\n" + std::to_string(w.width) + "x" + std::to_string(w.height) + ":";
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
		strncpy(images[i].name, string("input" + std::to_string(i)).c_str(), sizeof(images[i].name));
	}

	const EffectData& effectData = effectDataOf(globalDataP);
	const bool hasContent = !effectData.command.empty();
	const bool preview = hasContent && (effectData.previewOnly || sequenceDataP->floatValue[PARAM_PREVIEW][0] > 0);
	// G'MIC-Qt's Apply follows a run of the preview command, whose _persistent and status the main
	// command gets (FilterThread.cpp, MainWindow.cpp in c-koi/gmic-qt): the filters whose main
	// command may need them (EffectData::previewFirst) run their preview command first, on the
	// same inputs and with the same variables as with Preview/Draft Mode, its results discarded
	// (GmicRun::kPreviewState: nothing runs after it); then the main command, with the
	// _persistent that the preview left, and the arguments that its status gives (gmicCommand).
	// _persistent starts empty on every render, so that a render doesn't depend on the previous
	// ones (G'MIC-Qt keeps it between its runs of a filter). If the preview command fails, the main
	// command runs without them, as G'MIC-Qt's Apply after a failed preview.
	const bool previewFirst = !preview && effectData.previewFirst;
	// the custom commands, set in the options by fullCommand(): must stay valid until the G'MIC calls return
	string customCommands;
	// customCommands goes out of scope: reset the pointer on return, also when G'MIC throws
	struct ResetCustomCommands {
		gmic_interface_options& options;
		explicit ResetCustomCommands(gmic_interface_options& o) : options(o) {}
		~ResetCustomCommands() { options.custom_commands = NULL; }
	} resetCustomCommands(instanceDataP->options());
	string cmd = fullCommand(sequenceDataP, globalDataP, instanceDataP, nofImages, preview ? GmicRun::kPreview : GmicRun::kMain, customCommands);
	bool aborted = false;
	int result = 0;
	string errmsg;
	string warning; // shown on the node after a successful render (showRenderWarning)
	// the preview command run first failed (shown with warning; kept with Output Layer "Frame"
	// results, and shown again for the frames taken from them)
	string previewWarning;
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
		cachedFrame = frameCacheFind(instanceDataP, cacheKey, frame, effect, &claim.claimed, &aborted, &previewWarning);
	} else {
		frameCacheRelease(instanceDataP);
	}
	const bool runGmic = !cachedFrame && !aborted;
	// Frame: at most the results that can be kept (see kFrameSelectCommand), after the key
	const string frameSelect = frameMode ? " -_openfx_gmic_frame_select " + std::to_string(frame) + "," +
		std::to_string(kMaxFrameResults) + "," + std::to_string((unsigned long long)frameCacheBudget()) : string();
	if (runGmic) {
		// counted until the G'MIC runs (the preview command first, if any, and the main command)
		// are done, on every path; the thread count is added after the cache key was made (see
		// gmicThreadsPrefix), and is the same for both runs
		const RunningRender running;
		const string threads = gmicThreadsPrefix();
		// one progress for the render: the preview run takes the first half of it
		HostProgress hostProgress(effect, effectData.name);
		GmicRunState state; // what the preview run left (nothing if it failed)
		string previewError; // the preview run's error, if it failed
		bool previewDone = false;
		if (previewFirst) {
			const string previewCmd = threads + fullCommand(sequenceDataP, globalDataP, instanceDataP, nofImages, GmicRun::kPreviewState, customCommands);
			hostProgress.setRange(0., 0.5);
			// (the preview's results are discarded, and the inputs stay unchanged: images[] too;
			// _persistent starts empty: no persistentIn)
			GmicCall call;
			call.cmd = previewCmd.c_str();
			call.nofImages = &nofImages;
			call.images = images;
			call.maxImages = maxImages;
			call.options = &instanceDataP->options();
			call.stateOut = &state;
			call.results = GmicResults::kDiscard;
			result = gmicCallAbortable(effect, hostProgress, call, &aborted);
			previewDone = result == 0;
			if (!previewDone && !aborted) {
				previewError = instanceDataP->options().error_message_buffer;
				// (render_plugins.py fails a render whose log has kPreviewFirstFailed)
				gmicLog("G'MIC: %s: the preview command, %s (%s): the main command runs without its state\n",
						effectData.name.c_str(), kPreviewFirstFailed, previewError.c_str());
			}
			hostProgress.setRange(0.5, 0.5);
		}
		if (!aborted) {
			// the main command, with the preview's status (the cache key is made without it: the
			// status only depends on what the key holds)
			if (previewFirst) {
				cmd = fullCommand(sequenceDataP, globalDataP, instanceDataP, nofImages, GmicRun::kMain, customCommands,
				                  previewDone ? &state.status : nullptr);
			}
			cmd = threads + cmd + frameSelect;
			GmicCall call;
			call.cmd = cmd.c_str();
			call.nofImages = &nofImages;
			call.images = images;
			call.maxImages = maxImages;
			call.options = &instanceDataP->options();
			call.persistentIn = previewDone ? &state.persistent : nullptr;
			result = gmicCallAbortable(effect, hostProgress, call, &aborted);
			errmsg = string(instanceDataP->options().error_message_buffer);
			if (previewFirst && !previewDone && !aborted) {
				if (result != 0) {
					errmsg += " (the preview command, run first, failed too: " + previewError + ")";
				} else {
					previewWarning = "The preview command, which G'MIC-Qt runs before the main command, failed (" +
						previewError + "): the main command ran without the state it may need from it.";
				}
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
			int idx = (int)sequenceDataP->floatValue[PARAM_OUTPUT][0] - 1;
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
				warning = frameCacheStore(instanceDataP, cacheKey, images, nofImages, effectData.name, previewWarning);
			} catch (const std::bad_alloc&) {
			}
		} else if (result == 0 && useCache && selected) {
			warning = frameCacheNotKept(cacheKey, frameCacheTooLargeWarning(effectData.name));
		}
		if (claim.claimed) { claim.claimed = false; frameCacheRunDone(cacheKey); } // the waiting renders go on
	}
	if (!previewWarning.empty()) {
		warning = warning.empty() ? previewWarning : previewWarning + "\n" + warning;
	}
	if (result != 0 && !aborted) {
		// persistent message on the node, instead of a modal message on every render
		showRenderError(effect, errmsg, "G'MIC Error");
		ok = false;
	}
	else if (!aborted && !warning.empty()) {
		showRenderWarning(effect, warning);
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
