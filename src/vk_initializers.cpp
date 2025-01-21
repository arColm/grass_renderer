#include "vk_initializers.hpp"

VkCommandPoolCreateInfo vkinit::commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
{
	VkCommandPoolCreateInfo commandPoolInfo{};
	commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolInfo.pNext = nullptr;
	commandPoolInfo.flags = flags;
	commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
	return commandPoolInfo;
}

VkCommandBufferAllocateInfo vkinit::commandBufferAllocateInfo(VkCommandPool pool, uint32_t count)
{
	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; //we only use primary
	cmdAllocInfo.commandBufferCount = count;
	cmdAllocInfo.commandPool = pool;
	return cmdAllocInfo;
}

VkFenceCreateInfo vkinit::fenceCreateInfo(VkFenceCreateFlags flags)
{
	VkFenceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.flags = flags;
	return createInfo;
}

VkSemaphoreCreateInfo vkinit::semaphoreCreateInfo(VkSemaphoreCreateFlags flags)
{
	VkSemaphoreCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.flags = flags;
	return createInfo;
}

VkCommandBufferBeginInfo vkinit::commandBufferBeginInfo(VkCommandBufferUsageFlags flags)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = nullptr;
	beginInfo.pInheritanceInfo = nullptr;
	beginInfo.flags = flags;
	return beginInfo;
}

VkImageSubresourceRange vkinit::imageSubresourceRange(VkImageAspectFlags aspectMask)
{
	//subresourceRange lets us target a part of the image with the barrier
	//useful for array images or mipmapped images, where we only need a barrier on a given layer or mipmap level
	VkImageSubresourceRange subImage{};
	//this implementation is default and transitions all mipmap levels and layers
	subImage.baseMipLevel = 0;
	subImage.levelCount = VK_REMAINING_MIP_LEVELS;
	subImage.baseArrayLayer = 0;
	subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

	subImage.aspectMask = aspectMask;

	return subImage;
}

VkSemaphoreSubmitInfo vkinit::semaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore)
{
	VkSemaphoreSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;

	submitInfo.deviceIndex = 0; //used for multi-GPU semaphore usage, but we dont use this in the tutorial
	submitInfo.value = 1; //used for timeline semaphores (semaphores that work through a counter instead of binary state), which we dont use.

	submitInfo.semaphore = semaphore;
	submitInfo.stageMask = stageMask;

	return submitInfo;
}

VkCommandBufferSubmitInfo vkinit::commandBufferSubmitInfo(VkCommandBuffer cmd)
{
	VkCommandBufferSubmitInfo info{};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	info.pNext = nullptr;
	info.deviceMask = 0;
	info.commandBuffer = cmd;

	return info;
}

VkSubmitInfo2 vkinit::submitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo)
{
	VkSubmitInfo2 info{};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	info.pNext = nullptr;

	info.waitSemaphoreInfoCount = waitSemaphoreInfo == nullptr ? 0 : 1;
	info.pWaitSemaphoreInfos = waitSemaphoreInfo;

	info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
	info.pSignalSemaphoreInfos = signalSemaphoreInfo;

	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = cmd;

	return info;
}

VkImageCreateInfo vkinit::imageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent, 
	VkImageType imageType /* = VK_IMAGE_TYPE_2D*/)
{
	VkImageCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.pNext = nullptr;

	info.imageType = imageType;

	info.format = format;
	info.extent = extent;

	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;

	//TILING lets the gpu shuffle the data as it sees fit
	//LINEAR is needed for images read from cpu, which turns the image into a 2d array
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usageFlags;

	return info;
}

VkImageViewCreateInfo vkinit::imageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags,
	VkImageViewType viewType /* = VK_IMAGE_VIEW_TYPE_2D*/)
{
	VkImageViewCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.pNext = nullptr;

	info.viewType = viewType;
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	info.subresourceRange.aspectMask = aspectFlags;

	//COMPONENTS used for swizzling (identity is default)

	return info;
}

VkRenderingAttachmentInfo vkinit::attachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/)
{
	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.pNext = nullptr;
	
	colorAttachment.imageView = view;
	colorAttachment.imageLayout = layout;

	// loadOp and storeOp control what happens to the render target in this attachment
	// LOAD uses the existing data in image, CLEAR sets it to clear value at start, DONT_CARE skips loading it from memory (to replace later)
	colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	if (clear)
	{
		colorAttachment.clearValue = *clear;
	}
	return colorAttachment;
}

VkRenderingAttachmentInfo vkinit::depthAttachmentInfo(VkImageView view, VkImageLayout layout)
{
	VkRenderingAttachmentInfo depthAttachment{};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.pNext = nullptr;

	depthAttachment.imageView = view;
	depthAttachment.imageLayout = layout;

	// loadOp and storeOp control what happens to the render target in this attachment
	// LOAD uses the existing data in image, CLEAR sets it to clear value at start, DONT_CARE skips loading it from memory (to replace later)
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 1.f;
	return depthAttachment;
}

VkRenderingInfo vkinit::renderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment)
{
	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.pNext = nullptr;

	renderingInfo.renderArea = VkRect2D{ VkOffset2D{0,0}, renderExtent };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = colorAttachment;
	renderingInfo.pDepthAttachment = depthAttachment;
	renderingInfo.pStencilAttachment = nullptr;

	return renderingInfo;
}

VkPipelineShaderStageCreateInfo vkinit::pipelineShaderStageCreateInfo(
	VkShaderStageFlagBits flags, VkShaderModule shaderModule, const char* entry /* = "main"*/)
{
	VkPipelineShaderStageCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.stage = flags;
	createInfo.module = shaderModule;
	createInfo.pName = entry;
	return createInfo;
}

VkPipelineLayoutCreateInfo vkinit::pipelineLayoutCreateInfo()
{
	VkPipelineLayoutCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	createInfo.pNext = nullptr;
	
	//empty defaults
	createInfo.flags = 0;
	createInfo.pushConstantRangeCount = 0;
	createInfo.pPushConstantRanges = nullptr;
	createInfo.setLayoutCount = 0;
	createInfo.pSetLayouts = nullptr;

	return createInfo;
}
