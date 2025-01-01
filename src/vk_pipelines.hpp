#include "vk_types.hpp"

namespace vkutil
{
	bool loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);

	enum ColorBlendingMode {
		DISABLED,
		ADDITIVE,
		ALPHABLEND
	};

	class PipelineBuilder
	{
	public:
		std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;

		VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
		VkPipelineRasterizationStateCreateInfo _rasterizer;
		std::vector<VkPipelineColorBlendAttachmentState> _colorBlendAttachments;
		VkPipelineMultisampleStateCreateInfo _multisampling;
		VkPipelineLayout _pipelineLayout;
		VkPipelineDepthStencilStateCreateInfo _depthStencil;
		VkPipelineRenderingCreateInfo _renderInfo;
		std::vector<VkFormat> _colorAttachmentFormats;

		PipelineBuilder() { clear(); }

		void clear();

		VkPipeline buildPipeline(VkDevice device);

		void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
		void setVertexShader(VkShaderModule vertexShader);
		void setInputTopology(VkPrimitiveTopology topology);
		void setPolygonMode(VkPolygonMode mode);
		void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
		void setMultisamplingNone();

		void setBlendingModes(const std::vector<ColorBlendingMode>& modes);

		void disableDepthTest();
		void enableDepthTest(bool depthWriteEnable, VkCompareOp op);

		void setColorAttachmentFormat(VkFormat format);
		void setColorAttachmentFormats(const std::vector<VkFormat>& format);
		void setDepthFormat(VkFormat format);

	private:

		void disableBlending(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
		void enableBlendingAdditive(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
		void enableBlendingAlphaBlend(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
	};

}