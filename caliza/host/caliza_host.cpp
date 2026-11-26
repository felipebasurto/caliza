// Caliza Estratos host preview: scripted software-Vulkan renderer and PNG capture.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../core/ui_model.h"
#include "../native/estratos_scene.h"
#include "../native/vk2d_renderer.h"
#include "host_demo.h"

#include "../../ps5/frontend/fe_vk.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{
using namespace caliza;

bool ReadFile(const char* path, std::vector<uint8_t>& out)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	file.seekg(0, std::ios::end);
	const std::streamoff size = file.tellg();
	file.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(size));
	return size >= 0 && file.read(reinterpret_cast<char*>(out.data()), size).good();
}

uint32_t BE32(uint32_t value)
{
	return ((value & 0xff) << 24) | ((value & 0xff00) << 8) | ((value >> 8) & 0xff00) | ((value >> 24) & 0xff);
}

void Append32(std::vector<uint8_t>& out, uint32_t value)
{
	value = BE32(value);
	const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
	out.insert(out.end(), bytes, bytes + 4);
}

void PngChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& data)
{
	Append32(png, static_cast<uint32_t>(data.size()));
	const size_t crc_at = png.size();
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), data.begin(), data.end());
	const uLong crc = crc32(0, png.data() + crc_at, static_cast<uInt>(png.size() - crc_at));
	Append32(png, static_cast<uint32_t>(crc));
}

bool WritePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba)
{
	std::vector<uint8_t> raw;
	raw.reserve(static_cast<size_t>(height) * (width * 4 + 1));
	for (uint32_t y = 0; y < height; y++)
	{
		raw.push_back(0);
		const size_t row = static_cast<size_t>(y) * width * 4;
		raw.insert(raw.end(), rgba.begin() + row, rgba.begin() + row + width * 4);
	}
	uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
	std::vector<uint8_t> compressed(compressed_size);
	if (compress2(compressed.data(), &compressed_size, raw.data(), static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK)
		return false;
	compressed.resize(compressed_size);
	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
	std::vector<uint8_t> ihdr;
	Append32(ihdr, width);
	Append32(ihdr, height);
	ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
	PngChunk(png, "IHDR", ihdr);
	PngChunk(png, "IDAT", compressed);
	PngChunk(png, "IEND", {});
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
	return out.good();
}

struct VulkanHost
{
	void* library = nullptr;
	fe::Vk vk;
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	uint32_t queue_family = 0;
	VkCommandPool setup_pool = VK_NULL_HANDLE;
	std::vector<VkImage> images;
	std::vector<VkDeviceMemory> memories;
	uint32_t width = 0, height = 0;
	Vk2DRenderer renderer;

	bool Load(const std::string& library_path)
	{
		library = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (!library)
		{
			std::cerr << "dlopen Vulkan failed: " << dlerror() << "\n";
			return false;
		}
		auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
		const char* missing = nullptr;
		if (!gipa || !vk.LoadGlobal(gipa, &missing))
		{
			std::cerr << "missing Vulkan global entry point: " << (missing ? missing : "vkGetInstanceProcAddr") << "\n";
			return false;
		}
		return true;
	}

	bool Create(uint32_t w, uint32_t h)
	{
		width = w;
		height = h;
		VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
		app.pApplicationName = "Caliza Estratos Host";
		app.apiVersion = VK_API_VERSION_1_1;
		VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		instance_info.pApplicationInfo = &app;
		if (vk.vkCreateInstance(&instance_info, nullptr, &instance) != VK_SUCCESS)
		{
			std::cerr << "vkCreateInstance failed\n";
			return false;
		}
		const char* missing = nullptr;
		if (!vk.LoadInstance(instance, false, &missing))
		{
			std::cerr << "missing Vulkan instance entry point: " << missing << "\n";
			return false;
		}
		uint32_t count = 0;
		vk.vkEnumeratePhysicalDevices(instance, &count, nullptr);
		if (!count)
		{
			std::cerr << "no Vulkan physical device\n";
			return false;
		}
		std::vector<VkPhysicalDevice> devices(count);
		vk.vkEnumeratePhysicalDevices(instance, &count, devices.data());
		physical = devices[0];
		uint32_t family_count = 0;
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, nullptr);
		std::vector<VkQueueFamilyProperties> families(family_count);
		vk.vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, families.data());
		bool found = false;
		for (uint32_t i = 0; i < family_count; i++)
			if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				queue_family = i;
				found = true;
				break;
			}
		if (!found)
		{
			std::cerr << "no graphics queue family\n";
			return false;
		}
		const float priority = 1.0f;
		VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queue_info.queueFamilyIndex = queue_family;
		queue_info.queueCount = 1;
		queue_info.pQueuePriorities = &priority;
		VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		device_info.queueCreateInfoCount = 1;
		device_info.pQueueCreateInfos = &queue_info;
		if (vk.vkCreateDevice(physical, &device_info, nullptr, &device) != VK_SUCCESS)
		{
			std::cerr << "vkCreateDevice failed\n";
			return false;
		}
		if (!vk.LoadDevice(device, false, &missing))
		{
			std::cerr << "missing Vulkan device entry point: " << missing << "\n";
			return false;
		}
		vk.vkGetDeviceQueue(device, queue_family, 0, &queue);
		VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		pool.queueFamilyIndex = queue_family;
		vk.vkCreateCommandPool(device, &pool, nullptr, &setup_pool);
		for (int i = 0; i < 2; i++)
		{
			VkImageCreateInfo image = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
			image.imageType = VK_IMAGE_TYPE_2D;
			image.format = VK_FORMAT_B8G8R8A8_UNORM;
			image.extent = {width, height, 1};
			image.mipLevels = 1;
			image.arrayLayers = 1;
			image.samples = VK_SAMPLE_COUNT_1_BIT;
			image.tiling = VK_IMAGE_TILING_OPTIMAL;
			image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImage handle = VK_NULL_HANDLE;
			if (vk.vkCreateImage(device, &image, nullptr, &handle) != VK_SUCCESS)
			{
				std::cerr << "vkCreateImage(output) failed\n";
				return false;
			}
			VkMemoryRequirements requirements;
			vk.vkGetImageMemoryRequirements(device, handle, &requirements);
			VkPhysicalDeviceMemoryProperties properties;
			vk.vkGetPhysicalDeviceMemoryProperties(physical, &properties);
			uint32_t type = UINT32_MAX;
			for (uint32_t t = 0; t < properties.memoryTypeCount; t++)
				if ((requirements.memoryTypeBits & (1u << t)) &&
					(properties.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
				{
					type = t;
					break;
				}
			if (type == UINT32_MAX)
				type = 0;
			VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
			allocate.allocationSize = requirements.size;
			allocate.memoryTypeIndex = type;
			VkDeviceMemory memory = VK_NULL_HANDLE;
			if (vk.vkAllocateMemory(device, &allocate, nullptr, &memory) != VK_SUCCESS ||
				vk.vkBindImageMemory(device, handle, memory, 0) != VK_SUCCESS)
			{
				std::cerr << "output image memory failed\n";
				return false;
			}
			images.push_back(handle);
			memories.push_back(memory);
		}
		if (!PrepareImages())
			return false;
		// The scripted preview is software-Vulkan only; console output handoff needs proper testing.
		if (!renderer.Init(&vk, physical, device, queue_family, queue, width, height, VK_FORMAT_B8G8R8A8_UNORM, images,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
		{
			std::cerr << "renderer init: " << renderer.error() << "\n";
			return false;
		}
		return true;
	}

	bool PrepareImages()
	{
		VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = setup_pool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		VkCommandBuffer cmd = VK_NULL_HANDLE;
		if (vk.vkAllocateCommandBuffers(device, &allocate, &cmd) != VK_SUCCESS)
			return false;
		VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		vk.vkBeginCommandBuffer(cmd, &begin);
		for (VkImage image : images)
		{
			VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			vk.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
				nullptr, 0, nullptr, 1, &barrier);
		}
		vk.vkEndCommandBuffer(cmd);
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		const bool ok = vk.vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
			vk.vkQueueWaitIdle(queue) == VK_SUCCESS;
		vk.vkFreeCommandBuffers(device, setup_pool, 1, &cmd);
		return ok;
	}

	~VulkanHost()
	{
		if (device)
		{
			vk.vkDeviceWaitIdle(device);
			renderer.Shutdown();
			for (VkImage image : images)
				vk.vkDestroyImage(device, image, nullptr);
			for (VkDeviceMemory memory : memories)
				vk.vkFreeMemory(device, memory, nullptr);
			if (setup_pool)
				vk.vkDestroyCommandPool(device, setup_pool, nullptr);
			vk.vkDestroyDevice(device, nullptr);
		}
		if (instance)
			vk.vkDestroyInstance(instance, nullptr);
		if (library)
			dlclose(library);
	}
};

Input KeyInput(const std::string& key)
{
	Input in;
	if (key == "left") in.left = true;
	else if (key == "right") in.right = true;
	else if (key == "up") in.up = true;
	else if (key == "down") in.down = true;
	else if (key == "cross" || key == "x") in.cross = true;
	else if (key == "circle") in.circle = true;
	else if (key == "square") in.square = true;
	else if (key == "triangle") in.triangle = true;
	else if (key == "options" || key == "start") in.options = true;
	else if (key == "l1") in.l1 = true;
	else if (key == "r1") in.r1 = true;
	else if (key == "l2") in.l2 = true;
	else if (key == "r2") in.r2 = true;
	return in;
}

bool ValidKey(const std::string& key)
{
	static const char* keys[] = {"left", "right", "up", "down", "cross", "x", "circle", "square", "triangle", "options", "start", "l1", "r1", "l2", "r2"};
	for (const char* candidate : keys)
		if (key == candidate)
			return true;
	return false;
}

std::vector<std::string> ScriptLines(const std::string& script)
{
	std::vector<std::string> out;
	std::string current;
	for (char c : script)
	{
		if (c == ';' || c == '\n')
		{
			out.push_back(current);
			current.clear();
		}
		else
			current.push_back(c);
	}
	out.push_back(current);
	for (std::string& line : out)
	{
		const size_t comment = line.find('#');
		if (comment != std::string::npos)
			line.resize(comment);
		const size_t first = line.find_first_not_of(" \t\r");
		const size_t last = line.find_last_not_of(" \t\r");
		line = first == std::string::npos ? std::string() : line.substr(first, last - first + 1);
	}
	out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }), out.end());
	return out;
}

struct HostContext
{
	DemoBackend backend;
	UiModel model;
	EstratosScene scene;
	VulkanHost vulkan;
	double seconds = 0.0;
	int output_index = 0;
	HostContext() : model(&backend) {}

	void Tick(const Input& input, double dt)
	{
		seconds += dt;
		model.Tick(backend.GetSnapshot(), backend.PollEvents(), input, dt);
		// Build every frame, as the console does, so transitions start when the view changes, not when captured.
		(void)scene.Build(model.BuildView(), vulkan.width, vulkan.height, seconds);
	}

	bool Shot(const std::string& path)
	{
		UiView view = model.BuildView();
		DrawList draw = scene.Build(view, vulkan.width, vulkan.height, seconds);
		if (!vulkan.renderer.Render(draw, static_cast<uint32_t>(output_index)))
		{
			std::cerr << "render failed: " << vulkan.renderer.error() << "\n";
			return false;
		}
		std::vector<uint8_t> rgba;
		if (!vulkan.renderer.ReadPixels(static_cast<uint32_t>(output_index), rgba))
		{
			std::cerr << "readback failed: " << vulkan.renderer.error() << "\n";
			return false;
		}
		if (!WritePng(path, vulkan.width, vulkan.height, rgba))
		{
			std::cerr << "PNG write failed: " << path << "\n";
			return false;
		}
		std::cout << "shot " << path << " screen=" << view.screen_name << " focus=" << view.focus_id << "\n";
		output_index = (output_index + 1) % static_cast<int>(vulkan.images.size());
		return true;
	}
};

void Usage()
{
	std::cout << "caliza_host --width 1920 --height 1080 --script <file-or-script> [--vulkan <dylib>]\n"
		"commands, separated by semicolons or newlines:\n"
		"  wait <ms>                  run the model clock\n"
		"  press <button>             cross/circle/square/triangle/dpad/options/l1/r1\n"
		"  hold <button> <ms>         hold a movement/button, then release\n"
		"  shot <path.png>            render and capture PNG\n"
		"  scale <100-200>            host accessibility override\n"
		"  contrast <0|1>             host accessibility override\n"
		"  motion <0|1>               host accessibility override\n"
		"  bios <0|1>                 toggle demo BIOS recovery state\n"
		"  games <0|1>                toggle the fictional demo library\n"
		"  expect-screen <name>       fail unless the model is on that screen\n";
}

} // namespace

int main(int argc, char** argv)
{
	uint32_t width = 1920, height = 1080;
	std::string script;
	std::string library = "/Applications/Google Chrome.app/Contents/Frameworks/Google Chrome Framework.framework/Versions/Current/Libraries/libvk_swiftshader.dylib";
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		auto need = [&](const char* name) -> const char* {
			if (++i >= argc)
			{
				std::cerr << "missing value for " << name << "\n";
				std::exit(2);
			}
			return argv[i];
		};
		if (arg == "--width") width = static_cast<uint32_t>(std::strtoul(need("--width"), nullptr, 10));
		else if (arg == "--height") height = static_cast<uint32_t>(std::strtoul(need("--height"), nullptr, 10));
		else if (arg == "--script") script = need("--script");
		else if (arg == "--vulkan") library = need("--vulkan");
		else if (arg == "--help" || arg == "-h")
		{
			Usage();
			return 0;
		}
		else
		{
			std::cerr << "unknown argument: " << arg << "\n";
			Usage();
			return 2;
		}
	}
	if (width < 640 || height < 360)
	{
		std::cerr << "width/height are too small\n";
		return 2;
	}
	if (std::ifstream(script).good())
	{
		std::ifstream file(script);
		std::ostringstream data;
		data << file.rdbuf();
		script = data.str();
	}
	if (script.empty())
		script = "wait 1400; shot estratos-home.png";
	HostContext host;
	std::vector<uint8_t> roboto, prompt;
	if (!ReadFile("bin/resources/fonts/Roboto-Regular.ttf", roboto) || !ReadFile("bin/resources/fonts/promptfont.otf", prompt))
	{
		std::cerr << "could not read bundled Roboto/PromptFont resources\n";
		return 1;
	}
	if (!host.scene.InitFonts(roboto.data(), roboto.size(), prompt.data(), prompt.size()))
	{
		std::cerr << "font initialisation failed\n";
		return 1;
	}
	if (!host.vulkan.Load(library) || !host.vulkan.Create(width, height))
		return 1;
	if (!host.vulkan.renderer.SetNeutralTexture(host.scene.AtlasTexture()))
	{
		std::cerr << "font atlas upload failed: " << host.vulkan.renderer.error() << "\n";
		return 1;
	}
	Input none;
	host.Tick(none, 1.0 / 60.0);
	for (const std::string& line : ScriptLines(script))
	{
		std::istringstream command(line);
		std::string verb;
		command >> verb;
		if (verb == "wait")
		{
			int ms = 0;
			command >> ms;
			for (int frames = std::max(1, ms * 60 / 1000); frames > 0; frames--)
				host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "press")
		{
			std::string key;
			command >> key;
			if (!ValidKey(key))
			{
				std::cerr << "unknown button in script: " << key << "\n";
				return 2;
			}
			host.Tick(KeyInput(key), 1.0 / 60.0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "hold")
		{
			std::string key;
			int ms = 0;
			command >> key >> ms;
			if (!ValidKey(key))
			{
				std::cerr << "unknown button in script: " << key << "\n";
				return 2;
			}
			const Input held = KeyInput(key);
			for (int frames = std::max(1, ms * 60 / 1000); frames > 0; frames--)
				host.Tick(held, 1.0 / 60.0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "shot" || verb == "snap")
		{
			// shot lets transitions settle first; snap captures the frame as it is.
			std::string path;
			command >> path;
			if (verb == "shot")
				for (int frames = 27; frames > 0; frames--)
					host.Tick(none, 1.0 / 60.0);
			if (path.empty() || !host.Shot(path))
				return 1;
		}
		else if (verb == "clip")
		{
			// clip <path-prefix> <frames> <step-ms>: a numbered frame sequence for checking motion.
			std::string prefix;
			int frames = 0, step = 33;
			command >> prefix >> frames >> step;
			for (int i = 0; i < frames; i++)
			{
				char name[32];
				std::snprintf(name, sizeof(name), "-%03d.png", i);
				if (!host.Shot(prefix + name))
					return 1;
				for (int f = std::max(1, step * 60 / 1000); f > 0; f--)
					host.Tick(none, 1.0 / 60.0);
			}
		}
		else if (verb == "scale")
		{
			uint32_t percent = 100;
			command >> percent;
			host.backend.SetTextScale(std::max(100u, std::min(200u, percent)));
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "bios")
		{
			int enabled = 0;
			command >> enabled;
			host.backend.SetBiosPresent(enabled != 0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "games")
		{
			int present = 1;
			command >> present;
			host.backend.SetLibraryEmpty(present == 0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "contrast")
		{
			int enabled = 0;
			command >> enabled;
			host.backend.SetHighContrast(enabled != 0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "motion")
		{
			int enabled = 0;
			command >> enabled;
			host.backend.SetReduceMotion(enabled != 0);
			host.Tick(none, 1.0 / 60.0);
		}
		else if (verb == "expect-screen")
		{
			std::string expected;
			command >> expected;
			const std::string actual = host.model.BuildView().screen_name;
			if (actual != expected)
			{
				std::cerr << "expected screen " << expected << ", got " << actual << " after: " << line << "\n";
				return 1;
			}
		}
		else
		{
			std::cerr << "unknown script command: " << line << "\n";
			return 2;
		}
	}
	return 0;
}
