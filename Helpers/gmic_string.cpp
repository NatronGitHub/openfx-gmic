/*
 #
 #  File        : gmic_string.cpp
 #
 #  Description : String helpers of the plug-in: see gmic_string.h
 #
 #  Copyright   : Tobias Fleischer / reduxFX Productions (http://www.reduxfx.com),
 #                openfx-gmic contributors
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

#include "gmic_string.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <locale.h>
#include <stdlib.h>
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <xlocale.h> // strtod_l
#endif

using namespace std;

namespace gmic_ofx {

string strLowercase(string_view str)
{
	string s(str);
	// ASCII letters only, whatever the locale: std::tolower on a negative char (a UTF-8 byte) is
	// undefined behaviour, and in a Latin-1 locale it would change UTF-8 bytes
	transform(s.begin(), s.end(), s.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
	return s;
}

string strTrim(string_view str, string_view whitespace)
{
    const size_t strBegin = str.find_first_not_of(whitespace);
    if (strBegin == string_view::npos) return string(); // no content
    const size_t strEnd = str.find_last_not_of(whitespace);
    return string(str.substr(strBegin, strEnd - strBegin + 1));
}

void strReplace(string& str, string_view search, string_view replace)
{
	assert(!search.empty());
	size_t pos = 0;
	while ((pos = str.find(search, pos)) != string::npos) {
		str.replace(pos, search.size(), replace);
		pos += replace.size();
	}
}

vector<string> strSplit(string_view s, char separator)
{
	vector<string> results;
	size_t begin = 0;
	for (;;) {
		const size_t end = s.find(separator, begin);
		string field = strTrim(s.substr(begin, end == string_view::npos ? string_view::npos : end - begin));
		if (!field.empty()) results.push_back(std::move(field));
		if (end == string_view::npos) return results;
		begin = end + 1;
	}
}

// Number conversions ignore the global C and C++ locales of the host: G'MIC and the G'MIC stdlib
// always use '.' as the decimal separator and no digit grouping (atof and a default stream follow
// LC_NUMERIC or std::locale::global, e.g. "0,5" or "1 000"). Integers: std::to_string ("%d", which
// no locale changes).

string floatToString(float f)
{
    // NaN: "nan", whatever its sign and payload, as G'MIC-Qt passes a removed point. std::to_chars
    // would give "-nan(ind)" or "nan(snan)" (libc++ and MSVC), and the stream it replaces gave
    // "nan" on macOS but "-nan" for a negative NaN with glibc (G'MIC reads them all as NaN).
    if (std::isnan(f)) return "nan";
    // Otherwise, std::to_chars with a precision: printf's "%.8g" in the "C" locale, the text of the
    // stream with setprecision(8) and the classic locale that it replaces ("inf", "-inf" too)
    array<char, 32> buf{}; // "-1.1754944e-38": 14 characters
    const to_chars_result r = to_chars(buf.data(), buf.data() + buf.size(), f, chars_format::general, 8);
    assert(r.ec == errc());
    return string(buf.data(), r.ptr);
}

// strtod in the "C" locale, whatever the current one: strtod_l where available (an istringstream
// is not a drop-in replacement: libc++ fails on "10a", "0.5f", "1e", "inf", "nan", "2e400"...)
#if defined(_WIN32)
// MSVC, and MinGW-w64 with UCRT or a recent msvcrt (the condition of mingw-w64's locale.h)
#  if defined(_MSC_VER) || defined(_UCRT) || (defined(__MSVCRT_VERSION__) && (__MSVCRT_VERSION__ >= 0x0800 || (__MSVCRT_VERSION__ == 0x0600 && defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0600)))
#    define GMIC_HAS_STRTOD_C 1
static double strtodC(const char* str)
{
    static const _locale_t loc = _create_locale(LC_NUMERIC, "C"); // never freed
    return _strtod_l(str, nullptr, loc);
}
#  endif
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__GLIBC__)
#  define GMIC_HAS_STRTOD_C 1
static double strtodC(const char* str)
{
    static const locale_t loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); // never freed
    return strtod_l(str, nullptr, loc);
}
#endif

double stringToDouble(const string& s)
{
    // same result as atof in the "C" locale: leading whitespace skipped, trailing characters
    // ignored, 0 if there is no number; hexadecimal, inf and nan accepted
#ifdef GMIC_HAS_STRTOD_C
    return strtodC(s.c_str());
#else
    // other platforms: use the decimal point of the current C locale instead of '.'
    string t = s;
    const char* dp = localeconv()->decimal_point;
    if (dp && dp[0] && string(dp) != ".") {
        size_t pos = t.find('.');
        if (pos != string::npos) t.replace(pos, 1, dp);
    }
    return strtod(t.c_str(), NULL);
#endif
}

// Append the UTF-8 encoding of the code point cp: U+FFFD if it is not a valid one, or a control
// character other than tab, LF and CR (e.g. from "&#1;").
static void appendUtf8(string& out, unsigned long cp)
{
	const bool control = (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r') || cp == 0x7F;
	if (control || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
	if (cp < 0x80) {
		out += static_cast<char>(cp);
	} else if (cp < 0x800) {
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

// The code point of the entity name e (the text between '&' and ';': "amp", "#233", "#xE9"), or
// -1 if it isn't one we know.
static long entityCodePoint(const string& e)
{
	static const struct { const char* name; long cp; } named[] = {
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
		{ "nbsp", ' ' }, // a plain space, as before
		{ "hearts", 0x2665 },
	};
	if (e.size() >= 2 && e[0] == '#') {
		const bool hex = (e[1] == 'x' || e[1] == 'X');
		const string digits = e.substr(hex ? 2 : 1);
		if (digits.empty() || digits.find_first_not_of(hex ? "0123456789abcdefABCDEF" : "0123456789") != string::npos) return -1;
		return static_cast<long>(std::min(strtoul(digits.c_str(), NULL, hex ? 16 : 10), 0x110000UL)); // > U+10FFFF: invalid
	}
	for (const auto& n : named) {
		if (e == n.name) return n.cp;
	}
	return -1;
}

// Replace the HTML entities of s (&amp; &nbsp; ..., &#233; and &#xE9;) and G'MIC's "\251" with
// their UTF-8 characters, in one pass (so that "&amp;#233;" gives "&#233;"). Unknown entities are
// left as they are.
static string utf8Entities(const string& s)
{
	const size_t maxEntity = 10; // characters between '&' and ';' ("#x10FFFF", "hearts")
	string r;
	r.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		size_t next = i + 1;
		if (s.compare(i, 4, "\\251") == 0) { // the octal escape of the copyright sign
			appendUtf8(r, 0xA9);
			next = i + 4;
		} else if (s[i] == '&') {
			const size_t semi = s.substr(i + 1, maxEntity + 1).find(';');
			const long cp = (semi == string::npos) ? -1 : entityCodePoint(s.substr(i + 1, semi));
			if (cp >= 0) {
				appendUtf8(r, static_cast<unsigned long>(cp));
				next = i + semi + 2;
			} else {
				r += s[i];
			}
		} else {
			r += s[i];
		}
		i = next;
	}
	return r;
}

string strRemoveXmlTags(string_view s, bool replaceEntities)
{
	// a tag is '<' followed by a letter, '/' or '!' (as "<b>", "</i>", "<!-- -->"), up to the next
	// '>'; any other '<' or '>' is text ("a < b", "Anisotropy>0")
	const size_t lastGt = s.rfind('>');
	string r;
	r.reserve(s.size());
	bool inHtml = false;
	for (size_t i = 0; i < s.size(); i++) {
		const char c = s[i];
		if (inHtml) {
			if (c == '>') inHtml = false;
		} else if (c == '<' && lastGt != string_view::npos && i + 1 < lastGt &&
		           ((s[i + 1] >= 'a' && s[i + 1] <= 'z') || (s[i + 1] >= 'A' && s[i + 1] <= 'Z') || s[i + 1] == '/' || s[i + 1] == '!')) {
			inHtml = true;
		} else {
			r += c;
		}
	}
	if (replaceEntities) {
		r = utf8Entities(r);
	}
	return r;
}

} // namespace gmic_ofx
