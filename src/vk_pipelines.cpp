#include "vk_pipelines.hpp"
#include <fstream>
#include "vk_initializers.hpp"


bool vkutil::loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
		return false;

	//find size of file by looking up location of cursor
	size_t fileSize = (size_t)file.tellg();

	//spirv expects buffer to be in uint32 -- reserve an int vector big enough for entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	//put file cursor at beginning
	file.seekg(0);

	//load entire file into buffer
	file.read((char*)buffer.data(), fileSize);

	//close file
	file.close();

	//create a new shader module, using buffer we loaded
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	//codesize has to be in bytes
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

void vkutil::PipelineBuilder::clear()
{
	_inputAssembly = {};
	_inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	_rasterizer = {};
	_rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

	_colorBlendAttachments.clear();
	
	_multisampling = {};
	_multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

	_pipelineLayout = {};
	
	_depthStencil = {};
	_depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	_renderInfo = {};
	_renderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;

	_shaderStages.clear();
}

VkPipeline vkutil::PipelineBuilder::buildPipeline(VkDevice device)
{
	//make viewport state from stored viewport and scissor
	//	note: we currently only support a single viewport/scissor
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	//setup color blending.
	//	we aren't using transparency yet, so NO_BLEND
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;
	
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = _colorBlendAttachments.size();
	colorBlending.pAttachments = _colorBlendAttachments.data();

	// clear VertexInputStateCreateInfo
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	//build pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &_renderInfo;

	pipelineInfo.stageCount = (uint32_t)_shaderStages.size();
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pipelineLayout;

	//dynamic state
	VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo{};
	dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicInfo.pDynamicStates = &state[0];
	dynamicInfo.dynamicStateCount = 2;

	pipelineInfo.pDynamicState = &dynamicInfo;

	//create pipeline
	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		fmt::println("failed to create pipeline");
		return VK_NULL_HANDLE;
	}

	return newPipeline;

}

void vkutil::PipelineBuilder::setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
	_shaderStages.clear();
	_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
	_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}
void vkutil::PipelineBuilder::setVertexShader(VkShaderModule vertexShader)
{
	_shaderStages.clear();
	_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
}

void vkutil::PipelineBuilder::setInputTopology(VkPrimitiveTopology topology)
{
	_inputAssembly.topology = topology;

	_inputAssembly.primitiveRestartEnable = VK_FALSE; //used for triangle strips and line strips
}

void vkutil::PipelineBuilder::setPolygonMode(VkPolygonMode mode)
{
	//controls wireframe vs solid rendering vs point rendering
	_rasterizer.polygonMode = mode;
	_rasterizer.lineWidth = 1.f;
}

void vkutil::PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	_rasterizer.cullMode = cullMode;
	_rasterizer.frontFace = frontFace;
}

void vkutil::PipelineBuilder::setMultisamplingNone()
{
	_multisampling.sampleShadingEnable = VK_FALSE;
	//default to no multisampling	
	_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	_multisampling.minSampleShading = 1.0f;
	_multisampling.pSampleMask = nullptr;

	_multisampling.alphaToCoverageEnable = VK_FALSE;
	_multisampling.alphaToOneEnable = VK_FALSE;
}

void vkutil::PipelineBuilder::disableBlending(VkPipelineColorBlendAttachmentState& colorBlendAttachment)
{
	colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	colorBlendAttachment.blendEnable = VK_FALSE;
}

void vkutil::PipelineBuilder::enableBlendingAdditive(VkPipelineColorBlendAttachmentState& colorBlendAttachment)
{
	colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void vkutil::PipelineBuilder::enableBlendingAlphaBlend(VkPipelineColorBlendAttachmentState& colorBlendAttachment)
{
	colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void vkutil::PipelineBuilder::setBlendingModes(const std::vector<ColorBlendingMode>& modes)
{
	_colorBlendAttachments.clear();
	for (int i = 0; i < modes.size(); i++)
	{
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		switch (modes[i])
		{
		case ADDITIVE:
			enableBlendingAdditive(colorBlendAttachment);
			break;
		case ALPHABLEND:
			enableBlendingAlphaBlend(colorBlendAttachment);
			break;
		default:
			disableBlending(colorBlendAttachment);
			break;
		}
		_colorBlendAttachments.push_back(colorBlendAttachment);
	}
}

void vkutil::PipelineBuilder::disableDepthTest()
{
	_depthStencil.depthTestEnable = VK_FALSE;
	_depthStencil.depthWriteEnable = VK_FALSE;
	_depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;
	_depthStencil.front = {};
	_depthStencil.back = {};
	_depthStencil.minDepthBounds = 0.f;
	_depthStencil.maxDepthBounds = 1.f;
}

void vkutil::PipelineBuilder::enableDepthTest(bool depthWriteEnable, VkCompareOp op)
{
	_depthStencil.depthTestEnable = VK_TRUE;
	_depthStencil.depthWriteEnable = depthWriteEnable;
	_depthStencil.depthCompareOp = op;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;
	_depthStencil.front = {};
	_depthStencil.back = {};
	_depthStencil.minDepthBounds = 0.f;
	_depthStencil.maxDepthBounds = 1.f;
}

void vkutil::PipelineBuilder::setColorAttachmentFormat(VkFormat format)
{
	_colorAttachmentFormats.clear();
	_colorAttachmentFormats.push_back(format);
	//connect format to renderInfo structure
	_renderInfo.colorAttachmentCount = 1;
	_renderInfo.pColorAttachmentFormats = _colorAttachmentFormats.data();
}
void vkutil::PipelineBuilder::setColorAttachmentFormats(const std::vector<VkFormat>& formats)
{
	_colorAttachmentFormats.clear();
	for (int i = 0; i < formats.size(); i++)
	{
		_colorAttachmentFormats.push_back(formats[i]);
	}
	//connect format to renderInfo structure
	_renderInfo.colorAttachmentCount = _colorAttachmentFormats.size();
	_renderInfo.pColorAttachmentFormats = _colorAttachmentFormats.data();
}

void vkutil::PipelineBuilder::setDepthFormat(VkFormat format)
{
	_renderInfo.depthAttachmentFormat = format;
}


