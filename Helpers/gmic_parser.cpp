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
//static const char* const master_name_c = "gmic_ae";


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

static
void processParam(const string& s, EffectParameter& cp)
{
	string r = s;
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
	string pval = strTrim(r.substr(p1 + 1));
	pval = pval.substr(0, pval.size() - 1);
	strLowercase(pval);
	strReplace(pval, "true", "1");
	strReplace(pval, "false", "0");

	// For parameters description, see "Specific rules for the universal plug-in"
	// in the gmic_stdlib.gmic file in the G'MIC sources.
	if (cp.paramType == "bool") {
		// 'bool(default_value={ 0 | 1 | false | true })'
		// Add a boolean parameter (0 or 1) (as a checkbutton).
		cp.defaultValue = pval;
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
		if (p2 != (int)std::string::npos) { // ignore everything except the default value
			cp.defaultValue = pval.substr(0, p2);
		} else {
			cp.defaultValue = pval;
		}
		strReplace(cp.defaultValue, ",", "|");
	} else if (cp.paramType == "value") {
		// 'value(value)'
		// Add a pre-defined value parameter (not displayed). It is passed as a quoted string, so
		// strip quotes from a quoted value ('value("-1")'), or they would be passed literally.
		cp.defaultValue = strTrim(pval);
		if (cp.defaultValue.size() >= 2 && cp.defaultValue[0] == '"' && cp.defaultValue[cp.defaultValue.size() - 1] == '"') {
			cp.defaultValue = cp.defaultValue.substr(1, cp.defaultValue.size() - 2);
		}
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
		p1 = (int)pval.find(",");
		p2 = (int)pval.rfind(",");
		if (p1 == p2) {
			cp.minValue = pval.substr(0, p1);
			cp.maxValue = pval.substr(p1);
			cp.defaultValue = cp.minValue;
		} else {
			cp.defaultValue = pval.substr(0, p1);
			cp.maxValue = pval.substr(p2 + 1);
			cp.minValue = pval.substr(p1 + 1, p2 - p1 - 1);
		}
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
			cp.text = strTrim(pval.substr(p1+1, p2-1), " \t\r\n'\"") + ": " + strTrim(pval.substr(p2+1), " \t\r\n'\"");
		} else if (p1 != (int)std::string::npos) {
			// label, URL
			cp.text = strTrim(pval.substr(0, p1-1), " \t\r\n'\"") + ": " + strTrim(pval.substr(p1+1), " \t\r\n'\"");
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
		p1 = (int)pval.find(",");
		p2 = (int)pval.find("\"");
		if (p1 >= 0 && p1 < p2) {
			cp.maxValue = pval.substr(0, p1);
			pval = pval.substr(p1 + 1);
		} else {
			cp.maxValue = "0";
		}
		cp.text = strTrim(pval, "\"");
		cp.defaultValue = cp.text;
	} else if (cp.paramType == "separator") {
		// 'separator()'
		// Display an horizontal separator (do not add a parameter).Add a single or multi-line text parameter (as a text entry).
		(void)0;
	} else if (cp.paramType == "input") {
		// ???? not in the G'MIC doc (see gmic_stdlib.gmic)
		cp.defaultValue = strTrim(pval, "\"");
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
	string lc = content;
	for (size_t i = 0; i < lc.size(); ++i) {
		lc[i] = (char)tolower((unsigned char)lc[i]);
	}
	static const char* const refs[] = { "top layer", "bottom layer", "upper layer", "lower layer",
	                                    "top-layer", "bottom-layer", "upper-layer", "lower-layer" };
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
        // TODO: there should be a "while" loop here, in case there are several parameters on the line
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
				processCommand(line, cd);
			} else {
				strReplace(line, dst_prefix + " : ", "");
				while (!line.empty()) {
					if (inMulti) {
						string &name = cd.param[cd.param.size() - 1].name;
						// Only add a space if the previous line does not end with "\n" or "\"
						// (see eg. the "Privacy Notice" in gmic_stdlib.gmic)
						if (name.size() < 2 ||
							!(name[name.size()-2] == '\\' && name[name.size()-1] == 'n') ||
							!(name[name.size()-1] == '\\')) {
							cd.param[cd.param.size() - 1].name += ' ';
						}
						int pEnd = (int)line.find(inMultiClose);
						if (pEnd > 0) {
							cd.param[cd.param.size() - 1].name += line.substr(0, pEnd + 1);
							inMulti = false;
							line = strTrim(line.substr(pEnd));
						} else {
							cd.param[cd.param.size() - 1].name += line;
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
						}
					}
				}
			}
		} else if (sPos2 == 0) {
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
	for (int i = 0; i < (int)cd.param.size(); i++) {
		processParam(cd.param[i].name, cd.param[i]);
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
	stringstream ss(content);
	string line, cur, rest;
	vector<bool> guards; // one per enclosing if: is the current branch a narg($_persistent) branch?
	while (getline(ss, line)) {
		if (line.empty() || line[0] == '#') continue;
		string name = commandDefinition(line, rest);
		if (!name.empty()) {
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
	// - fx_rep_bitplane_shuffle (Bit-Plane Shuffler): without _persistent it runs
	//   "rep_bitplane_shuffle ...,$cli_base", and cli_base is only set by its preview command.
	static const char* const curated[] = { "fx_rep_bitplane_shuffle" };
	for (size_t c = 0; c < sizeof(curated) / sizeof(curated[0]); ++c) {
		if (mains.count(curated[c])) {
			flagged.insert(curated[c]);
		} else if (!mains.empty()) {
			fprintf(stderr, "G'MIC: PREVIEW_FALLBACK_UNKNOWN %s: not the main command of a filter, update markPersistentFilters()\n", curated[c]);
		}
	}
	for (vector<EffectData>::iterator it = cds.begin(); it != cds.end(); ++it) {
		if (!it->previewOnly && it->preview_command != it->command && flagged.count(it->command)) {
			it->previewFallback = true;
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

void gmic_parse_multi(const string& content, vector<EffectData>* cds, vector<string>* lines)
{
	const string src_prefix_old = src_prefix_old_c;
	const string src_prefix = src_prefix_c;
	const string dst_prefix = dst_prefix_c;
	stringstream ss(content);
	string line, cat, command;
	vector<string> commands;
	EffectData cd;
	//printf("content:\n%s\n", content.c_str());
	while (getline(ss, line)) {
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
						// add new title
						command += "\n#@gui : note = note{" + line.substr(spc+1, col - spc - 1) + '}';
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
    for (vector<EffectData>::iterator it = cds->begin(); it != cds->end(); ++it) {
        // post-process
        EffectData& cd = *it;
        if (cd.category == "About") {
            cd.category = "GMIC";
        } else {
            cd.category = "GMIC/" + cd.category;
            cd.name = "G'MIC " + cd.name;
        }
        strReplace(cd.name, "[", "");
        strReplace(cd.name, "]", "");
        strReplace(cd.name, " - ", " ");
        strReplace(cd.name, "inverse", "inv.");
    }
}

}
