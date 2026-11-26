// Caliza Estratos: small Vulkan 2D renderer for the native interface.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vk2d_renderer.h"

#include "estratos_shaders.inc"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace caliza
{
namespace
{
struct PushBlock
{
	float screen[4];
};

VkFormat TextureFormat(Texture2D::Format format)
{
	return format == Texture2D::Format::R8 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}
} // namespace

bool Vk2DRenderer::Fail(const char* what, VkResult result)
{
	char message[256];
	std::snprintf(message, sizeof(message), "%s failed (VkResult %d)", what, static_cast<int>(result));
	m_error = message;
	return false;
}

uint32_t Vk2DRenderer::MemoryType(uint32_t bits, VkMemoryPropertyFlags required) const
{
	for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (m_memory_properties.memoryTypes[i].propertyFlags & required) == required)
			return i;
	for (uint32_t i = 0; i < m_memory_properties.memoryTypeCount; i++)
		if (bits & (1u << i))
			return i;
	return UINT32_MAX;
}

bool Vk2DRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out)
{
	VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = m_vk->vkCreateBuffer(m_device, &buffer_info, nullptr, &out.buffer);
	if (result != VK_SUCCESS)
		return Fail("vkCreateBuffer", result);
	VkMemoryRequirements requirements;
	m_vk->vkGetBufferMemoryRequirements(m_device, out.buffer, &requirements);
	VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = MemoryType(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	result = m_vk->vkAllocateMemory(m_device, &allocate, nullptr, &out.memory);
	if (result != VK_SUCCESS)
		return Fail("vkAllocateMemory(buffer)", result);
	result = m_vk->vkBindBufferMemory(m_device, out.buffer, out.memory, 0);
	if (result != VK_SUCCESS)
		return Fail("vkBindBufferMemory", result);
	result = m_vk->vkMapMemory(m_device, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped);
	if (result != VK_SUCCESS)
		return Fail("vkMapMemory", result);
	out.size = size;
	return true;
}

void Vk2DRenderer::DestroyBuffer(Buffer& buffer)
{
	if (buffer.mapped)
		m_vk->vkUnmapMemory(m_device, buffer.memory);
	if (buffer.buffer)
		m_vk->vkDestroyBuffer(m_device, buffer.buffer, nullptr);
	if (buffer.memory)
		m_vk->vkFreeMemory(m_device, buffer.memory, nullptr);
	buffer = Buffer();
}

bool Vk2DRenderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, GpuTexture& out)
{
	VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = format;
	image_info.extent = {width, height, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = m_vk->vkCreateImage(m_device, &image_info, nullptr, &out.image);
	if (result != VK_SUCCESS)
		return Fail("vkCreateImage(texture)", result);
	VkMemoryRequirements requirements;
	m_vk->vkGetImageMemoryRequirements(m_device, out.image, &requirements);
	VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = m_vk->vkAllocateMemory(m_device, &allocate, nullptr, &out.memory);
	if (result != VK_SUCCESS)
		return Fail("vkAllocateMemory(texture)", result);
	result = m_vk->vkBindImageMemory(m_device, out.image, out.memory, 0);
	if (result != VK_SUCCESS)
		return Fail("vkBindImageMemory(texture)", result);
	VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	view.image = out.image;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	result = m_vk->vkCreateImageView(m_device, &view, nullptr, &out.view);
	if (result != VK_SUCCESS)
		return Fail("vkCreateImageView(texture)", result);
	out.width = width;
	out.height = height;
	return true;
}

void Vk2DRenderer::DestroyTexture(GpuTexture& texture)
{
	if (texture.view)
		m_vk->vkDestroyImageView(m_device, texture.view, nullptr);
	if (texture.image)
		m_vk->vkDestroyImage(m_device, texture.image, nullptr);
	if (texture.memory)
		m_vk->vkFreeMemory(m_device, texture.memory, nullptr);
	texture = GpuTexture();
}

void Vk2DRenderer::ImageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after,
	VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
{
	VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	barrier.srcAccessMask = src_access;
	barrier.dstAccessMask = dst_access;
	barrier.oldLayout = before;
	barrier.newLayout = after;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	m_vk->vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool Vk2DRenderer::OneShotCommands(bool (*record)(void*, VkCommandBuffer), void* user)
{
	VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	allocate.commandPool = m_command_pool;
	allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate.commandBufferCount = 1;
	VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkResult result = m_vk->vkAllocateCommandBuffers(m_device, &allocate, &cmd);
	if (result != VK_SUCCESS)
		return Fail("vkAllocateCommandBuffers(one-shot)", result);
	VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = m_vk->vkBeginCommandBuffer(cmd, &begin);
	bool ok = result == VK_SUCCESS && record(user, cmd);
	if (ok)
		result = m_vk->vkEndCommandBuffer(cmd);
	ok = ok && result == VK_SUCCESS;
	if (ok)
	{
		VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &cmd;
		result = m_vk->vkQueueSubmit(m_queue, 1, &submit, VK_NULL_HANDLE);
		ok = result == VK_SUCCESS;
	}
	if (ok)
		result = m_vk->vkQueueWaitIdle(m_queue);
	ok = ok && result == VK_SUCCESS;
	m_vk->vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
	if (!ok)
		return Fail("one-shot command", result);
	return true;
}

bool Vk2DRenderer::UploadTexture(const Texture2D& source, GpuTexture& out)
{
	if (!CreateImage(static_cast<uint32_t>(source.width), static_cast<uint32_t>(source.height), TextureFormat(source.format), out))
		return false;
	Buffer staging;
	const VkDeviceSize bytes = static_cast<VkDeviceSize>(source.pixels.size());
	if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
		return false;
	std::memcpy(staging.mapped, source.pixels.data(), static_cast<size_t>(bytes));
	struct Upload
	{
		Vk2DRenderer* self;
		GpuTexture* texture;
		Buffer* staging;
	} upload{this, &out, &staging};
	auto record = [](void* user_data, VkCommandBuffer cmd) {
		Upload* up = static_cast<Upload*>(user_data);
		up->self->ImageBarrier(cmd, up->texture->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferImageCopy copy = {};
		copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copy.imageExtent = {up->texture->width, up->texture->height, 1};
		up->self->m_vk->vkCmdCopyBufferToImage(cmd, up->staging->buffer, up->texture->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
		up->self->ImageBarrier(cmd, up->texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		return true;
	};
	const bool ok = OneShotCommands(record, &upload);
	DestroyBuffer(staging);
	return ok;
}

Vk2DRenderer::GpuTexture* Vk2DRenderer::EnsureTexture(const Texture2D* texture)
{
	if (!texture)
		texture = m_neutral;
	if (!texture)
		return nullptr;
	auto it = m_textures.find(texture);
	if (it != m_textures.end())
		return &it->second;
	GpuTexture gpu;
	if (!UploadTexture(*texture, gpu))
		return nullptr;
	VkDescriptorSetAllocateInfo allocate = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	allocate.descriptorPool = m_descriptor_pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &m_texture_layout;
	VkResult result = m_vk->vkAllocateDescriptorSets(m_device, &allocate, &gpu.descriptor);
	if (result != VK_SUCCESS)
	{
		Fail("vkAllocateDescriptorSets(texture)", result);
		DestroyTexture(gpu);
		return nullptr;
	}
	VkDescriptorImageInfo image = {};
	image.sampler = m_sampler;
	image.imageView = gpu.view;
	image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	write.dstSet = gpu.descriptor;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	m_vk->vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
	const auto [inserted, inserted_ok] = m_textures.emplace(texture, gpu);
	(void)inserted_ok;
	return &inserted->second;
}

bool Vk2DRenderer::CreateRenderResources()
{
	// Output render pass: one color attachment, no depth, classic render-pass compatibility for the console driver.
	VkAttachmentDescription attachment = {};
	attachment.format = m_output_format;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.initialLayout = m_output_final_layout;
	attachment.finalLayout = m_output_final_layout;
	VkAttachmentReference color_reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_reference;
	VkRenderPassCreateInfo pass_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	pass_info.attachmentCount = 1;
	pass_info.pAttachments = &attachment;
	pass_info.subpassCount = 1;
	pass_info.pSubpasses = &subpass;
	VkResult result = m_vk->vkCreateRenderPass(m_device, &pass_info, nullptr, &m_render_pass);
	if (result != VK_SUCCESS)
		return Fail("vkCreateRenderPass", result);

	m_output_views.resize(m_output_images.size());
	m_framebuffers.resize(m_output_images.size());
	for (size_t i = 0; i < m_output_images.size(); i++)
	{
		VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		view.image = m_output_images[i];
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = m_output_format;
		view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		result = m_vk->vkCreateImageView(m_device, &view, nullptr, &m_output_views[i]);
		if (result != VK_SUCCESS)
			return Fail("vkCreateImageView(output)", result);
		VkFramebufferCreateInfo framebuffer = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		framebuffer.renderPass = m_render_pass;
		framebuffer.attachmentCount = 1;
		framebuffer.pAttachments = &m_output_views[i];
		framebuffer.width = m_width;
		framebuffer.height = m_height;
		framebuffer.layers = 1;
		result = m_vk->vkCreateFramebuffer(m_device, &framebuffer, nullptr, &m_framebuffers[i]);
		if (result != VK_SUCCESS)
			return Fail("vkCreateFramebuffer", result);
	}

	VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool.queueFamilyIndex = m_queue_family;
	result = m_vk->vkCreateCommandPool(m_device, &pool, nullptr, &m_command_pool);
	if (result != VK_SUCCESS)
		return Fail("vkCreateCommandPool", result);
	for (Slot& slot : m_slots)
	{
		VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		allocate.commandPool = m_command_pool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 1;
		result = m_vk->vkAllocateCommandBuffers(m_device, &allocate, &slot.command);
		if (result != VK_SUCCESS)
			return Fail("vkAllocateCommandBuffers", result);
		VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		result = m_vk->vkCreateFence(m_device, &fence, nullptr, &slot.fence);
		if (result != VK_SUCCESS)
			return Fail("vkCreateFence", result);
		if (!CreateBuffer(kVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, slot.vertices))
			return false;
	}
	return true;
}

bool Vk2DRenderer::CreatePipeline()
{
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	layout_info.bindingCount = 1;
	layout_info.pBindings = &binding;
	VkResult result = m_vk->vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &m_texture_layout);
	if (result != VK_SUCCESS)
		return Fail("vkCreateDescriptorSetLayout", result);
	VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushBlock)};
	VkPipelineLayoutCreateInfo pipeline_layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	pipeline_layout.setLayoutCount = 1;
	pipeline_layout.pSetLayouts = &m_texture_layout;
	pipeline_layout.pushConstantRangeCount = 1;
	pipeline_layout.pPushConstantRanges = &push;
	result = m_vk->vkCreatePipelineLayout(m_device, &pipeline_layout, nullptr, &m_pipeline_layout);
	if (result != VK_SUCCESS)
		return Fail("vkCreatePipelineLayout", result);

	VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
	VkDescriptorPoolCreateInfo pool = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	pool.maxSets = 256;
	pool.poolSizeCount = 1;
	pool.pPoolSizes = &pool_size;
	result = m_vk->vkCreateDescriptorPool(m_device, &pool, nullptr, &m_descriptor_pool);
	if (result != VK_SUCCESS)
		return Fail("vkCreateDescriptorPool", result);
	VkSamplerCreateInfo sampler = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	result = m_vk->vkCreateSampler(m_device, &sampler, nullptr, &m_sampler);
	if (result != VK_SUCCESS)
		return Fail("vkCreateSampler", result);

	auto module = [&](const uint32_t* words, size_t bytes, VkShaderModule* out) {
		VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		info.codeSize = bytes;
		info.pCode = words;
		return m_vk->vkCreateShaderModule(m_device, &info, nullptr, out);
	};
	VkShaderModule vertex_module = VK_NULL_HANDLE, fragment_module = VK_NULL_HANDLE;
	result = module(caliza_spv_estratos2d_vert, sizeof(caliza_spv_estratos2d_vert), &vertex_module);
	if (result != VK_SUCCESS)
		return Fail("vkCreateShaderModule(vertex)", result);
	result = module(caliza_spv_estratos2d_frag, sizeof(caliza_spv_estratos2d_frag), &fragment_module);
	if (result != VK_SUCCESS)
		return Fail("vkCreateShaderModule(fragment)", result);
	VkPipelineShaderStageCreateInfo stages[2] = {};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertex_module;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragment_module;
	stages[1].pName = "main";

	VkVertexInputBindingDescription binding_description = {0, sizeof(DrawVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	VkVertexInputAttributeDescription attributes[4] = {};
	attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(DrawVertex, x)};
	attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(DrawVertex, u)};
	attributes[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DrawVertex, r)};
	attributes[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(DrawVertex, p0)};
	VkPipelineVertexInputStateCreateInfo vertex_input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertex_input.vertexBindingDescriptionCount = 1;
	vertex_input.pVertexBindingDescriptions = &binding_description;
	vertex_input.vertexAttributeDescriptionCount = 4;
	vertex_input.pVertexAttributeDescriptions = attributes;
	VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.cullMode = VK_CULL_MODE_NONE;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	raster.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState blend_attachment = {};
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	blend.attachmentCount = 1;
	blend.pAttachments = &blend_attachment;
	const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamic.dynamicStateCount = 2;
	dynamic.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	pipeline.stageCount = 2;
	pipeline.pStages = stages;
	pipeline.pVertexInputState = &vertex_input;
	pipeline.pInputAssemblyState = &assembly;
	pipeline.pViewportState = &viewport;
	pipeline.pRasterizationState = &raster;
	pipeline.pMultisampleState = &multisample;
	pipeline.pColorBlendState = &blend;
	pipeline.pDynamicState = &dynamic;
	pipeline.layout = m_pipeline_layout;
	pipeline.renderPass = m_render_pass;
	pipeline.subpass = 0;
	result = m_vk->vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &m_pipeline);
	m_vk->vkDestroyShaderModule(m_device, vertex_module, nullptr);
	m_vk->vkDestroyShaderModule(m_device, fragment_module, nullptr);
	if (result != VK_SUCCESS)
		return Fail("vkCreateGraphicsPipelines", result);
	return true;
}

bool Vk2DRenderer::Init(fe::Vk* vk, VkPhysicalDevice physical_device, VkDevice device, uint32_t queue_family, VkQueue queue,
	uint32_t width, uint32_t height, VkFormat output_format, const std::vector<VkImage>& output_images,
	VkImageLayout output_final_layout)
{
	Shutdown();
	m_vk = vk;
	m_physical = physical_device;
	m_device = device;
	m_queue_family = queue_family;
	m_queue = queue;
	m_width = width;
	m_height = height;
	m_output_format = output_format;
	m_output_final_layout = output_final_layout;
	m_output_images = output_images;
	m_error.clear();
	m_vk->vkGetPhysicalDeviceMemoryProperties(m_physical, &m_memory_properties);
	if (!CreateRenderResources() || !CreatePipeline())
	{
		Shutdown();
		return false;
	}
	return true;
}

bool Vk2DRenderer::SetNeutralTexture(const Texture2D* texture)
{
	m_neutral = texture;
	return EnsureTexture(texture) != nullptr;
}

void Vk2DRenderer::WaitIdle()
{
	if (m_device && m_vk)
		m_vk->vkDeviceWaitIdle(m_device);
}

void Vk2DRenderer::Shutdown()
{
	if (!m_device || !m_vk)
		return;
	m_vk->vkDeviceWaitIdle(m_device);
	for (auto& entry : m_textures)
		DestroyTexture(entry.second);
	m_textures.clear();
	for (Slot& slot : m_slots)
	{
		DestroyBuffer(slot.vertices);
		if (slot.fence)
			m_vk->vkDestroyFence(m_device, slot.fence, nullptr);
		if (slot.command)
			m_vk->vkFreeCommandBuffers(m_device, m_command_pool, 1, &slot.command);
		slot = Slot();
	}
	for (VkFramebuffer framebuffer : m_framebuffers)
		m_vk->vkDestroyFramebuffer(m_device, framebuffer, nullptr);
	for (VkImageView view : m_output_views)
		m_vk->vkDestroyImageView(m_device, view, nullptr);
	m_framebuffers.clear();
	m_output_views.clear();
	if (m_command_pool)
		m_vk->vkDestroyCommandPool(m_device, m_command_pool, nullptr);
	if (m_sampler)
		m_vk->vkDestroySampler(m_device, m_sampler, nullptr);
	if (m_descriptor_pool)
		m_vk->vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
	if (m_pipeline)
		m_vk->vkDestroyPipeline(m_device, m_pipeline, nullptr);
	if (m_pipeline_layout)
		m_vk->vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
	if (m_texture_layout)
		m_vk->vkDestroyDescriptorSetLayout(m_device, m_texture_layout, nullptr);
	if (m_render_pass)
		m_vk->vkDestroyRenderPass(m_device, m_render_pass, nullptr);
	m_command_pool = VK_NULL_HANDLE;
	m_sampler = VK_NULL_HANDLE;
	m_descriptor_pool = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
	m_pipeline_layout = VK_NULL_HANDLE;
	m_texture_layout = VK_NULL_HANDLE;
	m_render_pass = VK_NULL_HANDLE;
	m_output_images.clear();
	m_device = VK_NULL_HANDLE;
	m_physical = VK_NULL_HANDLE;
	m_vk = nullptr;
	m_neutral = nullptr;
}

bool Vk2DRenderer::Render(const DrawList& draw, uint32_t output_index, VkSemaphore wait, VkSemaphore signal)
{
	if (!m_pipeline || output_index >= m_output_images.size() || draw.Vertices().empty())
	{
		m_error = draw.Vertices().empty() ? "draw list is empty" : "renderer is not initialized";
		return false;
	}
	for (const DrawBatch& batch : draw.Batches())
	{
		if (!EnsureTexture(batch.texture))
			return false;
	}
	Slot& slot = m_slots[m_slot];
	m_slot = (m_slot + 1) % kSlots;
	VkResult result = m_vk->vkWaitForFences(m_device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
	if (result != VK_SUCCESS)
		return Fail("vkWaitForFences", result);
	result = m_vk->vkResetFences(m_device, 1, &slot.fence);
	if (result != VK_SUCCESS)
		return Fail("vkResetFences", result);
	const size_t bytes = draw.Vertices().size() * sizeof(DrawVertex);
	if (bytes > slot.vertices.size)
	{
		m_error = "draw list exceeds the per-frame vertex buffer";
		return false;
	}
	std::memcpy(slot.vertices.mapped, draw.Vertices().data(), bytes);
	result = m_vk->vkResetCommandBuffer(slot.command, 0);
	if (result != VK_SUCCESS)
		return Fail("vkResetCommandBuffer", result);
	VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	result = m_vk->vkBeginCommandBuffer(slot.command, &begin);
	if (result != VK_SUCCESS)
		return Fail("vkBeginCommandBuffer", result);
	VkClearValue clear = {};
	clear.color = {{0.027f, 0.043f, 0.071f, 1.0f}};
	VkRenderPassBeginInfo pass = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	pass.renderPass = m_render_pass;
	pass.framebuffer = m_framebuffers[output_index];
	pass.renderArea = {{0, 0}, {m_width, m_height}};
	pass.clearValueCount = 1;
	pass.pClearValues = &clear;
	m_vk->vkCmdBeginRenderPass(slot.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
	VkViewport viewport = {0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
	VkRect2D scissor = {{0, 0}, {m_width, m_height}};
	m_vk->vkCmdSetViewport(slot.command, 0, 1, &viewport);
	m_vk->vkCmdSetScissor(slot.command, 0, 1, &scissor);
	m_vk->vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	PushBlock push = {{static_cast<float>(m_width), static_cast<float>(m_height), 1.0f / m_width, 1.0f / m_height}};
	m_vk->vkCmdPushConstants(slot.command, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
	for (const DrawBatch& batch : draw.Batches())
	{
		const GpuTexture* texture = EnsureTexture(batch.texture);
		if (!texture)
			return false;
		const VkBuffer buffers[] = {slot.vertices.buffer};
		const VkDeviceSize offsets[] = {batch.first * sizeof(DrawVertex)};
		m_vk->vkCmdBindVertexBuffers(slot.command, 0, 1, buffers, offsets);
		m_vk->vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1,
			&texture->descriptor, 0, nullptr);
		m_vk->vkCmdDraw(slot.command, static_cast<uint32_t>(batch.count), 1, 0, 0);
	}
	m_vk->vkCmdEndRenderPass(slot.command);
	result = m_vk->vkEndCommandBuffer(slot.command);
	if (result != VK_SUCCESS)
		return Fail("vkEndCommandBuffer", result);
	const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
	if (wait)
	{
		submit.waitSemaphoreCount = 1;
		submit.pWaitSemaphores = &wait;
		submit.pWaitDstStageMask = &wait_stage;
	}
	if (signal)
	{
		submit.signalSemaphoreCount = 1;
		submit.pSignalSemaphores = &signal;
	}
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &slot.command;
	result = m_vk->vkQueueSubmit(m_queue, 1, &submit, slot.fence);
	if (result != VK_SUCCESS)
		return Fail("vkQueueSubmit", result);
	slot.submitted = true;
	return true;
}

bool Vk2DRenderer::ReadPixels(uint32_t output_index, std::vector<uint8_t>& rgba)
{
	rgba.clear();
	if (output_index >= m_output_images.size())
	{
		m_error = "output index is out of range";
		return false;
	}
	m_vk->vkDeviceWaitIdle(m_device);
	Buffer staging;
	const VkDeviceSize bytes = static_cast<VkDeviceSize>(m_width) * m_height * 4;
	if (!CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging))
		return false;
	struct CopyJob
	{
		Vk2DRenderer* self;
		VkImage image;
		Buffer* staging;
	} job{this, m_output_images[output_index], &staging};
	auto record = [](void* user_data, VkCommandBuffer cmd) {
		CopyJob* copy = static_cast<CopyJob*>(user_data);
		copy->self->ImageBarrier(cmd, copy->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferImageCopy region = {};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {copy->self->m_width, copy->self->m_height, 1};
		copy->self->m_vk->vkCmdCopyImageToBuffer(cmd, copy->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			copy->staging->buffer, 1, &region);
		return true;
	};
	if (!OneShotCommands(record, &job))
	{
		DestroyBuffer(staging);
		return false;
	}
	const uint8_t* src = static_cast<const uint8_t*>(staging.mapped);
	rgba.resize(static_cast<size_t>(bytes));
	for (size_t i = 0; i < static_cast<size_t>(bytes); i += 4)
	{
		// Host images use BGRA; expose stable RGBA bytes for PNG encoding and tests.
		// Other console output formats need proper testing before integration.
		rgba[i + 0] = src[i + 2];
		rgba[i + 1] = src[i + 1];
		rgba[i + 2] = src[i + 0];
		rgba[i + 3] = src[i + 3];
	}
	DestroyBuffer(staging);
	return true;
}
} // namespace caliza
