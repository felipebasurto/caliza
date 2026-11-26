// Caliza Estratos: small Vulkan 2D renderer for the native interface.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "draw_list.h"
#include "../../ps5/frontend/fe_vk.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace caliza
{
// Renders DrawList batches directly into caller-owned output images. The console adapter can pass
// swapchain images; the host passes offscreen images. The caller owns instance/device/swapchain policy.
class Vk2DRenderer
{
public:
	bool Init(fe::Vk* vk, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, VkQueue queue,
		uint32_t width, uint32_t height, VkFormat output_format, const std::vector<VkImage>& output_images,
		VkImageLayout output_final_layout);
	void Shutdown();

	bool SetNeutralTexture(const Texture2D* texture); // font atlas; also stands in for solid geometry
	bool Render(const DrawList& draw, uint32_t output_index, VkSemaphore wait = VK_NULL_HANDLE,
		VkSemaphore signal = VK_NULL_HANDLE);
	bool ReadPixels(uint32_t output_index, std::vector<uint8_t>& rgba);
	void WaitIdle();
	const std::string& error() const { return m_error; }

private:
	struct Buffer
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		void* mapped = nullptr;
		VkDeviceSize size = 0;
	};
	struct GpuTexture
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkDescriptorSet descriptor = VK_NULL_HANDLE;
		uint32_t width = 0, height = 0;
	};
	struct Slot
	{
		VkCommandBuffer command = VK_NULL_HANDLE;
		VkFence fence = VK_NULL_HANDLE;
		Buffer vertices;
		bool submitted = false;
	};

	bool Fail(const char* what, VkResult result);
	uint32_t MemoryType(uint32_t bits, VkMemoryPropertyFlags required) const;
	bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out);
	void DestroyBuffer(Buffer& buffer);
	bool CreateImage(uint32_t width, uint32_t height, VkFormat format, GpuTexture& out);
	bool UploadTexture(const Texture2D& source, GpuTexture& out);
	GpuTexture* EnsureTexture(const Texture2D* texture);
	void DestroyTexture(GpuTexture& texture);
	bool CreateRenderResources();
	bool CreatePipeline();
	bool OneShotCommands(bool (*record)(void*, VkCommandBuffer), void* user);
	void ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after,
		VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage);

	fe::Vk* m_vk = nullptr;
	VkPhysicalDevice m_physical = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueue m_queue = VK_NULL_HANDLE;
	uint32_t m_queue_family = 0;
	VkPhysicalDeviceMemoryProperties m_memory_properties = {};
	uint32_t m_width = 0, m_height = 0;
	VkFormat m_output_format = VK_FORMAT_UNDEFINED;
	VkImageLayout m_output_final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	std::vector<VkImage> m_output_images;
	std::vector<VkImageView> m_output_views;
	std::vector<VkFramebuffer> m_framebuffers;
	VkRenderPass m_render_pass = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_texture_layout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;
	VkCommandPool m_command_pool = VK_NULL_HANDLE;
	std::unordered_map<const Texture2D*, GpuTexture> m_textures;
	const Texture2D* m_neutral = nullptr;
	static constexpr int kSlots = 2;
	static constexpr VkDeviceSize kVertexBytes = 4u << 20;
	Slot m_slots[kSlots];
	int m_slot = 0;
	std::string m_error;
};
} // namespace caliza
