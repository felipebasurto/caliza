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
} // namespace

int main()
{
	TestDrawListBatching();
	TestSceneBuildIntoAndAtmosphere();
	if (failures == 0)
		return 0;
	std::cerr << failures << " failure(s)\n";
	return 1;
}
