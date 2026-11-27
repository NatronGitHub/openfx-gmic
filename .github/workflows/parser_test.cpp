// Parser test (CI, Linux job): the rules of gmic_parser.cpp that read the command definitions
// (previewFirst, the hidden filters), on cases that the G'MIC stdlib doesn't have, so that a
// change to these rules can't go unnoticed; and the value() placeholder rule in a locale with a
// decimal comma (fr_FR or de_DE, "skipped" if the system has none). Exits 1 if a case fails.
// Built and run by ci.yml with gmic_parser.cpp, gmic_string.cpp and gmic_log.cpp (see there).
#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

#include "gmic_parser.h"

namespace {

// One filter per case, each with a parameter; the command definitions after them
const char* const kFilters =
	"#@gui Test Swallow : fx_ta, fx_ta\n"
	"#@gui : A = float(1,0,2)\n"
	"#@gui Test Persist : fx_tb, fx_tb\n"
	"#@gui : B = float(1,0,2)\n"
	"#@gui Test Out Arg : fx_tc, fx_tc\n"
	"#@gui : C = float(1,0,2)\n"
	"#@gui Test Callee Arg : fx_td, fx_td\n"
	"#@gui : D = float(1,0,2)\n"
	"#@gui Test Callee : fx_te, fx_te\n"
	"#@gui : E = float(1,0,2)\n"
	"#@gui Test Continued : fx_tf, fx_tf\n"
	"#@gui : F = float(1,0,2)\n"
	"#@gui Test Joined : fx_tg, fx_tg\n"
	"#@gui : G = float(1,0,2)\n"
	"#@gui Test Cli In Continued : fx_tj, fx_tj\n"
	"#@gui : J = float(1,0,2)\n"
	"#@gui Test Split Def : fx_tk, fx_tk\n"
	"#@gui : K = float(1,0,2)\n"
	"#@gui Test Split Def B : fx_tl, fx_tl\n"
	"#@gui : L = float(1,0,2)\n"
	"#@gui Test Dynamic Arg : fx_th, fx_th\n"
	"#@gui : H = float(1,0,2)\n"
	"#@gui Test Dynamic Minus : fx_ti, fx_ti\n"
	"#@gui : I = float(1,0,2)\n"
	// a comment that ends with '\' doesn't continue the line (G'MIC drops the comment first)
	"fx_ta :\n"
	"  skip $* # see C:\\\n"
	"fx_tb :\n"
	"  _persistent=1\n"
	// "ox,oy=..." is an assignment, not the output command ox
	"fx_tc :\n"
	"  ox,oy=0,0 ${-path_rc}x.txt\n"
	// "_fx_td_helper$1" is not a call of _fx_td_helper
	"fx_td :\n"
	"  _fx_td_helper$1\n"
	"_fx_td_helper :\n"
	"  o ${-path_rc}foo.txt\n"
	// a real call of a helper that writes into G'MIC's folder: hidden
	"fx_te :\n"
	"  _fx_te_helper[0]\n"
	"_fx_te_helper :\n"
	"  o ${-path_rc}foo.txt\n"
	// an odd number of '\' continues the line, over a blank line: the definition of fx_tg is
	// part of fx_tf's code, which reads _persistent on the line after it
	"fx_tf :\n"
	"  skip $* \\\n"
	"\n"
	"fx_tg :\n"
	"  _persistent=1\n"
	// a "#@cli" line between the parts of a continued line still documents its command, whose
	// file isn't then one that the filter writes itself: shown
	"fx_tj :\n"
	"  _tj_helper \\\n"
	"#@cli _tj_helper : documented\n"
	"  [0]\n"
	"_tj_helper :\n"
	"  o ${-path_rc}x.txt\n"
	// a definition is on one physical line: "fx_tl \" then ": _persistent=1" is a line of fx_tk
	"fx_tk :\n"
	"  blur 1\n"
	"fx_tl \\\n"
	"  : _persistent=1\n"
	// the status rules (markStatusFilters): _th_helper sets a status with a visibility, so a filter
	// whose preview command calls it has a dynamic visibility; "_th_helper$1" is not a call,
	// "-_th_helper" is
	"_th_helper :\n"
	"  u \"{$1}_1\"\n"
	"fx_th :\n"
	"  _th_helper$1\n"
	"fx_ti :\n"
	"  -_th_helper\n";

// value(0.5) is a number, not a placeholder that the preview's status fills: no previewFirst,
// whatever the locale's decimal point
const char* const kValueFilter =
	"#@gui Test Value : fx_tv, fx_tv_preview\n"
	"#@gui : V = value(0.5)\n"
	"#@gui : A = float(1,0,2)\n"
	"fx_tv :\n"
	"  blur $2\n"
	"fx_tv_preview :\n"
	"  fx_tv $*\n";

// The rules on the #@gui lines alone (issue #204: no rule names a filter, an author or a folder
// other than G'MIC's Sequences convention): the pages of an About folder are filters of a folder
// like any other; a filter with "[interactive]" in any of its lines is left out, also in a Various
// folder; the Sequences folders are left out (issue #255); the page that G'MIC's update_deprecate
// adds to an outdated update file is shown, as in G'MIC-Qt; names are not abbreviated.
const char* const kGuiFilters =
	"#@gui <i>About</i>\n"
	"#@gui Test About Page : _none_, _none_\n"
	"#@gui : _ = note(\"text\")\n"
	"#@gui _<i>Various</i>\n"
	"#@gui Test Various : fx_tw, fx_tw\n"
	"#@gui : Mode = choice(\"Off\",\"New Curves [interactive]\")\n"
	"#@gui Test inverse Name : fx_tx, fx_tx\n"
	"#@gui : X = float(1,0,2)\n"
	"#@gui _<b>Sequences</b>\n"
	"#@gui Test Sequence : fx_ty, fx_ty\n"
	"#@gui : Frames = int(10,2,100)\n"
	"#@gui ________\n"
	"#@gui !<b><i>[ New Version Available! ]</i></b> : _none_, _none_\n"
	"#@gui : note = note{\"A new version is available\"}\n";

const gmic_ofx::EffectData* find(const std::vector<gmic_ofx::Filter>& filters, const std::string& id)
{
	for (const gmic_ofx::Filter& f : filters) {
		if (f.data.uniqueId == id) return &f.data;
	}
	return nullptr;
}

} // namespace

int main()
{
	const std::vector<gmic_ofx::Filter> filters = gmic_ofx::gmic_parse_multi(kFilters);
	struct Case
	{
		const char* id;
		bool shown;
		bool previewFirst;
	};
	const Case cases[] = {
		{ "eu.gmic.TestSwallow", true, false },
		{ "eu.gmic.TestPersist", true, true },
		{ "eu.gmic.TestOutArg", true, false },
		{ "eu.gmic.TestCalleeArg", true, false },
		{ "eu.gmic.TestCallee", false, false },
		{ "eu.gmic.TestContinued", true, true },
		{ "eu.gmic.TestJoined", true, false },
		{ "eu.gmic.TestCliInContinued", true, false },
		{ "eu.gmic.TestSplitDef", true, true },
		{ "eu.gmic.TestSplitDefB", true, false },
	};
	int failed = 0;
	// the calls of a command that sets a status with a visibility
	const struct { const char* id; bool dynamicVisibility; } dynamics[] = {
		{ "eu.gmic.TestDynamicArg", false },
		{ "eu.gmic.TestDynamicMinus", true },
	};
	for (const auto& c : dynamics) {
		const gmic_ofx::EffectData* const d = find(filters, c.id);
		const bool ok = d && d->dynamicVisibility == c.dynamicVisibility && d->statusOnEdit == c.dynamicVisibility;
		if (!ok) ++failed;
		std::printf("%s %s: %s (expected %s)\n", ok ? "ok" : "FAIL", c.id,
			!d ? "hidden" : d->dynamicVisibility ? "dynamic visibility" : "static", c.dynamicVisibility ? "dynamic visibility" : "static");
	}
	// the #@gui rules: id, then the name and category (nullptr: left out)
	const std::vector<gmic_ofx::Filter> guiFilters = gmic_ofx::gmic_parse_multi(kGuiFilters);
	const struct { const char* id; const char* name; const char* category; } guiCases[] = {
		{ "eu.gmic.TestAboutPage", "G'MIC Test About Page", "G'MIC/About" },
		{ "eu.gmic.TestVarious", nullptr, nullptr },
		{ "eu.gmic.TestinverseName", "G'MIC Test inverse Name", "G'MIC/Various" },
		{ "eu.gmic.TestSequence", nullptr, nullptr },
		{ "eu.gmic.NewVersionAvailable", "G'MIC New Version Available!", "G'MIC" },
	};
	for (const auto& c : guiCases) {
		const gmic_ofx::EffectData* const d = find(guiFilters, c.id);
		const bool ok = c.name ? d && d->name == c.name && d->category == c.category : !d;
		if (!ok) ++failed;
		std::printf("%s %s: %s%s%s (expected %s%s%s)\n", ok ? "ok" : "FAIL", c.id, d ? ("\"" + d->name + "\"").c_str() : "hidden",
			d ? " in " : "", d ? d->category.c_str() : "", c.name ? ("\"" + std::string(c.name) + "\"").c_str() : "hidden",
			c.name ? " in " : "", c.category ? c.category : "");
	}
	// the locale: the first one with a decimal comma that the system has
	const char* locale = nullptr;
	for (const char* l : { "fr_FR.UTF-8", "fr_FR.utf8", "de_DE.UTF-8", "de_DE.utf8" }) {
		if (std::setlocale(LC_NUMERIC, l)) {
			locale = l;
			break;
		}
	}
	if (locale) {
		const std::vector<gmic_ofx::Filter> valueFilters = gmic_ofx::gmic_parse_multi(kValueFilter);
		const gmic_ofx::EffectData* const d = find(valueFilters, "eu.gmic.TestValue");
		const bool ok = d && !d->previewFirst;
		if (!ok) ++failed;
		std::printf("%s eu.gmic.TestValue in %s: %s (expected no previewFirst)\n", ok ? "ok" : "FAIL", locale,
			!d ? "hidden" : d->previewFirst ? "previewFirst" : "no previewFirst");
		std::setlocale(LC_NUMERIC, "C");
	} else {
		std::printf("skipped eu.gmic.TestValue: no fr_FR or de_DE locale\n");
	}
	for (const Case& c : cases) {
		const gmic_ofx::EffectData* const d = find(filters, c.id);
		const bool ok = (d != nullptr) == c.shown && (!d || d->previewFirst == c.previewFirst);
		if (!ok) ++failed;
		std::printf("%s %s: %s (expected %s%s)\n", ok ? "ok" : "FAIL", c.id, d ? (d->previewFirst ? "shown, previewFirst" : "shown") : "hidden",
			c.shown ? "shown" : "hidden", c.previewFirst ? ", previewFirst" : "");
	}
	std::printf("%d failed\n", failed);
	return failed ? 1 : 0;
}
