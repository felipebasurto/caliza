// Caliza PS4 core: console-facing backend adapter over storage, settings and platform seams.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console_backend.h"
#include "settings_projection.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace caliza
{
namespace
{
struct Execution
{
	bool async = false;
	bool succeeded = false;
	std::string message;
};

Execution Success(std::string message = {})
{
	Execution out;
	out.succeeded = true;
	out.message = std::move(message);
	return out;
}

Execution Failure(std::string message)
{
	Execution out;
	out.message = std::move(message);
	return out;
}

Execution Unsupported(const std::string& what)
{
	return Failure(what + " is not supported by this build's console adapter");
}

bool IsScanCommand(CommandType type)
{
	return type == CommandType::RescanAll || type == CommandType::ScanSource || type == CommandType::CancelScan;
}

std::string LowerCopy(std::string value)
{
	for (char& ch : value)
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	return value;
}

std::string BaseName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string StemOf(const std::string& file)
{
	const size_t dot = file.find_last_of('.');
	return dot == std::string::npos ? file : file.substr(0, dot);
}

bool TruthyText(const std::string& value)
{
	return settings::Truthy(value);
}

std::string PercentEncode(const std::string& value)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string out;
	for (const unsigned char ch : value)
	{
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')
			out.push_back(static_cast<char>(ch));
		else
		{
			out.push_back('%');
			out.push_back(kHex[ch >> 4]);
			out.push_back(kHex[ch & 15]);
		}
	}
	return out;
}

bool PercentDecode(const std::string& encoded, std::string* out)
{
	out->clear();
	for (size_t i = 0; i < encoded.size(); i++)
	{
		if (encoded[i] != '%')
		{
			out->push_back(encoded[i]);
			continue;
		}
		if (i + 2 >= encoded.size() || !std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) ||
			!std::isxdigit(static_cast<unsigned char>(encoded[i + 2])))
			return false;
		const auto hex = [](char ch) -> unsigned {
			if (ch >= '0' && ch <= '9')
				return static_cast<unsigned>(ch - '0');
			if (ch >= 'a' && ch <= 'f')
				return static_cast<unsigned>(ch - 'a' + 10);
			return static_cast<unsigned>(ch - 'A' + 10);
		};
		out->push_back(static_cast<char>((hex(encoded[i + 1]) << 4) | hex(encoded[i + 2])));
		i += 2;
	}
	return true;
}

settings::DocumentState CanonicalDocument(const settings::DocumentState& input)
{
	settings::DocumentState out;
	for (const auto& kv : input.values)
	{
		const std::string key = settings::MigrateLegacyKey(kv.first);
		auto it = std::find_if(out.values.begin(), out.values.end(), [&](const auto& existing) { return existing.first == key; });
		if (it == out.values.end())
			out.values.emplace_back(key, kv.second);
		else
			it->second = kv.second;
	}
	out.patches = input.patches;
	out.cheats = input.cheats;
	return out;
}

bool IsAccessibilityControl(const settings::SettingDef& def)
{
	return def.group_id == "accessibility";
}

unsigned NearestPercentStep(const std::string& value)
{
	char* end = nullptr;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str())
		return 100;
	const long clamped = std::max<long>(100, std::min<long>(200, parsed));
	return static_cast<unsigned>(((clamped + 12) / 25) * 25);
}
} // namespace

class ConsoleBackend::Impl
{
public:
	Impl(ConsoleBackendOptions options, std::unique_ptr<Backend> storage_backend)
		: m_options(std::move(options)), m_storage(std::move(storage_backend))
	{
		if (!m_storage)
			m_storage = CreateBackend(m_options.storage);
		RefreshProjection(true);
		SyncFolderProjection();
		m_worker = std::thread([this] { WorkerMain(); });
		if (m_options.scan_on_create)
		{
			Command scan;
			scan.type = CommandType::RescanAll;
			Post(scan);
		}
	}

	~Impl()
	{
		m_stop.store(true);
		m_cv.notify_all();
		if (m_worker.joinable())
			m_worker.join();
	}

	CommandId Post(const Command& command)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const CommandId id = m_next_command++;
		m_queue.emplace_back(id, command);
		EmitLocked(id, command.type, CommandStatus::Queued, "queued");
		m_cv.notify_one();
		return id;
	}

	std::vector<CommandEvent> PollEvents()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		std::vector<CommandEvent> out;
		out.swap(m_events);
		return out;
	}

	Snapshot GetSnapshot()
	{
		Snapshot out = m_storage->GetSnapshot();
		bool refresh_due = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			MaybeRefreshProjectionLocked();
			refresh_due = m_projection_due;
		}
		if (refresh_due)
			RefreshProjection(false);
		ConsoleOverlay overlay;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			overlay = m_overlay;
		}
		out.product = "Caliza PS4";
		if (!m_options.build_tag.empty())
			out.build_tag = m_options.build_tag;
		out.bios = overlay.bios;
		out.vm = overlay.vm;
		out.settings = overlay.settings;
		out.controls = overlay.controls;
		out.memory_cards = overlay.memory_cards;
		out.accessibility = overlay.accessibility;
		out.continue_game_id = overlay.continue_game_id;
		out.operations = overlay.operations;
		if (!overlay.projection_problem.empty())
			out.diagnostics.console_status = overlay.projection_problem;
		else if (m_options.hooks.web_panel_url)
			out.diagnostics.console_status = m_options.hooks.web_panel_url();
		uint64_t high_water = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			high_water = m_event_revision_high_water;
		}
		out.revision = std::max(out.revision + overlay.revision, high_water);
		return out;
	}

	FolderListing BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit)
	{
		return m_storage->BrowseFolder(path, filter, offset, limit);
	}

	bool ConsumeLaunchRequest(ConsoleLaunchRequest* out)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_launch_pending)
			return false;
		if (out)
			*out = m_launch_request;
		m_launch_pending = false;
		m_launch_request = {};
		return true;
	}

private:
	struct ConsoleOverlay
	{
		BiosStatus bios;
		VmStatus vm;
		std::vector<SettingCategory> settings;
		std::vector<ControlBinding> controls;
		std::vector<MemoryCardInfo> memory_cards;
		AccessibilityState accessibility;
		std::string continue_game_id;
		std::string projection_problem;
		std::vector<OperationStatus> operations;
		uint64_t revision = 1;
		bool valid = false;
		std::chrono::steady_clock::time_point refreshed_at{};
	};

	struct QueuedCommand
	{
		QueuedCommand() = default;
		QueuedCommand(CommandId command_id, const Command& queued_command) : id(command_id), command(queued_command) {}
		CommandId id = 0;
		Command command;
	};

	void EmitLocked(CommandId id, CommandType type, CommandStatus status, const std::string& message)
	{
		if (status == CommandStatus::Queued)
		{
			for (CommandEvent& event : m_events)
			{
				if (event.command == id && event.status == CommandStatus::Queued)
				{
					event.message = message;
					return;
				}
			}
		}
		CommandEvent event;
		event.command = id;
		event.type = type;
		event.status = status;
		event.revision = ++m_event_revision_high_water;
		event.message = message;
		m_events.push_back(std::move(event));
	}

	void Emit(CommandId id, CommandType type, CommandStatus status, const std::string& message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		EmitLocked(id, type, status, message);
	}

	void WorkerMain()
	{
		while (!m_stop.load())
		{
			QueuedCommand work;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait_for(lock, std::chrono::milliseconds(20), [&] { return m_stop.load() || !m_queue.empty(); });
				if (m_stop.load())
					break;
				if (!m_queue.empty())
				{
					work = m_queue.front();
					m_queue.pop_front();
				}
			}
			DrainInnerEvents(0);
			if (work.id == 0)
				continue;
			Emit(work.id, work.command.type, CommandStatus::Running, "running");
			Execution execution = ExecuteCommand(work.id, work.command);
			if (execution.async)
				continue;
			if (execution.succeeded)
				RefreshProjection(true);
			Emit(work.id, work.command.type, execution.succeeded ? CommandStatus::Succeeded : CommandStatus::Failed, execution.message);
		}
	}

	// Returns the terminal execution for `target` when one was observed. Other tracked scan events are
	// re-published under their outer ids here, so inner command ids never leak through the adapter boundary.
	bool ProcessInnerEvents(const std::vector<CommandEvent>& events, CommandId target, Execution* terminal, bool* saw_running = nullptr)
	{
		bool found = false;
		for (const CommandEvent& inner : events)
		{
			const bool terminal_event = inner.status == CommandStatus::Succeeded || inner.status == CommandStatus::Failed || inner.status == CommandStatus::Cancelled;
			if (target != 0 && inner.command == target)
			{
				if (inner.status == CommandStatus::Running && saw_running)
					*saw_running = true;
				if (terminal_event)
				{
					terminal->succeeded = inner.status == CommandStatus::Succeeded;
					terminal->message = inner.message;
					found = true;
				}
				continue;
			}
			if (!terminal_event)
				continue;
			CommandId outer = 0;
			CommandType outer_type = CommandType::RescanAll;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto it = m_inner_to_outer.find(inner.command);
				if (it != m_inner_to_outer.end())
				{
					outer = it->second.first;
					outer_type = it->second.second;
					m_inner_to_outer.erase(it);
				}
			}
			if (outer != 0)
				Emit(outer, outer_type, inner.status, inner.message);
		}
		return found;
	}

	void DrainInnerEvents(CommandId target, Execution* terminal = nullptr)
	{
		Execution ignored;
		if (!terminal)
			terminal = &ignored;
		ProcessInnerEvents(m_storage->PollEvents(), target, terminal);
	}

	Execution DelegateStorageCommand(CommandId outer_id, const Command& command)
	{
		const CommandId inner = m_storage->Post(command);
		const bool scan = IsScanCommand(command.type);
		const auto started = std::chrono::steady_clock::now();
		bool saw_running = false;
		for (;;)
		{
			Execution terminal;
			if (ProcessInnerEvents(m_storage->PollEvents(), inner, &terminal, &saw_running))
				return terminal;
			if (!scan)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			if (!saw_running)
			{
				// The storage backend publishes Running before dispatching an asynchronous scan. A validation
				// failure can skip Running and reach a terminal state first, so this wait is part of the handoff.
				// 250 ms is generous for local command-thread scheduling without delaying cancellation input.
				if (std::chrono::steady_clock::now() - started < std::chrono::milliseconds(250))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
					continue;
				}
			}
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_inner_to_outer[inner] = {outer_id, command.type};
			}
			Execution out;
			out.async = true;
			return out;
		}
	}

	bool PostInternalStorageCommandAndWait(const Command& command, std::string* error)
	{
		const CommandId inner = m_storage->Post(command);
		for (;;)
		{
			Execution terminal;
			if (ProcessInnerEvents(m_storage->PollEvents(), inner, &terminal))
			{
				if (!terminal.succeeded && error)
					*error = terminal.message;
				return terminal.succeeded;
			}
			if (m_stop.load())
			{
				if (error)
					*error = "console backend is shutting down";
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}

	Execution ExecuteCommand(CommandId outer_id, const Command& command)
	{
		switch (command.type)
		{
			case CommandType::RescanAll:
			case CommandType::ScanSource:
			case CommandType::CancelScan:
				return DelegateStorageCommand(outer_id, command);
			case CommandType::AddLibraryFolder:
				return AddLibraryFolder(command);
			case CommandType::RemoveLibraryFolder:
				return RemoveLibraryFolder(command);
			case CommandType::RelocateLibraryFolder:
				return RelocateLibraryFolder(command);
			case CommandType::SetBiosFolder:
				return SetBiosFolder(command);
			case CommandType::SelectBios:
				return SelectBios(command);
			case CommandType::SetSetting:
				return SetSetting(command);
			case CommandType::UnsetSetting:
			case CommandType::ResetSetting:
				return ResetSetting(command);
			case CommandType::ResetSettingsScope:
				return ResetSettingsScope(command);
			case CommandType::ApplyRecommendedSettings:
				return ApplyRecommended(command);
			case CommandType::CreateMemoryCard:
				return CreateMemoryCard(command);
			case CommandType::AssignMemoryCard:
				return AssignMemoryCard(command);
			case CommandType::SetControlBinding:
				return SetControlBinding(command);
			case CommandType::ResetControlBindings:
				return ResetControlBindings(command);
			case CommandType::SetAccessibility:
				return SetAccessibility(command);
			case CommandType::FetchOnlinePatches:
				return FetchOnlinePatches(command);
			case CommandType::RetryTexturePackCatalog:
				return RetryTextureCatalog(command);
			case CommandType::DownloadTexturePack:
				return DownloadTexturePack(command);
			case CommandType::CancelTexturePack:
				return CancelTexturePack(command);
			case CommandType::DeleteTexturePack:
				return DeleteTexturePack(command);
			case CommandType::LaunchGame:
				return LaunchGame(command);
			case CommandType::LaunchSystemMenu:
				return LaunchSystemMenu(command);
			case CommandType::LaunchSmartTune:
			{
				Command smart = command;
				smart.type = CommandType::LaunchGame;
				smart.value = "mode=smart_tune";
				return LaunchGame(smart);
			}
			case CommandType::SaveState:
			case CommandType::LoadState:
			case CommandType::ToggleFastForward:
			case CommandType::PauseVm:
			case CommandType::ResumeVm:
			case CommandType::ReturnToLibrary:
			case CommandType::ChangeDisc:
				return RuntimeCommand(command);
			case CommandType::GenerateReport:
				return Unsupported("native report generation; use the existing diagnostics/web service");
			default:
				return Unsupported("this command");
		}
	}

	bool ReadSettingsDocument(const std::string& path, settings::DocumentState* out, std::string* error)
	{
		std::string text;
		bool ok = false;
		std::string read_error;
		if (m_options.hooks.read_text_file)
			ok = m_options.hooks.read_text_file(path, &text, &read_error);
		else
			ok = storage::fs::ReadFile(path, &text, &read_error);
		if (!ok)
		{
			// A missing per-game file is an empty document. A read error is still non-fatal for projection: the
			// schema defaults remain visible and the failed mutation path reports the real write error.
			if (error)
				*error = read_error;
			text.clear();
		}
		*out = CanonicalDocument(settings::ParseSettingsDocument(text));
		return true;
	}

	bool EditSettings(const std::string& path, const std::vector<settings::Mutation>& mutations, std::string* summary, std::string* error)
	{
		if (!m_options.hooks.edit_settings_file)
		{
			*error = "the platform settings writer is not bound";
			return false;
		}
		if (!m_options.hooks.edit_settings_file(path, mutations, summary, error))
			return false;
		SyncFolderProjection();
		RefreshProjection(true);
		return true;
	}

	std::string GlobalFolderList()
	{
		settings::DocumentState doc;
		std::string error;
		ReadSettingsDocument(m_options.global_settings_path, &doc, &error);
		if (const std::string* value = settings::FindValue(doc, "Caliza/GameFolders"))
			return *value;
		return {};
	}

	Execution WriteFolderList(const std::vector<std::string>& folders, const std::string& success_message)
	{
		settings::Mutation mutation;
		mutation.kind = settings::Mutation::Kind::Set;
		mutation.key = "Caliza/GameFolders";
		mutation.value = settings::JoinFolderList(folders);
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, {mutation}, &summary, &error))
			return Failure(error);
		return Success(success_message + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution AddLibraryFolder(const Command& command)
	{
		if (command.path.empty())
			return Failure("a folder path is required");
		std::vector<std::string> folders = settings::SplitFolderList(GlobalFolderList());
		const std::string normalized = settings::SplitFolderList(command.path).empty() ? command.path : settings::SplitFolderList(command.path).front();
		if (std::find(folders.begin(), folders.end(), normalized) == folders.end())
			folders.push_back(normalized);
		return WriteFolderList(folders, "library folder linked");
	}

	std::string SourcePathForId(const std::string& id) const
	{
		const Snapshot snapshot = m_storage->GetSnapshot();
		for (const LibrarySource& source : snapshot.sources)
		{
			if (source.id == id || source.path == id)
				return source.path;
		}
		return id;
	}

	Execution RemoveLibraryFolder(const Command& command)
	{
		const std::string path = SourcePathForId(!command.id.empty() ? command.id : command.path);
		std::vector<std::string> folders = settings::SplitFolderList(GlobalFolderList());
		const size_t before = folders.size();
		folders.erase(std::remove(folders.begin(), folders.end(), path), folders.end());
		if (folders.size() == before)
			return Failure("that folder is not linked in Caliza/GameFolders");
		return WriteFolderList(folders, "library folder unlinked");
	}

	Execution RelocateLibraryFolder(const Command& command)
	{
		const std::string old_path = SourcePathForId(command.id);
		if (command.path.empty())
			return Failure("a replacement folder path is required");
		std::vector<std::string> folders = settings::SplitFolderList(GlobalFolderList());
		bool replaced = false;
		for (std::string& folder : folders)
		{
			if (folder == old_path)
			{
				folder = command.path;
				replaced = true;
			}
		}
		if (!replaced)
			return Failure("that folder is not linked in Caliza/GameFolders");
		return WriteFolderList(folders, "library folder relocated");
	}

	Execution SetBiosFolder(const Command& command)
	{
		if (command.path.empty())
			return Failure("a BIOS folder path is required");
		settings::Mutation mutation;
		mutation.kind = settings::Mutation::Kind::Set;
		mutation.key = "Caliza/BiosFolder";
		mutation.value = command.path;
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, {mutation}, &summary, &error))
			return Failure(error);
		return Success("BIOS folder updated" + (summary.empty() ? std::string() : ": " + summary));
	}

	struct SettingReference
	{
		const settings::SettingDef* def = nullptr;
		ConsoleSettingScope scope = ConsoleSettingScope::Global;
		std::string game_key;
		std::string error;
	};

	SettingReference ResolveSettingReference(const std::string& encoded)
	{
		SettingReference out;
		ConsoleSettingScope scope = ConsoleSettingScope::Global;
		std::string schema_id, game_key, decode_error;
		if (DecodeSettingId(encoded, &scope, &schema_id, &game_key, &decode_error))
		{
			out.scope = scope;
			out.game_key = game_key;
		}
		else if (!decode_error.empty())
		{
			out.error = decode_error;
			return out;
		}
		else
		{
			schema_id = encoded;
		}
		out.def = settings::FindSetting(schema_id);
		if (!out.def)
			out.def = settings::FindSettingByKey(settings::MigrateLegacyKey(schema_id));
		if (!out.def)
		{
			out.error = "unknown setting " + encoded;
			return out;
		}
		const bool explicit_scope = encoded.rfind("global:", 0) == 0 || encoded.rfind("game:", 0) == 0;
		if (!explicit_scope)
		{
			if (settings::HasScope(out.def->scopes, settings::ScopeMask::Global))
				out.scope = ConsoleSettingScope::Global;
			else
			{
				out.error = out.def->id + " is game-scoped; use an encoded game setting id";
				return out;
			}
		}
		if (out.scope == ConsoleSettingScope::Global && !settings::HasScope(out.def->scopes, settings::ScopeMask::Global))
		{
			out.error = out.def->id + " does not have a global settings layer";
			return out;
		}
		if (out.scope == ConsoleSettingScope::Game && (!settings::HasScope(out.def->scopes, settings::ScopeMask::Game) || out.game_key.empty()))
		{
			out.error = out.def->id + " requires a valid game scope";
			return out;
		}
		return out;
	}

	const GameEntry* FindGameInSnapshot(const std::string& key, Snapshot* snapshot)
	{
		*snapshot = m_storage->GetSnapshot();
		for (const GameEntry& game : snapshot->games)
		{
			if (game.id == key || game.path == key || game.file == key)
				return &game;
		}
		return nullptr;
	}

	std::string GameSettingsPath(const GameEntry& game) const
	{
		std::string stem;
		if (m_options.game_settings_stem)
			stem = m_options.game_settings_stem(game);
		if (stem.empty())
			stem = StemOf(game.file);
		return m_options.game_settings_dir + "/" + stem + ".ini";
	}

	Execution SelectBios(const Command& command)
	{
		const std::string raw_selected = !command.path.empty() ? command.path : (!command.id.empty() ? command.id : command.value);
		std::string selected = raw_selected;
		if (raw_selected.find('/') != std::string::npos)
		{
			const std::string normalized = storage::fs::NormalizePath(raw_selected);
			const std::string bios_root = storage::fs::NormalizePath(m_options.bios_dir);
			if (bios_root.empty() || !storage::fs::IsPathWithin(normalized, bios_root))
				return Failure("a BIOS path must stay inside the configured BIOS folder");
			selected = storage::fs::BaseName(normalized);
		}
		settings::Mutation mutation;
		mutation.key = "Filenames/BIOS";
		if (selected.empty() || selected == "." || selected == "automatic")
			mutation.kind = settings::Mutation::Kind::Unset;
		else
		{
			if (selected.find('/') != std::string::npos || selected.find('\\') != std::string::npos)
				return Failure("BIOS selection uses a file name, not a path");
			if (!m_options.bios_dir.empty() && !storage::fs::IsRegularFile(storage::fs::JoinPath(m_options.bios_dir, selected)))
				return Failure("the selected BIOS file is not present in the configured BIOS folder");
			mutation.kind = settings::Mutation::Kind::Set;
			mutation.value = selected;
		}
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, {mutation}, &summary, &error))
			return Failure(error);
		return Success(selected.empty() || selected == "." || selected == "automatic" ? "BIOS selection returned to automatic" : "BIOS selected");
	}

	Execution ResetSettingsScope(const Command& command)
	{
		const std::string scope = LowerCopy(command.id + " " + command.path + " " + command.value);
		if (scope.find("controls") != std::string::npos)
			return ResetControlBindings(command);
		if (scope.find("game") != std::string::npos)
			return Unsupported("resetting a game settings scope without a selected-game contract field");
		std::vector<settings::Mutation> mutations;
		for (const settings::SettingDef& def : settings::Settings())
		{
			if (!settings::HasScope(def.scopes, settings::ScopeMask::Global) || def.player_owned || def.compatibility_key.empty())
				continue;
			settings::Mutation mutation;
			mutation.kind = settings::Mutation::Kind::Unset;
			mutation.key = def.compatibility_key;
			mutations.push_back(std::move(mutation));
		}
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, mutations, &summary, &error))
			return Failure(error);
		return Success("global tuning settings reset; player-owned folders, cards and accessibility were preserved");
	}

	Execution SetSetting(const Command& command)
	{
		if (command.value == "activate")
		{
			if (const settings::ActionDef* action = settings::FindAction(command.id))
				return Failure("schema action " + action->id + " has no safe native implementation in this adapter build");
		}
		SettingReference ref = ResolveSettingReference(command.id);
		if (!ref.error.empty())
			return Failure(ref.error);

		std::vector<settings::Mutation> mutations;
		settings::Mutation mutation;
		mutation.key = ref.def->compatibility_key;
		if (ref.def->type == settings::ValueType::GroupList)
		{
			const std::string lower = LowerCopy(command.value);
			const bool patch = ref.def->compatibility_key == "Patches/Enable";
			if (lower.rfind(patch ? "patch+ " : "cheat+ ", 0) == 0)
			{
				mutation.kind = patch ? settings::Mutation::Kind::PatchOn : settings::Mutation::Kind::CheatOn;
				mutation.key = command.value.substr((patch ? 7 : 7));
			}
			else if (lower.rfind(patch ? "patch- " : "cheat- ", 0) == 0)
			{
				mutation.kind = patch ? settings::Mutation::Kind::PatchOff : settings::Mutation::Kind::CheatOff;
				mutation.key = command.value.substr((patch ? 7 : 7));
			}
			else
				return Failure("group-list values use patch+ name/patch- name or cheat+ name/cheat- name");
		}
		else
		{
			settings::ValidationResult validation = settings::ValidateValue(*ref.def, command.value);
			if (!validation.ok)
				return Failure(validation.error);
			mutation.kind = settings::Mutation::Kind::Set;
			mutation.value = validation.normalized;
		}
		mutations.push_back(mutation);

		std::string path = m_options.global_settings_path;
		if (ref.scope == ConsoleSettingScope::Game)
		{
			Snapshot snapshot;
			const GameEntry* game = FindGameInSnapshot(ref.game_key, &snapshot);
			if (!game)
				return Failure("the game settings layer was not found in the catalog");
			path = GameSettingsPath(*game);
		}
		std::string summary, error;
		if (!EditSettings(path, mutations, &summary, &error))
			return Failure(error);
		return Success("setting updated" + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution ResetSetting(const Command& command)
	{
		SettingReference ref = ResolveSettingReference(command.id);
		if (!ref.error.empty())
			return Failure(ref.error);
		settings::Mutation mutation;
		mutation.kind = settings::Mutation::Kind::Unset;
		mutation.key = ref.def->compatibility_key;
		std::string path = m_options.global_settings_path;
		if (ref.scope == ConsoleSettingScope::Game)
		{
			Snapshot snapshot;
			const GameEntry* game = FindGameInSnapshot(ref.game_key, &snapshot);
			if (!game)
				return Failure("the game settings layer was not found in the catalog");
			path = GameSettingsPath(*game);
		}
		std::string summary, error;
		if (!EditSettings(path, {mutation}, &summary, &error))
			return Failure(error);
		return Success("setting reset" + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution ApplyRecommended(const Command& command)
	{
		if (!m_options.hooks.apply_recommended)
			return Unsupported("recommended settings without the platform preset writer");
		std::string path = m_options.global_settings_path;
		std::string preset_id = "@global";
		std::string key = !command.id.empty() ? command.id : command.path;
		if (!key.empty())
		{
			Snapshot snapshot;
			const GameEntry* game = FindGameInSnapshot(key, &snapshot);
			if (!game)
				return Failure("the game for recommended settings was not found in the catalog");
			path = GameSettingsPath(*game);
			preset_id = !game->serial.empty() ? game->serial : StemOf(game->file);
		}
		std::string summary, error;
		if (!m_options.hooks.apply_recommended(path, preset_id, &summary, &error))
			return Failure(error);
		RefreshProjection(true);
		return Success("recommended settings applied" + (summary.empty() ? std::string() : ": " + summary));
	}

	static void SlotKeys(int slot, std::string* file_key, std::string* enable_key)
	{
		if (slot == 0)
		{
			*file_key = "MemoryCards/Slot1_Filename";
			*enable_key = "MemoryCards/Slot1_Enable";
		}
		else
		{
			*file_key = "MemoryCards/Slot2_Filename";
			*enable_key = "MemoryCards/Slot2_Enable";
		}
	}

	Execution CreateMemoryCard(const Command& command)
	{
		if (!m_options.hooks.create_memory_card)
			return Unsupported("memory-card creation without the platform card writer");
		uint64_t megabytes = 8;
		bool assign_slot = false;
		int slot = 0;
		if (command.number == 8 || command.number == 16 || command.number == 32 || command.number == 64)
			megabytes = static_cast<uint64_t>(command.number);
		else if (command.number == 0 || command.number == 1)
		{
			assign_slot = true;
			slot = command.number;
		}
		std::string name = !command.id.empty() ? command.id : (!command.path.empty() ? command.path : command.value);
		if (name.empty())
			name = "Caliza Card " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
									 std::chrono::system_clock::now().time_since_epoch())
										 .count()) +
				   ".ps2";
		std::string created, error;
		if (!m_options.hooks.create_memory_card(m_options.memory_cards_dir, megabytes, name, &created, &error))
			return Failure(error);
		if (assign_slot)
		{
			Command assign;
			assign.type = CommandType::AssignMemoryCard;
			assign.id = created;
			assign.number = slot;
			Execution assigned = AssignMemoryCard(assign);
			if (!assigned.succeeded)
				return assigned;
		}
		RefreshProjection(true);
		return Success("memory card created: " + created);
	}

	Execution AssignMemoryCard(const Command& command)
	{
		if (command.number < 0 || command.number > 1)
			return Failure("memory-card slot must be 1 or 2");
		std::string file_key, enable_key;
		SlotKeys(command.number, &file_key, &enable_key);
		std::vector<settings::Mutation> mutations;
		settings::Mutation file_mutation;
		settings::Mutation enable_mutation;
		const std::string file = BaseName(command.id.empty() ? command.path : command.id);
		if (file == "@none" || file == "none")
		{
			file_mutation.kind = settings::Mutation::Kind::Unset;
			file_mutation.key = file_key;
			enable_mutation.kind = settings::Mutation::Kind::Set;
			enable_mutation.key = enable_key;
			enable_mutation.value = "false";
		}
		else if (file == "inherit" || file.empty())
		{
			file_mutation.kind = settings::Mutation::Kind::Unset;
			file_mutation.key = file_key;
			enable_mutation.kind = settings::Mutation::Kind::Unset;
			enable_mutation.key = enable_key;
		}
		else
		{
			if (file.find('/') != std::string::npos || file.find('\\') != std::string::npos)
				return Failure("memory-card assignment uses a file name, not a path");
			file_mutation.kind = settings::Mutation::Kind::Set;
			file_mutation.key = file_key;
			file_mutation.value = file;
			enable_mutation.kind = settings::Mutation::Kind::Set;
			enable_mutation.key = enable_key;
			enable_mutation.value = "true";
		}
		mutations.push_back(file_mutation);
		mutations.push_back(enable_mutation);
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, mutations, &summary, &error))
			return Failure(error);
		return Success("memory-card slot updated" + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution SetControlBinding(const Command& command)
	{
		const settings::SettingDef* def = settings::FindSetting(command.id);
		if (!def || def->tab != settings::UiTab::Controls || def->type != settings::ValueType::Choice || def->group_id != "buttons")
			return Failure("unknown editable control action " + command.id);
		settings::ValidationResult validation = settings::ValidateValue(*def, command.value);
		if (!validation.ok)
			return Failure(validation.error);
		settings::Mutation mutation;
		mutation.kind = settings::Mutation::Kind::Set;
		mutation.key = def->compatibility_key;
		mutation.value = validation.normalized;
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, {mutation}, &summary, &error))
			return Failure(error);
		return Success("control binding updated" + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution ResetControlBindings(const Command&)
	{
		std::vector<settings::Mutation> mutations;
		for (const settings::SettingDef& def : settings::Settings())
		{
			if (def.tab != settings::UiTab::Controls || def.group_id != "buttons" || def.compatibility_key.empty())
				continue;
			settings::Mutation mutation;
			mutation.kind = settings::Mutation::Kind::Unset;
			mutation.key = def.compatibility_key;
			mutations.push_back(std::move(mutation));
		}
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, mutations, &summary, &error))
			return Failure(error);
		return Success("control bindings reset" + (summary.empty() ? std::string() : ": " + summary));
	}

	Execution SetAccessibility(const Command& command)
	{
		std::string schema_id;
		std::string normalized = command.value;
		if (command.id == "text_scale")
		{
			schema_id = "accessibility.text-scale";
			if (normalized.empty() && command.number != 0)
				normalized = std::to_string(command.number);
			normalized = std::to_string(NearestPercentStep(normalized));
		}
		else if (command.id == "high_contrast")
			schema_id = "accessibility.high-contrast";
		else if (command.id == "reduce_motion")
			schema_id = "accessibility.reduce-motion";
		else if (command.id == "large_focus")
			schema_id = "accessibility.large-focus";
		else if (settings::FindSetting(command.id) && IsAccessibilityControl(*settings::FindSetting(command.id)))
			schema_id = command.id;
		else
			return Failure("unknown accessibility setting " + command.id);
		const settings::SettingDef* def = settings::FindSetting(schema_id);
		settings::ValidationResult validation = settings::ValidateValue(*def, normalized);
		if (!validation.ok)
			return Failure(validation.error);
		settings::Mutation mutation;
		mutation.kind = settings::Mutation::Kind::Set;
		mutation.key = def->compatibility_key;
		mutation.value = validation.normalized;
		std::string summary, error;
		if (!EditSettings(m_options.global_settings_path, {mutation}, &summary, &error))
			return Failure(error);
		return Success("accessibility updated");
	}

	const GameEntry* RequireServiceGame(const Command& command, Snapshot* snapshot, std::string* error)
	{
		const GameEntry* game = FindGameInSnapshot(!command.id.empty() ? command.id : command.path, snapshot);
		if (!game)
			*error = "the catalog entry is missing; rescan the library";
		else if (game->serial.empty())
			*error = "this image has no serial yet; rescan it before using online services";
		return game;
	}

	Execution FetchOnlinePatches(const Command& command)
	{
		if (!m_options.hooks.fetch_online_patches)
			return Unsupported("online patch downloads");
		Snapshot snapshot;
		std::string error;
		const GameEntry* game = RequireServiceGame(command, &snapshot, &error);
		if (!game)
			return Failure(error);
		if (!m_options.hooks.fetch_online_patches(*game, &error))
			return Failure(error.empty() ? "the patch service is already busy or unavailable" : error);
		RefreshProjection(true);
		return Success("patch and cheat lookup queued");
	}

	Execution RetryTextureCatalog(const Command&)
	{
		if (!m_options.hooks.retry_texture_pack_catalog)
			return Unsupported("texture pack catalog retries");
		std::string error;
		if (!m_options.hooks.retry_texture_pack_catalog(&error))
			return Failure(error.empty() ? "the texture pack catalog service is unavailable" : error);
		RefreshProjection(true);
		return Success("texture pack catalog lookup queued");
	}

	Execution DownloadTexturePack(const Command& command)
	{
		if (!m_options.hooks.download_texture_pack)
			return Unsupported("texture pack downloads");
		Snapshot snapshot;
		std::string error;
		const GameEntry* game = RequireServiceGame(command, &snapshot, &error);
		if (!game)
			return Failure(error);
		if (!m_options.hooks.download_texture_pack(*game, command.number, &error))
			return Failure(error.empty() ? "the texture pack service could not start" : error);
		RefreshProjection(true);
		return Success("texture pack download queued");
	}

	Execution CancelTexturePack(const Command& command)
	{
		if (!m_options.hooks.cancel_texture_pack)
			return Unsupported("texture pack cancellation");
		Snapshot snapshot;
		std::string error;
		const GameEntry* game = RequireServiceGame(command, &snapshot, &error);
		if (!game)
			return Failure(error);
		if (!m_options.hooks.cancel_texture_pack(*game, &error))
			return Failure(error.empty() ? "no texture pack download can be cancelled now" : error);
		RefreshProjection(true);
		return Success("texture pack download cancellation requested");
	}

	Execution DeleteTexturePack(const Command& command)
	{
		if (!m_options.hooks.delete_texture_pack)
			return Unsupported("texture pack deletion");
		Snapshot snapshot;
		std::string error;
		const GameEntry* game = RequireServiceGame(command, &snapshot, &error);
		if (!game)
			return Failure(error);
		if (!m_options.hooks.delete_texture_pack(*game, &error))
			return Failure(error.empty() ? "no installed texture pack can be removed now" : error);
		RefreshProjection(true);
		return Success("texture pack removal requested");
	}

	Execution LaunchGame(const Command& command)
	{
		Snapshot snapshot;
		const GameEntry* game = FindGameInSnapshot(!command.id.empty() ? command.id : command.path, &snapshot);
		if (!game)
			return Failure("the catalog entry is missing; rescan the library");
		if (!game->can_launch || game->damaged)
			return Failure(game->problem.empty() ? "the image is damaged or unlaunchable" : game->problem);
		if (!snapshot.bios.present && !BuildOverlay().bios.present)
			return Failure("a BIOS is required before launch");
		ConsoleLaunchRequest request;
		request.kind = ConsoleLaunchRequest::Kind::Game;
		request.game_id = game->id;
		request.path = game->path;
		request.title = game->title;
		request.serial = game->serial;
		const std::string mode = LowerCopy(command.value);
		request.smart_tune = mode.find("smart_tune") != std::string::npos || mode.find("smart-tune") != std::string::npos;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_launch_pending)
				return Failure("another launch request is waiting for shell handoff");
			request.command = 0; // filled by the caller-visible event id only for diagnostics; ownership is still singular
			m_launch_request = request;
			m_launch_pending = true;
			m_overlay.vm.state = VmState::Starting;
			m_overlay.vm.game_id = game->id;
			m_overlay.vm.title = game->title;
			m_overlay.vm.message = "launch request accepted for shell handoff";
			m_overlay.revision++;
		}
		return Success("launch request accepted for shell handoff");
	}

	Execution LaunchSystemMenu(const Command&)
	{
		if (!BuildOverlay().bios.present)
			return Failure("a BIOS is required before launching the PS2 system menu");
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_launch_pending)
				return Failure("another launch request is waiting for shell handoff");
			m_launch_request = {};
			m_launch_request.kind = ConsoleLaunchRequest::Kind::SystemMenu;
			m_launch_request.title = "PlayStation 2 system menu";
			m_launch_pending = true;
			m_overlay.vm.state = VmState::Starting;
			m_overlay.vm.title = m_launch_request.title;
			m_overlay.vm.message = "system-menu handoff accepted";
			m_overlay.revision++;
		}
		return Success("system-menu handoff accepted");
	}

	Execution RuntimeCommand(const Command& command)
	{
		if (m_options.hooks.vm_command)
		{
			std::string error;
			if (m_options.hooks.vm_command(command, &error))
				return Success("runtime command sent");
			return Failure(error.empty() ? "runtime command failed" : error);
		}
		return Unsupported("runtime VM control in the re-exec menu process");
	}

	void MaybeRefreshProjectionLocked()
	{
		const auto now = std::chrono::steady_clock::now();
		if (m_overlay.valid && now - m_overlay.refreshed_at < std::chrono::milliseconds(m_options.projection_refresh_ms))
			return;
		// BuildOverlay performs file and directory work, so copying the small overlay under the mutex is cheaper
		// than holding callers while every frame rereads ini state. Mutations force an immediate refresh.
		m_projection_due = true;
	}

	void RefreshProjection(bool force)
	{
		if (!force && !m_projection_due)
			return;
		std::lock_guard<std::mutex> projection_lock(m_projection_mutex);
		if (!force && !m_projection_due)
			return;
		ConsoleOverlay built = BuildOverlay();
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_overlay.valid)
			built.revision = m_overlay.revision + 1;
		built.valid = true;
		built.refreshed_at = std::chrono::steady_clock::now();
		// Preserve a launch-handoff VM state; file polling must not claim the VM stopped while the shell is exiting.
		if (m_launch_pending && m_overlay.vm.state == VmState::Starting)
			built.vm = m_overlay.vm;
		m_overlay = std::move(built);
		m_projection_due = false;
	}

	ConsoleOverlay BuildOverlay()
	{
		ConsoleOverlay out;
		settings::DocumentState global_doc;
		std::string error;
		ReadSettingsDocument(m_options.global_settings_path, &global_doc, &error);

		// The shared schema projection is the native/web boundary for visible rows. The console surface merges the
		// global layer with game-scoped rows so the registry is complete in one snapshot; writing a game-only row still
		// requires the explicit scoped command id because the current contract has no selected-game field.
		settings::ProjectionOptions projection;
		projection.scope = settings::ScopeMask::Global;
		projection.include_composite_memory_cards = true;
		for (const auto& kv : global_doc.values)
			projection.values[kv.first] = kv.second;
		out.settings = settings::ProjectSettings(projection);
		projection.scope = settings::ScopeMask::Game;
		const std::vector<SettingCategory> game_projection = settings::ProjectSettings(projection);
		for (const SettingCategory& game_category : game_projection)
		{
			auto category_it = std::find_if(out.settings.begin(), out.settings.end(),
				[&](const SettingCategory& category) { return category.id == game_category.id; });
			if (category_it == out.settings.end())
			{
				SettingCategory category;
				category.id = game_category.id;
				category.label = game_category.label;
				out.settings.push_back(std::move(category));
				category_it = std::prev(out.settings.end());
			}
			for (const SettingItem& game_item : game_category.items)
			{
				const bool already_projected = std::any_of(category_it->items.begin(), category_it->items.end(),
					[&](const SettingItem& item) { return item.id == game_item.id; });
				if (!already_projected)
					category_it->items.push_back(game_item);
			}
		}
		for (SettingCategory& category : out.settings)
		{
			for (SettingItem& item : category.items)
			{
				const settings::SettingDef* def = settings::FindSetting(item.id);
				if (!def)
					def = settings::FindSettingByKey(item.id);
				if (!def || !def->dynamic_choices)
					continue;
				std::vector<settings::Choice> dynamic;
				if (m_options.hooks.dynamic_choices)
					dynamic = m_options.hooks.dynamic_choices(*def);
				if (dynamic.empty() && (def->id == "discs.cheat-disc-image" || def->id == "discs.elf-disc-image"))
				{
					// Catalog-backed selectors use the scan worker's entries, never a synchronous scan on the UI thread.
					const Snapshot catalog = m_storage->GetSnapshot();
					dynamic.push_back({"", def->id == "discs.elf-disc-image" ? "No disc" : "None"});
					for (const GameEntry& game : catalog.games)
						dynamic.push_back({StemOf(game.file), game.title});
				}
				item.choices.clear();
				for (const settings::Choice& choice : dynamic)
					item.choices.push_back({choice.value, choice.label});
			}
		}

		BuildControls(global_doc, &out);
		BuildAccessibility(global_doc, &out);
		BuildMemoryCards(global_doc, &out);
		BuildBios(&out);
		BuildContinueState(&out);
		if (m_options.hooks.service_operations)
			out.operations = m_options.hooks.service_operations();
		out.projection_problem = m_folder_projection_error;
		out.vm.state = VmState::Idle;
		out.vm.message.clear();
		return out;
	}

	void BuildControls(const settings::DocumentState& global_doc, ConsoleOverlay* out)
	{
		std::map<std::string, int> users;
		for (const settings::SettingDef& def : settings::Settings())
		{
			if (def.tab != settings::UiTab::Controls || def.group_id != "buttons" || def.type != settings::ValueType::Choice)
				continue;
			ControlBinding binding;
			binding.action = def.id;
			binding.label = def.label;
			binding.editable = true;
			binding.button = settings::EffectiveValue(def, global_doc, settings::DocumentState(), nullptr);
			if (!binding.button.empty() && binding.button != "None")
				users[binding.button]++;
			out->controls.push_back(std::move(binding));
		}
		for (ControlBinding& binding : out->controls)
		{
			auto it = users.find(binding.button);
			if (!binding.button.empty() && binding.button != "None" && it != users.end() && it->second > 1)
			{
				binding.conflict = true;
			}
		}
	}

	void BuildAccessibility(const settings::DocumentState& global_doc, ConsoleOverlay* out)
	{
		const settings::DocumentState empty;
		const settings::SettingDef* scale = settings::FindSetting("accessibility.text-scale");
		const settings::SettingDef* contrast = settings::FindSetting("accessibility.high-contrast");
		const settings::SettingDef* motion = settings::FindSetting("accessibility.reduce-motion");
		const settings::SettingDef* focus = settings::FindSetting("accessibility.large-focus");
		if (scale)
			out->accessibility.text_scale_percent = static_cast<uint32_t>(std::strtoul(settings::EffectiveValue(*scale, global_doc, empty).c_str(), nullptr, 10));
		if (contrast)
			out->accessibility.high_contrast = TruthyText(settings::EffectiveValue(*contrast, global_doc, empty));
		if (motion)
			out->accessibility.reduce_motion = TruthyText(settings::EffectiveValue(*motion, global_doc, empty));
		if (focus)
			out->accessibility.large_focus = TruthyText(settings::EffectiveValue(*focus, global_doc, empty));
	}

	void BuildMemoryCards(const settings::DocumentState& global_doc, ConsoleOverlay* out)
	{
		std::vector<std::pair<std::string, uint64_t>> files;
		if (m_options.hooks.list_memory_card_files)
			files = m_options.hooks.list_memory_card_files();
		std::map<std::string, uint64_t> sizes;
		for (const auto& file : files)
			sizes[LowerCopy(file.first)] = file.second;
		std::set<std::string> assigned;
		for (int slot = 0; slot < 2; slot++)
		{
			std::string file_key, enable_key;
			SlotKeys(slot, &file_key, &enable_key);
			const std::string* file_value = settings::FindValue(global_doc, file_key);
			const std::string* enable_value = settings::FindValue(global_doc, enable_key);
			std::string file = file_value ? *file_value : (slot == 0 ? "Mcd001.ps2" : "Mcd002.ps2");
			const bool enabled = enable_value ? TruthyText(*enable_value) : true;
			MemoryCardInfo card;
			card.slot = slot;
			card.file = file;
			card.enabled = enabled;
			const auto size_it = sizes.find(LowerCopy(file));
			card.present = size_it != sizes.end();
			if (card.present)
				card.bytes = size_it->second;
			out->memory_cards.push_back(card);
			assigned.insert(LowerCopy(file));
		}
		for (const auto& file : files)
		{
			if (assigned.count(LowerCopy(file.first)) != 0)
				continue;
			MemoryCardInfo card;
			card.slot = -1;
			card.file = file.first;
			card.enabled = true;
			card.present = true;
			card.bytes = file.second;
			out->memory_cards.push_back(std::move(card));
		}
	}

	void BuildBios(ConsoleOverlay* out)
	{
		if (!m_options.hooks.bios_status)
		{
			out->bios.present = false;
			out->bios.problem = "the platform BIOS probe is not bound";
			return;
		}
		out->bios = m_options.hooks.bios_status();
	}

	void BuildContinueState(ConsoleOverlay* out)
	{
		if (m_options.last_game_path.empty())
			return;
		std::string text;
		std::string error;
		const bool ok = m_options.hooks.read_text_file ? m_options.hooks.read_text_file(m_options.last_game_path, &text, &error) :
											 storage::fs::ReadFile(m_options.last_game_path, &text, &error);
		if (!ok)
			return;
		const std::string wanted = BaseName(settings::Trim(text));
		const Snapshot snapshot = m_storage->GetSnapshot();
		for (const GameEntry& game : snapshot.games)
		{
			if (game.file == wanted || game.path == settings::Trim(text))
			{
				out->continue_game_id = game.id;
				return;
			}
		}
	}

	void SyncFolderProjection()
	{
		// Caliza/GameFolders is the user-facing owner of folder links. StorageBackend's persisted source table is
		// used here only as its scan projection: this adapter removes projection rows absent from the settings list
		// and adds rows present in settings. Removing a projection never deletes image files.
		std::vector<std::string> wanted = settings::SplitFolderList(GlobalFolderList());
		std::string first_error;
		const Snapshot snapshot = m_storage->GetSnapshot();
		std::set<std::string> wanted_set(wanted.begin(), wanted.end());
		for (const LibrarySource& source : snapshot.sources)
		{
			if (!source.user_added || wanted_set.count(source.path) != 0)
				continue;
			Command remove;
			remove.type = CommandType::RemoveLibraryFolder;
			remove.id = source.id;
			std::string error;
			if (!PostInternalStorageCommandAndWait(remove, &error) && first_error.empty())
				first_error = error;
		}
		for (const std::string& folder : wanted)
		{
			bool present = false;
			for (const LibrarySource& source : snapshot.sources)
				present = present || (source.user_added && source.path == folder);
			if (present)
				continue;
			Command add;
			add.type = CommandType::AddLibraryFolder;
			add.path = folder;
			add.value = BaseName(folder);
			std::string error;
			if (!PostInternalStorageCommandAndWait(add, &error) && first_error.empty())
				first_error = error;
		}
		{
			std::lock_guard<std::mutex> projection_lock(m_projection_mutex);
			m_folder_projection_error = first_error;
		}
	}

	ConsoleBackendOptions m_options;
	std::unique_ptr<Backend> m_storage;
	std::mutex m_mutex;
	std::mutex m_projection_mutex;
	std::condition_variable m_cv;
	std::deque<QueuedCommand> m_queue;
	std::vector<CommandEvent> m_events;
	std::map<CommandId, std::pair<CommandId, CommandType>> m_inner_to_outer;
	ConsoleOverlay m_overlay;
	ConsoleLaunchRequest m_launch_request;
	bool m_launch_pending = false;
	std::atomic<bool> m_projection_due{false};
	std::string m_folder_projection_error;
	CommandId m_next_command = 1;
	uint64_t m_event_revision_high_water = 1;
	std::atomic<bool> m_stop{false};
	std::thread m_worker;
};

std::string ConsoleBackend::EncodeSettingId(ConsoleSettingScope scope, const std::string& schema_id, const std::string& game_key)
{
	if (scope == ConsoleSettingScope::Global)
		return "global:" + schema_id;
	return "game:" + schema_id + ":" + PercentEncode(game_key);
}

bool ConsoleBackend::DecodeSettingId(const std::string& encoded, ConsoleSettingScope* scope, std::string* schema_id, std::string* game_key,
	std::string* error)
{
	if (encoded.rfind("global:", 0) == 0)
	{
		if (scope)
			*scope = ConsoleSettingScope::Global;
		if (schema_id)
			*schema_id = encoded.substr(7);
		if (game_key)
			game_key->clear();
		return true;
	}
	if (encoded.rfind("game:", 0) == 0)
	{
		const std::string rest = encoded.substr(5);
		const size_t colon = rest.find(':');
		if (colon == std::string::npos || colon == 0 || colon + 1 >= rest.size())
		{
			if (error)
				*error = "game-scoped setting ids need a schema id and game key";
			return false;
		}
		if (scope)
			*scope = ConsoleSettingScope::Game;
		if (schema_id)
			*schema_id = rest.substr(0, colon);
		std::string decoded;
		if (!PercentDecode(rest.substr(colon + 1), &decoded))
		{
			if (error)
				*error = "bad percent encoding in game setting id";
			return false;
		}
		if (game_key)
			*game_key = decoded;
		return true;
	}
	if (scope)
		*scope = ConsoleSettingScope::Global;
	if (schema_id)
		*schema_id = encoded;
	if (game_key)
		game_key->clear();
	return true;
}

ConsoleBackend::ConsoleBackend(const ConsoleBackendOptions& options, std::unique_ptr<Backend> storage_backend)
	: m_impl(std::make_unique<Impl>(options, std::move(storage_backend)))
{
}

ConsoleBackend::~ConsoleBackend() = default;

Snapshot ConsoleBackend::GetSnapshot()
{
	return m_impl->GetSnapshot();
}

CommandId ConsoleBackend::Post(const Command& command)
{
	return m_impl->Post(command);
}

std::vector<CommandEvent> ConsoleBackend::PollEvents()
{
	return m_impl->PollEvents();
}

FolderListing ConsoleBackend::BrowseFolder(const std::string& path, const std::string& filter, uint32_t offset, uint32_t limit)
{
	return m_impl->BrowseFolder(path, filter, offset, limit);
}

bool ConsoleBackend::ConsumeLaunchRequest(ConsoleLaunchRequest* out)
{
	return m_impl->ConsumeLaunchRequest(out);
}
} // namespace caliza
