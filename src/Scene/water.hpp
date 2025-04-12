#pragma once

#include "../vk_types.hpp"
#include "../vk_loader.hpp"
#include "../vk_descriptors.hpp"


class WaterMesh
{
public:
	static const unsigned int TEXTURE_SIZE = 512;


	void update(VulkanEngine* engine, VkCommandBuffer cmd);
	void init(VulkanEngine* engine);
	int draw(VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants, VkCommandBuffer cmd); //returns number of tris
	void cleanup();

private:
	VulkanEngine* _engine;

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
	VkPipelineLayout _ifft2DPipelineLayout;
	VkPipeline _ifft2DPipeline;
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

	void step(float deltaTime);
	void fourierPass(float deltaTime);
	void ifft2D(AllocatedImage& initialTexture);
	void copyToResultTextures();
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

	unsigned int logSize;

	VkSampler _sampler;
};