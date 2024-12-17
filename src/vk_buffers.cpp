#include "vk_buffers.hpp"


void vkutil::bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset,
	VkPipelineStageFlags2 srcStageMask, 
	VkPipelineStageFlags2 dstStageMask)
{

	VkBufferMemoryBarrier2 bufferBarrier{};
	bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	bufferBarrier.pNext = nullptr;

	bufferBarrier.srcStageMask = srcStageMask;
	bufferBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	bufferBarrier.dstStageMask = dstStageMask;
	bufferBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	bufferBarrier.size = size;
	bufferBarrier.offset = offset;
	bufferBarrier.buffer = buffer;



	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;

	depInfo.bufferMemoryBarrierCount = 1;
	depInfo.pBufferMemoryBarriers = &bufferBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}
