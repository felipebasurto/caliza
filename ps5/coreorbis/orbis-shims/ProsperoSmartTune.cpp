// PS4 port (AI-assisted): smart tune — an experimental per-game graphics auto-tuner.
//
// The shelf's smart-tune row launches the game with flags/smarttune present; the boot consumes
// the flag and starts this thread. The tuner walks an ordered quality ladder (upscale multiplier
// first, then the cheaper extras at the winning resolution), applying each step live through the
// game ini + gs.ini reload pipeline (OrbisLiveTune/orbis_reload_gs_ini_cpu) and measuring the
// game's speed for a few seconds while the user plays. The best configuration that still holds
// full speed stays written in the game's own settings file, so it persists. needs proper testing
// on hardware: measurement truth depends entirely on the user playing real in-game scenes, not
// sitting in menus, and no part of this has been console-validated.
//
// Honest scope notes:
// - This PCSX2 base has no MSAA setting; "quality" means upscale multiplier, anisotropic
//   filtering and CAS sharpening. Blending accuracy is deliberately not tuned: it changes
//   rendering correctness, not just cost, and mid-game switches can break effects.
// - The greedy ladder assumes cost rises monotonically with upscale multiplier, which is
//   reliable on this GPU. The first failing multiplier prunes the rest of the sweep.
// - Speed samples read PerformanceMetrics from this thread while the GS thread writes them;
//   aligned float reads can't tear on this architecture and a rare racy sample only costs
//   noise in an 8-second window, which the pass rule tolerates.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "pcsx2/Config.h"
#include "pcsx2/GS/GSExtra.h"
#include "pcsx2/PerformanceMetrics.h"

// Tuner status lines for the GS overlay box (GSRenderer.cpp picks them up while active).
std::atomic<int> g_orbis_tune_active{0};
char g_orbis_tune_line1[64] = {};
char g_orbis_tune_line2[64] = {};

extern const char* OrbisGameIniPath();
extern void OrbisOSDLabel(const char* text);

namespace
{

constexpr float kPassAvgSpeed = 98.5f;   // average speed over the window, percent of full speed
constexpr float kDipSpeed = 95.0f;       // a sample below this counts as a dip
constexpr int kMaxDips = 3;              // tolerated dips per 32-sample window (hitches, IO)
constexpr int kMeasureSeconds = 8;
constexpr int kMeasureHz = 4;

struct TuneStep
{
	float upscale = 1.0f;
	int aniso = 0;   // MaxAnisotropy, 0 = off/default
	int cas = 0;     // CASMode, 0 = disabled
	const char* label = "";
};

// Read the whole file; empty when it does not exist.
std::string ReadFile(const char* path)
{
	FILE* f = std::fopen(path, "r");
	if (!f)
		return {};
	std::string out;
	char buf[512];
	while (size_t n = std::fread(buf, 1, sizeof(buf), f))
		out.append(buf, n);
	std::fclose(f);
	return out;
}

// Write raw content through the same temp+rename pattern; used to restore the original file.
bool WriteRaw(const char* path, const std::string& out)
{
	const std::string tmp = std::string(path) + ".smarttune.tmp";
	FILE* f = std::fopen(tmp.c_str(), "w");
	if (!f)
		return false;
	const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
	std::fclose(f);
	if (!ok)
	{
		std::remove(tmp.c_str());
		return false;
	}
	return std::rename(tmp.c_str(), path) == 0;
}

// Rewrite the ini preserving every line that is not one of ours, then append ours. The format
// is the port's flat key = value lines (orbis_apply_ini_file). Written through a temp sibling so
// the live pipeline never reads a half-written file as final (it also double-reads before
// applying, but a torn read could still win the compare). needs proper testing.
bool WriteIniKeys(const char* path, const std::string& original,
	const char* key1, const std::string& val1, const char* key2, const std::string& val2, const char* key3,
	const std::string& val3)
{
	std::string out;
	out.reserve(original.size() + 128);
	bool wrote1 = false, wrote2 = false, wrote3 = false;
	size_t pos = 0;
	while (pos < original.size())
	{
		size_t eol = original.find('\n', pos);
		if (eol == std::string::npos)
			eol = original.size();
		std::string line = original.substr(pos, eol - pos);
		// Identify our keys with or without the section prefix, tolerating spaces.
		const auto is_key = [&line](const char* k) {
			if (!k)
				return false;
			const std::string full = std::string("EmuCore/GS/") + k;
			std::string l = line;
			size_t eq = l.find('=');
			if (eq == std::string::npos)
				return false;
			l = l.substr(0, eq);
			while (!l.empty() && (l.back() == ' ' || l.back() == '\t'))
				l.pop_back();
			size_t b = 0;
			while (b < l.size() && (l[b] == ' ' || l[b] == '\t'))
				b++;
			l = l.substr(b);
			return l == k || l == full;
		};
		if (is_key(key1))
		{
			out += std::string("EmuCore/GS/") + key1 + " = " + val1 + "\n";
			wrote1 = true;
		}
		else if (key2 && is_key(key2))
		{
			out += std::string("EmuCore/GS/") + key2 + " = " + val2 + "\n";
			wrote2 = true;
		}
		else if (key3 && is_key(key3))
		{
			out += std::string("EmuCore/GS/") + key3 + " = " + val3 + "\n";
			wrote3 = true;
		}
		else
		{
			out += line;
			out += '\n';
		}
		pos = eol + 1;
	}
	if (!wrote1)
		out += std::string("EmuCore/GS/") + key1 + " = " + val1 + "\n";
	if (key2 && !wrote2)
		out += std::string("EmuCore/GS/") + key2 + " = " + val2 + "\n";
	if (key3 && !wrote3)
		out += std::string("EmuCore/GS/") + key3 + " = " + val3 + "\n";

	const std::string tmp = std::string(path) + ".smarttune.tmp";
	FILE* f = std::fopen(tmp.c_str(), "w");
	if (!f)
		return false;
	const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
	std::fclose(f);
	if (!ok)
	{
		std::remove(tmp.c_str());
		return false;
	}
	return std::rename(tmp.c_str(), path) == 0;
}

void TuneStatus(const char* line1, const char* line2)
{
	// line2 before line1 before the active flag: a torn frame shows at worst a stale pair.
	snprintf(g_orbis_tune_line2, sizeof(g_orbis_tune_line2), "%s", line2 ? line2 : "");
	snprintf(g_orbis_tune_line1, sizeof(g_orbis_tune_line1), "%s", line1 ? line1 : "");
	g_orbis_tune_active.store(line1 ? 1 : 0, std::memory_order_release);
}

void TuneStatusOff()
{
	g_orbis_tune_active.store(0, std::memory_order_release);
}

struct MeasureResult
{
	float avg = 0.0f;
	int dips = 0;
	bool Passes() const { return avg >= kPassAvgSpeed && dips <= kMaxDips; }
};

MeasureResult Measure(int seconds)
{
	MeasureResult r;
	const int samples = seconds * kMeasureHz;
	float sum = 0.0f;
	int got = 0;
	for (int i = 0; i < samples; i++)
	{
		usleep(1000000 / kMeasureHz);
		const float speed = PerformanceMetrics::GetSpeed();
		if (speed <= 0.0f)
			continue; // no frames yet (loading); skip rather than count as a dip
		sum += speed;
		if (speed < kDipSpeed)
			r.dips++;
		got++;
	}
	r.avg = got ? sum / got : 0.0f;
	return r;
}

// Wait until the live pipeline has applied the wanted upscale multiplier, then settle a little
// longer for the texture-cache purge hitch to pass. False when the apply never lands.
bool WaitApplied(float upscale, int aniso, int cas)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (GSConfig.UpscaleMultiplier == upscale && static_cast<int>(GSConfig.MaxAnisotropy) == aniso &&
			static_cast<int>(GSConfig.CASMode) == cas)
		{
			usleep(1500000);
			return true;
		}
		usleep(250000);
	}
	return false;
}

void TuneThread()
{
	const char* ini = OrbisGameIniPath();
	if (!ini || !*ini)
	{
		printf("[smarttune] no game settings file for this game; tuning impossible\n");
		TuneStatusOff();
		return;
	}
	const std::string original = ReadFile(ini);

	char line1[64], line2[64];
	// Lead-in: the shelf told the user to reach real gameplay. Count down in the overlay so the
	// moment testing starts is never a surprise.
	for (int left = 30; left > 0; left--)
	{
		snprintf(line1, sizeof(line1), "SMART TUNE IN %ds", left);
		TuneStatus(line1, "GET IN GAME, NOT MENUS");
		usleep(1000000);
	}

	// The ladder: upscale sweep first (greedy, pruned at the first failure), then the cheaper
	// extras measured separately at the winning resolution. needs proper testing.
	std::vector<TuneStep> steps = {
		{1.0f, 0, 0, "1x"},
		{2.0f, 0, 0, "2x"},
		{3.0f, 0, 0, "3x"},
		{4.0f, 0, 0, "4x"},
	};

	struct StepResult
	{
		TuneStep step;
		MeasureResult m;
		bool tested = false;
		bool applied = false;
	};
	std::vector<StepResult> results;

	int step_num = 0;
	const int total_estimate = 6;
	float best_upscale = 0.0f;
	bool baseline_passed = false;

	auto apply_step = [&](const TuneStep& s) -> bool {
		char v1[16], v2[16], v3[16];
		snprintf(v1, sizeof(v1), "%g", static_cast<double>(s.upscale));
		snprintf(v2, sizeof(v2), "%d", s.aniso);
		snprintf(v3, sizeof(v3), "%d", s.cas);
		const std::string current = ReadFile(ini);
		if (!WriteIniKeys(ini, current.empty() ? original : current, "upscale_multiplier", v1, "MaxAnisotropy", v2,
				"CASMode", v3))
		{
			printf("[smarttune] cannot write %s; tuning aborted\n", ini);
			TuneStatus("TUNE FAILED", "CANNOT WRITE SETTINGS");
			usleep(4000000);
			return false;
		}
		return WaitApplied(s.upscale, s.aniso, s.cas);
	};

	for (const TuneStep& s : steps)
	{
		step_num++;
		snprintf(line1, sizeof(line1), "TUNE %d/~%d: %s", step_num, total_estimate, s.label);
		TuneStatus(line1, "APPLYING...");
		StepResult r;
		r.step = s;
		if (!apply_step(s))
		{
			if (step_num == 1)
			{
				TuneStatusOff();
				return;
			}
			printf("[smarttune] %s never applied; treating as failed\n", s.label);
			r.tested = true; // counts as failed below
			r.m.avg = 0.0f;
			r.m.dips = 99;
			results.push_back(r);
			break;
		}
		snprintf(line2, sizeof(line2), "MEASURING %s - PLAY!", s.label);
		TuneStatus(line1, line2);
		r.m = Measure(kMeasureSeconds);
		r.tested = true;
		r.applied = true;
		results.push_back(r);
		printf("[smarttune] %s: avg speed %.1f%%, dips %d -> %s\n", s.label, r.m.avg, r.m.dips,
			r.m.Passes() ? "pass" : "fail");
		fflush(stdout);
		if (step_num == 1)
			baseline_passed = r.m.Passes();
		if (!r.m.Passes())
			break; // cost rises monotonically with multiplier; no point testing higher
		if (r.m.Passes())
			best_upscale = s.upscale;
	}

	if (!baseline_passed)
	{
		// Even 1x cannot hold full speed: the game is limited by something upscaling cannot help
		// (usually the EE or VU threads). Restore the original settings and say so honestly.
		WriteRaw(ini, original);
		TuneStatus("TUNE: NO GAIN", "1x ISN'T FULL SPEED");
		printf("[smarttune] baseline 1x fails (avg %.1f%%): game is not GPU-limited; original settings restored\n",
			results.empty() ? 0.0f : results[0].m.avg);
		usleep(5000000);
		TuneStatusOff();
		return;
	}

	// Extras at the winning resolution, one at a time; each must re-pass to be kept. Aniso first
	// (cheapest), then CAS sharpening.
	int best_aniso = 0, best_cas = 0;
	const struct
	{
		int aniso, cas;
		const char* label;
	} extras[] = {
		{8, 0, "ANISO 8x"},
		{8, 1, "ANISO+CAS"},
	};
	for (const auto& ex : extras)
	{
		step_num++;
		TuneStep s;
		s.upscale = best_upscale;
		s.aniso = ex.aniso;
		s.cas = ex.cas;
		s.label = ex.label;
		snprintf(line1, sizeof(line1), "TUNE %d/~%d: %s@%gx", step_num, total_estimate, ex.label, static_cast<double>(best_upscale));
		TuneStatus(line1, "MEASURING - PLAY!");
		if (!apply_step(s))
			break;
		MeasureResult m = Measure(kMeasureSeconds);
		printf("[smarttune] %s@%gx: avg %.1f%%, dips %d -> %s\n", ex.label, static_cast<double>(best_upscale), m.avg, m.dips,
			m.Passes() ? "pass" : "fail");
		fflush(stdout);
		if (m.Passes())
		{
			best_aniso = ex.aniso;
			best_cas = ex.cas;
		}
		else
			break; // extras also rise in cost; stop at the first failure
	}

	// Persist the winner: it is the last thing written, so it is what stays applied too.
	char v1[16], v2[16], v3[16];
	snprintf(v1, sizeof(v1), "%g", static_cast<double>(best_upscale));
	snprintf(v2, sizeof(v2), "%d", best_aniso);
	snprintf(v3, sizeof(v3), "%d", best_cas);
	const std::string current = ReadFile(ini);
	if (!WriteIniKeys(ini, current.empty() ? original : current, "upscale_multiplier", v1, "MaxAnisotropy", v2, "CASMode",
			v3))
	{
		TuneStatus("TUNE FAILED", "CANNOT SAVE WINNER");
		printf("[smarttune] cannot persist the winning config to %s\n", ini);
		usleep(4000000);
		TuneStatusOff();
		return;
	}
	WaitApplied(best_upscale, best_aniso, best_cas);

	snprintf(line1, sizeof(line1), "TUNE DONE: %gx%s%s", static_cast<double>(best_upscale),
		best_aniso ? "+ANISO" : "", best_cas ? "+CAS" : "");
	TuneStatus(line1, "SAVED FOR THIS GAME");
	printf("[smarttune] winner: upscale %gx, aniso %d, cas %d (saved to the game's settings)\n",
		static_cast<double>(best_upscale), best_aniso, best_cas);
	fflush(stdout);
	usleep(6000000);
	TuneStatusOff();
}

} // namespace

// Called once from the boot when flags/smarttune was present. The thread is detached: the
// process is the VM's lifetime and every object it touches is either global or owned by the
// game-ini file itself. needs proper testing.
void OrbisSmartTuneStart()
{
	std::thread([] { TuneThread(); }).detach();
}
