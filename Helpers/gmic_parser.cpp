/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:t; tab-width:2; c-basic-offset: 2 -*- */
/*
 #
 #  File        : gmic_parser.h
 #
 #  Description : A self-contained header file with helper functions to
 #                parse the G'MIC standard library file into a param structure
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


#include "gmic_parser.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <iostream>
#include <fstream>
#include <sstream>

#include "gmic_string.h"
#include "gmic_log.h"

using namespace std;

namespace gmic_ofx {

static const char* const src_prefix_old_c = "#@gimp";
static const char* const src_prefix_c = "#@gui";
static const char* const dst_prefix_c = "#@gmic_plugin";


static
string getUniqueId(const string& name)
{
	string uniqueid = "";
	for (unsigned int i = 0; i < name.size(); i++) {
		if ((name[i] >= 'a' && name[i] <= 'z')
			|| (name[i] >= 'A' && name[i] <= 'Z')
			|| (name[i] >= '0' && name[i] <= '9'))
			uniqueid += name[i];
	}
    // The recommended format is the reverse domain name format of the developer, for example "uk.co.thefoundry", followed by the developer's unique name for the plug-in. e.g. "uk.co.thefoundry.F_Kronos".
	return "eu.gmic." + uniqueid;
}

static
void processCommand(const string& s, EffectData& cd)
{
	const string dst_prefix = dst_prefix_c;
	string r = s;
	if (r == dst_prefix + " :") return;
	r = strRemoveXmlTags(r, false);
	int sPos = (int)r.find(":");
	if (sPos < 0) return;
	string r1 = r.substr(0, sPos); // G'MIC >= 3 writes "Name:cmd,preview" without spaces
	int sPos2 = (int)r1.find(dst_prefix);
	if (sPos2 >= 0) cd.name = strTrim(r1.substr(sPos2 + dst_prefix.size()), " \n");

	string r2 = r.substr(sPos + 1);
	// optional input mode after the preview command ("#@gui Name : cmd, preview : *"): not part of
	// either command (it is used by usesSeveralLayers())
	sPos = (int)r2.find(":");
	if (sPos >= 0) r2 = r2.substr(0, sPos);
	string r3 = r2;
	sPos = (int)r2.find(",");
	const bool previewGiven = sPos >= 0; // (else the preview command is the main command)
	if (sPos >= 0) {
		r3 = r2.substr(sPos + 1);
		r2 = r2.substr(0, sPos);
	}
	// remove the preview options after the preview command: "(factor)", the preview's zoom factor
	// (a number, e.g. "(0)", "(1)" or "(1.5)"), followed by "+" (accurate on zoom) and/or "*"
	// (G'MIC >= 3, accurate on parameter change). As G'MIC-Qt (FiltersModelReader.cpp), the
	// preview command is what comes before the first '(', whatever the factor.
	r3 = r3.substr(0, r3.find('('));

	// As G'MIC-Qt (FiltersPresenter::Filter::isNoApplyFilter, isNoPreviewFilter): a command that is
	// empty or exactly "_none_" is no command. No main command: a preview-only filter, rendered with
	// its preview command (the preview's output is the result). Neither: a page with no command (e.g.
	// the About pages), which passes Source through (EffectData::command empty).
	string command = strTrim(r2, " \t\r\n");
	string preview = strTrim(r3, " \t\r\n");
	const auto isNone = [](const string& c) { return c.empty() || c == "_none_"; };
	if (isNone(command)) {
		command = isNone(preview) ? string() : preview;
		cd.previewOnly = !command.empty();
	}
	// "_none_" or empty after the comma: G'MIC-Qt shows no preview (isNoPreviewFilter), and its
	// Apply follows no preview run (noPreview). Without a comma, the main command is the preview
	// command, which G'MIC-Qt does run first.
	if (isNone(preview)) {
		cd.noPreview = previewGiven && !command.empty();
		preview = command;
	}
	cd.command = command;
	cd.preview_command = preview;
}

// a value() default that is not a list of numbers ("0", "-1", "50,50", "0:0:50", "1e-3", "nan")
static bool isPlaceholder(const string& v)
{
	size_t b = 0;
	while (b <= v.size()) {
		size_t e = v.find_first_of(",:;", b);
		if (e == string::npos) e = v.size();
		const string piece = strTrim(v.substr(b, e - b), " \t");
		if (!piece.empty()) {
			char* end = NULL;
			strtod(piece.c_str(), &end);
			if (end == piece.c_str() || *end != '\0') return true;
		}
		b = e + 1;
	}
	return false;
}

// G'MIC-Qt's unquoted(): the text between enclosing double quotes (surrounding blanks allowed),
// else the trimmed text
static string textUnquoted(const string& s)
{
	const string t = strTrim(s, " \t\r\n\v\f");
	if (t.size() >= 2 && t[0] == '"' && t[t.size() - 1] == '"') return t.substr(1, t.size() - 2);
	return t;
}

static void appendUtf8(string& d, unsigned int c)
{
	if (c <= 0x7f) {
		d += (char)c;
	} else if (c <= 0x7ff) {
		d += (char)((c >> 6) | 0xc0);
		d += (char)((c & 0x3f) | 0x80);
	} else if (c <= 0xffff) {
		d += (char)((c >> 12) | 0xe0);
		d += (char)(((c >> 6) & 0x3f) | 0x80);
		d += (char)((c & 0x3f) | 0x80);
	} else {
		d += (char)((c >> 18) | 0xf0);
		d += (char)(((c >> 12) & 0x3f) | 0x80);
		d += (char)(((c >> 6) & 0x3f) | 0x80);
		d += (char)((c & 0x3f) | 0x80);
	}
}

// G'MIC-Qt's unescaped(), i.e. cimg::strunescape: \n \t \\ \" etc., octal \ooo, hex \xHH,
// \uHHHH and \UHHHHHHHH (as UTF-8); any other "\c" gives "c"
static string textUnescaped(const string& s)
{
	string d;
	for (size_t i = 0; i < s.size(); ) {
		if (s[i] != '\\') {
			d += s[i++];
			continue;
		}
		if (++i >= s.size()) break;
		const char c = s[i];
		const char* const esc = "abefnrtv\\'\"?";
		const char* const val = "\a\b\x1b\f\n\r\t\v\\'\"?";
		const char* e = strchr(esc, c);
		if (e) {
			d += val[e - esc];
			++i;
		} else if (c >= '0' && c <= '7') {
			unsigned int v = 0;
			for (int n = 0; n < 3 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++n) v = (v << 3) | (unsigned int)(s[i++] - '0');
			d += (char)v;
		} else if (c == 'x') {
			unsigned int v = 0;
			int n = 0;
			++i;
			while (n < 2 && i < s.size() && isxdigit((unsigned char)s[i])) {
				const char h = (char)tolower((unsigned char)s[i++]);
				v = (v << 4) | (unsigned int)(h <= '9' ? h - '0' : h - 'a' + 10);
				++n;
			}
			if (n) d += (char)v;
			else if (i < s.size()) d += s[i++];
		} else if (c == 'u' || c == 'U') {
			const size_t n = (c == 'u') ? 4 : 8;
			bool ok = true;
			for (size_t k = 1; ok && k <= n; ++k) ok = i + k < s.size() && isxdigit((unsigned char)s[i + k]);
			if (ok) {
				appendUtf8(d, (unsigned int)strtoul(s.substr(i + 1, n).c_str(), NULL, 16));
				i += n + 1;
			} else {
				d += s[i++];
			}
		} else {
			d += s[i++];
		}
	}
	return d;
}

static
void processParam(const string& s, EffectParameter& cp)
{
	string r = s;
	cp.removable = 0;
	cp.minValue = "";
	cp.maxValue = "";
	cp.defaultValue = "";
	cp.text = "";
	int p2 = (int)r.find("=");
	if (p2 < 0) return;
	// skip a "#@gui :" style prefix if still present; a ':' ending the name itself
	// ("Opacity (%):=float(...)" in G'MIC 4) is not a prefix separator
	int start = 0;
	int p1 = -1;
	if (r.compare(0, 2, "#@") == 0) {
		p1 = (int)r.find(":");
		if (p1 >= 0 && p1 < p2) start = p1 + 1;
	}
	// G'MIC 2 writes "Name = type(...)", G'MIC >= 3 "Name=type(...)": don't assume a space before '='
	cp.name = strTrim(strTrim(r.substr(start, p2 - start)), " \t:");
	r = strTrim(r.substr(p2 + 1));
	p1 = (int)r.find_first_of("([{");
	if (p1 < 0) return;
	cp.paramType = strTrim(r.substr(0, p1));
	strReplace(cp.paramType, "_", ""); // "_type": no preview refresh
	strReplace(cp.paramType, "~", ""); // "~type" (G'MIC >= 3): not randomized
	cp.paramType = strLowercase(cp.paramType);
	// a value whose delimiter is never closed ("float(0,1"), which G'MIC-Qt rejects
	// (AbstractParameter::parseText, "cannot find closing"): gmic_parse_single() ended it with the
	// filter's #@gui lines, and it is read as if it were closed there (its last character is not
	// dropped as a delimiter), with a warning
	const char closing = (r[p1] == '(') ? ')' : (r[p1] == '[') ? ']' : '}';
	if (r[r.size() - 1] != closing) {
		gmicLog("openfx-gmic: Warning: %s parameter \"%s\" has no closing '%c': it ends with the filter\n",
			cp.paramType.c_str(), cp.name.c_str(), closing);
		r += closing;
	}
	string pval = strTrim(r.substr(p1 + 1));
	pval = pval.substr(0, pval.size() - 1);

	// For parameters description, see "Specific rules for the universal plug-in"
	// in the gmic_stdlib.gmic file in the G'MIC sources.
	if (cp.paramType == "bool") {
		// 'bool(default_value={ 0 | 1 | false | true })'
		// Add a boolean parameter (0 or 1) (as a checkbutton).
		// As G'MIC-Qt (BoolParameter::initFromText): true if the trimmed value starts with "true" or
		// "1" (case sensitive: "True" is false), else false; only for bool(), not in other values
		const string v = strTrim(pval, " \t\r\n");
		cp.defaultValue = (v.compare(0, 4, "true") == 0 || v.compare(0, 1, "1") == 0) ? "1" : "0";
		cp.minValue = "0";
		cp.maxValue = "1";
	} else if (cp.paramType == "button") {
		// 'button(_alignment)'
		// Add a boolean parameter (0 or 1) (as a button). Kept as "button" (issue #174): an OFX
		// push button, which gives 1 only to the run made when it is pressed, as G'MIC-Qt's
		// ButtonParameter (1 for one run, then cleared)
		cp.minValue = "0";
		cp.maxValue = "1";
		cp.defaultValue = "0";
	} else if (cp.paramType == "choice") {
		// 'choice(_default_indice,Choice0,..,ChoiceN)'
		// Add a integer parameter (as a combobox).
		// the first argument is the default index if it is an unquoted integer, also when the
		// labels themselves are unquoted (e.g. 'choice(4,16,25,36)': default 4, labels 16,25,36)
		p1 = (int)pval.find(",");
		string first = strTrim(p1 >= 0 ? pval.substr(0, p1) : string());
		if (p1 >= 0 && !first.empty() && first.find_first_not_of("0123456789") == string::npos) {
			cp.defaultValue = first;
			cp.text = pval.substr(p1 + 1);
		} else {
			cp.defaultValue = "0";
			cp.text = pval;
		}
		vector<string> c = strSplit(cp.text, ',');
		// no label ("choice()", "choice(,)", "choice(1,)"): don't read c[-1]. G'MIC-Qt
		// (ChoiceParameter::initFromText) shows one empty label; here the empty label is dropped
		// when the plug-in splits the labels, so the host gets a choice with no options, passing 0
		if (c.empty()) c.push_back(string());
		cp.text = "";
		for (int i = 0; i < (int)c.size() - 1; i++) {
			cp.text += strTrim(c[i]) + "|";
		}
		cp.text += strTrim(c[(int)c.size() - 1]);
		strReplace(cp.text, "\"", "");
		cp.minValue = "0";
		cp.maxValue = std::to_string((int)c.size() - 1);
	} else if (cp.paramType == "color") {
		// 'color(R,_G,_B,_A)', or 'color(#RRGGBB[AA])' (G'MIC >= 3)
		// Add R,G,B[,A] parameters (as a colorchooser).
		string v = strTrim(pval);
		if (!v.empty() && v[0] == '#' && (v.size() == 7 || v.size() == 9) &&
			v.find_first_not_of("0123456789abcdefABCDEF", 1) == string::npos) {
			cp.defaultValue = "";
			for (size_t k = 1; k < v.size(); k += 2) {
				if (k > 1) cp.defaultValue += "|";
				cp.defaultValue += std::to_string((int)strtol(v.substr(k, 2).c_str(), NULL, 16));
			}
		} else {
			cp.defaultValue = v;
			strReplace(cp.defaultValue, ",", "|");
		}
	} else if (cp.paramType == "point") {
		// 'point(_X,_Y,_removable={ -1 | 0 | 1 },_burst={ 0 | 1 },_R,_G,_B,_[-]A,_radius%,_is_visible={ 0 | 1 })'
		// Add X,Y parameters (as a moveable point over the preview)
		p1 = (int)pval.find(",");
		p2 = (int)pval.find(",", p1 + 1);
		if (p2 != (int)std::string::npos) { // ignore everything except the default value and removable
			cp.defaultValue = pval.substr(0, p2);
			const int p3 = (int)pval.find(",", p2 + 1);
			const string removable = strTrim(pval.substr(p2 + 1, p3 < 0 ? string::npos : (size_t)(p3 - p2 - 1)), " \t");
			cp.removable = (removable == "-1") ? -1 : (removable == "1") ? 1 : 0;
		} else {
			cp.defaultValue = pval;
		}
		strReplace(cp.defaultValue, ",", "|");
	} else if (cp.paramType == "value") {
		// 'value(value)'
		// Add a pre-defined value parameter (not displayed). It is passed as a quoted string, so
		// strip quotes from a quoted value ('value("-1")'), or they would be passed literally.
		// As G'MIC-Qt (ConstParameter::initFromText): unquoted then unescaped, like text().
		cp.defaultValue = textUnescaped(textUnquoted(pval));
	} else if (cp.paramType == "file" || cp.paramType == "filein" || cp.paramType == "fileout") {
		// 'file[_in,_out](_default_filename)'
		// Add a filename parameter (as a filechooser).
//		cp.paramType = "text";
		cp.text = strTrim(pval, "\"");
		// no default: leave it empty (a made-up "test.txt" made filters fail with "Unknown filename")
		cp.defaultValue = cp.text;
	} else if (cp.paramType == "float" || cp.paramType == "int") {
		// 'float(default_value,min_value,max_value)'
		// Add a float-valued parameter (as a float slider).
		// 'int(default_value,min_value,max_value)'
		// Add a integer parameter (as an integer slider).
		// As G'MIC-Qt (FloatParameter/IntParameter::initFromText), which rejects any other count
		// of values (e.g. "float(1)" or "float(0,1)"): exactly three. A malformed one is left
		// without a type, as an unknown type: a hidden parameter passing 0, so that the later
		// arguments keep their positions.
		const size_t c1 = pval.find(',');
		const size_t c2 = (c1 == string::npos) ? c1 : pval.find(',', c1 + 1);
		if (c1 == string::npos || c2 == string::npos || pval.find(',', c2 + 1) != string::npos) {
			gmicLog("openfx-gmic: Warning: %s parameter \"%s\" does not have three values (\"%s\"): passed as 0\n",
				cp.paramType.c_str(), cp.name.c_str(), pval.c_str());
			cp.paramType = "";
			return;
		}
		cp.defaultValue = pval.substr(0, c1);
		cp.minValue = pval.substr(c1 + 1, c2 - c1 - 1);
		cp.maxValue = pval.substr(c2 + 1);
	} else if (cp.paramType == "folder") {
		// 'folder(_default_foldername)'
		// Add a foldername parameter (as a folderchooser).
//		cp.paramType = "text";
		// an empty default stays empty: the plug-in passes the home folder for an empty value, as
		// G'MIC-Qt (see gmicCommand)
		cp.text = strTrim(pval, "\"");
		cp.defaultValue = cp.text;
	} else if (cp.paramType == "link") {
		// 'link(_alignment,_label,URL)'
		// Display a URL (do not add a parameter).
		p1 = (int)pval.find(",");
		p2 = (int)pval.find(",", p1 + 1);
		if (p2 != (int)std::string::npos) {
			// alignment, label, URL
			cp.text = strTrim(pval.substr(p1 + 1, p2 - p1 - 1), " \t\r\n'\"") + ": " + strTrim(pval.substr(p2+1), " \t\r\n'\"");
		} else if (p1 != (int)std::string::npos) {
			// label, URL
			cp.text = strTrim(pval.substr(0, p1), " \t\r\n'\"") + ": " + strTrim(pval.substr(p1+1), " \t\r\n'\"");
		} else {
			// URL
			cp.text = strTrim(pval, " \t\r\n'\"");
		}
		strReplace(cp.text, ",", ": ");
	} else if (cp.paramType == "note") {
		// 'note(_label)'
		// Display a label (do not add a parameter).
		cp.text = pval;
	} else if (cp.paramType == "text") { // || cp.paramType == "flags") {
		// 'text(_is_multiline={ 0 | 1 },_default text)'
		// Add a single or multi-line text parameter (as a text entry).
		// As G'MIC-Qt (TextParameter::initFromText): a leading "0," or "1," (regex "^\s*(0|1)\s*,")
		// is the multi-line flag, also before an unquoted default (e.g. "text(0,0xFFFFFFFF)" in
		// Kaleido Patterns: the default is "0xFFFFFFFF"); the rest is unquoted (one pair of
		// enclosing double quotes, else trimmed) then unescaped as cimg::strunescape does.
		// maxValue holds the flag.
		string v = pval;
		size_t b = v.find_first_not_of(" \t\r\n");
		cp.maxValue = "0";
		if (b != string::npos && (v[b] == '0' || v[b] == '1')) {
			size_t c = v.find_first_not_of(" \t\r\n", b + 1);
			if (c != string::npos && v[c] == ',') {
				cp.maxValue = v.substr(b, 1);
				v = v.substr(c + 1);
			}
		}
		cp.text = textUnescaped(textUnquoted(v));
		cp.defaultValue = cp.text;
	} else if (cp.paramType == "separator") {
		// 'separator()'
		// Display an horizontal separator (do not add a parameter).Add a single or multi-line text parameter (as a text entry).
		(void)0;
	} else if (cp.paramType == "input") {
		// ???? not in the G'MIC doc (see gmic_stdlib.gmic)
		cp.defaultValue = strTrim(pval, "\"");
	} else {
		// e.g. a type added by a newer G'MIC: pluginSetup passes 0 for it (a hidden float)
		gmicLog("openfx-gmic: Warning: parameter \"%s\" has an unknown type \"%s\": passed as 0\n",
			cp.name.c_str(), cp.paramType.c_str());
	}
}

// Does the filter use more than one input layer? G'MIC filters have no explicit declaration, so look
// at the GUI text: "... layers", or a reference to a "top layer", "bottom layer", "upper layer" or
// "lower layer" (e.g. Stylize: "Custom Style (Top Layer)", Smooth [Guided]: "Guide As = Top Layer").
static bool usesSeveralLayers(const string& content)
{
	// explicit input mode on the filter line: "#@gui Name : cmd, preview : *" (or "+"),
	// e.g. Align Layers, Blend [Standard], Transfer Colors
	const vector<string> lines = strSplit(content, '\n');
	for (size_t i = 0; i < lines.size(); ++i) {
		const string& l = lines[i];
		// the prefix is "#@gui", or "#@gmic_plugin" once gmic_parse_multi() rewrote it; skip
		// parameter lines ("#@gui : ...") and category lines ("#@gui ____Category")
		size_t sp = l.find(' ');
		if (l.compare(0, 2, "#@") != 0 || sp == string::npos || sp + 1 >= l.size() || l[sp + 1] == ':' || l[sp + 1] == '_') {
			continue;
		}
		size_t c1 = l.find(':');
		if (c1 == string::npos) {
			continue; // category line without the "_" prefix, e.g. "#@gui <i>Misc</i>"
		}
		size_t c2 = l.find(':', c1 + 1);
		if (c2 != string::npos) {
			const string mode = strTrim(l.substr(c2 + 1), " \t\r");
			if (!mode.empty() && (mode[0] == '*' || mode[0] == '+')) {
				return true;
			}
		}
		break; // only the first (filter) line declares the input mode
	}
	if ((int)content.find(" layers") > 0) {
		return true;
	}
	const string lc = strLowercase(content);
	// also a note asking for two images, or a choice/note naming another input: "requires 2 images
	// as input" (Save Noise Print), "Two input images" (Fast Median Stack), "at least 2 layer"
	// (Tiled Form), "another image" (Softlight), "Other Layer (Image)" (Page Curl); not the
	// plural "other images" ("may work on other images as well")
	static const char* const refs[] = { "top layer", "bottom layer", "upper layer", "lower layer",
	                                    "top-layer", "bottom-layer", "upper-layer", "lower-layer",
	                                    "2 images", "two images", "2 input images", "two input images",
	                                    "2 layer", "another image", "another layer", "other layer" };
	for (size_t i = 0; i < sizeof(refs) / sizeof(refs[0]); ++i) {
		size_t pos = lc.find(refs[i]);
		while (pos != string::npos) {
			size_t end = pos + strlen(refs[i]);
			// whole word only (reject e.g. "top layered")
			if ((pos == 0 || !isalnum((unsigned char)lc[pos - 1])) && (end >= lc.size() || !isalnum((unsigned char)lc[end]))) {
				return true;
			}
			pos = lc.find(refs[i], pos + 1);
		}
	}
	return false;
}

// Visibility propagation of a parameter, as G'MIC-Qt's AbstractParameter::VisibilityPropagation
using VisibilityPropagation = EffectParameter::Propagation;

struct VisibilitySuffix {
	size_t length = 0; // 0: no suffix
	EffectParameter::Visibility visibility = EffectParameter::Visibility::Visible;
	VisibilityPropagation propagation = VisibilityPropagation::None;
};

// As G'MIC-Qt (AbstractParameter::parseText): "_0" (hidden), "_1" (disabled) or "_2" (visible)
// right after the closing delimiter, which is at s[pos - 1], then an optional '-' (propagate up)
// or '+' or '*' (propagate down).
static VisibilitySuffix parseVisibilitySuffix(const string& s, size_t pos)
{
	VisibilitySuffix r;
	if (pos + 1 >= s.size() || s[pos] != '_' || s[pos + 1] < '0' || s[pos + 1] > '2') {
		return r;
	}
	r.visibility = static_cast<EffectParameter::Visibility>(s[pos + 1] - '0');
	const char sign = (pos + 2 < s.size()) ? s[pos + 2] : '\0';
	r.propagation = (sign == '-') ? VisibilityPropagation::Up :
	                (sign == '+' || sign == '*') ? VisibilityPropagation::Down : VisibilityPropagation::None;
	r.length = (r.propagation == VisibilityPropagation::None) ? 2 : 3;
	return r;
}

// a parameter without a value of its own (passes no argument to the commands): G'MIC-Qt's
// NoValueParameters, which also ignore their own visibility suffix
static bool isNoValueParameter(const EffectParameter& p)
{
	return p.paramType == "note" || p.paramType == "link" || p.paramType == "separator";
}

string gmic_parse_single(const string& content, EffectData& cd)
{
	const string src_prefix_old = src_prefix_old_c;
	const string src_prefix = src_prefix_c;
	const string src_prefix_en = src_prefix + "_en";
	const string dst_prefix = dst_prefix_c;
	const string dst_prefix_en = dst_prefix + "_en";
	string result;
	bool inMulti = false;
	std::string inMultiClose = "";
	cd.name = "";
	cd.command = "";
	cd.preview_command = "";
	cd.notes = "";
	cd.previewOnly = false;
	cd.noPreview = false;
	cd.previewFirst = false;
	cd.statusOnEdit = false;
	cd.dynamicVisibility = false;
	cd.param.clear();
	vector<VisibilityPropagation> propagation; // one per cd.param entry, until the "pointenabled" ones

	const vector<string> lines = strSplit(content, '\n');
	for (int i = 0; i < (int)lines.size(); i++) {
		//printf("[%d]: %s\n", i,lines[i].c_str());
		string line = strTrim(lines[i], " \r\n\t");
		if (line.size() > 0 && line[0] != '#') result += line + "\n";
		strReplace(line, src_prefix_old, dst_prefix);
		strReplace(line, src_prefix_en, dst_prefix);
		strReplace(line, src_prefix, dst_prefix);
		strReplace(line, dst_prefix_en, dst_prefix);
		// G'MIC >= 3 writes parameter lines as "#@gui :Name=type(...)": normalize to "#@gui : Name=..."
		if (line.compare(0, dst_prefix.size() + 2, dst_prefix + " :") == 0 &&
			line.compare(0, dst_prefix.size() + 3, dst_prefix + " : ") != 0) {
			line.insert(dst_prefix.size() + 2, " ");
		}
		string n = strTrim(line, " \r\n\t");
		int sPos = (int)line.find(":");
		int sPos2 = (int)line.find("#");
		if (sPos >= 0 && sPos2 == 0) {
			if (line.substr(0, dst_prefix.size() + 3) != dst_prefix + " : ") {
				inMulti = false; // a value left open ends with its filter's #@gui lines (see processParam)
				processCommand(line, cd);
			} else {
				strReplace(line, dst_prefix + " : ", "");
				while (!line.empty()) {
					if (inMulti) {
						string &name = cd.param[cd.param.size() - 1].name;
						// as G'MIC-Qt (AbstractParameter::parseText): the first closing delimiter ends
						// the value, even at the start of the line; then skip the ", " before the next one
						const string::size_type pEnd = line.find(inMultiClose);
						// Only add a space if the previous line does not end with "\n" or "\"
						// (see eg. the "Privacy Notice" in gmic_stdlib.gmic), and not before a
						// closing delimiter that starts the line (the value would end with a space)
						const bool endsWithNewline = name.size() >= 2 && name.compare(name.size() - 2, 2, "\\n") == 0;
						const bool endsWithBackslash = !name.empty() && name[name.size() - 1] == '\\';
						if (pEnd != 0 && !(endsWithNewline || endsWithBackslash)) {
							name += ' ';
						}
						if (pEnd != string::npos) {
							name += line.substr(0, pEnd + 1);
							inMulti = false;
							const VisibilitySuffix v = parseVisibilitySuffix(line, pEnd + 1);
							cd.param.back().visibility = v.visibility;
							propagation.back() = v.propagation;
							line = strTrim(line.substr(pEnd + 1 + v.length), ", ");
						} else {
							name += line;
							line.clear();
						}
						continue;
					}
					int ePos = (int)line.find("=");
					if (ePos < 0) {
						// garbage?
						line.clear();
						continue;
					} else {
						int pStart = (int)line.find_first_of("([{", ePos + 1);
						int pEnd = -1;
						if (pStart > 0) {
							if (line[pStart] == '(') {
								inMultiClose = ")";
							} else if (line[pStart] == '[') {
								inMultiClose = "]";
							} else if (line[pStart] == '{') {
								inMultiClose = "}";
							} 
							pEnd = (int)line.find(inMultiClose, pStart + 1);

							if (pEnd > 0) {
								// there is one more param
								EffectParameter p;
								p.name = line.substr(0, pEnd+1);
								//std::cout << p.name << std::endl;
								const VisibilitySuffix v = parseVisibilitySuffix(line, pEnd + 1);
								p.visibility = v.visibility;
								cd.param.push_back(p);
								propagation.push_back(v.propagation);
								line = strTrim(line.substr(pEnd + 1 + v.length), ", ");
								if (!line.empty()) {
									line = dst_prefix + " : " + line;
								}
							} else {
								// multi-line
								EffectParameter p;
								p.name = line;
								cd.param.push_back(p);
								propagation.push_back(VisibilityPropagation::None);
								inMulti = true;
								line.clear();
							}
						} else {
							// "Name = value" with no "(", "[" or "{" after the '=' is not a parameter
							// (G'MIC-Qt's AbstractParameter::createFromText makes none), and no other
							// parameter can follow it on this line: skip the line, so that the loop ends
							line.clear();
						}
					}
				}
			}
		} else if (sPos2 == 0) {
			// a category line: it ends a value left open, as a filter line does
			inMulti = false;
			sPos = (int)n.find(" ");
			if (sPos >= 0) {
				n = n.substr(sPos);
				n = strRemoveXmlTags(n, false);
				strReplace(n, " & ", " and ");
				n = strTrim(n, " \r\n\t_");
				if (n != "") {
					cd.category = n;
				}
			}
		}
	}
	set<string> realNames;
	for (int i = 0; i < (int)cd.param.size(); i++) {
		processParam(cd.param[i].name, cd.param[i]);
		realNames.insert(cd.param[i].name);
	}
	// a parameter without a name ("=float(1,0,2)"): G'MIC-Qt shows it without a label, but a host
	// parameter needs a name. Name it by its position, so that it keeps its argument, and never with
	// the name of another parameter of the filter.
	for (int i = 0; i < (int)cd.param.size(); i++) {
		if (!cd.param[i].name.empty()) continue;
		const string base = "Parameter " + std::to_string(i + 1);
		string name = base;
		for (int n = 2; realNames.count(name); n++) name = base + "_" + std::to_string(n);
		cd.param[i].name = name;
		realNames.insert(name);
		gmicLog("openfx-gmic: Warning: %s parameter without a name: named \"%s\"\n",
			cd.param[i].paramType.c_str(), name.c_str());
	}
	for (int i = 0; i < (int)cd.param.size(); i++) {
		bool ok = false;
		int cnt = 2;
		string t = cd.param[i].name;
		while (!ok) {
			ok = true;
			for (int j = 0; j < i; j++) {
				if (cd.param[j].name == cd.param[i].name) {
					cd.param[i].name = t + "_" + std::to_string(cnt++);
					ok = false;
					break;
				}
			}
		}
	}
	// As G'MIC-Qt (FilterParametersWidget::setVisibilityStates, with the default states): a
	// note, link or separator has no visibility of its own; a parameter with '-' ('+') gives its
	// visibility to the ones right before (after) it, up to the previous (next) parameter that has
	// a value, in this order, so that a later parameter wins.
	for (size_t i = 0; i < cd.param.size(); i++) {
		if (isNoValueParameter(cd.param[i])) {
			cd.param[i].visibility = EffectParameter::Visibility::Visible;
			propagation[i] = VisibilityPropagation::None;
		}
	}
	for (size_t i = 0; i < cd.param.size(); i++) {
		cd.param[i].propagation = propagation[i];
		if (isNoValueParameter(cd.param[i])) continue;
		const EffectParameter::Visibility v = cd.param[i].visibility;
		if (propagation[i] == VisibilityPropagation::Up) {
			for (size_t j = i; j > 0 && isNoValueParameter(cd.param[j - 1]); j--) cd.param[j - 1].visibility = v;
		} else if (propagation[i] == VisibilityPropagation::Down) {
			for (size_t j = i + 1; j < cd.param.size() && isNoValueParameter(cd.param[j]); j++) cd.param[j].visibility = v;
		}
	}
	// G'MIC-Qt hides only a point's widgets and keeps its handle on the preview, while a host
	// doesn't draw the overlay of a hidden or disabled parameter (Natron: shouldDrawOverlayInteract):
	// a point (and its "Enabled" checkbox, added below) stays visible, so that it can be moved
	for (size_t i = 0; i < cd.param.size(); i++) {
		if (cd.param[i].paramType == "point") cd.param[i].visibility = EffectParameter::Visibility::Visible;
	}
	// A removable point (point(x,y,-1) or point(x,y,1)) can be removed in G'MIC-Qt, which then
	// passes nan,nan for it (a -1 point starts removed): give it a checkbox right after it, whose
	// name is the point's (without " (%)", as the plug-in shows it) with " Enabled". It is not a
	// G'MIC argument: the plug-in passes nan,nan for the point when it is off (issue #79).
	for (int i = (int)cd.param.size() - 1; i >= 0; i--) {
		if (cd.param[i].paramType != "point" || cd.param[i].removable == 0) continue;
		EffectParameter e;
		string base = cd.param[i].name;
		const size_t pc = base.find(" (%)");
		if (pc != string::npos) base.erase(pc, 4);
		base = strTrim(base, " \t") + " Enabled";
		e.name = base;
		for (int cnt = 2; ; cnt++) {
			bool clash = false;
			for (int j = 0; j < (int)cd.param.size() && !clash; j++) clash = (cd.param[j].name == e.name);
			if (!clash) break;
			e.name = base + "_" + std::to_string(cnt);
		}
		e.paramType = "pointenabled";
		e.defaultValue = (cd.param[i].removable < 0) ? "0" : "1";
		e.minValue = "0";
		e.maxValue = "1";
		e.removable = cd.param[i].removable;
		cd.param.insert(cd.param.begin() + i + 1, e);
	}
	cd.multiLayer = usesSeveralLayers(content);
	// a hidden value() parameter holding a placeholder string (e.g. Shear's "place_string_here")
	// is filled in by G'MIC-Qt from the status set by the preview command (issue #52): run the
	// preview command first (see markPersistentFilters for the other rule)
	for (int i = 0; i < (int)cd.param.size(); i++) {
		if (cd.param[i].paramType == "value" && !cd.command.empty() && !cd.previewOnly && !cd.noPreview &&
			isPlaceholder(cd.param[i].defaultValue)) {
			cd.previewFirst = true;
		}
	}
	cd.notes = strTrim(cd.notes, "\n");

	cd.name = strRemoveXmlTags(cd.name, true);
	cd.category = strRemoveXmlTags(cd.category, true);
	cd.notes = strRemoveXmlTags(cd.notes, true);
	cd.uniqueId = getUniqueId(cd.name);

	return result;
}

// Is "line" the definition of a G'MIC custom command ("[+]name :[ body]", not "name:=expr")?
// Returns the command name, and the rest of the line (the start of the body) in "rest".
static string commandDefinition(const string& line, string& rest)
{
	size_t i = 0;
	if (i < line.size() && line[i] == '+') ++i;
	const size_t b = i;
	if (i >= line.size() || !(isalpha((unsigned char)line[i]) || line[i] == '_')) return string();
	while (i < line.size() && (isalnum((unsigned char)line[i]) || line[i] == '_')) ++i;
	const size_t e = i;
	while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
	if (i >= line.size() || line[i] != ':' || (i + 1 < line.size() && line[i + 1] == '=')) return string();
	rest = line.substr(i + 1);
	return line.substr(b, e - b);
}

// Split a G'MIC command line into items, separated by blanks outside double quotes. As G'MIC's
// command_line_to_CImgList, a character after '\' is part of the item ("${-quote\ $1}" is one
// item), and an escaped '"' doesn't start or end a quoted text.
static void gmicItems(const string& line, vector<string>& items)
{
	string item;
	bool quoted = false, escaped = false;
	for (size_t k = 0; k < line.size(); ++k) {
		const char c = line[k];
		if (escaped) {
			item += c;
			escaped = false;
			continue;
		}
		if (c == '\\') escaped = true;
		else if (c == '"') quoted = !quoted;
		if (!quoted && (c == ' ' || c == '\t' || c == '\r')) {
			if (!item.empty()) items.push_back(item);
			item.clear();
		} else {
			item += c;
		}
	}
	if (!item.empty()) items.push_back(item);
}

// Calls f(name) for each name in a line of G'MIC code: the longest runs of letters, digits and '_'
// that don't start with a digit (command and variable names; "$_persistent" gives "_persistent").
// name is a buffer reused from call to call.
template<typename F>
static void forEachGmicName(const string& line, string& name, F f)
{
	size_t k = 0;
	while (k < line.size()) {
		if (!(isalnum((unsigned char)line[k]) || line[k] == '_')) {
			++k;
			continue;
		}
		const size_t b = k;
		while (k < line.size() && (isalnum((unsigned char)line[k]) || line[k] == '_')) ++k;
		if (!isdigit((unsigned char)line[b])) {
			name.assign(line, b, k - b);
			f(name);
		}
	}
}

// G'MIC-Qt runs a filter's preview command before each Apply, and gives the main command what the
// preview left (FilterThread.cpp, PersistentMemory.cpp, MainWindow.cpp in c-koi/gmic-qt): the
// global variable _persistent, and the parameter values that the preview's status sets. Mark the
// filters whose main command may need either (previewFirst: pluginProcess runs their preview
// command first, see there):
// - the definition of the main command, or of a command that it calls, directly or not, names
//   _persistent. A global variable is seen by every command, so a call counts. Every name of a
//   defined command in a definition counts as a call (also a variable or a word in a string that
//   has a command's name): this can only add filters, which then run their preview command once
//   more for nothing;
// - a hidden value() parameter holds a placeholder that the status fills (see gmic_parse_single,
//   which sets previewFirst for these).
// Not the filters with a "_none_" preview command (noPreview), which G'MIC-Qt doesn't preview.
// This needs the command definitions in "content": with a gmic_ofx.gmic that has only #@gui
// lines, only the value() rule applies.
static void markPersistentFilters(const string& content, vector<EffectData>& cds)
{
	// the definitions, in order: (command, its lines; the first one is the rest of the
	// definition's line)
	vector<pair<string, vector<string> > > defs;
	unordered_set<string> defined;
	{
		stringstream ss(content);
		string line, rest;
		while (getline(ss, line)) {
			if (line.empty() || line[0] == '#') continue;
			string name = commandDefinition(line, rest);
			if (!name.empty()) {
				defined.insert(name);
				defs.push_back(make_pair(std::move(name), vector<string>(1, rest)));
			} else if (!defs.empty()) {
				defs.back().second.push_back(line);
			}
		}
	}
	unordered_map<string, vector<const string*> > callers; // command -> the commands naming it
	unordered_set<string> reading; // the commands that read _persistent, directly or not
	vector<const string*> todo;
	string name;
	for (size_t d = 0; d < defs.size(); ++d) {
		const string& cmd = defs[d].first;
		const vector<string>& lines = defs[d].second;
		for (size_t l = 0; l < lines.size(); ++l) {
			forEachGmicName(lines[l], name, [&](const string& n) {
				if (n == "_persistent") {
					if (reading.insert(cmd).second) todo.push_back(&cmd);
				} else if (n != cmd && defined.count(n)) {
					callers[n].push_back(&cmd);
				}
			});
		}
	}
	while (!todo.empty()) {
		const string* const cmd = todo.back();
		todo.pop_back();
		const auto c = callers.find(*cmd);
		if (c == callers.end()) continue;
		for (const string* caller : c->second) {
			if (reading.insert(*caller).second) todo.push_back(caller);
		}
	}
	for (EffectData& cd : cds) {
		if (!cd.previewOnly && !cd.noPreview && reading.count(cd.command)) {
			cd.previewFirst = true;
		}
	}
}

// What a line of G'MIC code sets of a G'MIC-Qt status. G'MIC-Qt reads the status that a filter's
// command sets (with "u" or "status") as "{value}{value}...", where a value may be followed by
// "_0", "_1" or "_2", its new visibility (FilterThread's status2StringList, status2Visibilities).
// In the G'MIC code, such an argument of "u" starts with {, "{ or \{ (kValues), and a visibility
// marker follows the end of a value (} or ") with '_' then a digit or an expression ($var,
// {expr}, "..."), e.g. Marker Drawing's preview: u "{"$b_type"}{"$b_blur"}_"{$b_type==0?2:1}"...
// (kVisibility). A status held in a variable ("u $s") isn't seen.
enum class StatusKind { kNone, kValues, kVisibility };

static StatusKind setsStatus(const vector<string>& items)
{
	StatusKind kind = StatusKind::kNone;
	for (size_t k = 0; k + 1 < items.size(); ++k) {
		const string& it = items[k];
		if (it[0] == '#') break; // comment
		if (it != "u" && it != "status") continue;
		const string& a = items[k + 1];
		if (a.compare(0, 1, "{") != 0 && a.compare(0, 2, "\"{") != 0 && a.compare(0, 2, "\\{") != 0) continue;
		kind = StatusKind::kValues;
		for (size_t p = a.find('_', 1); p != string::npos && p + 1 < a.size(); p = a.find('_', p + 1)) {
			const char c = a[p + 1];
			if ((a[p - 1] == '}' || a[p - 1] == '"') && ((c >= '0' && c <= '2') || c == '"' || c == '$' || c == '{')) {
				return StatusKind::kVisibility;
			}
		}
	}
	return kind;
}

// If the text at s[pos] starts with blanks (spaces or tabs) and '#', the position after the '#',
// else npos (G'MIC-Qt's containsLeadingSpaceAndCrossSign)
static size_t commentStart(const string& s, size_t pos)
{
	pos = s.find_first_not_of(" \t", pos);
	return (pos != string::npos && s[pos] == '#') ? pos + 1 : string::npos;
}

// getline() for G'MIC code: a code line that ends with '\' goes on with the next line, without
// its leading blanks (the unminified gmic_stdlib.gmic splits long "u" commands this way). Only code
// lines are joined here; comment lines (a '#', maybe after blanks, see commentStart), which hold
// the #@gui definitions, are joined by getGuiLine().
static bool getCodeLine(istream& in, string& line)
{
	if (!getline(in, line)) return false;
	string next;
	while (!line.empty() && commentStart(line, 0) == string::npos && line[line.size() - 1] == '\\' && getline(in, next)) {
		line.erase(line.size() - 1);
		line += next.substr(min(next.find_first_not_of(" \t"), next.size()));
	}
	return true;
}

// The filters whose status G'MIC-Qt applies at run time (see FilterThread's status2StringList and
// status2Visibilities; MainWindow::onPreviewImageAvailable(), onGUIDynamismRunDone()):
// - statusOnEdit: the preview command, or a command that it calls directly (one level, e.g.
//   Mitchell Concatenation's _fx_rep_mitchell_concatenation_base), sets a status (see setsStatus).
//   The plug-in runs the preview command after each user edit of the filter's parameters, for its
//   status (issue #173). Not the filters without a preview run ("_none_" preview, or no command).
// - dynamicVisibility: the main or preview command, or a command that one of them calls
//   directly, sets a status with visibility markers. The visibility suffixes ("_0", "_1") are
//   then only the initial state: e.g. Marker Drawing enables Background Blur when Background Type
//   is "Blur". The parameters keep it, and the plug-in follows the status on Natron (other hosts
//   show every parameter, see describeInContext). In a filter whose visibility status never runs
//   after an edit (not statusOnEdit: it is in the main command only), every parameter stays
//   visible and enabled, as the plug-in can't follow its changes (issue #170).
// This needs the command definitions in "content": with a gmic_ofx.gmic that has only #@gui
// lines, every filter is static.
static void markStatusFilters(const string& content, vector<EffectData>& cds)
{
	set<string> filterCommands;
	for (vector<EffectData>::const_iterator it = cds.begin(); it != cds.end(); ++it) {
		filterCommands.insert(it->command);
		filterCommands.insert(it->preview_command);
	}
	// pass 1: the defined commands, and those that set a status themselves (with visibility
	// markers: also in visibility)
	set<string> status, visibility;
	{
		stringstream ss(content);
		string line, cur, rest;
		while (getCodeLine(ss, line)) {
			if (line.empty() || line[0] == '#') continue;
			const string name = commandDefinition(line, rest);
			if (!name.empty()) {
				cur = name;
			} else {
				rest = line;
			}
			if (cur.empty() || visibility.count(cur)) continue;
			vector<string> items;
			gmicItems(rest, items);
			const StatusKind kind = setsStatus(items);
			if (kind != StatusKind::kNone) status.insert(cur);
			if (kind == StatusKind::kVisibility) visibility.insert(cur);
		}
	}
	// pass 2: the filter commands that set one, or call a command that does
	set<string> statusFilterCommands, visibilityFilterCommands;
	{
		stringstream ss(content);
		string line, cur, rest;
		while (getCodeLine(ss, line)) {
			if (line.empty() || line[0] == '#') continue;
			const string name = commandDefinition(line, rest);
			if (!name.empty()) {
				cur = filterCommands.count(name) ? name : string();
				if (!cur.empty() && status.count(name)) statusFilterCommands.insert(name);
				if (!cur.empty() && visibility.count(name)) visibilityFilterCommands.insert(name);
			} else {
				rest = line;
			}
			if (cur.empty() || visibilityFilterCommands.count(cur)) continue;
			vector<string> items;
			gmicItems(rest, items);
			for (size_t k = 0; k < items.size(); ++k) {
				const string& it = items[k];
				if (it[0] == '#') break; // comment
				// the command name of the item: "+cmd", "cmd[0]", "cmd.", "cmd arg"
				size_t b = (it[0] == '+') ? 1 : 0, e = b;
				while (e < it.size() && (isalnum((unsigned char)it[e]) || it[e] == '_')) ++e;
				const string callee = it.substr(b, e - b);
				if (callee.empty() || callee == cur) continue;
				if (status.count(callee)) statusFilterCommands.insert(cur);
				if (visibility.count(callee)) {
					visibilityFilterCommands.insert(cur);
					break;
				}
			}
		}
	}
	// About G'MIC gathers the pages of the About folder (see gmic_parse_multi), whose own commands
	// are not kept: one of them, What's New?, changes the visibility at run time
	visibilityFilterCommands.insert("fx_gmicky");
	for (vector<EffectData>::iterator it = cds.begin(); it != cds.end(); ++it) {
		it->statusOnEdit = !it->command.empty() && !it->noPreview && statusFilterCommands.count(it->preview_command);
		it->dynamicVisibility = visibilityFilterCommands.count(it->command) || visibilityFilterCommands.count(it->preview_command);
		if (!it->dynamicVisibility || it->statusOnEdit) continue;
		it->dynamicVisibility = false; // (see above)
		for (size_t i = 0; i < it->param.size(); ++i) {
			it->param[i].visibility = EffectParameter::Visibility::Visible;
		}
	}
}

// The labels of a choice() parameter (stored as "label|label|..."), with runs of blanks as one
// space (two filters of G'MIC 4.0.8 have "Forward  Horizontal")
static vector<string> choiceLabels(const string& text)
{
	vector<string> labels;
	string label;
	bool blank = false;
	for (size_t i = 0; i <= text.size(); ++i) {
		const char c = i < text.size() ? text[i] : '|';
		if (c == '|') {
			labels.push_back(strTrim(label));
			label.clear();
			blank = false;
		} else if (c == ' ' || c == '\t') {
			if (!blank) label += ' ';
			blank = true;
		} else {
			label += c;
			blank = false;
		}
	}
	return labels;
}

// the choice of G'MIC's split preview: its first label "Full..." (e.g. "Full (Allows
// Multi-Layers)"), its second "Forward Horizontal", whatever its name ("Preview Type", "Preview",
// "Type", "24-26. Preview Type"...)
static bool isSplitPreviewChoice(const EffectParameter& param)
{
	if (param.paramType != "choice") return false;
	static const string kFirstLabelPrefix = "Full";
	static const string kSecondLabel = "Forward Horizontal";
	const vector<string> labels = choiceLabels(param.text);
	return labels.size() >= 2 && labels[0].compare(0, kFirstLabelPrefix.size(), kFirstLabelPrefix) == 0 &&
		labels[1] == kSecondLabel;
}

static bool hasSuffix(const string& s, const string& suffix)
{
	return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Marks the parameters of G'MIC's split preview (EffectParameter::splitPreview): the choice of
// isSplitPreviewChoice, and its point (the next parameter with a value, or a point whose name ends
// with "Preview Split"). By G'MIC's convention, they are the last arguments, which the preview
// command passes to gui_split_preview, and which the main command ignores: they only matter with
// Preview/Draft Mode. Generic rules only, no list of filters: not in the filters that run their
// preview command anyway (same command, previewOnly), nor in those that run it before their main
// command (previewFirst, see markPersistentFilters), where these arguments can change the state
// that the main command gets. A main command that reads these arguments anyway is a bug of the filter:
// its parameters are marked too (their values are still passed to G'MIC).
static void markSplitPreviewParams(vector<EffectData>& cds)
{
	for (vector<EffectData>::iterator it = cds.begin(); it != cds.end(); ++it) {
		EffectData& cd = *it;
		if (cd.previewOnly || cd.preview_command == cd.command || cd.previewFirst) continue;
		bool afterChoice = false; // the previous parameter with a value is a split preview choice
		for (size_t i = 0; i < cd.param.size(); ++i) {
			EffectParameter& param = cd.param[i];
			if (isNoValueParameter(param)) continue;
			if (isSplitPreviewChoice(param)) {
				param.splitPreview = true;
				afterChoice = true;
				continue;
			}
			if (param.paramType == "point" && (afterChoice || hasSuffix(param.name, "Preview Split"))) {
				param.splitPreview = true;
			}
			afterChoice = false;
		}
	}
}

// Filters that need a second input although their #@gui text doesn't say so (see usesSeveralLayers),
// from the start of the definition of the main command, before its first command that is not a
// test (after "skip ..." or "check ..." and "var=..." assignments):
// - a test "if $!<2", "if $!<=1" or "if $!!=2" followed by "return" or "error" (with one image
//   it does nothing or fails): Blur by Color, Depth Map Reconstruction, Lenticular Print,
//   Stereoscopic Image Alignment, DOF Manipulation (individual mask modes);
// - "repeat" over the pairs of images ($!/2): Plasma Transition;
// - a first command that is not "+..." (nor "(...)", an image given by its values) selecting
//   image 1 ("cmd[1]", "cmd[0,1]") or reading it in an unquoted argument ("{1,w}", "#1"): Fourier
//   Picture Watermark ("rs[0] {1,w*$1},{1,h*$1}", the watermark is the second image).
// This needs the command definitions in "content".
static bool selectsImage1(const string& cmd)
{
	const size_t b = cmd.find('[');
	if (b == string::npos || cmd[cmd.size() - 1] != ']') return false;
	const string sel = "," + cmd.substr(b + 1, cmd.size() - b - 2) + ",";
	return sel.find(",1,") != string::npos;
}

static bool readsImage1(const string& arg)
{
	if (arg.empty() || arg[0] == '"') return false;
	if (arg.find("{1,") != string::npos) return true;
	for (size_t p = arg.find("#1"); p != string::npos; p = arg.find("#1", p + 1)) {
		if (p + 2 >= arg.size() || !isxdigit((unsigned char)arg[p + 2])) return true; // not "#1F"
	}
	return false;
}

// Does the condition (blanks removed) contain the test t, not followed by a digit ("$!<2", not "$!<20")?
static bool testsImageCount(const string& cond, const char* t)
{
	const size_t n = strlen(t);
	for (size_t p = cond.find(t); p != string::npos; p = cond.find(t, p + 1)) {
		if (p + n >= cond.size() || !isdigit((unsigned char)cond[p + n])) return true;
	}
	return false;
}

// "var=value", "var:=expr", "a,b=..." (and "var+=..." etc.)
static bool isAssignment(const string& item)
{
	size_t i = 0;
	while (i < item.size() && (isalnum((unsigned char)item[i]) || item[i] == '_' || item[i] == ',')) ++i;
	if (i == 0 || i >= item.size()) return false;
	if (item[i] == '=') return true;
	return i + 1 < item.size() && item[i + 1] == '=' && strchr(":+-*/%&|^<>.", item[i]) != NULL;
}

static bool needsSeveralImages(const vector<string>& items)
{
	for (size_t k = 0; k < items.size(); ++k) {
		const string& a = items[k];
		if (a[0] == '#') return false; // comment
		if (a == "skip" || a == "check") {
			++k; // and its argument
		} else if (a == "if" || a == "elif") {
			if (k + 1 >= items.size()) return false;
			string cond = items[++k];
			strReplace(cond, " ", "");
			const bool fewImages = testsImageCount(cond, "$!<2") || testsImageCount(cond, "$!<=1") || testsImageCount(cond, "$!!=2");
			if (fewImages && k + 1 < items.size() && (items[k + 1] == "return" || items[k + 1].compare(0, 5, "error") == 0)) {
				return true;
			}
		} else if (a == "else" || a == "fi" || a == "return" || a.compare(0, 5, "error") == 0) {
			if (a.compare(0, 5, "error") == 0 && k + 1 < items.size() && items[k + 1][0] == '"') ++k; // its message
		} else if (isAssignment(a)) {
			// "var=value", "a,b:=expr"...
		} else if (a == "repeat") {
			return k + 1 < items.size() && items[k + 1].find("$!/2") != string::npos;
		} else {
			// the first command
			return a[0] != '+' && a[0] != '(' && (selectsImage1(a) || (k + 1 < items.size() && readsImage1(items[k + 1])));
		}
	}
	return false;
}

static void markMultiInputFilters(const string& content, vector<EffectData>& cds)
{
	set<string> mains;
	for (vector<EffectData>::const_iterator it = cds.begin(); it != cds.end(); ++it) {
		if (!it->multiLayer) mains.insert(it->command);
	}
	map<string, vector<string> > starts; // main command -> the first items of its definition
	stringstream ss(content);
	string line, cur, rest;
	while (getline(ss, line)) {
		if (line.empty() || line[0] == '#') continue;
		string name = commandDefinition(line, rest);
		if (!name.empty()) {
			cur = (mains.count(name) && !starts.count(name)) ? name : string();
		} else {
			rest = line;
		}
		if (cur.empty() || starts[cur].size() >= 64) continue;
		gmicItems(rest, starts[cur]);
	}
	for (vector<EffectData>::iterator it = cds.begin(); it != cds.end(); ++it) {
		map<string, vector<string> >::const_iterator s = starts.find(it->command);
		if (s != starts.end() && needsSeveralImages(s->second)) it->multiLayer = true;
	}
}

// Does s contain the word "video", "frame" or "frames" (any case)?
static bool namesFrames(const string& s)
{
	static const char* const words[] = { "video", "frame", "frames" };
	const string lc = strLowercase(s);
	for (size_t w = 0; w < sizeof(words) / sizeof(words[0]); ++w) {
		const size_t n = strlen(words[w]);
		for (size_t pos = lc.find(words[w]); pos != string::npos; pos = lc.find(words[w], pos + 1)) {
			if ((pos == 0 || !isalnum((unsigned char)lc[pos - 1])) && (pos + n >= lc.size() || !isalnum((unsigned char)lc[pos + n]))) {
				return true;
			}
		}
	}
	return false;
}

// Does the item test a key of a display window, e.g. "{*,ESC}" or "{{*,ESC}||{*,Q}}"? (The window
// size, "{*,w}", is lowercase.)
static bool testsKey(const string& item)
{
	for (size_t p = item.find("{*"); p != string::npos; p = item.find("{*", p + 1)) {
		size_t k = p + 2;
		while (k < item.size() && isdigit((unsigned char)item[k])) ++k; // window number
		if (k >= item.size() || item[k] != ',') continue;
		size_t e = ++k;
		while (e < item.size() && (isupper((unsigned char)item[e]) || isdigit((unsigned char)item[e]))) ++e;
		if (e > k && isupper((unsigned char)item[k]) && e < item.size() && item[e] == '}') return true;
	}
	return false;
}

// Does one of the items test a key of a display window (testsKey)?
static bool testsKeys(const vector<string>& items)
{
	for (size_t k = 0; k < items.size(); ++k) {
		if (testsKey(items[k])) return true;
	}
	return false;
}

// Does the text substitute an argument of the command ("$1", "${2}", "${1=0}", "${2-4}", "$*",
// "${*}", "$\"*\""), not "$>", "$!", "${-command}" or a variable?
static bool readsArgument(const string& s)
{
	for (size_t p = s.find('$'); p != string::npos; p = s.find('$', p + 1)) {
		size_t k = p + 1;
		if (k < s.size() && s[k] == '{') ++k;
		if (k < s.size() && (isdigit((unsigned char)s[k]) || s[k] == '*')) return true;
		if (s.compare(k, 3, "\"*\"") == 0) return true;
	}
	return false;
}

// The names of the variables that the text substitutes ("$name", "${name}"), added to names
static void substitutedVariables(const string& s, vector<string>& names)
{
	for (size_t p = s.find('$'); p != string::npos; p = s.find('$', p + 1)) {
		size_t k = p + 1;
		if (k < s.size() && s[k] == '{') ++k;
		size_t e = k;
		while (e < s.size() && (isalnum((unsigned char)s[e]) || s[e] == '_')) ++e;
		if (e > k && !isdigit((unsigned char)s[k])) names.push_back(s.substr(k, e - k));
	}
}

// The command name of an item: without a "-" or "+" prefix, a selection ("o[0]") or dots ("ot..")
static string commandName(const string& item)
{
	const size_t b = (!item.empty() && (item[0] == '-' || item[0] == '+')) ? 1 : 0;
	const size_t sel = item.find('[');
	string cmd = item.substr(b, sel == string::npos ? string::npos : sel - b);
	while (!cmd.empty() && cmd[cmd.size() - 1] == '.') cmd.erase(cmd.size() - 1);
	return cmd;
}

// Is the item a command that writes a file: "output" and its shortcuts in G'MIC 4.0.8's command
// table ("o", "on", "op", "ot", "ow", "ox"), "output_text", "outputn", "output_cube"...?
static bool isOutputCommand(const string& item)
{
	static const char* const shortcuts[] = { "o", "on", "op", "ot", "ow", "ox" };
	const string cmd = commandName(item);
	for (size_t i = 0; i < sizeof(shortcuts) / sizeof(shortcuts[0]); ++i) {
		if (cmd == shortcuts[i]) return true;
	}
	return cmd.compare(0, 6, "output") == 0;
}

// Is the path a temporary file (${-path_tmp}, ${-filename_rand}), which isn't kept?
static bool isTemporaryPath(const string& path)
{
	return path.find("${-path_tmp}") != string::npos || path.find("${-filename_rand") != string::npos;
}

// Does the definition (its items) write a file that the user doesn't choose: into G'MIC's own
// folders ($_path_rc, ${-path_rc}, ${-path_cache}), or at a path that a command computes
// ("${-command}") and that no argument of the command (i.e. no parameter of the filter) names?
// Temporary files (${-path_tmp}, ${-filename_rand}) don't count. A variable in the file name is
// replaced by the values that the definition assigns to it, and so on for the variables in these
// ("file=${-path_cache}x.txt ... ot $file"). This is a rule on the files written, not on network
// access: filters that download data into G'MIC's cache with input_cached are not caught (they
// write nothing themselves), while the pages that download or post are, through the files where
// they keep their data. A file in a folder that a parameter gives, or with a name of the filter's
// own in the current folder, doesn't count.
static bool writesGmicChosenFile(const vector<string>& items)
{
	map<string, string> values; // variable -> the values assigned to it
	for (size_t k = 0; k < items.size(); ++k) {
		const string& it = items[k];
		if (!isAssignment(it)) continue;
		const size_t op = it.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_,");
		const size_t eq = it.find('=', op);
		stringstream names(it.substr(0, op));
		string name;
		while (getline(names, name, ',')) values[name] += ' ' + it.substr(eq + 1);
	}
	for (size_t k = 0; k + 1 < items.size(); ++k) {
		if (!isOutputCommand(items[k])) continue;
		// the file name, followed by the values of the variables it substitutes (to a fixpoint)
		string fileName = items[k + 1];
		set<string> expanded;
		vector<string> vars;
		substitutedVariables(fileName, vars);
		for (size_t v = 0; v < vars.size(); ++v) {
			if (!expanded.insert(vars[v]).second) continue;
			const map<string, string>::const_iterator val = values.find(vars[v]);
			if (val == values.end()) continue;
			fileName += val->second;
			substitutedVariables(val->second, vars);
		}
		const bool gmicFolder = fileName.find("path_rc") != string::npos || fileName.find("path_cache") != string::npos;
		const bool computed = fileName.find("${-") != string::npos && !isTemporaryPath(fileName) && !readsArgument(fileName);
		if (gmicFolder || computed) return true;
	}
	return false;
}

// Filters that make no sense when rendering frame by frame in an OpenFX host (issue #64): batch
// converters that read or write image sequences on disk on every render, filters that write files
// that the user doesn't choose, and interactive tools that open a window. Rules on the #@gui
// definition and on the definitions of the commands that the filter runs (its main command, or the
// preview command of a preview-only filter, called "main" here):
// - a parameter that reads files or folders (file(), filein(), folder()) together with the word
//   "video" or "frame(s)" in the filter name or a parameter name: Stereo Video, 3D Video Conversion;
// - the main command, its helper "_<main>" (G'MIC's naming convention), or a command that the main
//   command calls and that the library doesn't document (no "#@cli" line; one level), writes a file
//   into G'MIC's folders or at a path that a command computes (writesGmicChosenFile): e.g. Layers
//   To PNG, and the pages that download or post through the files where they keep their data;
// - the main command (only) waits for keys in a display window ({*,ESC}...): Diaporama.
// (The parser already leaves out the Sequences category and the [animated] and [interactive]
// filters.) Removed after the plug-in identifiers are assigned, so that the others keep theirs.
// There is no list of filters: as in G'MIC-Qt, which hides none, a filter that these rules miss is
// shown (e.g. Intarsia, which writes its HTML page and images into the Output Directory that the
// user chooses), and if it really can't work in a host, it is for upstream to mark it.
// This needs the command definitions in "content": with a gmic_ofx.gmic that only has #@gui lines,
// only the parameter rule applies.
static void hideBatchFilters(const string& content, vector<EffectData>& cds, vector<string>* lines)
{
	set<string> mains;
	for (vector<EffectData>::const_iterator it = cds.begin(); it != cds.end(); ++it) {
		if (!it->command.empty()) mains.insert(it->command); // (not a page with no command)
	}
	// the items of every definition, without the comments, and the documented commands (#@cli)
	map<string, vector<string> > bodies;
	set<string> documented;
	{
		stringstream ss(content);
		string line, rest;
		vector<string>* body = nullptr;
		while (getline(ss, line)) {
			if (line.compare(0, 6, "#@cli ") == 0) {
				const size_t b = line.find_first_not_of(' ', 6), e = line.find_first_of(" :", b);
				if (b != string::npos && e != b) documented.insert(line.substr(b, e == string::npos ? string::npos : e - b));
			}
			if (line.empty() || line[0] == '#') continue;
			const string name = commandDefinition(line, rest);
			if (!name.empty()) {
				body = &bodies[name];
			} else {
				rest = line;
			}
			if (!body) continue;
			vector<string> items;
			gmicItems(rest, items);
			for (size_t k = 0; k < items.size() && items[k][0] != '#'; ++k) body->push_back(items[k]);
		}
	}
	map<string, bool> writes; // command -> writesGmicChosenFile(its definition), computed once
	const auto writesFile = [&bodies, &writes](const string& cmd) {
		const map<string, bool>::const_iterator w = writes.find(cmd);
		if (w != writes.end()) return w->second;
		const map<string, vector<string> >::const_iterator d = bodies.find(cmd);
		return writes[cmd] = (d != bodies.end() && writesGmicChosenFile(d->second));
	};
	set<string> hidden;
	for (set<string>::const_iterator m = mains.begin(); m != mains.end(); ++m) {
		const map<string, vector<string> >::const_iterator d = bodies.find(*m);
		if (d == bodies.end()) continue;
		bool hide = testsKeys(d->second) || writesFile(*m) || writesFile("_" + *m);
		// the commands that it calls and that the library doesn't document (one level)
		for (size_t k = 0; k < d->second.size() && !hide; ++k) {
			const string cmd = commandName(d->second[k]);
			hide = cmd != *m && bodies.count(cmd) && !documented.count(cmd) && writesFile(cmd);
		}
		if (hide) hidden.insert(*m);
	}
	for (size_t i = cds.size(); i-- > 0; ) {
		const EffectData& cd = cds[i];
		bool readsFiles = false, frames = namesFrames(cd.name);
		for (size_t j = 0; j < cd.param.size(); ++j) {
			const string& t = cd.param[j].paramType;
			readsFiles = readsFiles || t == "file" || t == "filein" || t == "folder";
			frames = frames || namesFrames(cd.param[j].name);
		}
		if ((readsFiles && frames) || hidden.count(cd.command)) {
			cds.erase(cds.begin() + i);
			if (lines && i < lines->size()) lines->erase(lines->begin() + i);
		}
	}
}

// getline() on content from pos, as G'MIC-Qt reads the filter definitions
// (FiltersModelReader::readBufferLine): a comment line ending with '\' goes on with the next line
// if that one is a comment line too, from after its first '#' (e.g. the long notes of "About
// G'MIC" in G'MIC 4.0.8's unminified gmic_stdlib.gmic go on over "# ..." lines). The '\' is dropped,
// also before a line that is not a comment, which starts a line of its own. As in G'MIC-Qt, a line
// ending with "\\\r" (CRLF) does not go on, and the last line keeps its '\' if it has no newline.
// Only comment lines are joined here; G'MIC code lines ending with '\' are joined by getCodeLine().
static bool getGuiLine(const string& content, size_t& pos, string& line)
{
	if (pos >= content.size()) return false;
	size_t eol = content.find('\n', pos);
	line.assign(content, pos, eol == string::npos ? string::npos : eol - pos);
	pos = (eol == string::npos) ? content.size() : eol + 1;
	if (commentStart(line, 0) == string::npos) return true;
	while (eol != string::npos && !line.empty() && line.back() == '\\') {
		line.pop_back();
		const size_t start = commentStart(content, pos);
		if (start == string::npos) break;
		eol = content.find('\n', start);
		line.append(content, start, eol == string::npos ? string::npos : eol - start);
		pos = (eol == string::npos) ? content.size() : eol + 1;
	}
	return true;
}

// G'MIC-Qt's plain text of a folder or filter name (HtmlTranslator::html2txt, which matches the
// paths of hide() against it): without the HTML tags, with the entities decoded, and the blanks
// collapsed and trimmed, as QTextDocument::toPlainText() does
static string plainText(const string& s)
{
	const string t = strRemoveXmlTags(s, true);
	string r;
	r.reserve(t.size());
	for (const char c : t) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			if (!r.empty() && r.back() != ' ') r += ' ';
		} else {
			r += c;
		}
	}
	if (!r.empty() && r.back() == ' ') r.pop_back();
	return r;
}

// Is the #@gui line (without its leading blanks) G'MIC-Qt's "#@gui_en hide(path)" (FiltersModelReader.cpp,
// containsHidePath, for its "en" language, used when no translation is configured)? Gives the
// path: the folders, '/'-separated, optionally followed by a filter name.
static bool isHideLine(const string& line, string& path)
{
	static const string kPrefix = "#@gui_en ";
	if (line.compare(0, kPrefix.size(), kPrefix) != 0) return false;
	const size_t b = line.find_first_not_of(" \t", kPrefix.size());
	if (b == string::npos || line.compare(b, 5, "hide(") != 0) return false;
	const size_t e = line.find(')', b + 5);
	if (e == string::npos) return false;
	path = line.substr(b + 5, e - b - 5);
	return true;
}

// The names of a hide() path, without the empty ones (G'MIC-Qt splits it on '/', skipping the
// empty parts, without trimming them)
static vector<string> hidePathNames(const string& path)
{
	vector<string> names;
	size_t b = 0;
	while (b <= path.size()) {
		size_t e = path.find('/', b);
		if (e == string::npos) e = path.size();
		if (e > b) names.push_back(path.substr(b, e - b));
		b = e + 1;
	}
	return names;
}

// A filter's #@gui lines, with the plain text of its folders and name (see plainText)
struct GuiFilter
{
	string text; // its folder line (all its folders, '/'-separated), then its #@gui lines
	vector<string> plainPath;
	string plainName;
	bool hidden = false; // by a hide() line
};

// Does hide(path) hide the filter (G'MIC-Qt's FiltersModel::Filter::matchFullPath)? Yes if the
// path's names are the first folders of the filter, or all its folders followed by its name.
static bool hides(const vector<string>& path, const GuiFilter& filter)
{
	size_t k = 0;
	while (k < filter.plainPath.size() && k < path.size() && filter.plainPath[k] == path[k]) ++k;
	return k == path.size() || (k == filter.plainPath.size() && filter.plainName == path[k]);
}

void gmic_parse_multi(const string& content, vector<EffectData>* cds, vector<string>* lines)
{
	const string src_prefix_old = src_prefix_old_c;
	const string src_prefix = src_prefix_c;
	const string dst_prefix = dst_prefix_c;
	size_t pos = 0;
	string rawLine, command;
	vector<string> folders; // the current folder and the ones that hold it, as in the #@gui lines
	vector<GuiFilter> filters;
	vector<string> hidden; // the paths of the hide() lines
	GuiFilter filter;
	EffectData cd;
	const auto endFilter = [&filters, &filter, &command]() {
		if (command.empty()) return;
		filter.text += '\n' + command;
		filters.push_back(std::move(filter));
		filter = GuiFilter();
		command.clear();
	};
	while (getGuiLine(content, pos, rawLine)) {
		// as G'MIC-Qt (containsGuiComment...), blanks may come before "#@gui"
		const size_t b = rawLine.find_first_not_of(" \t");
		string line = (b == string::npos) ? string() : rawLine.substr(b);
		string path;
		if (isHideLine(line, path)) {
			hidden.push_back(path);
			continue;
		}
		if ( (line.substr(0, src_prefix.size() + 1) == src_prefix + " ") ||
			 (line.substr(0, src_prefix.size() + 4) == src_prefix + "_en ") ||
			 (line.substr(0, src_prefix_old.size() + 1) == src_prefix_old + " ") ) {
			// This is a @gui line or equivalent.
			// Three possibilities:
			// "#@gui Folder name" -> new folder
			// "#@gui Command name : ..." -> new plugin
			// "#@gui : ..." -> plugin parameters description
			string::size_type spc = line.find(' ');
			if (spc != string::npos && line[spc+1] == ':') {
				// plugin parameters description
				command += '\n' + line;
			} else {
				string::size_type col = line.find(':', spc + 1);
				if (col == string::npos) {
					// new folder, as in G'MIC-Qt (FiltersModelReader::parseFiltersDefinitions): each
					// leading '_' closes the current folder (a nested folder goes back to its parent,
					// e.g. "#@gui _<b>Colors</b>" after "#@gui <i>Testing</i>"), the others are dropped,
					// and an empty name opens no folder
					endFilter();
					string name = line.substr(spc + 1);
					size_t u = 0;
					for (; u < name.size() && name[u] == '_' && !folders.empty(); ++u) folders.pop_back();
					while (u < name.size() && name[u] == '_') ++u;
					name = strTrim(name.substr(u), " \t\r");
					if (!name.empty()) folders.push_back(name);
				} else if (col != spc + 1) {
					// new command/Plugin (also each page of the About folder, as in G'MIC-Qt)
					endFilter();
					// G'MIC-Qt's warning prefix ("#@gui !Name : ..."): not part of the name
					if (line.compare(spc + 1, 1, "!") == 0) {
						line.erase(spc + 1, 1);
						--col;
					}
					// its folders, all in one folder line: G'MIC-Qt's path, with '/' between the
					// folders, which hosts show as nested menu groups (see gmic_parse_single)
					filter.text = src_prefix + " ";
					for (size_t f = 0; f < folders.size(); ++f) {
						filter.text += (f ? "/" : "") + folders[f];
						filter.plainPath.push_back(plainText(folders[f]));
					}
					filter.plainName = plainText(line.substr(spc + 1, col - spc - 1));
					command = line;
				}
			}
		}
	}
	// push last command being processed
	endFilter();

	// hide(): as G'MIC-Qt, after all the filters were read (a line also hides the filters of the
	// filter files before it). The hidden filters are only marked here: they are left out after
	// the identifiers are numbered (see below), so that hiding the first of two filters of the
	// same name doesn't give its identifier to the other (G'MIC-Qt identifies a filter by a hash
	// of its path, which hiding doesn't change).
	for (const string& path : hidden) {
		const vector<string> names = hidePathNames(path);
		bool found = false;
		for (GuiFilter& f : filters) {
			if (!f.hidden && hides(names, f)) {
				f.hidden = true;
				found = true;
			}
		}
		if (!found) {
			gmicLog("openfx-gmic: Warning: hide(%s): no folder or filter with this path\n", path.c_str());
		}
	}
	// per entry that this call adds to cds (and lines), from these positions: hidden by hide()
	vector<bool> hiddenCds;
	const size_t cdsStart = cds ? cds->size() : 0;
	const size_t linesStart = lines ? lines->size() : 0;

	for (vector<GuiFilter>::const_iterator it = filters.begin(); it != filters.end(); ++it) {
		string line2 = it->text;
		strReplace(line2, src_prefix, dst_prefix);
		strReplace(line2, src_prefix_old, dst_prefix);
		cd.category.clear(); // (no folder: the filter is at the top level)
		gmic_parse_single(line2, cd);
		//printf("%s/%s\n", cd.category.c_str(), cd.name.c_str());
		// the rules below look at the filter's own folder, the last one of its path (e.g.
		// Testing/Samj/Sequences is a Sequences folder)
		const string folder = it->plainPath.empty() ? string() : it->plainPath.back();
		bool doOutput = true;
		if (folder == "Various") {
			//doOutput = false;
			doOutput = true;
		} else if ((int)strLowercase(line2).find("[interactive]") >= 0) {
			doOutput = false;
		} else if (folder == "Sequences" || (int)strLowercase(line2).find("[animated]") >= 0) {
			doOutput = false;
		} else if (line2.find("[ New Version Available! ]") != string::npos) {
			// The page that G'MIC's update_deprecate inserts into the update file of an older
			// G'MIC version (update408.gmic once a newer G'MIC is out), which tells G'MIC-Qt users to
			// upgrade: no filter, and a plug-in there would shift the index of every other one.
			doOutput = false;
		}
		if (doOutput && !cd.name.empty() && (cds || !it->hidden)) {
			if (cds) cds->push_back(cd);
			if (lines) lines->push_back(line2);
			hiddenCds.push_back(it->hidden);
		}
	}
	if (!cds) {
		return;
	}
	markPersistentFilters(content, *cds);
	markSplitPreviewParams(*cds); // (after markPersistentFilters: previewFirst)
	markMultiInputFilters(content, *cds);
	markStatusFilters(content, *cds);
    // plug-in identifiers must be unique: G'MIC 4 has several filters with the same name in
    // different folders (in 4.0.7: the "About" page of each Testing author, giving About and
    // About2..About5, and "Color Harmonies", giving ColorHarmonies2). The first one in stdlib
    // order keeps its identifier, the others get the first free numbered suffix. These generated
    // identifiers are saved in projects: they depend on the stdlib order, so they may designate
    // another filter after a G'MIC update (a known limitation, kept for compatibility with
    // existing projects). A suffixed identifier never takes the identifier of a real filter
    // (e.g. a filter named "Foo 2", whatever its position): those are reserved first.
    set<string> realIds;
    for (vector<EffectData>::const_iterator it = cds->begin(); it != cds->end(); ++it) {
        realIds.insert(it->uniqueId);
    }
    set<string> ids;
    for (vector<EffectData>::iterator it = cds->begin(); it != cds->end(); ++it) {
        string id = it->uniqueId;
        for (int n = 2; ids.count(id) || (id != it->uniqueId && realIds.count(id)); ++n) {
            id = it->uniqueId + std::to_string(n);
        }
        it->uniqueId = id;
        ids.insert(id);
    }
    // the filters that hide() hides, now that the others have their identifiers
    for (size_t k = hiddenCds.size(); k-- > 0; ) {
        if (!hiddenCds[k]) continue;
        cds->erase(cds->begin() + (ptrdiff_t)(cdsStart + k));
        if (lines && linesStart + k < lines->size()) lines->erase(lines->begin() + (ptrdiff_t)(linesStart + k));
    }
    hideBatchFilters(content, *cds, lines);
    for (vector<EffectData>::iterator it = cds->begin(); it != cds->end(); ++it) {
        // post-process
        EffectData& cd = *it;
        // (nested folders: "G'MIC/Testing/Afre/Colors", see gmic_parse_multi; a filter in no
        // folder is in the G'MIC group itself)
        if (cd.category == "About") {
            cd.category = "G'MIC";
        } else {
            cd.category = cd.category.empty() ? string("G'MIC") : "G'MIC/" + cd.category;
            cd.name = "G'MIC " + cd.name;
        }
        strReplace(cd.name, "[", "");
        strReplace(cd.name, "]", "");
        strReplace(cd.name, " - ", " ");
        strReplace(cd.name, "inverse", "inv.");
    }
}

}
