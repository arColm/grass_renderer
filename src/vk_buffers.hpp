#pragma once

#include <vulkan/vulkan.h>

namespace vkutil
{
	void bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer,VkDeviceSize size, VkDeviceSize offset,
		VkPipelineStageFlags2 srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		VkPipelineStageFlags2 dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT); //note: ALL_COMMANDS is inefficient
}