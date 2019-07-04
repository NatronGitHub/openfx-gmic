/*
 #
 #  File        : RFX_String_Utils.h
 #
 #  Description : A self-contained header file with helper functions for    
 #                string operations on std::string    
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

#include "RFX_StringUtils.h"

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <vector>
#include <fstream>
#include <cassert>
#include <cstring>

using namespace std;

namespace reduxfx {

string strLowercase(const string& str)
{
	string s = str;
	transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s;
}

string strUppercase(const string& str)
{
	string s = str;
	transform(s.begin(), s.end(), s.begin(), ::toupper);
	return s;
}

string strTrim(const string& str, const string& whitespace)
{
    const size_t strBegin = str.find_first_not_of(whitespace);
    if (strBegin == string::npos) return ""; // no content
    const size_t strEnd = str.find_last_not_of(whitespace);
    const size_t strRange = strEnd - strBegin + 1;
    return str.substr(strBegin, strRange);
}

void strReplace(string& str, const string& search, const string& replace, bool firstOnly, bool caseInsensitive)
{
	assert(!search.empty());
	size_t pos = 0;
	if (caseInsensitive) {
		string strL = strLowercase(str);
		string searchL = strLowercase(search);
		while ((pos = strL.find(searchL, pos)) != string::npos) {
			strL.erase(pos, search.size());
			strL.insert(pos, replace);
			str.erase(pos, search.size());
			str.insert(pos, replace);
			pos += replace.size();
			if (firstOnly) break;
		}
	} else {
		while ((pos = str.find(search, pos)) != string::npos) {
			str.replace(pos, search.size(), replace);
			pos += replace.size();
			if (firstOnly) break;
		}
	}
}

void strSplit(const string& s, const char seperator, vector<string>& results)
{
	results.clear();
	string::size_type prev_pos = 0, pos = 0;
	while((pos = s.find(seperator, pos)) != std::string::npos) {
		string substring(s.substr(prev_pos, pos-prev_pos) );
		substring = strTrim(substring);
		if (substring != "") results.push_back(substring);
		prev_pos = ++pos;
	}
	string substring = strTrim(s.substr(prev_pos, pos - prev_pos));
	if (substring != "") results.push_back(substring); // Last word
}

string strAfterFirst(const string& s, const string& ss, const int ofs)
{ 
	int ii = (int)s.find(ss); 
	if (ii >= 0) 
		return s.substr(ii + ofs);
	else
		return "";
}

string strAfterLast(const string& s, const string& ss, const int ofs)
{ 
	int ii = (int)s.rfind(ss); 
	if (ii >= 0) 
		return s.substr(ii + ofs);
	else
		return "";
}

string intToString(const int i)
{
    stringstream ss;
    ss << i;
    return ss.str();
}

string floatToString(const float i)
{
    stringstream ss;
    ss << setprecision(8) << i;
    return ss.str();
}

// hex string/buffer conversion

string bufferToHexString(const unsigned char* buf, const unsigned int length)
{
	stringstream ss;
	for (size_t i = 0; length > i; ++i)
        	ss << hex << setw(2) << setfill('0') << static_cast<unsigned int>(static_cast<unsigned char>(buf[i]));
	return ss.str();
}

void hexBufferToBuffer(const unsigned char* inbuf, const unsigned int length, unsigned char* outbuf)
{
    unsigned int ocnt = 0;
    for (size_t i = 0; i < length; i += 2) {
        size_t s = 0;
        stringstream ss;
        ss << std::hex << inbuf[i] << inbuf[i + 1];
        ss >> s;
        outbuf[ocnt++] = (static_cast<unsigned char>(s));
    }
}

void hexStringToBuffer(const string& hexstr, unsigned char* outbuf)
{
	hexBufferToBuffer((const unsigned char*)hexstr.c_str(), (int)hexstr.length(), outbuf);
}

string hexStringToString(const string& hexstr)
{
	string h = hexstr;
	strReplace(h, " ", "");
	unsigned char* buf = new unsigned char[(h.length() / 2) + 1];
	hexStringToBuffer(h, buf);
	buf[h.length() / 2] = 0;
	string s = string((char*)buf);
	delete [] buf;
	return s;
}

string stringToHexString(const string& str)
{
	return bufferToHexString((const unsigned char*)str.c_str(), (int)str.length());
}

string strGetTokenValue(const string& script, const string& token, const string& defaultValue, bool ignoreCase)
{
    string val = defaultValue;
    int iPos = -1;
    int iStartPos = 0;

    bool found = false;
    do {
        if (ignoreCase) {
            string scriptL = strLowercase(script);
            string tokenL = strLowercase(token);
	        iPos = (int)scriptL.find(tokenL, iStartPos);
        } else {
	        iPos = (int)script.find(token, iStartPos);
        }
        if (iPos < 0) {
            break;
        } else if (iPos == 0 || script[iPos-1] == '\r' || script[iPos-1] == '\n') {
            found = true;
        } else {
            iStartPos = iPos + 1;
        }
    } while (!found);

	if (iPos >= 0) {
		iPos = (int)script.find("=", iPos);
		if (iPos < 0) iPos = (int)script.find(" ", iPos);
		int iPos2 = (int)script.find_first_of("\r\n)", iPos);
		if (iPos2 > 0) {
			val = script.substr(iPos + 1, iPos2 - iPos - 1);
        } else {
			val = script.substr(iPos + 1);
        }
	}
	val = strTrim(val, " \t\r\n'\"");
	return val;
}


string strToAscii(string& s)
{
#define ascii_conv(a,b) else if (s[i] == a) s[i] = b;
#ifdef _WIN32
	strReplace(s, "\xC4", "Ae"); // ƒ
	strReplace(s, "\xD6", "Oe"); // ÷
	strReplace(s, "\xDC", "Ue"); // ‹
	strReplace(s, "\xC6", "Ae"); // ∆
	strReplace(s, "\xDF", "ss"); // ﬂ
	strReplace(s, "\xE6", "ae"); // Ê
	strReplace(s, "\xE4", "ae"); // ‰
	strReplace(s, "\xF6", "oe"); // ˆ
	strReplace(s, "\xFC", "ue"); // ¸
#else
	strReplace(s, "\xa7", "ss");
	strReplace(s, "\x80", "Ae");
	strReplace(s, "\x85", "Oe");
	strReplace(s, "\x86", "Ue");
	strReplace(s, "\x8a", "ae");
	strReplace(s, "\x9a", "oe");
	strReplace(s, "\x9f", "ue");
	strReplace(s, "\xae", "Ae");
	strReplace(s, "\xce", "Oe");
	strReplace(s, "\xcf", "oe");
	strReplace(s, "\xde", "fi");
	strReplace(s, "\xdf", "fl");
#endif
	for (unsigned int i = 0; i < s.size(); i++) {
		if (s[i] == '\0') s[i] = ' ';
#ifdef _WIN32
		ascii_conv('\xbf', '?') // ?
		ascii_conv('\xa1', '!') // !
		ascii_conv('\xC0', 'A') // �
		ascii_conv('\xC1', 'A') // �
		ascii_conv('\xC2', 'A') // �
		ascii_conv('\xC3', 'A') // �
		ascii_conv('\xC5', 'A') // �
		ascii_conv('\xC7', 'C') // �
		ascii_conv('\xC8', 'E') // �
		ascii_conv('\xC9', 'E') // �
		ascii_conv('\xCA', 'E') // �
		ascii_conv('\xCB', 'E') // �
		ascii_conv('\xCC', 'I') // �
		ascii_conv('\xCD', 'I') // �
		ascii_conv('\xCE', 'I') // �
		ascii_conv('\xCF', 'I') // �
		ascii_conv('\xD0', 'D') // �
		ascii_conv('\xD1', 'N') // �
		ascii_conv('\xD2', 'O') // �
		ascii_conv('\xD3', 'O') // �
		ascii_conv('\xD4', 'O') // �
		ascii_conv('\xD5', 'O') // �
		ascii_conv('\xD8', 'O') // O
		ascii_conv('\xD9', 'U') // Ÿ
		ascii_conv('\xDA', 'U') // ⁄
		ascii_conv('\xDB', 'U') // €
		ascii_conv('\xDD', 'Y') // ›
		ascii_conv('\xE0', 'a') // ‡
		ascii_conv('\xE1', 'a') // ·
		ascii_conv('\xE2', 'a') // ‚
		ascii_conv('\xE3', 'a') // „
		ascii_conv('\xE5', 'a') // Â
		ascii_conv('\xE7', 'c') // Á
		ascii_conv('\xE8', 'e') // Ë
		ascii_conv('\xE9', 'e') // È
		ascii_conv('\xEA', 'e') // Í
		ascii_conv('\xEB', 'e') // Î
		ascii_conv('\xEC', 'i') // Ï
		ascii_conv('\xED', 'i') // Ì
		ascii_conv('\xEE', 'i') // Ó
		ascii_conv('\xEF', 'i') // Ô
		ascii_conv('\xF1', 'n') // Ò
		ascii_conv('\xF2', 'o') // Ú
		ascii_conv('\xF3', 'o') // Û
		ascii_conv('\xF4', 'o') // Ù
		ascii_conv('\xF5', 'o') // ı
		ascii_conv('\xF8', 'o') // ¯
		ascii_conv('\xF9', 'u') // ˘
		ascii_conv('\xFA', 'u') // ˙
		ascii_conv('\xFB', 'u') // ˚
		ascii_conv('\xFD', 'y') // ˝
		ascii_conv('\xFF', 'y') // ˇ
#else
		ascii_conv('\x81', 'A')
		ascii_conv('\x82', 'C')
		ascii_conv('\x83', 'E')
		ascii_conv('\x84', 'N')
		ascii_conv('\x87', 'a')
		ascii_conv('\x88', 'a')
		ascii_conv('\x89', 'a')
		ascii_conv('\x8b', 'a')
		ascii_conv('\x8c', 'a')
		ascii_conv('\x8d', 'c')
		ascii_conv('\x8e', 'e')
		ascii_conv('\x8f', 'e')
		ascii_conv('\x90', 'e')
		ascii_conv('\x91', 'e')
		ascii_conv('\x92', 'i')
		ascii_conv('\x93', 'i')
		ascii_conv('\x94', 'i')
		ascii_conv('\x95', 'i')
		ascii_conv('\x96', 'n')
		ascii_conv('\x97', 'o')
		ascii_conv('\x98', 'o')
		ascii_conv('\x99', 'o')
		ascii_conv('\x9b', 'o')
		ascii_conv('\x9c', 'u')
		ascii_conv('\x9d', 'u')
		ascii_conv('\x9e', 'u')
		ascii_conv('\xa2', 'c')
		ascii_conv('\xaf', 'O')
		ascii_conv('\xc0', '?')
		ascii_conv('\xc1', '!')
		ascii_conv('\xcb', 'A')
		ascii_conv('\xcc', 'A')
		ascii_conv('\xcd', 'O')
		ascii_conv('\xd0', '-')
		ascii_conv('\xd1', '-')
		ascii_conv('\xd8', 'y')
		ascii_conv('\xd9', 'Y')
		ascii_conv('\xe5', 'A')
		ascii_conv('\xe6', 'E')
		ascii_conv('\xe7', 'A')
		ascii_conv('\xe8', 'E')
		ascii_conv('\xe9', 'E')
		ascii_conv('\xea', 'I')
		ascii_conv('\xeb', 'I')
		ascii_conv('\xec', 'I')
		ascii_conv('\xed', 'I')
		ascii_conv('\xee', 'O')
		ascii_conv('\xef', 'O')
#endif
		else if ((unsigned char)s[i] >= 128 || (unsigned char)s[i] < 10) s[i] = ' ';
	}
	return s;
}

string strRemoveXmlTags(const string& s, bool replaceEntities)
{
	string r;
	bool inHtml = false;
	for (unsigned int i = 0; i < s.size(); i++) {
		if (s[i] == '<') {
			inHtml = true;
		} else if (s[i] == '>') {
			inHtml = false;
		} else if (!inHtml) {
			r += s[i];
		}
	}
	if (replaceEntities) {
		strReplace(r, "&amp;", "&");
		strReplace(r, "&nbsp;", " ");
		strReplace(r, "&hearts;", "");
		strReplace(r, "\\251", "(C)");
		strReplace(r, "&#224;", "a"); // agrave
		strReplace(r, "&#225;", "a"); // aacute
		strReplace(r, "&#228;", "a"); // auml
		strReplace(r, "&#229;", "a"); // aring
		strReplace(r, "&#231;", "c"); // ccedil
		strReplace(r, "&#232;", "e"); // egrave
		strReplace(r, "&#233;", "e"); // eacute
		strReplace(r, "&#239;", "i"); // iuml
		strReplace(r, "&#244;", "o"); // ocirc
		strReplace(r, "&#252;", "u"); // uuml
		for (int i = 230; i < 255; i++) {
			unsigned char c = (unsigned char)i;
			string ss; ss += c;
			strReplace(r, "&#" + intToString(i) + ";", ss);
		
		}
	}
	return r;
}

void strToChar(string src, char** dst)
{
	if (*dst) delete (*dst);
	*dst = new char[src.length() + 1];
	memset(*dst, 0, src.length() + 1);
	strcpy(*dst, src.c_str());
}

}
