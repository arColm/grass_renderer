#include "vk_buffers.hpp"


void vkutil::bufferBarrier(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size, VkDeviceSize offset,
	VkPipelineStageFlags2 srcStageMask, 
	VkPipelineStageFlags2 dstStageMask)
{

	VkBufferMemoryBarrier2 bufferBarrier{};
	bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	bufferBarrier.pNext = nullptr;

	bufferBarrier.srcStageMask = srcStageMask;
	bufferBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	bufferBarrier.dstStageMask = dstStageMask;
	bufferBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

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

AllocatedBuffer vkutil::createBuffer(VmaAllocator allocator, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	//allocate buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; //maps pointer so we can write to the memory

	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.allocationInfo));
	return newBuffer;
}

void vkutil::destroyBuffer(VmaAllocator allocator, const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}
