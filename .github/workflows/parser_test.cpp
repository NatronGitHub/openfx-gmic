// Parser test (CI, Linux job): the rules of gmic_parser.cpp that read the command definitions
// (previewFirst, the hidden filters), on cases that the G'MIC stdlib doesn't have, so that a
// change to these rules can't go unnoticed. Exits 1 if a case fails.
//
//   c++ -std=c++17 -IHelpers .github/workflows/parser_test.cpp Helpers/gmic_parser.cpp \
//     Helpers/gmic_string.cpp Helpers/gmic_log.cpp -o parser_test && ./parser_test
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
	"  _persistent=1\n";

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
	};
	int failed = 0;
	for (const Case& c : cases) {
		const gmic_ofx::EffectData* const d = find(filters, c.id);
		const bool ok = (d != nullptr) == c.shown && (!d || d->previewFirst == c.previewFirst);
		if (!ok) ++failed;
		std::printf("%s %s: %s (expected %s%s)\n", ok ? "ok" : "FAIL", c.id, d ? (d->previewFirst ? "shown, previewFirst" : "shown") : "hidden",
			c.shown ? "shown" : "hidden", c.previewFirst ? ", previewFirst" : "");
	}
	std::printf("%d of %zu cases failed\n", failed, sizeof(cases) / sizeof(cases[0]));
	return failed ? 1 : 0;
}
