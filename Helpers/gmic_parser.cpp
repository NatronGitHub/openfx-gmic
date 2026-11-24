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

#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

#include "RFX_StringUtils.h"

using namespace std;
using namespace reduxfx;

namespace reduxfx {

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
	if (sPos >= 0) {
		r3 = r2.substr(sPos + 1);
		r2 = r2.substr(0, sPos);
	}
	// remove the preview options after the preview command: "(N)" (preview mode/split) followed by
	// "+" (accurate on zoom) and/or "*" (G'MIC >= 3, accurate on parameter change)
	for (size_t lp = r3.find('('); lp != string::npos; lp = r3.find('(', lp)) {
		size_t rp = lp + 1;
		while (rp < r3.size() && isdigit((unsigned char)r3[rp])) ++rp;
		if (rp > lp + 1 && rp < r3.size() && r3[rp] == ')') {
			r3.erase(lp, rp - lp + 1);
		} else {
			++lp;
		}
	}
	strReplace(r3, "+", "");
	strReplace(r3, "*", "");

	// "_none_" main command: a preview-only filter (the preview's output is the result)
	sPos = (int)r2.find("_none_");
	if (sPos >= 0) {
		r2 = r3;
		cd.previewOnly = true;
	}
	sPos = (int)r3.find("_none_");
	if (sPos >= 0) r3 = r2;

	cd.command = strTrim(r2, " \n");
	cd.preview_command = strTrim(r3, " \n");
	if (cd.preview_command == "") cd.preview_command = cd.command;
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
		fprintf(stderr, "openfx-gmic: Warning: %s parameter \"%s\" has no closing '%c': it ends with the filter\n",
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
		// Add a boolean parameter (0 or 1) (as a button).
		cp.paramType = "bool";
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
		vector<string> c;
		strSplit(cp.text, ',', c);
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
		cp.maxValue = intToString((int)c.size() - 1);
	} else if (cp.paramType == "color") {
		// 'color(R,_G,_B,_A)', or 'color(#RRGGBB[AA])' (G'MIC >= 3)
		// Add R,G,B[,A] parameters (as a colorchooser).
		string v = strTrim(pval);
		if (!v.empty() && v[0] == '#' && (v.size() == 7 || v.size() == 9) &&
			v.find_first_not_of("0123456789abcdefABCDEF", 1) == string::npos) {
			cp.defaultValue = "";
			for (size_t k = 1; k < v.size(); k += 2) {
				if (k > 1) cp.defaultValue += "|";
				cp.defaultValue += intToString((int)strtol(v.substr(k, 2).c_str(), NULL, 16));
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
			fprintf(stderr, "openfx-gmic: Warning: %s parameter \"%s\" does not have three values (\"%s\"): passed as 0\n",
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
		cp.text = strTrim(pval, "\"");
		if (cp.text == "") cp.text = "./";
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
		fprintf(stderr, "openfx-gmic: Warning: parameter \"%s\" has an unknown type \"%s\": passed as 0\n",
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
	vector<string> lines;
	strSplit(content, '\n', lines);
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
	cd.previewFallback = false;
	cd.param.clear();

	vector<string> lines;
	strSplit(content, '\n', lines);
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
							line = strTrim(line.substr(pEnd + 1), ", ");
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
								cd.param.push_back(p);
								line = strTrim(line.substr(pEnd + 1), ", ");
								if (!line.empty()) {
									line = dst_prefix + " : " + line;
								}
							} else {
								// multi-line
								EffectParameter p;
								p.name = line;
								cd.param.push_back(p);
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
			// a category line, or the line that separates the pages of "About G'MIC" (see
			// gmic_parse_multi): it ends a value left open, as a filter line does
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
		const string base = "Parameter " + intToString(i + 1);
		string name = base;
		for (int n = 2; realNames.count(name); n++) name = base + "_" + intToString(n);
		cd.param[i].name = name;
		realNames.insert(name);
		fprintf(stderr, "openfx-gmic: Warning: %s parameter without a name: named \"%s\"\n",
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
					cd.param[i].name = t + "_" + intToString(cnt++);
					ok = false;
					break;
				}
			}
		}
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
			e.name = base + "_" + intToString(cnt);
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
	// is filled in by G'MIC-Qt from the status set by the preview command (issue #52)
	for (int i = 0; i < (int)cd.param.size(); i++) {
		if (cd.param[i].paramType == "value" && !cd.previewOnly && cd.preview_command != cd.command &&
			isPlaceholder(cd.param[i].defaultValue)) {
			cd.previewFallback = true;
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

// Split a G'MIC command line into items (separated by blanks outside double quotes).
static void gmicItems(const string& line, vector<string>& items)
{
	string item;
	bool quoted = false;
	for (size_t k = 0; k < line.size(); ++k) {
		const char c = line[k];
		if (c == '"' && (k == 0 || line[k - 1] != '\\')) quoted = !quoted;
		if (!quoted && (c == ' ' || c == '\t' || c == '\r')) {
			if (!item.empty()) items.push_back(item);
			item.clear();
		} else {
			item += c;
		}
	}
	if (!item.empty()) items.push_back(item);
}

// Does the item read $_persistent (other than in a narg($_persistent) test)?
static bool readsPersistent(const string& item)
{
	for (size_t p = item.find("$_persistent"); p != string::npos; p = item.find("$_persistent", p + 1)) {
		if (!(p >= 5 && item.compare(p - 5, 5, "narg(") == 0)) return true;
	}
	return false;
}

// Is the condition of an "if"/"elif" a positive narg($_persistent) test?
static bool persistentGuard(const string& cond)
{
	const size_t p = cond.find("narg($_persistent)");
	return p != string::npos && !(p > 0 && cond[p - 1] == '!');
}

// G'MIC-Qt always runs the preview command before the main one, and the main command of some
// filters relies on what the preview stored in the global variable _persistent (issue #52):
// - Tupper's Self-Referential Formula: "rm $_persistent"; Lavander Binary Map: "error
//   uninitialized" without it; Bit-Plane Shuffler reads it in both branches;
// - Array [Random] [Jumble by Px] and Statistical Mode call their preview command with "$*"
//   without it, which splits text arguments containing blanks.
// Flag these (previewFallback: rendered with the preview command if the main command fails):
// the definition of the main command (not the commands it calls) reads $_persistent outside an
// "if narg($_persistent)" branch, or calls the preview command. Filters that only use
// _persistent as a cache or compute the result themselves without it (3D Mesh, Diffusion
// Limited Aggregation, Color Region) are not flagged. Channel Blur-Sharpen-Overblur-Overline is
// (it calls its preview command), but its main command works. This needs the command
// definitions in "content": a gmic_ofx.gmic with only #@gui lines flags nothing here.
static void markPersistentFilters(const string& content, vector<EffectData>& cds)
{
	map<string, string> mains; // main command -> preview command
	for (vector<EffectData>::const_iterator it = cds.begin(); it != cds.end(); ++it) {
		if (!it->previewOnly && it->preview_command != it->command) mains[it->command] = it->preview_command;
	}
	set<string> flagged;
	set<string> defined; // all the commands defined in content
	stringstream ss(content);
	string line, cur, rest;
	vector<bool> guards; // one per enclosing if: is the current branch a narg($_persistent) branch?
	while (getline(ss, line)) {
		if (line.empty() || line[0] == '#') continue;
		string name = commandDefinition(line, rest);
		if (!name.empty()) {
			defined.insert(name);
			cur = mains.count(name) ? name : string();
			guards.clear();
		} else {
			rest = line;
		}
		if (cur.empty() || flagged.count(cur)) continue;
		vector<string> items;
		gmicItems(rest, items);
		for (size_t k = 0; k < items.size(); ++k) {
			const string& it = items[k];
			if (it[0] == '#') break; // comment
			if (it == "if" || it == "elif") {
				const bool g = k + 1 < items.size() && persistentGuard(items[k + 1]);
				if (it == "if") guards.push_back(g);
				else if (!guards.empty()) guards.back() = g;
				++k; // skip the condition
			} else if (it == "else") {
				if (!guards.empty()) guards.back() = false;
			} else if (it == "fi") {
				if (!guards.empty()) guards.pop_back();
			} else if (it == mains[cur] || (it[0] == '+' && it.substr(1) == mains[cur])) {
				flagged.insert(cur);
			} else if (readsPersistent(it)) {
				bool guarded = false;
				for (size_t g = 0; g < guards.size(); ++g) guarded = guarded || guards[g];
				if (!guarded) flagged.insert(cur);
			}
		}
	}
	// Main commands that need the preview's state in a way the rules above can't detect. A listed
	// command that no longer is the main command of a filter (e.g. after a G'MIC update) is
	// reported on stderr, and makes the describe test fail (.github/workflows/describe_plugins.sh).
	// COMMERCIAL builds (OFX_GMIC_COMMERCIAL) do not report one that is not defined at all: their
	// standard library leaves out its filter (see gmic_stdlib_commercial.txt).
	// - fx_rep_bitplane_shuffle (Bit-Plane Shuffler): without _persistent it runs
	//   "rep_bitplane_shuffle ...,$cli_base", and cli_base is only set by its preview command.
	static const char* const curated[] = { "fx_rep_bitplane_shuffle" };
	for (size_t c = 0; c < sizeof(curated) / sizeof(curated[0]); ++c) {
		if (mains.count(curated[c])) {
			flagged.insert(curated[c]);
		} else if (!mains.empty()) {
#ifdef OFX_GMIC_COMMERCIAL
			if (!defined.count(curated[c])) continue;
#endif
			fprintf(stderr, "G'MIC: PREVIEW_FALLBACK_UNKNOWN %s: not the main command of a filter, update markPersistentFilters()\n", curated[c]);
		}
	}
	for (vector<EffectData>::iterator it = cds.begin(); it != cds.end(); ++it) {
		if (!it->previewOnly && it->preview_command != it->command && flagged.count(it->command)) {
			it->previewFallback = true;
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

// Filters that make no sense when rendering frame by frame in an OpenFX host (issue #64): batch
// converters that read or write image sequences on disk on every render, filters that write files,
// and interactive tools that open a window. Rules on the #@gui definition and on the definition of
// the main command (not the commands it calls):
// - a parameter that reads files or folders (file(), filein(), folder()) together with the word
//   "video" or "frame(s)" in the filter name or a parameter name: Stereo Video, 3D Video Conversion;
// - the main command writes a file into G'MIC's own folder ("output ...$_path_rc..."): Layers To PNG;
// - the main command waits for keys in a display window ({*,ESC}...): Diaporama.
// (The parser already leaves out the Sequences category and the [animated] and [interactive]
// filters.) Removed after the plug-in identifiers are assigned, so that the others keep theirs.
// This needs the command definitions in "content": with a gmic_ofx.gmic that only has #@gui lines,
// only the parameter rule and the curated list below apply.
static void hideBatchFilters(const string& content, vector<EffectData>& cds, vector<string>* lines, bool checkCurated)
{
	set<string> mains;
	for (vector<EffectData>::const_iterator it = cds.begin(); it != cds.end(); ++it) {
		mains.insert(it->command);
	}
	set<string> hidden;
	stringstream ss(content);
	string line, cur, rest;
	while (getline(ss, line)) {
		if (line.empty() || line[0] == '#') continue;
		string name = commandDefinition(line, rest);
		if (!name.empty()) {
			cur = mains.count(name) ? name : string();
		} else {
			rest = line;
		}
		if (cur.empty() || hidden.count(cur)) continue;
		vector<string> items;
		gmicItems(rest, items);
		for (size_t k = 0; k < items.size(); ++k) {
			const string& it = items[k];
			if (it[0] == '#') break; // comment
			const size_t b = (it[0] == '-') ? 1 : 0, e = it.find('[');
			const string cmd = it.substr(b, e == string::npos ? string::npos : e - b);
			if ((cmd == "o" || cmd == "output") && k + 1 < items.size() && items[k + 1].find("$_path_rc") != string::npos) {
				hidden.insert(cur);
			} else if (testsKey(it)) {
				hidden.insert(cur);
			}
		}
	}
	// Main commands hidden whatever the rules say, so that they stay hidden if their definition
	// changes. A listed command that no longer is the main command of a filter (e.g. after a G'MIC
	// update) is reported on stderr, and makes the describe test fail
	// (.github/workflows/describe_plugins.sh).
	// Also hidden:
	// - Intarsia (fx_intarsia) writes an HTML page and PNG images into its Output Directory ($1,
	//   by default empty, so the root folder) on every render, which the rule above misses.
	// The filters that upstream disabled as broken after G'MIC 4.0.7, and that were hidden here
	// (Color Region, Prime Surface, Reverse Engineer Color Curve, Layer Manipulation, Halftone
	// Shapes, Montage Multi Rows), are no longer in G'MIC 4.0.8; Beauty Retouch (fx_tk_retouch,
	// https://github.com/GreycLab/gmic-community/issues/308) is fixed there.
	static const char* const curated[] = { "gcd_stereo_vid", "fx_tk_video3D", "samj_diaporama", "samj_Layers_To_PNG",
		"fx_intarsia" };
	for (size_t c = 0; c < sizeof(curated) / sizeof(curated[0]); ++c) {
		if (mains.count(curated[c])) {
			hidden.insert(curated[c]);
		} else if (checkCurated && !mains.empty()) {
#ifdef OFX_GMIC_COMMERCIAL
			// COMMERCIAL standard libraries leave out some of these filters, or only their #@gui
			// lines (e.g. fx_tk_video3D, see gmic_stdlib_commercial.txt): the GPL build checks the list
			continue;
#endif
			fprintf(stderr, "G'MIC: HIDDEN_FILTER_UNKNOWN %s: not the main command of a filter, update hideBatchFilters()\n", curated[c]);
		}
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

// "#@gui ____<i>About</i>" (G'MIC 2) or "#@gui <i>About</i>" (G'MIC >= 3)
static bool isAboutCategory(const string& cat)
{
	size_t sp = cat.find(' ');
	if (sp == string::npos) {
		return false;
	}
	string c = strTrim(cat.substr(sp), " \t\r_");
	return c == "<i>About</i>";
}

// If the text at s[pos] starts with blanks (spaces or tabs) and '#', the position after the '#',
// else npos (G'MIC-Qt's containsLeadingSpaceAndCrossSign)
static size_t commentStart(const string& s, size_t pos)
{
	pos = s.find_first_not_of(" \t", pos);
	return (pos != string::npos && s[pos] == '#') ? pos + 1 : string::npos;
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

void gmic_parse_multi(const string& content, vector<EffectData>* cds, vector<string>* lines, bool checkCurated)
{
	const string src_prefix_old = src_prefix_old_c;
	const string src_prefix = src_prefix_c;
	const string dst_prefix = dst_prefix_c;
	size_t pos = 0;
	string line, cat, command;
	vector<string> commands;
	EffectData cd;
	//printf("content:\n%s\n", content.c_str());
	while (getGuiLine(content, pos, line)) {
		//printf("%s\n%s\n", cat.c_str(), line.c_str());
		if ( (line.substr(0, src_prefix.size() + 1) == src_prefix + " ") ||
			 (line.substr(0, src_prefix.size() + 4) == src_prefix + "_en ") ||
			 (line.substr(0, src_prefix_old.size() + 1) == src_prefix_old + " ") ) {
			// This is a @gui line or equivalent.
			// Three possibilities:
			// "#@gui Folder name" -> new category
			// "#@gui Command name : ..." -> new plugin
			// "#@gui : ..." -> plugin parameters description
			string::size_type spc = line.find(' ');
			if (spc != string::npos && line[spc+1] == ':') {
				// plugin parameters description
				command += '\n' + line;
			} else {
				string::size_type col = line.find(':', spc + 1);
				if (col == string::npos) {
					// new Folder/category
					if (!command.empty()) {
						commands.push_back(cat + '\n' + command);
						command.clear();
					}
					cat = line;
					if (isAboutCategory(cat)) {
						command = "#@gui About G'MIC : fx_gmicky, fx_gmicky_preview";
					}
				} else if (col != spc + 1) {
					if (isAboutCategory(cat)) {
						// add new title, after a "#@gui" line that ends the previous page (as a filter
						// line does in G'MIC-Qt), so that a value left open there doesn't take it in
						command += "\n#@gui\n#@gui : note = note{" + line.substr(spc+1, col - spc - 1) + '}';
					} else {
						// new command/Plugin
						if (!command.empty()) {
							commands.push_back(cat + '\n' + command);
							command.clear();
						}
						command = line;
					}
				}
			}
		}
	}
	// push last command being processed
	if (!command.empty()) {
		commands.push_back(cat + '\n' + command);
		command.clear();
	}

	for (vector<string>::const_iterator it = commands.begin(); it != commands.end(); ++it) {
		string line2 = *it;
		strReplace(line2, src_prefix, dst_prefix);
		strReplace(line2, src_prefix_old, dst_prefix);
		gmic_parse_single(line2, cd);
		//printf("%s/%s\n", cd.category.c_str(), cd.name.c_str());
		bool doOutput = true;
		// skip entries from the about category
		if (cd.category == "About" && cd.uniqueId != "eu.gmic.AboutGMIC") {
			//doOutput = false;
		} else if (cd.category == "Various") {
			//doOutput = false;
			doOutput = true;
		} else if ((int)strLowercase(line2).find("[interactive]") >= 0) {
			doOutput = false;
		} else if (cd.category == "Sequences" || (int)strLowercase(line2).find("[animated]") >= 0) {
			doOutput = false;
		}
		if (doOutput && !cd.name.empty()) {
			if (cds) cds->push_back(cd);
			if (lines) lines->push_back(line2);
		}
	}
	if (!cds) {
		return;
	}
	markPersistentFilters(content, *cds);
	markMultiInputFilters(content, *cds);
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
            id = it->uniqueId + intToString(n);
        }
        it->uniqueId = id;
        ids.insert(id);
    }
    hideBatchFilters(content, *cds, lines, checkCurated);
    for (vector<EffectData>::iterator it = cds->begin(); it != cds->end(); ++it) {
        // post-process
        EffectData& cd = *it;
        if (cd.category == "About") {
            cd.category = "G'MIC";
        } else {
            cd.category = "G'MIC/" + cd.category;
            cd.name = "G'MIC " + cd.name;
        }
        strReplace(cd.name, "[", "");
        strReplace(cd.name, "]", "");
        strReplace(cd.name, " - ", " ");
        strReplace(cd.name, "inverse", "inv.");
    }
}

}
