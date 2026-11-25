/*
 #
 #  File        : gmic_string.h
 #
 #  Description : String helpers of the plug-in (formerly Helpers/RFX_StringUtils.h): trimming,
 #                splitting, replacing, the locale-independent number conversions of the G'MIC
 #                command line, and the removal of the markup of G'MIC's texts
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

#ifndef GMIC_STRING_H
#define GMIC_STRING_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gmic_ofx {

// [[nodiscard]]: these return a new value and leave their arguments alone (C++ Core Guidelines F.20,
// https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rf-out)

// str with its ASCII letters in lowercase (whatever the locale; other bytes, e.g. UTF-8, unchanged)
[[nodiscard]] std::string strLowercase(std::string_view str);

// str without the leading and trailing characters of whitespace
[[nodiscard]] std::string strTrim(std::string_view str, std::string_view whitespace = " \t");

// replaces every occurrence of search (not empty) in str, from left to right, without rescanning
// the replacements
void strReplace(std::string& str, std::string_view search, std::string_view replace);

// the fields of s between the separators, trimmed (strTrim), without the empty ones
[[nodiscard]] std::vector<std::string> strSplit(std::string_view s, char separator);

// f as a number of the G'MIC command line: 8 significant digits, '.' as the decimal separator
// (printf's "%.8g" in the "C" locale, e.g. "0.1" and "0.33333334"), whatever the locale
[[nodiscard]] std::string floatToString(float f);

// parse a number written with '.' as the decimal separator, whatever the locale (0 if none), as
// strtod does in the "C" locale. length: if not null, set to the number of characters read (0 if
// there is no number), as std::stod's pos.
[[nodiscard]] double stringToDouble(const std::string& s, std::size_t* length = nullptr);

// The syntax that parseDecimal accepts: digits with an optional sign (SignedInteger), or digits
// with an optional '.' and fraction, then an optional exponent ("2e3", "1.5E-2"), with
// (SignedDecimal) or without (UnsignedDecimal) a sign
enum class DecimalSyntax { SignedInteger, UnsignedDecimal, SignedDecimal };

// s as a number of that syntax, '.' as the decimal point whatever the locale, blanks (" \t\r\n")
// around it allowed; nothing for any other text ("inf", "nan", hexadecimal, other characters) and
// on an overflow (an underflow gives 0). Only this syntax reaches the conversion, so the result
// needs no isnan/isinf test (which -ffinite-math-only folds to false).
[[nodiscard]] std::optional<double> parseDecimal(std::string_view s, DecimalSyntax syntax);

// Append the UTF-8 encoding of the code point cp (up to U+10FFFF; not checked: the callers replace
// invalid code points first if they need to)
void appendUtf8(std::string& out, unsigned long cp);

// s without its markup tags ("<b>", "</i>", "<!-- -->")
[[nodiscard]] std::string strRemoveXmlTags(std::string_view s);

// s without its markup tags (strRemoveXmlTags), with its HTML entities ("&amp;", "&#233;") and
// G'MIC's "\251" replaced with their UTF-8 characters
[[nodiscard]] std::string strXmlToText(std::string_view s);

} // namespace gmic_ofx

#endif
