#include "noise.hpp"






AllocatedImage Noise::generatePerlinNoiseImage(VkDevice device, VmaAllocator allocator, uint32_t width, uint32_t height, VkFormat format)
{

    AllocatedImage aImage;
    aImage.imageFormat = format;
    aImage.imageExtent = VkExtent3D{ width,height,1};

    VkImageUsageFlags drawImageUsageFlags{};
    drawImageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;		//can copy from image
    drawImageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image

    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.pNext = nullptr;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    //imageCreateInfo.flags = 0;
    imageCreateInfo.format = format;
    imageCreateInfo.extent = aImage.imageExtent;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = drawImageUsageFlags;

    VmaAllocationCreateInfo imageAllocInfo{};
    imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
    imageAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access


    vmaCreateImage(allocator, &imageCreateInfo, &imageAllocInfo, &aImage.image, &aImage.allocation, nullptr);



    VkImageViewCreateInfo imageViewCreateInfo{};
    imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    imageViewCreateInfo.pNext = nullptr;
    imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    imageViewCreateInfo.image = aImage.image;
    imageViewCreateInfo.format = format;
    imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    imageViewCreateInfo.subresourceRange.levelCount = 1;
    imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    imageViewCreateInfo.subresourceRange.layerCount = 1;
    imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkCreateImageView(device, &imageViewCreateInfo, nullptr, &aImage.imageView);




    return aImage;
}
