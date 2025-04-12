#include "water.hpp"

#include "../vk_engine.hpp"
#include "../vk_initializers.hpp"
#include "../vk_pipelines.hpp"

void WaterMesh::update(VulkanEngine* engine, VkCommandBuffer cmd)
{
}

void WaterMesh::init(VulkanEngine* engine)
{
	_engine = engine;
	logSize = (int) std::log2(TEXTURE_SIZE);
	initSampler();
	initImages();
	initPipelines();
	initMesh();

	initButterflyTexture();
	initNoiseTexture();
	initSpectrumTextures();
}

void WaterMesh::initSampler()
{
	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.pNext = nullptr;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	vkCreateSampler(_engine->_device, &samplerCreateInfo, nullptr, &_sampler);
}

void WaterMesh::initImages()
{
	VkExtent3D imageExtent = {
		TEXTURE_SIZE,
		TEXTURE_SIZE,
		1
	};
	_displacementImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_displacementImage.imageExtent = imageExtent;

	VkImageUsageFlags imageUsageFlags{};
	imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
	imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_displacementImage.imageFormat, imageUsageFlags, imageExtent);

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_displacementImage.image, &_displacementImage.allocation, nullptr);

	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_displacementImage.imageFormat, _displacementImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_displacementImage.imageView));
	
	//derivatives
	_derivativesImage.imageFormat = _displacementImage.imageFormat;
	_derivativesImage.imageExtent = _displacementImage.imageExtent;
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_derivativesImage.image, &_derivativesImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_derivativesImage.imageFormat, _derivativesImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_derivativesImage.imageView));

	//noise
	_noiseImage.imageFormat = _displacementImage.imageFormat;
	_noiseImage.imageExtent = _displacementImage.imageExtent;
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_noiseImage.image, &_noiseImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_noiseImage.imageFormat, _noiseImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_noiseImage.imageView));

	//butterfly
	imageExtent = {
		logSize,
		TEXTURE_SIZE,
		1
	};

	_butterflyImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_butterflyImage.imageExtent = imageExtent;
	imageUsageFlags = VK_IMAGE_USAGE_STORAGE_BIT;
	imgInfo = vkinit::imageCreateInfo(_butterflyImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_butterflyImage.image, &_butterflyImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_butterflyImage.imageFormat, _butterflyImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_butterflyImage.imageView));

	//fourier
	imageExtent = {
		TEXTURE_SIZE,
		TEXTURE_SIZE,
		1
	};
	_fourierDx_DzImage.imageFormat = VK_FORMAT_R16G16_SFLOAT;
	_fourierDx_DzImage.imageExtent = imageExtent;
	imgInfo = vkinit::imageCreateInfo(_fourierDx_DzImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_fourierDx_DzImage.image, &_fourierDx_DzImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_fourierDx_DzImage.imageFormat, _fourierDx_DzImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_fourierDx_DzImage.imageView));

	_fourierDy_DxdzImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_fourierDy_DxdzImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_fourierDy_DxdzImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_fourierDy_DxdzImage.image, &_fourierDy_DxdzImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_fourierDy_DxdzImage.imageFormat, _fourierDy_DxdzImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_fourierDy_DxdzImage.imageView));

	_fourierDxdx_DzdzImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_fourierDxdx_DzdzImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_fourierDxdx_DzdzImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_fourierDxdx_DzdzImage.image, &_fourierDxdx_DzdzImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_fourierDxdx_DzdzImage.imageFormat, _fourierDxdx_DzdzImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_fourierDxdx_DzdzImage.imageView));

	_fourierDydx_DydzImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_fourierDydx_DydzImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_fourierDydx_DydzImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_fourierDydx_DydzImage.image, &_fourierDydx_DydzImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_fourierDydx_DydzImage.imageFormat, _fourierDydx_DydzImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_fourierDydx_DydzImage.imageView));

	_pingpongImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_pingpongImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_pingpongImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_pingpongImage.image, &_pingpongImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_pingpongImage.imageFormat, _pingpongImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_pingpongImage.imageView));

	_posSpectrumImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_posSpectrumImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_posSpectrumImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_posSpectrumImage.image, &_posSpectrumImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_posSpectrumImage.imageFormat, _posSpectrumImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_posSpectrumImage.imageView));

	_negSpectrumImage.imageFormat = _fourierDx_DzImage.imageFormat;
	_negSpectrumImage.imageExtent = _fourierDx_DzImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_negSpectrumImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_negSpectrumImage.image, &_negSpectrumImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_negSpectrumImage.imageFormat, _negSpectrumImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_negSpectrumImage.imageView));

	_turbulenceImage.imageFormat = VK_FORMAT_R16_SFLOAT;
	_turbulenceImage.imageExtent = _displacementImage.imageExtent;
	imgInfo = vkinit::imageCreateInfo(_turbulenceImage.imageFormat, imageUsageFlags, imageExtent);
	vmaCreateImage(_engine->_allocator, &imgInfo, &imgAllocInfo, &_turbulenceImage.image, &_turbulenceImage.allocation, nullptr);
	viewInfo = vkinit::imageViewCreateInfo(_turbulenceImage.imageFormat, _turbulenceImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_engine->_device, &viewInfo, nullptr, &_turbulenceImage.imageView));
}

void WaterMesh::initDescriptors()
{
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //displacement
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //derivatives
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //turbulence
		_waterDataDescriptorLayout = builder.build(_engine->_device, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); 
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_fourierDescriptorLayout = builder.build(_engine->_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); 
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); 
		_ifft2DDescriptorLayout = builder.build(_engine->_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //butterfgly
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //posspec
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //negspec
		builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //noise
		_computeResourceDescriptorLayout = builder.build(_engine->_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	//	writing to descriptor set
	{
		_waterDataDescriptorSet = _engine->_globalDescriptorAllocator.allocate(_engine->_device, _waterDataDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _displacementImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(1, _derivativesImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(2, _turbulenceImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_engine->_device, _waterDataDescriptorSet);
	}
	{
		_fourierDescriptorSet = _engine->_globalDescriptorAllocator.allocate(_engine->_device, _fourierDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _fourierDx_DzImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(1, _fourierDy_DxdzImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(2, _fourierDxdx_DzdzImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(3, _fourierDydx_DydzImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_engine->_device, _fourierDescriptorSet);
	}
	{
		_computeResourceDescriptorSet = _engine->_globalDescriptorAllocator.allocate(_engine->_device, _computeResourceDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _butterflyImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(1, _posSpectrumImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(2, _negSpectrumImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(3, _noiseImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_engine->_device, _computeResourceDescriptorSet);
	}
}

void WaterMesh::initPipelines()
{
	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("./shaders/scene/water.comp.spv", _engine->_device, &computeShader))
	{
		fmt::print("error when building water compute shader module\n");
	}
	else
	{
		fmt::print("water compute shader loaded\n");
	}
	VkShaderModule fragmentShader;
	if (!vkutil::loadShaderModule("./shaders/scene/water.frag.spv", _engine->_device, &fragmentShader))
	{
		fmt::print("error when building water frag shader module\n");
	}
	else
	{
		fmt::print("water frag shader loaded\n");
	}
	VkShaderModule vertexShader;
	if (!vkutil::loadShaderModule("./shaders/scene/water.vert.spv", _engine->_device, &vertexShader))
	{
		fmt::print("error when building water vert shader module\n");
	}
	else
	{
		fmt::print("water vert shader loaded\n");
	}

	VkPushConstantRange computeBufferRange{};
	computeBufferRange.offset = 0;
	computeBufferRange.size = sizeof(ComputePushConstants);
	computeBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pPushConstantRanges = &computeBufferRange;
	computePipelineLayoutInfo.pushConstantRangeCount = 1;

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = computeShader;

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;

	stageInfo.pName = "initButterfly"; //name of entrypoint function
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_computeResourceDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_initButterflyPipelineLayout));

	computePipelineCreateInfo.layout = _initButterflyPipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_initButterflyPipeline));


	stageInfo.pName = "initNoise"; //name of entrypoint function
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_computeResourceDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_initNoisePipelineLayout));

	computePipelineCreateInfo.layout = _initNoisePipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_initNoisePipeline));

	stageInfo.pName = "initSpectrum"; //name of entrypoint function
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_computeResourceDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_initSpectrumPipelineLayout));

	computePipelineCreateInfo.layout = _initSpectrumPipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_initSpectrumPipeline));
	{
		stageInfo.pName = "fourierPass"; //name of entrypoint function
		computePipelineLayoutInfo.setLayoutCount = 2;
		VkDescriptorSetLayout layouts[] = { _computeResourceDescriptorLayout, _fourierDescriptorLayout };
		computePipelineLayoutInfo.pSetLayouts = layouts;

		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_fourierPassPipelineLayout));

		computePipelineCreateInfo.layout = _fourierPassPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_fourierPassPipeline));
	}
	{
		stageInfo.pName = "ifft2D"; //name of entrypoint function
		computePipelineLayoutInfo.setLayoutCount = 1;
		VkDescriptorSetLayout layouts[] = { _computeResourceDescriptorLayout, _ifft2DDescriptorLayout };
		computePipelineLayoutInfo.pSetLayouts = layouts;

		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_ifft2DPipelineLayout));

		computePipelineCreateInfo.layout = _ifft2DPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_ifft2DPipeline));
	}
	{
		stageInfo.pName = "inversion"; //name of entrypoint function
		computePipelineLayoutInfo.setLayoutCount = 2;
		VkDescriptorSetLayout layouts[] = { _computeResourceDescriptorLayout, _ifft2DDescriptorLayout };
		computePipelineLayoutInfo.pSetLayouts = layouts;

		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_inversionPassPipelineLayout));

		computePipelineCreateInfo.layout = _inversionPassPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_inversionPassPipeline));
	}
	{
		stageInfo.pName = "copyResults"; //name of entrypoint function
		computePipelineLayoutInfo.setLayoutCount = 2;
		VkDescriptorSetLayout layouts[] = { _fourierDescriptorLayout, _waterDataDescriptorLayout };
		computePipelineLayoutInfo.pSetLayouts = layouts;

		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &_copyPassPipelineLayout));

		computePipelineCreateInfo.layout = _copyPassPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_copyPassPipeline));
	}

	//clean structures
	vkDestroyShaderModule(_engine->_device, computeShader, nullptr);
	vkDestroyShaderModule(_engine->_device, fragmentShader, nullptr);
	vkDestroyShaderModule(_engine->_device, vertexShader, nullptr);
}

void WaterMesh::initMesh()
{
}


int WaterMesh::draw(VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants, VkCommandBuffer cmd)
{
	return 0;
}

void WaterMesh::cleanup()
{
}

void WaterMesh::initButterflyTexture()
{
}

void WaterMesh::initNoiseTexture()
{
}

void WaterMesh::initSpectrumTextures()
{
}

void WaterMesh::step(float deltaTime)
{
	fourierPass(deltaTime);
	ifft2D(_fourierDx_DzImage);
	ifft2D(_fourierDy_DxdzImage);
	ifft2D(_fourierDxdx_DzdzImage);
	ifft2D(_fourierDydx_DydzImage);
	copyToResultTextures();
}

void WaterMesh::fourierPass(float deltaTime)
{
}

void WaterMesh::ifft2D(AllocatedImage& initialTexture)
{
}

void WaterMesh::copyToResultTextures()
{
}
