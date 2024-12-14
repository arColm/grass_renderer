#pragma once

#include "vk_types.hpp"





namespace Noise
{

	AllocatedImage generatePerlinNoiseImage(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, VkFormat format);
}