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

#ifndef RFX_STRUTILS_H
#define RFX_STRUTILS_H

#include <string>
#include <vector>

namespace reduxfx {

std::string strLowercase(const std::string& str);

std::string strUppercase(const std::string& str);

std::string strTrim(const std::string& str, const std::string& whitespace = " \t\0");

void strReplace(std::string& str, const std::string& search, const std::string& replace, bool firstOnly = false, bool caseInsensitive = false);

void strSplit(const std::string& s, const char seperator, std::vector<std::string>& results);


std::string strAfterFirst(const std::string& s, const std::string& ss, const int ofs = 0);

std::string strAfterLast(const std::string& s, const std::string& ss, const int ofs = 0);

std::string intToString(const int i);

std::string floatToString(const float i);

// hex string/buffer conversion

std::string bufferToHexString(const unsigned char* buf, const unsigned int length);

void hexBufferToBuffer(const unsigned char* inbuf, const unsigned int length, unsigned char* outbuf);

void hexStringToBuffer(const std::string& hexstr, unsigned char* outbuf);

std::string hexStringToString(const std::string& hexstr);

std::string stringToHexString(const std::string& str);

std::string strGetToken(const std::string& s, const std::string& token, const std::string& delim = "\"");

std::string strGetTokenValue(const std::string& script, const std::string& token);

std::string strGetTag(const std::string& s, const std::string& tag);

}

#endif

