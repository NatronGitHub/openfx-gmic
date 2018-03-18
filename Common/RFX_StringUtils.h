/*
 #
 #  File        : RFX_String_Utils.h
 #
 #  Description : A self-contained header file with helper functions for    
 #                string operations on std::string    
 #
 #  Copyright   : Tobias Fleischer / reduxFX Productions (http://www.reduxfx.com)
 #
 #  License     : CeCILL-B v1.0
 #                ( http://www.cecill.info/licences/Licence_CeCILL-B_V1-en.html )
 #
 #  This software is governed either by the CeCILL-B license
 #  under French law and abiding by the rules of distribution of free software.
 #  You can  use, modify and or redistribute the software under the terms of
 #  the CeCILL-B licenses as circulated by CEA, CNRS and INRIA
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
 #  knowledge of the CeCILL-B licenses and that you accept its terms.
 #
*/

#ifdef _WIN32
#pragma warning (disable:4996)
#endif
#pragma once
#ifndef _RFX_STRUTILS_H
#define _RFX_STRUTILS_H

#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <vector>
#include <fstream>

using namespace std;

namespace reduxfx {

static string strLowercase(const string& str)
{
	string s = str;
	transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s;
}

static string strUppercase(const string& str)
{
	string s = str;
	transform(s.begin(), s.end(), s.begin(), ::toupper);
	return s;
}

static string strTrim(const string& str, const string& whitespace = " \t\0")
{
    const size_t strBegin = str.find_first_not_of(whitespace);
    if (strBegin == string::npos) return ""; // no content
    const size_t strEnd = str.find_last_not_of(whitespace);
    const size_t strRange = strEnd - strBegin + 1;
    return str.substr(strBegin, strRange);
}

static string strReplace(string& str, const string& search, const string& replace, bool firstOnly = false, bool caseInsensitive = false)
{
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
	return str;
}
static void strSplit(const string& s, const char seperator, vector<string>& results)
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

static string strAfterFirst(const string& s, const string& ss, const int ofs = 0)
{ 
	int ii = (int)s.find(ss); 
	if (ii >= 0) 
		return s.substr(ii + ofs);
	else
		return "";
}

static string strAfterLast(const string& s, const string& ss, const int ofs = 0)
{ 
	int ii = (int)s.rfind(ss); 
	if (ii >= 0) 
		return s.substr(ii + ofs);
	else
		return "";
}

static string intToString(const int i)
{
    stringstream ss;
    ss << i;
    return ss.str();
}

static string floatToString(const float i)
{
    stringstream ss;
    ss << i;
    return ss.str();
}

// hex string/buffer conversion

static inline string bufferToHexString(const unsigned char* buf, const unsigned int length)
{
	stringstream ss;
	for (size_t i = 0; length > i; ++i)
        	ss << hex << setw(2) << setfill('0') << static_cast<unsigned int>(static_cast<unsigned char>(buf[i]));
	return ss.str();
}

static inline void hexBufferToBuffer(const unsigned char* inbuf, const unsigned int length, unsigned char* outbuf)
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

static inline void hexStringToBuffer(const string& hexstr, unsigned char* outbuf)
{
	hexBufferToBuffer((const unsigned char*)hexstr.c_str(), (int)hexstr.length(), outbuf);
}

static string hexStringToString(const string& hexstr)
{
	string h = hexstr;
	h = strReplace(h, " ", "");
	unsigned char* buf = new unsigned char[(h.length() / 2) + 1];
	hexStringToBuffer(h, buf);
	buf[h.length() / 2] = 0;
	string s = string((char*)buf);
	delete [] buf;
	return s;
}

static string stringToHexString(const string& str)
{
	return bufferToHexString((const unsigned char*)str.c_str(), (int)str.length());
}

static string strGetToken(const string& s, const string& token, const string& delim = "\"")
{
	string res;
	size_t pos = 0;
	pos = s.find(token);
	if (pos != string::npos) {
		res = s.substr(pos);
		pos = res.find(delim);
		res = res.substr(pos+1);
		pos = res.find(delim);
		res = res.substr(0, pos);
	}
	return res;
}

static string strGetTokenValue(const string& script, const string& token)
{
	int iPos = (int)script.find(token);
	if (iPos >= 0) {
		iPos = (int)script.find("=", iPos);
		if (iPos < 0) iPos = (int)script.find(" ", iPos);
		int iPos2 = (int)script.find_first_of("\r\n)", iPos);
		if (iPos2 >= 0) {
			string val = script.substr(iPos + 1, iPos2 - iPos - 1);
			val = strTrim(val, " \t\r\n'\"");
			return val;
		}
	}
	return "";
}


static string strGetTag(const string& s, const string& tag)
{
	string res;
	size_t pos = 0;
	pos = s.find(tag);
	if (pos != string::npos) {
		s = s.substr(pos);
		pos = s.find(">");
		s = s.substr(0, pos);
		res = s;
	}
	return res;
}

}

#endif

