// Caliza native shell adapter: Estratos entry point and orchestrator boundary.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ps4_shell.h"
#include "../core/console_backend.h"
#include "../core/ui_model.h"
#include "estratos_scene.h"
#include "vk2d_renderer.h"
#include "../../ps5/frontend/fe_ps5.h"
#include <cstdio>

#if defined(ORBIS_PS4)
#include "../../ps5/frontend/fe_bios.h"
#include "../../ps5/frontend/fe_covers.h"
#include "../../ps5/frontend/fe_games.h"
#include "../../ps5/frontend/fe_modern_controls.h"
#include "../../ps5/frontend/fe_settings.h"
#include "../../ps5/frontend/fe_texpacks.h"
#include "../../ps5/frontend/fe_patchdl.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" {
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);
int scePadInit(void);
int scePadOpen(int32_t userId, int32_t type, int32_t index, const void* param);
int scePadClose(int32_t handle);
int scePadReadState(int32_t handle, void* data);
int sceUserServiceInitialize(const void* params);
int sceUserServiceGetInitialUser(int32_t* userId);
}
extern "C" const uint8_t fe_font_text[], fe_font_text_end[], fe_font_icons[], fe_font_icons_end[];
extern "C" const uint8_t fe_presets[], fe_presets_end[];
int orbis_hide_splash();

namespace
{
struct PadData
{
	uint32_t buttons;
	uint8_t lx, ly, rx, ry, l2, r2, pad0, pad1;
	uint8_t rest[256];
};
constexpr uint32_t kPadRight = 0x20, kPadLeft = 0x80, kPadUp = 0x10, kPadDown = 0x40;
constexpr uint32_t kPadCross = 0x4000, kPadCircle = 0x2000, kPadSquare = 0x8000, kPadTriangle = 0x1000;
constexpr uint32_t kPadOptions = 0x8, kPadL1 = 0x400, kPadR1 = 0x800, kPadL2 = 0x100, kPadR2 = 0x200;

double NowSeconds()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) * 1e-9;
}

class ShellDisplay
{
public:
	fe::Vk vk;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkQueue queue = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> images;
	VkExtent2D extent = {};
	VkSemaphore acquired[2] = {};
	VkSemaphore rendered[2] = {};

	bool Fail(const char* what, VkResult result)
	{
		std::printf("[estratos] %s failed: %d\n", what, static_cast<int>(result));
		std::fflush(stdout);
		return false;
	}

	bool Init()
	{
		const char* missing = nullptr;
		if (!vk.LoadGlobal(vk_icdGetInstanceProcAddr, &missing))
			return Fail(missing, VK_ERROR_UNKNOWN);
		const char* instance_extensions[] = {"VK_KHR_surface", "VK_KHR_display"};
		VkApplicationInfo app = {};
		app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		app.pApplicationName = "Caliza";
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo instance_info = {};
		instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_info.pApplicationInfo = &app;
		instance_info.enabledExtensionCount = 2;
		instance_info.ppEnabledExtensionNames = instance_extensions;
		VkResult result = vk.vkCreateInstance(&instance_info, nullptr, &instance);
		if (result != VK_SUCCESS)
			return Fail("vkCreateInstance", result);
		if (!vk.LoadInstance(instance, true, &missing))
			return Fail(missing, VK_ERROR_UNKNOWN);
		uint32_t count = 1;
		result = vk.vkEnumeratePhysicalDevices(instance, &count, &physical);
		if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0)
			return Fail("vkEnumeratePhysicalDevices", result);
		uint32_t queue_count = 0;
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
		std::vector<VkQueueFamilyProperties> families(queue_count);
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, families.data());
		queue_family = UINT32_MAX;
		for (uint32_t i = 0; i < queue_count; i++)
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				queue_family = i;
				break;
			}
		if (queue_family == UINT32_MAX)
			return Fail("a graphics queue", VK_ERROR_UNKNOWN);
		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = {};
		queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_info.queueFamilyIndex = queue_family;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;
		const char* device_extensions[] = {"VK_KHR_swapchain"};
		VkDeviceCreateInfo device_info = {};
		device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;
		device_info.enabledExtensionCount = 1;
		device_info.ppEnabledExtensionNames = device_extensions;
		result = vk.vkCreateDevice(physical, &device_info, nullptr, &device);
		if (result != VK_SUCCESS)
			return Fail("vkCreateDevice", result);
		if (!vk.LoadDevice(device, true, &missing))
			return Fail(missing, VK_ERROR_UNKNOWN);
		vk.vkGetDeviceQueue(device, queue_family, 0, &queue);
		return CreateSurface() && CreateSwapchain();
	}

	bool CreateSurface()
	{
		uint32_t display_count = 0, plane_count = 0;
		if (vk.vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &display_count, nullptr) != VK_SUCCESS || !display_count)
			return Fail("vkGetPhysicalDeviceDisplayPropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPropertiesKHR> displays(display_count);
		vk.vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &display_count, displays.data());
		if (vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &plane_count, nullptr) != VK_SUCCESS || !plane_count)
			return Fail("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", VK_ERROR_UNKNOWN);
		std::vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
		vk.vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &plane_count, planes.data());
		VkDisplayKHR display = VK_NULL_HANDLE;
		VkDisplayModePropertiesKHR mode = {};
		for (const VkDisplayPropertiesKHR& candidate : displays)
		{
			uint32_t mode_count = 0;
			if (vk.vkGetDisplayModePropertiesKHR(physical, candidate.display, &mode_count, nullptr) != VK_SUCCESS || !mode_count)
				continue;
			std::vector<VkDisplayModePropertiesKHR> modes(mode_count);
			vk.vkGetDisplayModePropertiesKHR(physical, candidate.display, &mode_count, modes.data());
			for (const VkDisplayModePropertiesKHR& candidate_mode : modes)
			{
				const uint64_t area = static_cast<uint64_t>(candidate_mode.parameters.visibleRegion.width) * candidate_mode.parameters.visibleRegion.height;
				const uint64_t best = static_cast<uint64_t>(mode.parameters.visibleRegion.width) * mode.parameters.visibleRegion.height;
				if (!display || area > best)
				{
					display = candidate.display;
					mode = candidate_mode;
				}
			}
			if (display)
				break;
		}
		if (!display)
			return Fail("a display mode", VK_ERROR_UNKNOWN);
		uint32_t plane = UINT32_MAX;
		for (uint32_t i = 0; i < plane_count; i++)
			if (planes[i].currentDisplay == VK_NULL_HANDLE || planes[i].currentDisplay == display)
			{
				plane = i;
				break;
			}
		if (plane == UINT32_MAX)
			return Fail("a display plane", VK_ERROR_UNKNOWN);
		const VkDisplaySurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR, nullptr, 0,
			mode.displayMode, plane, planes[plane].currentStackIndex, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, 1.0f,
			VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR, mode.parameters.visibleRegion};
		const VkResult result = vk.vkCreateDisplayPlaneSurfaceKHR(instance, &surface_info, nullptr, &surface);
		if (result != VK_SUCCESS)
			return Fail("vkCreateDisplayPlaneSurfaceKHR", result);
		extent = mode.parameters.visibleRegion;
		return true;
	}

	bool CreateSwapchain()
	{
		VkSurfaceCapabilitiesKHR capabilities = {};
		VkResult result = vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
		if (result != VK_SUCCESS)
			return Fail("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", result);
		if (capabilities.currentExtent.width != UINT32_MAX)
			extent = capabilities.currentExtent;
		uint32_t image_count = std::max(2u, capabilities.minImageCount);
		if (capabilities.maxImageCount)
			image_count = std::min(image_count, capabilities.maxImageCount);
		VkSwapchainCreateInfoKHR create = {};
		create.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		create.surface = surface;
		create.minImageCount = image_count;
		create.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
		create.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		create.imageExtent = extent;
		create.imageArrayLayers = 1;
		create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		create.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
		create.clipped = VK_TRUE;
		result = vk.vkCreateSwapchainKHR(device, &create, nullptr, &swapchain);
		if (result != VK_SUCCESS)
			return Fail("vkCreateSwapchainKHR", result);
		uint32_t count = 0;
		vk.vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
		images.resize(count);
		vk.vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
		for (int i = 0; i < 2; i++)
		{
			VkSemaphoreCreateInfo semaphore = {};
			semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			if (vk.vkCreateSemaphore(device, &semaphore, nullptr, &acquired[i]) != VK_SUCCESS ||
				vk.vkCreateSemaphore(device, &semaphore, nullptr, &rendered[i]) != VK_SUCCESS)
				return Fail("vkCreateSemaphore", VK_ERROR_UNKNOWN);
		}
		return true;
	}

	void Destroy()
	{
		if (device)
		{
			vk.vkDeviceWaitIdle(device);
			for (int i = 0; i < 2; i++)
			{
				if (acquired[i]) vk.vkDestroySemaphore(device, acquired[i], nullptr);
				if (rendered[i]) vk.vkDestroySemaphore(device, rendered[i], nullptr);
			}
			if (swapchain) vk.vkDestroySwapchainKHR(device, swapchain, nullptr);
			vk.vkDestroyDevice(device, nullptr);
		}
		if (surface) vk.vkDestroySurfaceKHR(instance, surface, nullptr);
		if (instance) vk.vkDestroyInstance(instance, nullptr);
		*this = ShellDisplay();
	}
};

class FrontendImageInspector final : public caliza::ImageInspector
{
public:
	explicit FrontendImageInspector(OrbisFrontendPaths paths)
		: m_paths(std::move(paths))
		, m_covers(m_paths.covers_dir, m_paths.cache_dir)
	{
	}
	bool Inspect(caliza::GameEntry& entry) override
	{
		caliza::PosixImageInspector base;
		if (!base.Inspect(entry))
			return false;
		fe::GameInfo info;
		info.path = entry.path;
		info.file = entry.file;
		const size_t dot = info.file.find_last_of('.');
		info.stem = dot == std::string::npos ? info.file : info.file.substr(0, dot);
		info.bytes = entry.bytes;
		fe::MakeTitle(info.stem, info.title, info.region, info.extra);
		std::string read_reason;
		info.serial = fe::ReadSerial(entry.path, &read_reason);
		fe::ApplyGameDbTitle(info);
		if (info.serial.empty() && !read_reason.empty())
		{
			bool launchable = true;
			const std::string formatted = fe::FormatImageProblem(read_reason, &launchable);
			if (!formatted.empty())
				info.damaged = formatted;
		}
		fe::ReadBadges(info, m_paths.settings_dir, m_paths.gs_ini, m_paths.patches_dir);
		entry.title = info.title.empty() ? entry.title : info.title;
		entry.region = info.region;
		entry.serial = info.serial;
		entry.badges = info.badges;
		entry.hidden = info.hidden;
		// The cover the user supplied, found beside the image or on the drive, or downloaded into the cache
		// (fe_covers.h's own lookup order); empty when none exists yet. Estratos falls back to a procedural
		// poster (EstratosScene::CoverFor) until one does.
		entry.cover_path = m_covers.Best(info).path;
		if (!info.damaged.empty())
		{
			entry.damaged = true;
			entry.can_launch = false;
			entry.problem = info.damaged;
		}
		return true;
	}
private:
	OrbisFrontendPaths m_paths;
	fe::CoverFinder m_covers;
};

std::string ConfiguredBiosDir(const OrbisFrontendPaths& paths)
{
	std::string text;
	if (!fe::settings::ReadFile(paths.gs_ini, text))
		return paths.bios_dir;
	std::string configured;
	for (const fe::settings::IniLine& line : fe::settings::ParseIni(text))
		if (line.kv && line.key == "Caliza/BiosFolder")
			configured = line.value;
	return configured.empty() ? paths.bios_dir : configured;
}

bool HasPlausibleBiosFile(const std::string& dir_path, int depth = 0)
{
	DIR* dir = opendir(dir_path.c_str());
	if (!dir)
		return false;
	bool found = false;
	while (!found)
	{
		const dirent* entry = readdir(dir);
		if (!entry)
			break;
		if (entry->d_name[0] == '.')
			continue;
		const std::string path = dir_path + "/" + entry->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISREG(st.st_mode) && static_cast<uint64_t>(st.st_size) >= fe::kBiosMinSize && static_cast<uint64_t>(st.st_size) <= fe::kBiosMaxSize)
			found = true;
		else if (depth < 2 && S_ISDIR(st.st_mode))
			found = HasPlausibleBiosFile(path, depth + 1);
	}
	closedir(dir);
	return found;
}

std::vector<caliza::settings::Choice> DynamicChoices(const OrbisFrontendPaths& paths, const caliza::settings::SettingDef& definition)
{
	std::vector<caliza::settings::Choice> out;
	if (definition.id == "bios.image")
	{
		out.push_back({"", "Automatic"});
		if (DIR* dir = opendir(ConfiguredBiosDir(paths).c_str()))
		{
			while (const dirent* entry = readdir(dir))
				if (entry->d_name[0] != '.')
					out.push_back({entry->d_name, entry->d_name});
			closedir(dir);
		}
	}
	else if (definition.type == caliza::settings::ValueType::MemoryCardFile)
		for (const fe::settings::CardFile& card : fe::settings::ListCards(paths.memcards_dir))
			out.push_back({card.name, card.name});
	return out;
}

std::vector<caliza::ConsolePatchGroup> ListPatchGroups(const OrbisFrontendPaths& paths, const caliza::GameEntry& game, bool cheats)
{
	std::vector<caliza::ConsolePatchGroup> out;
	const std::string& dir = cheats ? paths.cheats_dir : paths.patches_dir;
	for (const fe::settings::PatchGroup& group : fe::settings::PatchGroups(dir, game.serial))
		out.push_back({group.name, group.file, group.description, fe::IsModernControlsGroup(group.name)});
	return out;
}

void BootLog(const std::string& line)
{
	std::printf("[boot] %s\n", line.c_str());
	std::fflush(stdout);
}

bool EditSettings(const std::string& path, const std::vector<caliza::settings::Mutation>& mutations, std::string* summary, std::string* error)
{
	std::vector<fe::settings::Change> changes;
	for (const caliza::settings::Mutation& mutation : mutations)
	{
		fe::settings::Change change;
		switch (mutation.kind)
		{
			case caliza::settings::Mutation::Kind::Set: change.kind = fe::settings::Change::Set; break;
			case caliza::settings::Mutation::Kind::Unset: change.kind = fe::settings::Change::Unset; break;
			case caliza::settings::Mutation::Kind::PatchOn: change.kind = fe::settings::Change::PatchOn; break;
			case caliza::settings::Mutation::Kind::PatchOff: change.kind = fe::settings::Change::PatchOff; break;
			case caliza::settings::Mutation::Kind::CheatOn: change.kind = fe::settings::Change::CheatOn; break;
			case caliza::settings::Mutation::Kind::CheatOff: change.kind = fe::settings::Change::CheatOff; break;
			case caliza::settings::Mutation::Kind::ApplyRecommended:
				*error = "recommended settings use the preset writer hook";
				return false;
		}
		change.key = mutation.key;
		change.value = mutation.value;
		changes.push_back(std::move(change));
	}
	return fe::settings::EditSettingsFile(path, "# Caliza settings", changes, *summary, *error);
}

std::string StemOfPath(const std::string& file)
{
	const size_t slash = file.find_last_of('/');
	const size_t dot = file.find_last_of('.');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return file;
	return file.substr(0, dot);
}

fe::GameInfo ToFrontendGame(const caliza::GameEntry& game)
{
	fe::GameInfo out;
	out.path = game.path;
	out.file = game.file;
	out.stem = StemOfPath(game.file);
	out.title = game.title;
	out.region = game.region;
	out.serial = game.serial;
	out.bytes = game.bytes;
	out.badges = game.badges;
	if (game.damaged)
		out.damaged = game.problem.empty() ? "damaged" : game.problem;
	out.hidden = game.hidden;
	return out;
}

struct ServiceTracking
{
	mutable std::mutex mutex;
	std::vector<std::string> serials;
};

void TrackSerial(const std::shared_ptr<ServiceTracking>& tracking, const std::string& serial)
{
	if (serial.empty())
		return;
	std::lock_guard<std::mutex> lock(tracking->mutex);
	if (std::find(tracking->serials.begin(), tracking->serials.end(), serial) == tracking->serials.end())
		tracking->serials.push_back(serial);
}

caliza::OperationStatus PatchOperation(const std::string& serial, const fe::OnlinePatchStatus& status)
{
	caliza::OperationStatus out;
	out.id = "online-patches:" + serial;
	out.label = "Patches and cheats";
	out.game_id = serial;
	out.message = status.message;
	switch (status.state)
	{
		case fe::OnlinePatchStatus::State::Idle: out.state = caliza::OperationState::Idle; break;
		case fe::OnlinePatchStatus::State::Working: out.state = caliza::OperationState::Running; break;
		case fe::OnlinePatchStatus::State::Done: out.state = caliza::OperationState::Succeeded; break;
		case fe::OnlinePatchStatus::State::Failed: out.state = caliza::OperationState::Failed; break;
	}
	return out;
}

caliza::OperationStatus TextureOperation(const std::string& serial, const fe::TexturePackStatus& status)
{
	using State = fe::TexturePackStatus::State;
	caliza::OperationStatus out;
	out.id = "texture-pack:" + serial;
	out.label = "HD texture pack";
	out.game_id = serial;
	out.message = status.message;
	if (out.message.empty())
	{
		switch (status.state)
		{
			case State::Queued: out.message = "Queued"; break;
			case State::Checking: out.message = "Checking saved download"; break;
			case State::Downloading: out.message = "Downloading"; break;
			case State::Verifying: out.message = "Verifying"; break;
			case State::Unpacking: out.message = "Unpacking"; break;
			case State::Installing: out.message = "Installing"; break;
			case State::Removing: out.message = "Removing installed pack"; break;
			default: out.message = status.installed; break;
		}
		if (!status.job_name.empty())
			out.message += " · " + status.job_name;
	}
	if (status.total)
		out.progress = static_cast<float>(static_cast<double>(status.done) / static_cast<double>(status.total));
	switch (status.state)
	{
		case State::Loading: out.state = caliza::OperationState::Running; out.message = "Loading catalog"; break;
		case State::Unavailable: out.state = caliza::OperationState::Failed; break;
		case State::None:
		case State::Available: out.state = caliza::OperationState::Idle; break;
		case State::Queued: out.state = caliza::OperationState::Queued; out.cancelable = true; break;
		case State::Checking:
		case State::Downloading:
		case State::Verifying:
		case State::Unpacking:
		case State::Installing:
			out.state = caliza::OperationState::Running;
			out.cancelable = status.state != State::Installing;
			break;
		case State::Installed: out.state = caliza::OperationState::Succeeded; break;
		case State::Removing: out.state = caliza::OperationState::Running; break;
		case State::Failed: out.state = caliza::OperationState::Failed; out.cancelable = true; break;
		case State::NeedSpace: out.state = caliza::OperationState::Failed; out.cancelable = true; break;
	}
	return out;
}

std::vector<caliza::OperationStatus> CollectServiceOperations(const fe::TexturePackService& texture_service,
	const fe::OnlinePatchService& patch_service, const std::shared_ptr<ServiceTracking>& tracking)
{
	std::vector<caliza::OperationStatus> out;
	std::vector<std::string> serials;
	{
		std::lock_guard<std::mutex> lock(tracking->mutex);
		serials = tracking->serials;
	}
	for (const std::string& serial : serials)
	{
		if (patch_service.status)
		{
			caliza::OperationStatus operation = PatchOperation(serial, patch_service.status(serial));
			if (operation.state != caliza::OperationState::Idle)
				out.push_back(operation);
		}
		if (texture_service.status)
		{
			caliza::OperationStatus operation = TextureOperation(serial, texture_service.status(serial));
			if (operation.state != caliza::OperationState::Idle)
				out.push_back(operation);
		}
	}
	if (texture_service.activity)
	{
		const fe::TexturePackActivity activity = texture_service.activity();
		if (activity.active && std::none_of(out.begin(), out.end(), [](const caliza::OperationStatus& operation) {
				return operation.label == "HD texture pack" && operation.state == caliza::OperationState::Running;
			}))
		{
			caliza::OperationStatus operation;
			operation.id = "texture-pack:active";
			operation.label = "HD texture pack";
			operation.state = activity.state == fe::TexturePackStatus::State::Queued ? caliza::OperationState::Queued : caliza::OperationState::Running;
			operation.progress = static_cast<float>(activity.fraction);
			operation.cancelable = true;
			operation.message = activity.title;
			out.push_back(std::move(operation));
		}
	}
	return out;
}

void WriteLastGame(const OrbisFrontendPaths& paths, const std::string& image_path)
{
	const size_t slash = image_path.find_last_of('/');
	const std::string file = slash == std::string::npos ? image_path : image_path.substr(slash + 1);
	fe::settings::WriteFileAtomic(paths.top_dir + "/lastgame.txt", file + "\n");
}

bool EstratosPerfEnabled(const OrbisFrontendPaths& paths)
{
	if (const char* env = std::getenv("CALIZA_ESTRATOS_PERF"))
	{
		if (env[0] != '\0' && std::strcmp(env, "0") != 0)
			return true;
	}
	struct stat st = {};
	const std::string flag_a = paths.top_dir + "/flags/estratos_perf";
	const std::string flag_b = paths.top_dir + "/estratos_perf";
	return stat(flag_a.c_str(), &st) == 0 || stat(flag_b.c_str(), &st) == 0;
}
} // namespace

namespace caliza::ps4shell
{
ShellResult RunEstratos(const OrbisFrontendPaths& paths, const char* build_tag)
{
	ShellResult result;
	int user_id = -1;
	if (sceUserServiceInitialize(nullptr) != 0 || sceUserServiceGetInitialUser(&user_id) != 0)
	{
		result.error = "user service initialization failed";
		return result;
	}
	scePadInit();
	const int pad = scePadOpen(user_id, 0, 0, nullptr);
	if (pad < 0)
	{
		result.error = "could not open the controller";
		return result;
	}
	ShellDisplay display;
	if (!display.Init())
	{
		result.error = "Vulkan direct-display initialization failed";
		display.Destroy();
		scePadClose(pad);
		return result;
	}
	fe::SetSerialCacheFile(paths.serial_cache);
	fe::SetGameDbFile(paths.gamedb_file);

	fe::TexturePackService texture_service;
	fe::OnlinePatchService patch_service;
	orbis_frontend_services_start(paths, &texture_service, &patch_service);
	const std::shared_ptr<ServiceTracking> service_tracking = std::make_shared<ServiceTracking>();

	caliza::ConsoleBackendOptions options;
	options.storage.data_root = paths.top_dir;
	// Orbis keeps empty /mnt/usbN mountpoint directories around even when no drive exists. Showing every
	// possible slot makes the library browser look as if the console had eight drives; only slots with a
	// readable, non-empty root are useful evidence here. /mnt/usb0 itself is still probed, and the direct
	// sandbox mount performed during boot makes it non-empty when the Toshiba is visible to this process.
	options.storage.discovery.include_absent_slots = false;
	for (const std::string& dir : paths.usb_dirs)
	{
		if (dir == "/net" || dir.compare(0, 5, "/net/") == 0)
		{
			auto& nets = options.storage.discovery.network_roots;
			if (std::find(nets.begin(), nets.end(), dir) == nets.end())
				nets.push_back(dir);
		}
	}
	options.storage.image_inspector = std::make_shared<FrontendImageInspector>(paths);
	options.global_settings_path = paths.gs_ini;
	options.game_settings_dir = paths.settings_dir;
	options.memory_cards_dir = paths.memcards_dir;
	options.patches_dir = paths.patches_dir;
	options.cheats_dir = paths.cheats_dir;
	options.bios_dir = paths.bios_dir;
	options.last_game_path = paths.top_dir + "/lastgame.txt";
	options.build_tag = build_tag ? build_tag : "";
	options.scan_on_create = true;
	options.hooks.edit_settings_file = EditSettings;
	options.hooks.apply_recommended = [](const std::string& path, const std::string& preset_id, std::string* summary, std::string* error) {
		const std::string presets(reinterpret_cast<const char*>(fe_presets), reinterpret_cast<const char*>(fe_presets_end));
		return fe::settings::ApplyRecommended(path, presets, preset_id, "# Caliza recommended settings", *summary, *error);
	};
	options.hooks.create_memory_card = [](const std::string& dir, uint64_t mb, const std::string& name, std::string* made, std::string* error) {
		return fe::settings::CreateCard(dir, mb, name, *made, *error);
	};
	options.hooks.list_memory_card_files = [&paths] {
		std::vector<std::pair<std::string, uint64_t>> out;
		for (const fe::settings::CardFile& card : fe::settings::ListCards(paths.memcards_dir))
			out.emplace_back(card.name, card.bytes);
		return out;
	};
	options.hooks.bios_status = [&paths] {
		caliza::BiosStatus status;
		const std::string active_dir = ConfiguredBiosDir(paths);
		if (active_dir == paths.bios_dir && paths.bios_check)
			status.present = paths.bios_check();
		else
			status.present = HasPlausibleBiosFile(active_dir);
		if (!status.present && active_dir == paths.bios_dir && paths.bios_problem)
			status.problem = paths.bios_problem();
		else if (!status.present)
			status.problem = "no PS2 BIOS-sized file was found in the selected folder";
		status.path = active_dir;
		return status;
	};
	options.hooks.dynamic_choices = [&paths](const caliza::settings::SettingDef& definition) { return DynamicChoices(paths, definition); };
	options.hooks.list_patch_groups = [&paths](const caliza::GameEntry& game, bool cheats) { return ListPatchGroups(paths, game, cheats); };
	options.hooks.boot_log = [](const std::string& line) { BootLog(line); };
	options.hooks.web_panel_url = [] {
		std::string url, shown;
		return orbis_web_browser_url(url, shown) ? shown : std::string();
	};
	options.hooks.fetch_online_patches = [patch_service, service_tracking](const caliza::GameEntry& game, std::string* error) {
		if (!patch_service.begin)
		{
			*error = "the online patch service is disabled or unavailable";
			return false;
		}
		TrackSerial(service_tracking, game.serial);
		return patch_service.begin(ToFrontendGame(game));
	};
	options.hooks.download_texture_pack = [texture_service, service_tracking, &paths](const caliza::GameEntry& game, int pack_index, const std::string& pack_id, std::string* error) {
		if (!texture_service.begin)
		{
			*error = "the texture pack service is disabled or unavailable";
			return false;
		}
		const std::string stem = StemOfPath(game.file);
		const std::string settings_path = fe::GameSettingsPath(paths.settings_dir, game.serial, stem);
		const std::string header = "# " + game.title + (game.serial.empty() ? std::string() : " (" + game.serial + ")");
		TrackSerial(service_tracking, game.serial);
		return texture_service.begin(game.serial, pack_index, settings_path, header, game.title, pack_id);
	};
	options.hooks.import_texture_pack = [texture_service, service_tracking, &paths](const caliza::GameEntry& game,
		const std::string& archive, std::string* error) {
		if (!texture_service.import)
		{
			*error = "The texture pack service is unavailable";
			return false;
		}
		const std::string settings_path = fe::GameSettingsPath(paths.settings_dir, game.serial, StemOfPath(game.file));
		const std::string header = "# " + game.title + " (" + game.serial + ")";
		TrackSerial(service_tracking, game.serial);
		return texture_service.import(game.serial, archive, settings_path, header, game.title);
	};
	options.hooks.texture_packs = [texture_service](const caliza::GameEntry& game) {
		using Catalog = caliza::TexturePacks::Catalog;
		caliza::TexturePacks result;
		if (!texture_service.status || fe::NormalSerial(game.serial).empty())
		{
			result.catalog_message = game.serial.empty() ? "This game has no recognized serial" : "Catalog unavailable for this game";
			return result;
		}
		const auto status = texture_service.status(game.serial);
		result.catalog = status.catalog_ready ? Catalog::Ready : status.catalog_loading ? Catalog::Loading : Catalog::Unavailable;
		result.catalog_message = status.catalog_message;
		result.operation = TextureOperation(game.serial, status);
		result.installed = status.state == fe::TexturePackStatus::State::Installed;
		result.installed_name = status.installed;
		result.removable = status.ours;
		using State = fe::TexturePackStatus::State;
		if (status.state == State::Unavailable || status.state == State::Loading)
			result.operation = {};
		for (const fe::TexturePack& pack : status.packs)
		{
			std::string authors;
			for (const std::string& author : pack.authors)
				authors += (authors.empty() ? "" : ", ") + author;
			result.entries.push_back({pack.id, pack.name, authors, pack.source_url, pack.license, pack.bytes});
		}
		return result;
	};
	options.hooks.cancel_texture_pack = [texture_service, service_tracking](const caliza::GameEntry& game, std::string*) {
		TrackSerial(service_tracking, game.serial);
		return texture_service.cancel && texture_service.cancel(game.serial);
	};
	options.hooks.delete_texture_pack = [texture_service, service_tracking](const caliza::GameEntry& game, std::string*) {
		TrackSerial(service_tracking, game.serial);
		return texture_service.remove && texture_service.remove(game.serial);
	};
	options.hooks.retry_texture_pack_catalog = [texture_service](std::string*) {
		if (!texture_service.retry)
			return false;
		texture_service.retry();
		return true;
	};
	options.hooks.service_operations = [texture_service, patch_service, service_tracking] {
		return CollectServiceOperations(texture_service, patch_service, service_tracking);
	};

	caliza::ConsoleBackend backend(options);
	caliza::UiModel model(&backend);
	caliza::EstratosScene scene;
	if (!scene.InitFonts(fe_font_text, static_cast<size_t>(fe_font_text_end - fe_font_text), fe_font_icons,
			static_cast<size_t>(fe_font_icons_end - fe_font_icons)))
	{
		result.error = "font initialization failed";
		orbis_frontend_services_stop();
		display.Destroy();
		scePadClose(pad);
		return result;
	}
	caliza::Vk2DRenderer renderer;
	if (!renderer.Init(&display.vk, display.physical, display.device, display.queue_family, display.queue, display.extent.width,
			display.extent.height, VK_FORMAT_B8G8R8A8_UNORM, display.images, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
	{
		result.error = renderer.error();
		orbis_frontend_services_stop();
		display.Destroy();
		scePadClose(pad);
		return result;
	}
	renderer.SetNeutralTexture(scene.AtlasTexture());
	result.ui_ran = true;

	double last = NowSeconds();
	double last_snapshot_at = last;
	caliza::Snapshot snapshot = backend.GetSnapshot();
	caliza::DrawList draw;
	draw.Reserve(16384);
	const bool perf_log = EstratosPerfEnabled(paths);
	double perf_window_start = last;
	double perf_sum_frame_ms = 0.0;
	double perf_max_frame_ms = 0.0;
	double perf_sum_tick_ms = 0.0;
	double perf_sum_scene_ms = 0.0;
	double perf_sum_acquire_ms = 0.0;
	double perf_sum_render_ms = 0.0;
	double perf_sum_present_ms = 0.0;
	unsigned perf_frames = 0;
	unsigned perf_snapshots = 0;
	unsigned slot = 0;
	bool first_frame = true;
	caliza::ConsoleLaunchRequest launch;
	while (!backend.ConsumeLaunchRequest(&launch))
	{
		const double frame_start = NowSeconds();
		const double dt = frame_start - last;
		last = frame_start;
		PadData pad_data = {};
		pad_data.lx = pad_data.ly = 128;
		caliza::Input input;
		if (scePadReadState(pad, &pad_data) == 0)
		{
			input.left = (pad_data.buttons & kPadLeft) || pad_data.lx < 48;
			input.right = (pad_data.buttons & kPadRight) || pad_data.lx > 208;
			input.up = (pad_data.buttons & kPadUp) || pad_data.ly < 48;
			input.down = (pad_data.buttons & kPadDown) || pad_data.ly > 208;
			input.cross = pad_data.buttons & kPadCross;
			input.circle = pad_data.buttons & kPadCircle;
			input.square = pad_data.buttons & kPadSquare;
			input.triangle = pad_data.buttons & kPadTriangle;
			input.options = pad_data.buttons & kPadOptions;
			input.l1 = pad_data.buttons & kPadL1;
			input.r1 = pad_data.buttons & kPadR1;
			input.l2 = (pad_data.buttons & kPadL2) || pad_data.l2 > 160;
			input.r2 = (pad_data.buttons & kPadR2) || pad_data.r2 > 160;
		}
		const std::vector<caliza::CommandEvent> events = backend.PollEvents();
		const bool scan_active =
			snapshot.scan.state == caliza::ScanState::Scanning ||
			snapshot.scan.state == caliza::ScanState::Cancelling;
		const double refresh_period = scan_active ? 0.10 : 0.25;
		if (!events.empty() || (frame_start - last_snapshot_at) >= refresh_period)
		{
			snapshot = backend.GetSnapshot();
			last_snapshot_at = frame_start;
			perf_snapshots++;
		}
		model.Tick(snapshot, events, input, dt);
		const double t_after_tick = perf_log ? NowSeconds() : 0.0;
		scene.BuildInto(draw, model.BuildView(), display.extent.width, display.extent.height, frame_start);
		const double t_after_scene = perf_log ? NowSeconds() : 0.0;
		uint32_t image_index = 0;
		const VkResult acquired_result = display.vk.vkAcquireNextImageKHR(display.device, display.swapchain, UINT64_MAX,
			display.acquired[slot], VK_NULL_HANDLE, &image_index);
		if (acquired_result != VK_SUCCESS && acquired_result != VK_SUBOPTIMAL_KHR)
		{
			result.error = "vkAcquireNextImageKHR failed";
			break;
		}
		const double t_after_acquire = perf_log ? NowSeconds() : 0.0;
		if (!renderer.Render(draw, image_index, display.acquired[slot], display.rendered[slot]))
		{
			result.error = renderer.error();
			break;
		}
		const double t_after_render = perf_log ? NowSeconds() : 0.0;
		VkPresentInfoKHR present = {};
		present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present.waitSemaphoreCount = 1;
		present.pWaitSemaphores = &display.rendered[slot];
		present.swapchainCount = 1;
		present.pSwapchains = &display.swapchain;
		present.pImageIndices = &image_index;
		const VkResult presented = display.vk.vkQueuePresentKHR(display.queue, &present);
		if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR)
		{
			result.error = "vkQueuePresentKHR failed";
			break;
		}
		if (perf_log)
		{
			const double t_end = NowSeconds();
			const double frame_ms = (t_end - frame_start) * 1000.0;
			perf_sum_frame_ms += frame_ms;
			if (frame_ms > perf_max_frame_ms)
				perf_max_frame_ms = frame_ms;
			perf_sum_tick_ms += (t_after_tick - frame_start) * 1000.0;
			perf_sum_scene_ms += (t_after_scene - t_after_tick) * 1000.0;
			perf_sum_acquire_ms += (t_after_acquire - t_after_scene) * 1000.0;
			perf_sum_render_ms += (t_after_render - t_after_acquire) * 1000.0;
			perf_sum_present_ms += (t_end - t_after_render) * 1000.0;
			perf_frames++;
			const double window_s = t_end - perf_window_start;
			if (window_s >= 1.0 && perf_frames > 0)
			{
				const double inv = 1.0 / static_cast<double>(perf_frames);
				std::printf(
					"[estratos] fps=%.1f frame=%.2fms (max=%.2fms) | tick=%.2fms scene=%.2fms acquire=%.2fms render=%.2fms present=%.2fms | verts=%zu batches=%zu snaps=%u\n",
					static_cast<double>(perf_frames) / window_s, perf_sum_frame_ms * inv, perf_max_frame_ms,
					perf_sum_tick_ms * inv, perf_sum_scene_ms * inv, perf_sum_acquire_ms * inv,
					perf_sum_render_ms * inv, perf_sum_present_ms * inv, draw.VertexCount(),
					draw.Batches().size(), perf_snapshots);
				std::fflush(stdout);
				perf_window_start = t_end;
				perf_sum_frame_ms = 0.0;
				perf_max_frame_ms = 0.0;
				perf_sum_tick_ms = 0.0;
				perf_sum_scene_ms = 0.0;
				perf_sum_acquire_ms = 0.0;
				perf_sum_render_ms = 0.0;
				perf_sum_present_ms = 0.0;
				perf_frames = 0;
				perf_snapshots = 0;
			}
		}
		if (first_frame)
		{
			orbis_hide_splash();
			first_frame = false;
		}
		slot ^= 1;
	}
	backend.Shutdown();
	orbis_frontend_services_stop();
	renderer.WaitIdle();
	renderer.Shutdown();
	display.Destroy();
	scePadClose(pad);
	if (launch.kind == caliza::ConsoleLaunchRequest::Kind::Game)
	{
		if (launch.smart_tune)
		{
			const std::string flags = paths.top_dir + "/flags";
			mkdir(flags.c_str(), 0777);
			fe::settings::WriteFileAtomic(flags + "/smarttune", "one-shot\n");
		}
		WriteLastGame(paths, launch.path);
		result.kind = ShellResult::Kind::Game;
		result.path = launch.path;
		result.error.clear();
	}
	else if (launch.kind == caliza::ConsoleLaunchRequest::Kind::SystemMenu)
	{
		result.kind = ShellResult::Kind::SystemMenu;
		result.path = kOrbisSystemMenuPath;
		result.error.clear();
	}
	else if (result.error.empty())
		result.error = "the interface ended without a boot selection";
	return result;
}
} // namespace caliza::ps4shell

#else
namespace caliza::ps4shell
{
ShellResult RunEstratos(const OrbisFrontendPaths&, const char*)
{
	ShellResult result;
	result.error = "the native shell requires the ORBIS_PS4 platform build";
	return result;
}
} // namespace caliza::ps4shell
#endif
