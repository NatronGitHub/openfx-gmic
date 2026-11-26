/*
 #
 #  File        : gmic_visibility.h
 #
 #  Description : The visibility state of a G'MIC filter parameter, as G'MIC-Qt's
 #                AbstractParameter::VisibilityState, shared by the parser (gmic_parser.h) and
 #                the G'MIC call (gmic_plugin_call.h), which reads it from a status
 #
 #  Licenses    : CeCILL-C or CeCILL v2.0, as gmic_parser.cpp
 #
*/

#ifndef GMIC_VISIBILITY_H
#define GMIC_VISIBILITY_H

namespace gmic_ofx {

// the values of G'MIC-Qt's VisibilityState: "_0", "_1", "_2" after a parameter or a status value
enum class ParamVisibility { Hidden = 0, Disabled = 1, Visible = 2 };

}
#endif
