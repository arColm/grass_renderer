#pragma once

#include "../vk_types.hpp"
#include "../vk_loader.hpp"
#include "../vk_descriptors.hpp"


class WaterMesh
{
public:
	struct DisplaySettings
	{
		float scale;
		float windSpeed;
		float windDirection;
		float fetch;
		float spreadBlend;
		float swell;
		float peakEnhancement;
		float shortWavesFade;
	};
	static const unsigned int TEXTURE_SIZE = 256;
	static const unsigned int MESH_SIZE = 120;
	static const unsigned int MESH_QUALITY = 4;

	DisplaySettings settings[2];

	void update(VkCommandBuffer cmd);
	void init(VulkanEngine* engine);
	int draw(VkCommandBuffer cmd, VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants); //returns number of tris
	void cleanup();

private:
	struct SpectrumSettings
	{
		float scale;
		float angle;
		float spreadBlend;
		float swell;
		float alpha;
		float peakOmega;
		float gamma;
		float shortWavesFade;
	};
	SpectrumSettings spectrumParams[2];

	VulkanEngine* _engine;
	void createComputePipeline(const std::string& path, VkDescriptorSetLayout* layouts, int layoutCount, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline);
	void initSettings();
	void initSampler();
	void initImages();
	void initDescriptors();
	void initPipelines();
	void initMesh();
	AllocatedImage _displacementImage;
	AllocatedImage _derivativesImage;
	AllocatedImage _turbulenceImage;
	AllocatedImage _butterflyImage;
	AllocatedImage _fourierDx_DzImage;
	AllocatedImage _fourierDy_DxdzImage;
	AllocatedImage _fourierDxdx_DzdzImage;
	AllocatedImage _fourierDydx_DydzImage;
	AllocatedImage _posSpectrumImage;
	AllocatedImage _negSpectrumImage;
	AllocatedImage _pingpongImage;
	AllocatedImage _noiseImage;

	AllocatedBuffer _spectrumParamsBuffer;

	VkDescriptorSet _waterDataDescriptorSet;
	VkDescriptorSetLayout _waterDataDescriptorLayout;
	VkDescriptorSet _waterDataSamplerDescriptorSet;
	VkDescriptorSetLayout _waterDataSamplerDescriptorLayout;
	VkDescriptorSet _fourierDescriptorSet; 
	VkDescriptorSetLayout _fourierDescriptorLayout;
	VkDescriptorSet _ifft2DDescriptorSet; //write to this in runtime changing pinpong and fourier
	VkDescriptorSetLayout _ifft2DDescriptorLayout;
	VkDescriptorSet _computeResourceDescriptorSet;
	VkDescriptorSetLayout _computeResourceDescriptorLayout;

	VkPipelineLayout _initButterflyPipelineLayout;
	VkPipeline _initButterflyPipeline;
	VkPipelineLayout _initNoisePipelineLayout;
	VkPipeline _initNoisePipeline;
	VkPipelineLayout _initSpectrumPipelineLayout;
	VkPipeline _initSpectrumPipeline;
	VkPipelineLayout _fourierPassPipelineLayout;
	VkPipeline _fourierPassPipeline;
	VkPipelineLayout _horizontalPassPipelineLayout;
	VkPipeline _horizontalPassPipeline;
	VkPipelineLayout _verticalPassPipelineLayout;
	VkPipeline _verticalPassPipeline;
	VkPipelineLayout _inversionPassPipelineLayout;
	VkPipeline _inversionPassPipeline;
	VkPipelineLayout _copyPassPipelineLayout;
	VkPipeline _copyPassPipeline;


	std::shared_ptr<MeshAsset> _waterMesh;
	VkPipelineLayout _waterPipelineLayout;
	VkPipeline _waterPipeline;


	void initButterflyTexture();
	void initNoiseTexture();
	void initSpectrumTextures();

	void step(VkCommandBuffer cmd);
	void fourierPass(VkCommandBuffer cmd);
	void ifft2D(VkCommandBuffer cmd, AllocatedImage& initialTexture);
	void copyToResultTextures(VkCommandBuffer cmd);
	VkPipelineLayout _computeButterflyPipelineLayout;
	VkPipeline _computeButterflyPipeline;
	VkPipelineLayout _computeNoiseLayout;
	VkPipeline _computeNoisePipeline;
	VkPipelineLayout _computeSpectrumPipelineLayout;
	VkPipeline _computeSpectrumPipeline;
	VkPipelineLayout _computeFourierPassPipelineLayout;
	VkPipeline _computeFourierPassPipeline;
	VkPipelineLayout _computeHorizontalPassPipelineLayout;
	VkPipeline _computeHorizontalpassPipeline;
	VkPipelineLayout _computeVerticalPipelineLayout;
	VkPipeline _computeVerticalPipeline;
	VkPipelineLayout _computeInversionPipelineLayout;
	VkPipeline _computeInversionPipeline;
	VkPipelineLayout _computeCopyResultsPipelineLayout;
	VkPipeline _computeCopyResultsPipeline;

	unsigned int _logSize;

	VkSampler _sampler;
};