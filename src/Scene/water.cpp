#include "water.hpp"

#include "../vk_engine.hpp"
#include "../vk_initializers.hpp"
#include "../vk_pipelines.hpp"
#include "../vk_images.hpp"
#include <iostream>

void WaterMesh::update(VkCommandBuffer cmd, float deltaTime)
{
	step(cmd);
}

void WaterMesh::init(VulkanEngine* engine)
{
	_engine = engine;
	_logSize = (int) std::log2(TEXTURE_SIZE);
	initSampler();
	initImages();
	initDescriptors();
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
		_logSize,
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

void WaterMesh::createComputePipeline(const std::string& path, VkDescriptorSetLayout* layouts, int layoutCount, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline)
{
	std::cerr << path << '\n';
	std::cerr << layouts << '\n';
	VkShaderModule shader;
	if (!vkutil::loadShaderModule(path.c_str(), _engine->_device, &shader))
	{
		fmt::print("error when building water compute shader module\n");
	}
	else
	{
		fmt::print("water compute shader loaded\n");
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
	stageInfo.module = shader;
	stageInfo.pName = "main"; //name of entrypoint function

	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineLayoutInfo.setLayoutCount = layoutCount;
	computePipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &pipelineLayout));

	computePipelineCreateInfo.layout = pipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &pipeline));

	vkDestroyShaderModule(_engine->_device, shader, nullptr);
}

void WaterMesh::initPipelines()
{
	std::vector<VkDescriptorSetLayout> layouts = { _computeResourceDescriptorLayout};
	std::cerr << layouts.data() << '\n';
	createComputePipeline("./shaders/scene/water/water_initButterfly.comp.spv", layouts.data(), layouts.size(), _initButterflyPipelineLayout, _initButterflyPipeline);
	createComputePipeline("./shaders/scene/water/water_initNoise.comp.spv", layouts.data(), layouts.size(), _initNoisePipelineLayout, _initNoisePipeline);
	createComputePipeline("./shaders/scene/water/water_initSpectrums.comp.spv", layouts.data(), layouts.size(), _initSpectrumPipelineLayout, _initSpectrumPipeline);
	layouts = { _computeResourceDescriptorLayout, _fourierDescriptorLayout };
	createComputePipeline("./shaders/scene/water/water_fourierPass.comp.spv", layouts.data(), layouts.size(), _fourierPassPipelineLayout, _fourierPassPipeline);
	layouts = { _computeResourceDescriptorLayout, _ifft2DDescriptorLayout };
	createComputePipeline("./shaders/scene/water/water_horizontalPass.comp.spv", layouts.data(), layouts.size(), _horizontalPassPipelineLayout, _horizontalPassPipeline);
	createComputePipeline("./shaders/scene/water/water_verticalPass.comp.spv", layouts.data(), layouts.size(), _verticalPassPipelineLayout, _verticalPassPipeline);
	createComputePipeline("./shaders/scene/water/water_inversionPass.comp.spv", layouts.data(), layouts.size(), _inversionPassPipelineLayout, _inversionPassPipeline);
	layouts = { _fourierDescriptorLayout, _waterDataDescriptorLayout };
	createComputePipeline("./shaders/scene/water/water_copyResults.comp.spv", layouts.data(), layouts.size(), _copyPassPipelineLayout, _copyPassPipeline);

	VkShaderModule fragmentShader;
	if (!vkutil::loadShaderModule("./shaders/scene/water/water.frag.spv", _engine->_device, &fragmentShader))
	{
		fmt::print("error when building water frag shader module\n");
	}
	else
	{
		fmt::print("water frag shader loaded\n");
	}
	VkShaderModule vertexShader;
	if (!vkutil::loadShaderModule("./shaders/scene/water/water.vert.spv", _engine->_device, &vertexShader))
	{
		fmt::print("error when building water vert shader module\n");
	}
	else
	{
		fmt::print("water vert shader loaded\n");
	}
	{
		VkPushConstantRange drawPushRange[1];
		drawPushRange[0].offset = 0;
		drawPushRange[0].size = sizeof(GPUDrawPushConstants);
		drawPushRange[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayout layouts[] = {
			_engine->_sceneDataDescriptorLayout,
			_engine->_shadowMapDescriptorLayout,
			_waterDataDescriptorLayout
		};

		VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = drawPushRange;
		pipelineLayoutInfo.setLayoutCount = 3;
		pipelineLayoutInfo.pSetLayouts = layouts;
		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &pipelineLayoutInfo, nullptr, &_waterPipelineLayout));

		std::vector<VkFormat> colorAttachmentFormats = {
			_engine->_drawImage.imageFormat,
			_engine->_normalsImage.imageFormat,
			_engine->_specularMapImage.imageFormat,
			_engine->_positionsImage.imageFormat
		};
		vkutil::PipelineBuilder pipelineBuilder;
		pipelineBuilder._pipelineLayout = _waterPipelineLayout;
		pipelineBuilder.setShaders(vertexShader, fragmentShader);
		pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
		pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
		pipelineBuilder.setMultisamplingNone();
		std::vector<vkutil::ColorBlendingMode> modes = {
			vkutil::ALPHABLEND,
			vkutil::ALPHABLEND,
			vkutil::ALPHABLEND,
			vkutil::ALPHABLEND,
		};
		pipelineBuilder.setBlendingModes(modes);
		pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
		pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
		pipelineBuilder.setDepthFormat(_engine->_depthImage.imageFormat);

		//build pipeline
		_waterPipeline = pipelineBuilder.buildPipeline(_engine->_device);
	}

	//clean structures
	vkDestroyShaderModule(_engine->_device, fragmentShader, nullptr);
	vkDestroyShaderModule(_engine->_device, vertexShader, nullptr);
}

void WaterMesh::initMesh()
{

	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	uint32_t numVerticesPerSide = (MESH_SIZE * MESH_QUALITY + 2);
	const size_t vertexBufferSize = numVerticesPerSide * numVerticesPerSide;
	const size_t indexBufferSize = (numVerticesPerSide - 1) * (numVerticesPerSide - 1) * 6;
	meshBuffers.vertexBuffer = _engine->createBuffer(vertexBufferSize * sizeof(Vertex),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	meshBuffers.indexBuffer = _engine->createBuffer(indexBufferSize * sizeof(uint32_t),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].startIndex = static_cast<uint32_t>(0);
	surfaces[0].count = static_cast<uint32_t>(indexBufferSize);

	VkBufferDeviceAddressInfo vertexBufferAddressInfo{};
	vertexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	vertexBufferAddressInfo.pNext = nullptr;
	vertexBufferAddressInfo.buffer = meshBuffers.vertexBuffer.buffer;
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(_engine->_device, &vertexBufferAddressInfo);

	VkBufferDeviceAddressInfo indexBufferAddressInfo{};
	indexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	indexBufferAddressInfo.pNext = nullptr;
	indexBufferAddressInfo.buffer = meshBuffers.indexBuffer.buffer;
	VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddress(_engine->_device, &indexBufferAddressInfo);

	//VERTEX
	{
		//	PIPELINE
		VkShaderModule computeVertexShader;
		if (!vkutil::loadShaderModule("./shaders/scene/water/water_mesh_vertices.comp.spv", _engine->_device, &computeVertexShader))
		{
			fmt::print("error when building water mesh vertex compute shader module\n");
		}
		else
		{
			fmt::print("water mesh vertex compute shader loaded\n");
		}

		struct ComputeVertexPushConstants {
			glm::vec4 data;
			VkDeviceAddress vertexBuffer;
		};

		//push constant range
		VkPushConstantRange computeVertexBufferRange{};
		computeVertexBufferRange.offset = 0;
		computeVertexBufferRange.size = sizeof(ComputeVertexPushConstants);
		computeVertexBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		//sets

		//build pipeline layout that controls the input/outputs of shader
		//	note: no descriptor sets or other yet.
		VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
		computePipelineLayoutInfo.pPushConstantRanges = &computeVertexBufferRange;
		computePipelineLayoutInfo.pushConstantRangeCount = 1;
		computePipelineLayoutInfo.setLayoutCount = 0;

		VkPipelineLayout computeVertexPipelineLayout;
		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &computeVertexPipelineLayout));

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.pNext = nullptr;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.pName = "main"; //name of entrypoint function
		stageInfo.module = computeVertexShader;

		////create pipelines
		VkComputePipelineCreateInfo computePipelineCreateInfo{};
		computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		computePipelineCreateInfo.pNext = nullptr;
		computePipelineCreateInfo.layout = computeVertexPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VkPipeline computeVertexPipeline;
		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeVertexPipeline));

		ComputeVertexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide, MESH_QUALITY, 0, 0);
		pushConstants.vertexBuffer = meshBuffers.vertexBufferAddress;

		////clean structures
		vkDestroyShaderModule(_engine->_device, computeVertexShader, nullptr);

		_engine->immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeVertexPipeline);
				vkCmdPushConstants(cmd, computeVertexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeVertexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil(numVerticesPerSide / 16.0f), std::ceil(numVerticesPerSide / 16.0f), 1);
			}
		);
		vkDestroyPipelineLayout(_engine->_device, computeVertexPipelineLayout, nullptr);
		vkDestroyPipeline(_engine->_device, computeVertexPipeline, nullptr);
	}


	//INDICES
	{
		//	PIPELINE
		VkShaderModule computeIndexShader;
		if (!vkutil::loadShaderModule("./shaders/scene/water/water_mesh_indices.comp.spv", _engine->_device, &computeIndexShader))
		{
			fmt::print("error when building water mesh indices compute shader module\n");
		}
		else
		{
			fmt::print("water mesh index compute shader loaded\n");
		}

		struct ComputeIndexPushConstants {
			glm::vec4 data;
			VkDeviceAddress indexBuffer;
		};

		//push constant range
		VkPushConstantRange computeIndexBufferRange{};
		computeIndexBufferRange.offset = 0;
		computeIndexBufferRange.size = sizeof(ComputeIndexPushConstants);
		computeIndexBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

		//sets

		//build pipeline layout that controls the input/outputs of shader
		//	note: no descriptor sets or other yet.
		VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
		computePipelineLayoutInfo.pPushConstantRanges = &computeIndexBufferRange;
		computePipelineLayoutInfo.pushConstantRangeCount = 1;
		//computePipelineLayoutInfo.pSetLayouts = &_heightMapDescriptorLayout;
		computePipelineLayoutInfo.setLayoutCount = 0;

		VkPipelineLayout computeIndexPipelineLayout;
		VK_CHECK(vkCreatePipelineLayout(_engine->_device, &computePipelineLayoutInfo, nullptr, &computeIndexPipelineLayout));

		VkPipelineShaderStageCreateInfo stageInfo{};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.pNext = nullptr;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stageInfo.pName = "main"; //name of entrypoint function
		stageInfo.module = computeIndexShader;

		////create pipelines
		VkComputePipelineCreateInfo computePipelineCreateInfo{};
		computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		computePipelineCreateInfo.pNext = nullptr;
		computePipelineCreateInfo.layout = computeIndexPipelineLayout;
		computePipelineCreateInfo.stage = stageInfo;

		VkPipeline computeIndexPipeline;
		VK_CHECK(vkCreateComputePipelines(_engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeIndexPipeline));

		ComputeIndexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide - 1, 0, 0, 0);
		pushConstants.indexBuffer = indexBufferAddress;

		vkDestroyShaderModule(_engine->_device, computeIndexShader, nullptr);

		_engine->immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeIndexPipeline);
				vkCmdPushConstants(cmd, computeIndexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeIndexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil((float)indexBufferSize / (6 * 64)), 1, 1);
			}
		);
		vkDestroyPipelineLayout(_engine->_device, computeIndexPipelineLayout, nullptr);
		vkDestroyPipeline(_engine->_device, computeIndexPipeline, nullptr);
	}
	//TODO OPTIMIZATION make the vertices and indices in same buffer with offset to improve performance
	meshAsset.name = "water";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = meshBuffers;

	_waterMesh = std::make_shared<MeshAsset>(std::move(meshAsset));



	_engine->_mainDeletionQueue.pushFunction(
		[&]() {
			_engine->destroyBuffer(_waterMesh->meshBuffers.vertexBuffer);
			_engine->destroyBuffer(_waterMesh->meshBuffers.indexBuffer);

		}
	);
}


int WaterMesh::draw(VkCommandBuffer cmd, VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _waterPipeline);
	{
		VkDescriptorSet sets[] = {
			*sceneDataDescriptorSet,
			_engine->_shadowMapDescriptorSet,
			_waterDataDescriptorSet
		};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _waterPipelineLayout, 0, 3, sets, 0, nullptr);
	}
	pushConstants.vertexBuffer = _waterMesh->meshBuffers.vertexBufferAddress;
	vkCmdPushConstants(cmd, _waterPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _waterMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _waterMesh->surfaces[0].count, 1, _waterMesh->surfaces[0].startIndex, 0, 0);
	return _waterMesh->surfaces[0].count;
}

void WaterMesh::cleanup()
{
	vkDestroyImageView(_engine->_device, _displacementImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _displacementImage.image, _displacementImage.allocation);
	vkDestroyImageView(_engine->_device, _derivativesImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _derivativesImage.image, _derivativesImage.allocation);
	vkDestroyImageView(_engine->_device, _turbulenceImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _turbulenceImage.image, _turbulenceImage.allocation);
	vkDestroyImageView(_engine->_device, _butterflyImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _butterflyImage.image, _butterflyImage.allocation);
	vkDestroyImageView(_engine->_device, _fourierDx_DzImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _fourierDx_DzImage.image, _fourierDx_DzImage.allocation);
	vkDestroyImageView(_engine->_device, _fourierDy_DxdzImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _fourierDy_DxdzImage.image, _fourierDy_DxdzImage.allocation);
	vkDestroyImageView(_engine->_device, _fourierDxdx_DzdzImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _fourierDxdx_DzdzImage.image, _fourierDxdx_DzdzImage.allocation);
	vkDestroyImageView(_engine->_device, _fourierDydx_DydzImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _fourierDydx_DydzImage.image, _fourierDydx_DydzImage.allocation);
	vkDestroyImageView(_engine->_device, _posSpectrumImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _posSpectrumImage.image, _posSpectrumImage.allocation);
	vkDestroyImageView(_engine->_device, _negSpectrumImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _negSpectrumImage.image, _negSpectrumImage.allocation);
	vkDestroyImageView(_engine->_device, _pingpongImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _pingpongImage.image, _pingpongImage.allocation);
	vkDestroyImageView(_engine->_device, _noiseImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _noiseImage.image, _noiseImage.allocation);
	vkDestroyImageView(_engine->_device, _displacementImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _displacementImage.image, _displacementImage.allocation);

	vkDestroySampler(_engine->_device, _sampler, nullptr);

	vkDestroyPipelineLayout(_engine->_device, _initButterflyPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _initButterflyPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _initNoisePipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _initNoisePipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _initSpectrumPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _initSpectrumPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _fourierPassPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _fourierPassPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _horizontalPassPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _horizontalPassPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _verticalPassPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _verticalPassPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _inversionPassPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _inversionPassPipeline, nullptr);
	vkDestroyPipelineLayout(_engine->_device, _copyPassPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _copyPassPipeline, nullptr);

	_engine->destroyBuffer(_waterMesh->meshBuffers.vertexBuffer);
	_engine->destroyBuffer(_waterMesh->meshBuffers.indexBuffer);
}

void WaterMesh::initButterflyTexture()
{
	_engine->immediateSubmit(
		[&](VkCommandBuffer cmd) {
			vkutil::transitionImage(cmd, _butterflyImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initButterflyPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initButterflyPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
		}
	);
}

void WaterMesh::initNoiseTexture()
{
	_engine->immediateSubmit(
		[&](VkCommandBuffer cmd) {
			vkutil::transitionImage(cmd, _noiseImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initNoisePipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initNoisePipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
		}
	);
}

void WaterMesh::initSpectrumTextures()
{
	_engine->immediateSubmit(
		[&](VkCommandBuffer cmd) {
			vkutil::transitionImage(cmd, _posSpectrumImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkutil::transitionImage(cmd, _negSpectrumImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initSpectrumPipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _initSpectrumPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
		}
	);
}

void WaterMesh::step(VkCommandBuffer cmd)
{
	fourierPass(cmd);
	ifft2D(cmd, _fourierDx_DzImage);
	ifft2D(cmd, _fourierDy_DxdzImage);
	ifft2D(cmd, _fourierDxdx_DzdzImage);
	ifft2D(cmd, _fourierDydx_DydzImage);
	copyToResultTextures(cmd);
}

void WaterMesh::fourierPass(VkCommandBuffer cmd)
{
	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_engine->_time, 1, 1, 1);

	vkutil::transitionImage(cmd, _fourierDx_DzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDy_DxdzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDxdx_DzdzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDydx_DydzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _fourierPassPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _fourierPassPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _fourierPassPipelineLayout, 1, 1, &_fourierDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _fourierPassPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
}

void WaterMesh::ifft2D(VkCommandBuffer cmd, AllocatedImage& initialTexture)
{
	bool pingpong = false;
	ComputePushConstants pushConstants;

	vkutil::transitionImage(cmd, initialTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _pingpongImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _butterflyImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	{
		_ifft2DDescriptorSet = _engine->_globalDescriptorAllocator.allocate(_engine->_device, _ifft2DDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, initialTexture.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(1, _pingpongImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_engine->_device, _ifft2DDescriptorSet);
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _horizontalPassPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _horizontalPassPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _horizontalPassPipelineLayout, 1, 1, &_ifft2DDescriptorSet, 0, nullptr);

	for (int i = 0; i < _logSize; i++)
	{
		pushConstants.data1 = glm::vec4(_engine->_time, pingpong? 1 : 0, i, 1);
		vkCmdPushConstants(cmd, _horizontalPassPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
		vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);

		vkutil::transitionImage(cmd, initialTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		vkutil::transitionImage(cmd, _pingpongImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); 
		pingpong = !pingpong;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _verticalPassPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _verticalPassPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _verticalPassPipelineLayout, 1, 1, &_ifft2DDescriptorSet, 0, nullptr);

	for (int i = 0; i < _logSize; i++)
	{
		pushConstants.data1 = glm::vec4(_engine->_time, pingpong ? 1 : 0, i, 1);
		vkCmdPushConstants(cmd, _verticalPassPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
		vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);

		vkutil::transitionImage(cmd, initialTexture.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		vkutil::transitionImage(cmd, _pingpongImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		pingpong = !pingpong;
	}

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _inversionPassPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _inversionPassPipelineLayout, 0, 1, &_computeResourceDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _inversionPassPipelineLayout, 1, 1, &_ifft2DDescriptorSet, 0, nullptr);
	pushConstants.data1 = glm::vec4(_engine->_time, pingpong ? 1 : 0, 0, 0);		
	vkCmdPushConstants(cmd, _inversionPassPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
}

void WaterMesh::copyToResultTextures(VkCommandBuffer cmd)
{
	vkutil::transitionImage(cmd, _fourierDx_DzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDy_DxdzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDxdx_DzdzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fourierDydx_DydzImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _displacementImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _derivativesImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _turbulenceImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_engine->_time, 1, 0, 0);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _copyPassPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _copyPassPipelineLayout, 0, 1, &_fourierDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _copyPassPipelineLayout, 1, 1, &_waterDataDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _copyPassPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	vkCmdDispatch(cmd, std::ceil(TEXTURE_SIZE / 8), std::ceil(TEXTURE_SIZE / 8), 1);
}
