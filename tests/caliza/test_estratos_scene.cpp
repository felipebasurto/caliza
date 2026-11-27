// Caliza Estratos native draw-list and scene tests: opaque backdrop batching, BuildInto reuse and atmosphere caching.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../caliza/native/draw_list.h"
#include "../../caliza/native/estratos_scene.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
int failures = 0;

void Check(bool ok, const char* expr, int line)
{
	if (!ok)
	{
		std::cerr << "FAIL line " << line << ": " << expr << "\n";
		++failures;
	}
}

#define CHECK(x) Check((x), #x, __LINE__)

// A real, tiny (64x96) JPEG, so a decoded cover actually has to survive stb_image (see tests/caliza/test_covers.cpp,
// which uses the same fixture for fe::CoverService::Decode directly).
const unsigned char kTinyJpeg[] = {
	0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01, 0x02, 0x00, 0x00,
	0x01, 0x00, 0x01, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x10, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x30,
	0x2e, 0x33, 0x31, 0x2e, 0x31, 0x30, 0x32, 0x00, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x08, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x08, 0x08, 0x08, 0x07,
	0x07, 0x07, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x08, 0x08,
	0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0c, 0x0c, 0x0b, 0x0b, 0x0e, 0x0e, 0x0e, 0x11,
	0x11, 0x14, 0xff, 0xc4, 0x00, 0x4d, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0x01, 0x01, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x07,
	0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x60, 0x00, 0x40,
	0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00, 0xff, 0xda, 0x00, 0x0c, 0x03,
	0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0x8e, 0x00, 0xdf, 0xd2, 0xc0, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x01, 0xff, 0xd9};

bool ReadBinary(const char* path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
		return false;
	const std::streamsize size = file.tellg();
	if (size <= 0)
		return false;
	file.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(size));
	return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

void TestDrawListBatching()
{
	using namespace caliza;
	DrawList dl;
	CHECK(!dl.HasOpaqueFullscreenBackdrop());
	Texture2D tex;
	tex.width = 4;
	tex.height = 4;
	tex.pixels.assign(4 * 4 * 4, 255);

	dl.AddOpaqueImage(&tex, 0, 0, 1920, 1080);
	dl.SetOpaqueFullscreenBackdrop(true);
	dl.AddQuadPoints(10, 10, 50, 10, 50, 50, 10, 50, {1, 1, 1, 0.5f});
	dl.AddLine(10, 10, 50, 50, 2.0f, {1, 1, 1, 0.8f});
	dl.AddImage(&tex, 0, 0, 1920, 1080);

	CHECK(dl.HasOpaqueFullscreenBackdrop());
	CHECK(dl.Batches().size() == 3);
	CHECK(dl.Batches()[0].opaque && dl.Batches()[0].texture == &tex && dl.Batches()[0].count == 6);
	CHECK(!dl.Batches()[1].opaque && dl.Batches()[1].texture == nullptr && dl.Batches()[1].count == 12);
	CHECK(!dl.Batches()[2].opaque && dl.Batches()[2].texture == &tex && dl.Batches()[2].count == 6);

	dl.Clear();
	CHECK(dl.VertexCount() == 0);
	CHECK(dl.Batches().empty());
	CHECK(!dl.HasOpaqueFullscreenBackdrop());
}

void TestSceneBuildIntoAndAtmosphere()
{
	using namespace caliza;
	std::vector<uint8_t> roboto, prompt;
	CHECK(ReadBinary("bin/resources/fonts/Roboto-Regular.ttf", roboto));
	CHECK(ReadBinary("bin/resources/fonts/promptfont.otf", prompt));
	if (roboto.empty() || prompt.empty())
		return;

	EstratosScene scene;
	CHECK(scene.InitFonts(roboto.data(), roboto.size(), prompt.data(), prompt.size()));

	UiView view;
	view.screen = Screen::Home;
	view.title = "Caliza";
	view.status = "Ready";
	view.prompts = {{PromptGlyph::Cross, "Open"}, {PromptGlyph::Options, "Settings"}};
	for (int i = 0; i < 6; i++)
	{
		UiEntry entry;
		entry.id = "game-" + std::to_string(i);
		entry.label = "Example PlayStation 2 Title " + std::to_string(i);
		entry.detail = "SLUS-2105" + std::to_string(i);
		entry.focused = (i == 0);
		view.items.push_back(std::move(entry));
	}

	DrawList reused;
	reused.Reserve(16384);
	scene.BuildInto(reused, view, 1920, 1080, 1.0);
	CHECK(reused.HasOpaqueFullscreenBackdrop());
	CHECK(!reused.Batches().empty());
	CHECK(reused.Batches().front().opaque);
	const Texture2D* first_backdrop = reused.Batches().front().texture;
	CHECK(first_backdrop != nullptr);
	CHECK(first_backdrop->width == 480 && first_backdrop->height == 270);

	const DrawList built = scene.Build(view, 1920, 1080, 1.0);
	CHECK(built.VertexCount() == reused.VertexCount());
	CHECK(built.Batches().size() == reused.Batches().size());
	CHECK(built.Batches().front().texture == first_backdrop);

	// PauseOverlay uses the opaque solid gradient backdrop without the 3D cube field.
	UiView pause_view = view;
	pause_view.screen = Screen::PauseOverlay;
	scene.BuildInto(reused, pause_view, 1920, 1080, 1.5);
	CHECK(reused.HasOpaqueFullscreenBackdrop());
	CHECK(!reused.Batches().empty() && reused.Batches().front().opaque && reused.Batches().front().texture == nullptr);

	// PauseTriggerSetup renders the preset list + right-hand trade-off / warning panel cleanly.
	UiView setup_view = view;
	setup_view.screen = Screen::PauseTriggerSetup;
	setup_view.title = "Pause menu trigger";
	setup_view.subtitle = "Choose how to open the in-game pause menu.";
	setup_view.items.clear();
	for (int i = 0; i < 6; ++i)
	{
		UiEntry entry;
		entry.id = i == 1 ? "touchpad" : ("preset-" + std::to_string(i));
		entry.label = i == 1 ? "Touchpad click" : "L3 + R3";
		entry.value = i == 1 ? "Replaces Select" : "Recommended";
		entry.detail = i == 1
			? "Touchpad acts as PS2 Select; choosing it means Select must be mapped to another button for games that use it."
			: "Press and release both sticks alone.";
		entry.destructive = (i == 1);
		entry.focused = (i == 1);
		setup_view.items.push_back(std::move(entry));
	}
	scene.BuildInto(reused, setup_view, 1920, 1080, 1.75);
	CHECK(reused.HasOpaqueFullscreenBackdrop());
	CHECK(reused.VertexCount() > 500);

	// Warm steady-state Home frames to check CPU cost and atmosphere texture pointer stability.
	const auto t0 = std::chrono::steady_clock::now();
	constexpr int kFrames = 120;
	for (int f = 0; f < kFrames; f++)
	{
		scene.BuildInto(reused, view, 1920, 1080, 2.0 + f * (1.0 / 60.0));
		CHECK(reused.Batches().front().texture == first_backdrop);
	}
	const double elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("estratos scene test passed (1080p home: %.3f ms/frame avg, %zu verts, %zu batches)\n",
		elapsed_ms / kFrames, reused.VertexCount(), reused.Batches().size());
}

// EstratosScene::CoverFor must draw a resolved cover_path's actual file (what ui_model.cpp sets GameEntry::cover_path
// to once the console's cover lookup/download finds one), not only the fictional procedural poster; a "proc:<id>" key
// (no cover resolved yet) and a key pointing at a file that fails to decode must both still fall back to it rather
// than drawing nothing.
void TestCoverForRealFileAndFallback()
{
	using namespace caliza;
	char tmpl[] = "/tmp/estratos-cover-test-XXXXXX";
	const std::string dir = mkdtemp(tmpl);
	const std::string jpg_path = dir + "/SLUS-20099.jpg";
	const std::string garbage_path = dir + "/SLUS-20098.jpg";
	{
		std::ofstream out(jpg_path, std::ios::binary);
		out.write(reinterpret_cast<const char*>(kTinyJpeg), sizeof(kTinyJpeg));
	}
	{
		std::ofstream out(garbage_path, std::ios::binary);
		out << "not actually a jpeg";
	}

	EstratosScene scene;
	const Texture2D* proc = scene.CoverFor("proc:game-1", "Procedural Title");
	CHECK(proc != nullptr);
	CHECK(proc->width != 0 && proc->height != 0);
	// MakeProceduralCover's fixed canvas size (see caliza/native/procedural_covers.cpp); a decoded cover never
	// happens to match it, so this also confirms the next check is not accidentally comparing two placeholders.
	CHECK(proc->width == 384 && proc->height == 512);

	const Texture2D* real = scene.CoverFor(jpg_path, "Real Cover Title");
	CHECK(real != nullptr);
	CHECK(real != proc);
	CHECK(real->width == 64 && real->height == 96);
	CHECK(real->pixels.size() == static_cast<size_t>(real->width) * real->height * 4);

	const Texture2D* broken = scene.CoverFor(garbage_path, "Broken Cover Title");
	CHECK(broken != nullptr);
	CHECK(broken->width == 384 && broken->height == 512); // undecodable: falls back to the procedural poster

	const Texture2D* missing = scene.CoverFor(dir + "/does-not-exist.jpg", "Missing File Title");
	CHECK(missing != nullptr);
	CHECK(missing->width == 384 && missing->height == 512); // no file at all: also falls back

	// The decoded texture is cached: a second call with the same key returns the exact same object, not a
	// fresh decode (CoverService::Run() polls covers every frame while the shelf is up).
	const Texture2D* real_again = scene.CoverFor(jpg_path, "Real Cover Title");
	CHECK(real_again == real);

	unlink(jpg_path.c_str());
	unlink(garbage_path.c_str());
	rmdir(dir.c_str());
}
} // namespace

int main()
{
	TestDrawListBatching();
	TestSceneBuildIntoAndAtmosphere();
	TestCoverForRealFileAndFallback();
	if (failures == 0)
		return 0;
	std::cerr << failures << " failure(s)\n";
	return 1;
}
