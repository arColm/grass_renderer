#include "vk_types.hpp"

namespace vkutil
{
	bool loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);

	class PipelineBuilder
	{
	public:
		std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;

		VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
		VkPipelineRasterizationStateCreateInfo _rasterizer;
		VkPipelineColorBlendAttachmentState _colorBlendAttachment;
		VkPipelineMultisampleStateCreateInfo _multisampling;
		VkPipelineLayout _pipelineLayout;
		VkPipelineDepthStencilStateCreateInfo _depthStencil;
		VkPipelineRenderingCreateInfo _renderInfo;
		VkFormat _colorAttachmentFormat;

		PipelineBuilder() { clear(); }

		void clear();

		VkPipeline buildPipeline(VkDevice device);

		void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
		void setInputTopology(VkPrimitiveTopology topology);
		void setPolygonMode(VkPolygonMode mode);
		void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
		void setMultisamplingNone();

		void disableBlending();
		void enableBlendingAdditive();
		void enableBlendingAlphaBlend();

		void disableDepthTest();
		void enableDepthTest(bool depthWriteEnable, VkCompareOp op);

		void setColorAttachmentFormat(VkFormat format);
		void setDepthFormat(VkFormat format);

	};
}