#pragma once

#include "../vk_types.hpp"
#include "../vk_loader.hpp"
#include "../vk_descriptors.hpp"


class CloudMesh {
public:
	static const int CLOUD_MAP_SIZE = 1024;
	static const int CLOUD_MAP_HEIGHT = 128;

	void update(VulkanEngine* engine, VkCommandBuffer cmd);
	void init(VulkanEngine* engine);
	int draw(GPUDrawPushConstants pushConstants, VkCommandBuffer cmd); //returns number of tris

	void cleanup();
private:
	VulkanEngine* _engine;

	AllocatedImage _cloudMapImage;
	VkDescriptorSet _cloudMapDescriptorSet;
	VkDescriptorSetLayout _cloudMapDescriptorLayout;
	VkDescriptorSet _cloudMapSamplerDescriptorSet;
	VkDescriptorSetLayout _cloudMapSamplerDescriptorLayout;
	VkPipelineLayout _cloudMapComputePipelineLayout;
	VkPipeline _cloudMapComputePipeline;
	std::shared_ptr<MeshAsset> _cloudMesh;
	VkPipelineLayout _cloudPipelineLayout;
	VkPipeline _cloudPipeline;
};