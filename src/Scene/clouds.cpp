
#include "clouds.hpp"
#include "../vk_engine.hpp"
#include "../vk_engine_settings.hpp"
#include "../vk_initializers.hpp"
#include "../vk_pipelines.hpp"
#include "../vk_buffers.hpp"
#include "../vk_images.hpp"
#include "../../thirdparty/imgui/imgui.h"
#include <iostream>

void CloudMesh::update(VulkanEngine* engine, VkCommandBuffer cmd)
{
	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(engine->_time, 1, 1, 1);

	vkutil::transitionImage(cmd, _baseNoiseImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _detailNoiseImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _fluidNoiseImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _weatherImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudMapComputePipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _cloudMapComputePipelineLayout, 0, 1, &_cloudMapDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _cloudMapComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	vkCmdDispatch(cmd, std::ceil(RENDER_DISTANCE * 2 / 16.0f), std::ceil(CLOUD_MAP_HEIGHT / 1.0f), std::ceil(RENDER_DISTANCE*2 / 16.0f));
}

void CloudMesh::init(VulkanEngine* engine)
{
	_engine = engine;

	/*
	*  SAMPLER
	*/
	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.pNext = nullptr;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

	vkCreateSampler(engine->_device, &samplerCreateInfo, nullptr, &_sampler);


	/*
	*  IMAGES
	*/
	{
		VkExtent3D imageExtent = {
			CLOUD_MAP_SIZE,
			CLOUD_MAP_HEIGHT,
			CLOUD_MAP_SIZE,
		};

		//hardcoding draw format to 32 bit float
		_baseNoiseImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		_baseNoiseImage.imageExtent = imageExtent;

		VkImageUsageFlags imageUsageFlags{};
		imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
		if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_baseNoiseImage.imageFormat, imageUsageFlags, imageExtent, VK_IMAGE_TYPE_3D);

		//allocate draw image from gpu memory
		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
		imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

		//allocate and create image
		vmaCreateImage(engine->_allocator, &imgInfo, &imgAllocInfo, &_baseNoiseImage.image, &_baseNoiseImage.allocation, nullptr);

		//build image view for the draw image to use for rendering
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_baseNoiseImage.imageFormat, _baseNoiseImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_3D);

		VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &_baseNoiseImage.imageView));
	}
	{
		VkExtent3D imageExtent = {
			CLOUD_MAP_SIZE/4,
			CLOUD_MAP_HEIGHT/4,
			CLOUD_MAP_SIZE/4,
		};

		//hardcoding draw format to 32 bit float
		_detailNoiseImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		_detailNoiseImage.imageExtent = imageExtent;

		VkImageUsageFlags imageUsageFlags{};
		imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
		if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_detailNoiseImage.imageFormat, imageUsageFlags, imageExtent, VK_IMAGE_TYPE_3D);

		//allocate draw image from gpu memory
		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
		imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

		//allocate and create image
		vmaCreateImage(engine->_allocator, &imgInfo, &imgAllocInfo, &_detailNoiseImage.image, &_detailNoiseImage.allocation, nullptr);

		//build image view for the draw image to use for rendering
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_detailNoiseImage.imageFormat, _detailNoiseImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_3D);

		VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &_detailNoiseImage.imageView));
	}
	{
		VkExtent3D imageExtent = {
			CLOUD_MAP_SIZE,
			CLOUD_MAP_SIZE,
			1
		};

		//hardcoding draw format to 32 bit float
		_fluidNoiseImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		_fluidNoiseImage.imageExtent = imageExtent;

		VkImageUsageFlags imageUsageFlags{};
		imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
		if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_fluidNoiseImage.imageFormat, imageUsageFlags, imageExtent, VK_IMAGE_TYPE_2D);

		//allocate draw image from gpu memory
		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
		imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

		//allocate and create image
		vmaCreateImage(engine->_allocator, &imgInfo, &imgAllocInfo, &_fluidNoiseImage.image, &_fluidNoiseImage.allocation, nullptr);

		//build image view for the draw image to use for rendering
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_fluidNoiseImage.imageFormat, _fluidNoiseImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);

		VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &_fluidNoiseImage.imageView));
	}
	{
		VkExtent3D imageExtent = {
			RENDER_DISTANCE*2,
			RENDER_DISTANCE * 2,
			1
		};

		//hardcoding draw format to 32 bit float
		_weatherImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
		_weatherImage.imageExtent = imageExtent;

		VkImageUsageFlags imageUsageFlags{};
		imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
		if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_weatherImage.imageFormat, imageUsageFlags, imageExtent, VK_IMAGE_TYPE_2D);

		//allocate draw image from gpu memory
		VmaAllocationCreateInfo imgAllocInfo{};
		imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
		imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

		//allocate and create image
		vmaCreateImage(engine->_allocator, &imgInfo, &imgAllocInfo, &_weatherImage.image, &_weatherImage.allocation, nullptr);

		//build image view for the draw image to use for rendering
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_weatherImage.imageFormat, _weatherImage.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D);

		VK_CHECK(vkCreateImageView(engine->_device, &viewInfo, nullptr, &_weatherImage.imageView));
	}

	/*
	*  DESCRIPTORS
	*/
	//	descriptor layout
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_cloudMapDescriptorLayout = builder.build(engine->_device, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	{
		//	writing to descriptor set
		_cloudMapDescriptorSet = engine->_globalDescriptorAllocator.allocate(engine->_device, _cloudMapDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _baseNoiseImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(1, _detailNoiseImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(2, _fluidNoiseImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.writeImage(3, _weatherImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(engine->_device, _cloudMapDescriptorSet);
	}


	//	PIPELINE
	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("./shaders/scene/cloudmap.comp.spv", engine->_device, &computeShader))
	{
		fmt::print("error when building cloudmap compute shader module\n");
	}
	else
	{
		fmt::print("cloudmap compute shader loaded\n");
	}

	//push constant range
	VkPushConstantRange computeBufferRange{};
	computeBufferRange.offset = 0;
	computeBufferRange.size = sizeof(ComputePushConstants);
	computeBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	//sets

	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pPushConstantRanges = &computeBufferRange;
	computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_cloudMapDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(engine->_device, &computePipelineLayoutInfo, nullptr, &_cloudMapComputePipelineLayout));

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.pName = "main"; //name of entrypoint function
	stageInfo.module = computeShader;

	//create pipelines
	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _cloudMapComputePipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(engine->_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_cloudMapComputePipeline));

	//clean structures
	vkDestroyShaderModule(engine->_device, computeShader, nullptr);

	engine->immediateSubmit(
		[&](VkCommandBuffer cmd) {
			update(engine, cmd);
		}
	);


	/*
	*  CLOUD MESH
	*/
	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	const std::vector<glm::vec4> vertices{
		glm::vec4(-RENDER_DISTANCE, 180.9f, -RENDER_DISTANCE ,1.0f),
		glm::vec4(RENDER_DISTANCE , 180.9f, -RENDER_DISTANCE ,1.0f),
		glm::vec4(-RENDER_DISTANCE , 180.9f, RENDER_DISTANCE ,1.0f),
		glm::vec4(RENDER_DISTANCE , 180.9f, RENDER_DISTANCE,1.0f),

		glm::vec4(-RENDER_DISTANCE, 300.9f, -RENDER_DISTANCE  ,1.0f),
		glm::vec4(RENDER_DISTANCE , 300.9f, -RENDER_DISTANCE ,1.0f),
		glm::vec4(-RENDER_DISTANCE , 300.9f, RENDER_DISTANCE ,1.0f),
		glm::vec4(RENDER_DISTANCE , 300.9f, RENDER_DISTANCE ,1.0f)
	};
	const std::vector<uint32_t> indices({
		0,1,2,
		1,3,2,

		4,5,6,
		5,7,6,

		0,4,1,
		4,5,1,

		0,4,2,
		4,6,2,

		3,7,1,
		7,5,1,

		3,7,2,
		7,6,2
		});

	const size_t vertexBufferSize = vertices.size() * sizeof(glm::vec4);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	meshBuffers.vertexBuffer = engine->createBuffer(
		vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//find address of vertex buffer
	VkBufferDeviceAddressInfo deviceAddressInfo{};
	deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAddressInfo.pNext = nullptr;
	deviceAddressInfo.buffer = meshBuffers.vertexBuffer.buffer;
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(engine->_device, &deviceAddressInfo);

	//create index buffer
	meshBuffers.indexBuffer = engine->createBuffer(
		indexBufferSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging = engine->createBuffer(
		vertexBufferSize + indexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(engine->_allocator, staging.allocation, &data);

	//copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	//copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	//	note:	with this we have to wait for GPU commmands to finish before uploading
	//			usually we have a DEDICATED BACKGROUND THREAD/COMMAND BUFFER to handle transfers
	engine->immediateSubmit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.indexBuffer.buffer, 1, &indexCopy);
		});

	vmaUnmapMemory(engine->_allocator, staging.allocation);
	engine->destroyBuffer(staging);


	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].startIndex = static_cast<uint32_t>(0);
	surfaces[0].count = static_cast<uint32_t>(indices.size());

	meshAsset.name = "clouds";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = meshBuffers;

	_cloudMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

	// DESCRIPTORS
	//	descriptor layout
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		builder.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		_cloudMapSamplerDescriptorLayout = builder.build(engine->_device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	{
		//	writing to descriptor set
		_cloudMapSamplerDescriptorSet = engine->_globalDescriptorAllocator.allocate(engine->_device, _cloudMapSamplerDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _baseNoiseImage.imageView, _sampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.writeImage(1, _detailNoiseImage.imageView, _sampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.writeImage(2, _fluidNoiseImage.imageView, _sampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.writeImage(3, _weatherImage.imageView, _sampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(engine->_device, _cloudMapSamplerDescriptorSet);
	}

	VkShaderModule fragShader;
	if (!vkutil::loadShaderModule("./shaders/scene/cloud.frag.spv", engine->_device, &fragShader))
	{
		fmt::print("error when building cloud fragmentshader module");
	}
	else
	{
		fmt::print("cloud fragment shader loaded");
	}
	VkShaderModule vertShader;
	if (!vkutil::loadShaderModule("./shaders/scene/cloud.vert.spv", engine->_device, &vertShader))
	{
		fmt::print("error when building cloud vertex shader module");
	}
	else
	{
		fmt::print("cloud vertex shader loaded");
	}

	//push constant range
	VkPushConstantRange pushRange[1];
	pushRange[0].offset = 0;
	pushRange[0].size = sizeof(GPUDrawPushConstants);
	pushRange[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;


	//sets
	VkDescriptorSetLayout layouts[] = {
		engine->_sceneDataDescriptorLayout,
		_cloudMapSamplerDescriptorLayout
	};
	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = pushRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(engine->_device, &pipelineLayoutInfo, nullptr, &_cloudPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		engine->_drawImage.imageFormat,
		engine->_normalsImage.imageFormat,
		engine->_specularMapImage.imageFormat,
		engine->_positionsImage.imageFormat
	};
	//CREATE PIPELINE
	vkutil::PipelineBuilder pipelineBuilder;

	//	pipeline layout
	pipelineBuilder._pipelineLayout = _cloudPipelineLayout;
	//	connect vertex and fragment shaders to pipeline
	pipelineBuilder.setShaders(vertShader, fragShader);
	//	input topology
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//	polygon mode
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	//	cull mode
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	//	disable multisampling
	pipelineBuilder.setMultisamplingNone();
	//	BLENDING
	std::vector<vkutil::ColorBlendingMode> modes = {
		vkutil::ALPHABLEND,
		vkutil::ALPHABLEND,
		vkutil::ALPHABLEND,
		vkutil::ALPHABLEND,
	};
	pipelineBuilder.setBlendingModes(modes);
	// depth testing
	pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
	//pipelineBuilder.disableDepthTest();

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
	pipelineBuilder.setDepthFormat(engine->_depthImage.imageFormat);

	//build pipeline
	_cloudPipeline = pipelineBuilder.buildPipeline(engine->_device);

	//clean structures
	vkDestroyShaderModule(engine->_device, fragShader, nullptr);
	vkDestroyShaderModule(engine->_device, vertShader, nullptr);


}

int CloudMesh::draw(VkDescriptorSet* sceneDataDescriptorSet, GPUDrawPushConstants pushConstants, VkCommandBuffer cmd)
{
	CloudSettingsPushConstants settings;
	//settings.coverage = (-_cloudCoverage + 0.5f) * 2;
	settings.coverage = (_cloudCoverage-1.1)*5.;
	settings.hgConstant = _hgConstant;

	pushConstants.vertexBuffer = _cloudMesh->meshBuffers.vertexBufferAddress;
	pushConstants.data = glm::vec4(settings.coverage, settings.hgConstant, 1, settings.coverage);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _cloudPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _cloudPipelineLayout, 0, 1, sceneDataDescriptorSet, 0, nullptr);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _cloudPipelineLayout, 1, 1, &_cloudMapSamplerDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _cloudPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);	
	vkCmdBindIndexBuffer(cmd, _cloudMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _cloudMesh->surfaces[0].count, 1, _cloudMesh->surfaces[0].startIndex, 0, 0);
	return _cloudMesh->surfaces[0].count / 3 * 1;
}

void CloudMesh::drawGUI()
{
	if (ImGui::Begin("cloud settings"))
	{
		ImGui::SliderFloat("coverage", &_cloudCoverage, 0, 1.);
		ImGui::SliderFloat("hg", &_hgConstant, 0., 1.);
		ImGui::End();
	}

}

void CloudMesh::cleanup()
{
	vkDestroyImageView(_engine->_device, _baseNoiseImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _baseNoiseImage.image, _baseNoiseImage.allocation);
	vkDestroyImageView(_engine->_device, _detailNoiseImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _detailNoiseImage.image, _detailNoiseImage.allocation);
	vkDestroyImageView(_engine->_device, _fluidNoiseImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _fluidNoiseImage.image, _fluidNoiseImage.allocation);
	vkDestroyImageView(_engine->_device, _weatherImage.imageView, nullptr);
	vmaDestroyImage(_engine->_allocator, _weatherImage.image, _weatherImage.allocation);

	vkDestroySampler(_engine->_device, _sampler, nullptr);

	vkDestroyPipelineLayout(_engine->_device, _cloudMapComputePipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _cloudMapComputePipeline, nullptr);

	_engine->destroyBuffer(_cloudMesh->meshBuffers.vertexBuffer);
	_engine->destroyBuffer(_cloudMesh->meshBuffers.indexBuffer);

	vkDestroyPipelineLayout(_engine->_device, _cloudPipelineLayout, nullptr);
	vkDestroyPipeline(_engine->_device, _cloudPipeline, nullptr);
}