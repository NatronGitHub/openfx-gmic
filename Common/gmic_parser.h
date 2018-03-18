/*
 #
 #  File        : gmic_parser.h
 #
 #  Description : A self-contained header file with helper functions to
 #                parse the G'MIC standard library file into a param structure
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

#ifndef GMIC_PARSER_H
#define GMIC_PARSER_H

#include <string>
#include <vector>

namespace reduxfx {

struct EffectParameter {
    std::string name;
    std::string minValue;
    std::string maxValue;
    std::string defaultValue;
    std::string text;
    std::string paramType;
};

struct EffectData {
    std::string category;
    std::string name;
    std::string uniqueId;
    std::string command;
    std::string preview_command;
    std::string notes;
    bool multiLayer;
    std::vector<EffectParameter> param;
};


std::string gmic_parse_single(const std::string& content, EffectData& cd);

void gmic_parse_multi(const std::string& content, std::vector<EffectData>* cds, std::vector<std::string>* lines);

}


#endif
