#pragma once

#include "../vk_types.hpp"
#include "../vk_loader.hpp"
#include "../vk_descriptors.hpp"


class CloudMesh 
{
public:
	static const int CLOUD_MAP_SIZE = 128;
	static const int CLOUD_MAP_HEIGHT = 128;

	void update(VulkanEngine* engine, VkCommandBuffer cmd);
	void updateWeather(VulkanEngine* engine);
	void init(VulkanEngine* engine);
	int draw(VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants, VkCommandBuffer cmd); //returns number of tris

	void drawGUI();

	void cleanup();
private:
	int currentVideoFrame = 0;
	struct CloudSettingsPushConstants
	{
		float coverage;
		float hgConstant;
	};
	float _cloudCoverage = 0.7f;
	float _hgConstant = 0.1f;

	VulkanEngine* _engine;

	AllocatedImage _baseNoiseImage;
	AllocatedImage _detailNoiseImage;
	AllocatedImage _fluidNoiseImage;
	AllocatedImage _weatherImage;

	VkDescriptorSet _cloudMapDescriptorSet;
	VkDescriptorSetLayout _cloudMapDescriptorLayout;
	VkDescriptorSet _cloudMapSamplerDescriptorSet;
	VkDescriptorSetLayout _cloudMapSamplerDescriptorLayout;
	VkDescriptorSet _cloudSettingsDescriptorSet;
	VkDescriptorSetLayout _cloudSettingsDescriptorLayout;
	VkPipelineLayout _cloudMapComputePipelineLayout;
	VkPipeline _cloudMapComputePipeline;
	std::shared_ptr<MeshAsset> _cloudMesh;
	VkPipelineLayout _cloudPipelineLayout;
	VkPipeline _cloudPipeline;

	VkSampler _sampler;
};