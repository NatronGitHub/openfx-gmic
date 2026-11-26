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


#ifndef _GMIC_PARSER_H
#define _GMIC_PARSER_H

#include <string>
#include <vector>

#include "gmic_visibility.h"

namespace gmic_ofx {

struct EffectParameter {
	std::string name;
	std::string minValue;
	std::string maxValue;
	std::string defaultValue;
	std::string text;
	std::string paramType;
	// point(): its _removable argument (-1: removable, initially removed; 1: removable; 0: not
	// removable). "pointenabled": the checkbox the parser adds after a removable point (see
	// gmic_parse_single), whose name is the point's with " Enabled".
	int removable;
	// G'MIC-Qt's initial visibility state, from "_0", "_1" or "_2" after the closing delimiter
	// (AbstractParameter::parseText), with the values of its VisibilityState. Visible for a point,
	// whose handle a host doesn't draw when it is hidden or disabled; for a note, link or separator,
	// the one that a neighbour gives it (Propagation). A filter that changes the visibility at run
	// time (EffectData::dynamicVisibility) keeps it too: the plug-in follows its status on Natron
	// only, and makes every parameter visible on other hosts (see describeInContext).
	using Visibility = ParamVisibility;
	Visibility visibility = Visibility::Visible;
	// G'MIC-Qt's VisibilityPropagation, from a '-' (Up) or a '+' or '*' (Down) after the visibility:
	// the parameter gives its visibility to the notes, links and separators right before (after) it,
	// at the start and after each status (FilterParametersWidget::setVisibilityStates). None for
	// the notes, links and separators themselves.
	enum class Propagation { None, Up, Down };
	Propagation propagation = Propagation::None;
	// a parameter of G'MIC's split preview (gui_split_preview: the choice of "Full", "Forward
	// Horizontal"... and its point), which only the filter's preview command uses (see
	// markSplitPreviewParams, issue #184)
	bool splitPreview;
	EffectParameter() : removable(0), splitPreview(false) {}
};

struct EffectData {
	std::string category;
	std::string name;
	std::string uniqueId;
	// the command that the plug-in runs (the preview command of a preview-only filter), empty for a
	// page with no command ("_none_" or empty main and preview commands), which passes Source through
	std::string command;
	std::string preview_command;
	std::string notes;
	bool multiLayer = false;
	// no main command ("_none_" or empty) but a preview command: always render with the preview
	// command, as G'MIC-Qt's preview
	bool previewOnly = false;
	// "_none_" preview command: G'MIC-Qt shows no preview (preview_command is then the main command)
	bool noPreview = false;
	// the main command may need the state left by G'MIC-Qt's preview (_persistent, or the status
	// that fills a value() parameter): run the preview command first, and give the main command
	// its _persistent and status, as G'MIC-Qt's Apply does (issues #52, #206). The _persistent
	// rule needs the command definitions (gmic_parse_multi); per filter, gmic_parse_single applies
	// only the value() rule.
	bool previewFirst = false;
	// G'MIC-Qt's run of the preview command after a change of the parameters, for its status
	// (issue #173): the preview command, or a command that it calls directly, sets a status list
	// with "u" or "status" (see markStatusFilters). The plug-in runs it after each user edit of
	// the filter's parameters, and sets the values (and, on Natron, the visibilities) that the
	// status gives. Never for a filter without a preview run ("_none_" preview, or no command).
	bool statusOnEdit = false;
	// the status may change the visibility of the parameters (a "_0", "_1" or "_2" after a value):
	// a statusOnEdit filter whose preview command, or a command that it calls directly, sets such a
	// status (see markStatusFilters)
	bool dynamicVisibility = false;
	// what the preview factor after the preview command says of the preview at another zoom
	// (gmic_stdlib.gmic's #@gui documentation): no factor; "(N)", a preview that is only right at
	// its own zoom N; "(N)+", a preview that is right at any zoom; "(N)*", a preview that needs the
	// whole image at full size, and zooms itself. Read as G'MIC-Qt does (FiltersModelReader.cpp).
	// The plug-in accepts a render scale only for Accurate filters (issue #179, see
	// pluginSupportsRenderScale). This departs from G'MIC-Qt on purpose for two kinds: G'MIC-Qt
	// counts a filter without a factor as accurate when zoomed (its accurateIfZoomed starts true),
	// and "*" too, whose preview gets the full-size image, which no render scale gives; nothing in
	// a filter without a factor says that it works on a downscaled image.
	enum class PreviewFactor { NoFactor, Fixed, Accurate, FullImage };
	PreviewFactor previewFactor = PreviewFactor::NoFactor;
    std::vector<EffectParameter> param;
};


std::string gmic_parse_single(const std::string& content, EffectData& cd);

void gmic_parse_multi(const std::string& content, std::vector<EffectData>* cds, std::vector<std::string>* lines);

}
#endif
