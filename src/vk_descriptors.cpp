#include "vk_descriptors.hpp"

void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding newBind{};
	newBind.binding = binding;
	newBind.descriptorCount = 1;
	newBind.descriptorType = type;

	bindings.push_back(newBind);
}

void DescriptorLayoutBuilder::clear()
{
	bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
	for (auto& b : bindings)
	{
		b.stageFlags |= shaderStages;
	}

	VkDescriptorSetLayoutCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.pNext = nullptr;

	info.pBindings = bindings.data();
	info.bindingCount = (uint32_t)bindings.size();
	info.flags = flags;

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

	return set;
}



/*
*  DESCRIPTOR ALLOCATOR GROWABLE
*/

void DescriptorAllocatorGrowable::init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios)
{
	_ratios.clear();

	for (auto r : poolRatios)
	{
		_ratios.push_back(r);
	}

	VkDescriptorPool newPool = createPool(device, initialSets, poolRatios);

	_setsPerPool = initialSets * 1.5;

	_readyPools.push_back(newPool);
}

void DescriptorAllocatorGrowable::clearPools(VkDevice device)
{
	for (auto p : _readyPools)
	{
		vkResetDescriptorPool(device, p, 0);
	}

	for (auto p : _fullPools)
	{
		vkResetDescriptorPool(device, p, 0);
		_readyPools.push_back(p);
	}
	_fullPools.clear();
}

void DescriptorAllocatorGrowable::destroyPools(VkDevice device)
{
	for (auto p : _readyPools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	_readyPools.clear();
	for (auto p : _fullPools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	_fullPools.clear();
}

VkDescriptorSet DescriptorAllocatorGrowable::allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext)
{
	// get or create a pool to allocate from
	VkDescriptorPool poolToUse = getPool(device);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;

	allocInfo.descriptorPool = poolToUse;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet ds;
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

	//if allocation failed, try again
	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
	{
		_fullPools.push_back(poolToUse);
		poolToUse = getPool(device);
		allocInfo.descriptorPool = poolToUse;

		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	}

	_readyPools.push_back(poolToUse);
	return ds;
}

VkDescriptorPool DescriptorAllocatorGrowable::getPool(VkDevice device)
{
	VkDescriptorPool newPool;
	if (_readyPools.size() != 0)
	{
		newPool = _readyPools.back();
		_readyPools.pop_back();
	}
	else
	{
		//need to create a new pool
		newPool = createPool(device, _setsPerPool, _ratios);

		_setsPerPool = std::max((int)(_setsPerPool * 1.5), 4092);
	}
	return newPool;
}

VkDescriptorPool DescriptorAllocatorGrowable::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios)
	{
		poolSizes.push_back(
			VkDescriptorPoolSize{
				.type = ratio._type,
				.descriptorCount = uint32_t(ratio._ratio * setCount)
			});
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.pNext = nullptr;

	poolInfo.maxSets = setCount;
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	VkDescriptorPool newPool;
	vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool);
	return newPool;
}


/*
*  DESCRIPTOR WRITER
*/

void DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
{
	VkDescriptorImageInfo& info = _imageInfos.emplace_back(
		VkDescriptorImageInfo{
			.sampler = sampler,
			.imageView = image,
			.imageLayout = layout
		}
	);

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = &info;

	_writes.push_back(write);
}

void DescriptorWriter::writeImageArray(int binding, std::vector<VkImageView>& images, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
{
	for (int i = 0; i < images.size(); i++)
	{
		_imageInfoList.push_back(
			VkDescriptorImageInfo{
				.sampler = sampler,
				.imageView = images[i],
				.imageLayout = layout
			}
		);
	}
	VkDescriptorImageInfo* info = _imageInfoList.data();
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = info;

	_writes.push_back(write);
}

void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
	VkDescriptorBufferInfo& info = _bufferInfos.emplace_back(
		VkDescriptorBufferInfo{
			.buffer = buffer,
			.offset = offset,
			.range = size
		});

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE; //leave empty until we need to write to it
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = &info;

	_writes.push_back(write);
}

void DescriptorWriter::clear()
{
	_imageInfos.clear();
	_imageInfoList.clear();
	_bufferInfos.clear();
	_writes.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set)
{
	for (VkWriteDescriptorSet& write : _writes)
	{
		write.dstSet = set;
	}
	vkUpdateDescriptorSets(device, (uint32_t)_writes.size(), _writes.data(), 0, nullptr);
}