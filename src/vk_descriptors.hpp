#pragma once

#include "vk_types.hpp"



struct DescriptorLayoutBuilder
{
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void addBinding(uint32_t binding, VkDescriptorType type);
	void clear();
	VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, 
		void* pNext = nullptr, 
		VkDescriptorSetLayoutCreateFlags flags = 0);
};