// Parser test of the point() arguments that give a point's handle (issue #176): the colour,
// G'MIC-Qt's colormap (its order, its restart for each filter, its pseudo-random colours after the
// seventh), burst, and malformed arguments, which are ignored. Built by CI (Linux job) with Helpers/gmic_parser.cpp, gmic_string.cpp and
// gmic_log.cpp only; exits with 1 if a check fails.
#include <cstdio>
#include <string>
#include <vector>

#include "gmic_parser.h"

namespace {

const char* const kFilters =
	"#@gui Point Test A : fx_point_test_a, fx_point_test_a\n"
	"#@gui : A1 = point(10,20)\n"
	"#@gui : A2 = point(10,20,0,1,10,20,30,-0,5%)\n"
	"#@gui : A3 = point(10,20,0,0)\n"
	"#@gui : A4 = point(10,20,0,2,100)\n"
	"#@gui : A5 = point(10,20,0,0,100,50)\n"
	"#@gui : A6 = point(10,20,-1,0,1,2,3,-128,2.5 %)\n"
	"#@gui : A7 = point(50,50)\n"
	"#@gui : A8 = point(50,50)\n"
	"#@gui : A9 = point(50,50)\n"
	"#@gui : A10 = point(50,50)\n"
	"#@gui : A11 = point(50,50)\n"
	"#@gui : A12 = point(50,50)\n"
	"#@gui : A13 = point(50,50,,,,,,,,)\n"
	"fx_point_test_a :\n"
	"#@gui Point Test B : fx_point_test_b, fx_point_test_b\n"
	"#@gui : B1 = point(1,2)\n"
	"#@gui : B2 = point(1,2,0,x,9,9,9)\n"
	"#@gui : B3 = point(1,2,0,1.5,9,8,7)\n"
	"#@gui : B4 = point(1,2,0,1,99999999999)\n"
	"#@gui : B5 = point(1,2,0,+1,+9,1.5,-1,256)\n"
	"#@gui : B6 = point(1,2,0,0,300,-5,+-2)\n"
	"fx_point_test_b :\n";

struct Expected
{
	const char* filter;
	const char* name;
	gmic_ofx::PointHandle handle;
};

// the colour and burst G'MIC-Qt gives these points (PointParameter::initFromText); the
// pseudo-random colours of A12 and A13 are PointParameter::randomChannel's (seed 12345)
const Expected kExpected[] = {
	{ "Point Test A", "A1", { { 255, 255, 255 }, false } },
	{ "Point Test A", "A2", { { 10, 20, 30 }, true } }, // the alpha and radius are not used
	{ "Point Test A", "A3", { { 255, 0, 0 }, false } }, // A2 has a colour: the next one
	{ "Point Test A", "A4", { { 100, 100, 100 }, true } }, // burst 2, a gray
	{ "Point Test A", "A5", { { 100, 50, 0 }, false } }, // R,G: blue 0
	{ "Point Test A", "A6", { { 1, 2, 3 }, false } },
	{ "Point Test A", "A7", { { 0, 255, 0 }, false } },
	{ "Point Test A", "A8", { { 64, 64, 255 }, false } },
	{ "Point Test A", "A9", { { 0, 255, 255 }, false } },
	{ "Point Test A", "A10", { { 255, 0, 255 }, false } },
	{ "Point Test A", "A11", { { 255, 255, 0 }, false } },
	{ "Point Test A", "A12", { { 0, 220, 4 }, false } },
	{ "Point Test A", "A13", { { 101, 170, 31 }, false } }, // empty arguments skipped
	{ "Point Test B", "B1", { { 255, 255, 255 }, false } }, // the colormap restarts
	{ "Point Test B", "B2", { { 9, 9, 9 }, false } }, // a malformed burst is ignored
	{ "Point Test B", "B3", { { 9, 8, 7 }, false } }, // so is 1.5
	{ "Point Test B", "B4", { { 255, 0, 0 }, true } }, // R out of range: ignored, the colormap
	{ "Point Test B", "B5", { { 9, 9, 0 }, true } }, // +1, +9; G 1.5 ignored; B -1 clamped
	{ "Point Test B", "B6", { { 255, 0, 0 }, false } }, // R 300, G -5 clamped; B +-2 ignored (blue 0 after R,G)
};

bool same(const gmic_ofx::PointHandle& a, const gmic_ofx::PointHandle& b)
{
	for (int c = 0; c < 3; c++) {
		if (a.color[c] != b.color[c]) return false;
	}
	return a.burst == b.burst;
}

void print(const char* what, const gmic_ofx::PointHandle& h)
{
	std::printf("  %s: rgb %d,%d,%d burst %d\n", what, h.color[0], h.color[1], h.color[2], h.burst);
}

} // namespace

int main()
{
	std::vector<gmic_ofx::EffectData> filters;
	std::vector<std::string> lines;
	gmic_ofx::gmic_parse_multi(kFilters, &filters, &lines);
	int failures = 0;
	for (const Expected& e : kExpected) {
		const gmic_ofx::EffectParameter* found = nullptr;
		for (const gmic_ofx::EffectData& f : filters) {
			if (f.name.find(e.filter) == std::string::npos) continue;
			for (const gmic_ofx::EffectParameter& p : f.param) {
				if (p.paramType == "point" && p.name == e.name) found = &p;
			}
		}
		if (!found) {
			std::printf("FAIL %s / %s: no such point\n", e.filter, e.name);
			++failures;
		} else if (!same(found->pointHandle, e.handle)) {
			std::printf("FAIL %s / %s\n", e.filter, e.name);
			print("expected", e.handle);
			print("got     ", found->pointHandle);
			++failures;
		}
	}
	const int total = (int)(sizeof(kExpected) / sizeof(kExpected[0]));
	std::printf("point arguments: %d of %d points right\n", total - failures, total);
	return failures ? 1 : 0;
}
