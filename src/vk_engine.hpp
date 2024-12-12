#pragma once

#include "vk_types.hpp"
#include "vk_loader.hpp"


class VulkanEngine
{
public:

	struct DeletionQueue
	{
		//this structure stores a queue of functions which delete vulkan objects
		//note: this is not optimal for large number of objects!
		//		better is to store arrays of each object type (e.g. VkImage), and iterate through those and delete.
		std::deque<std::function<void()>> deletors;

		void pushFunction(std::function<void()>&& function)
		{
			deletors.push_back(function);
		}

		void flush()
		{
			//reverse iterate to execute all functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
			{
				(*it)();
			}

			deletors.clear();
		}
	};
	struct FrameData
	{
		VkCommandPool commandPool;
		VkCommandBuffer mainCommandBuffer;

		VkSemaphore swapchainSemaphore, renderSemaphore;
		VkFence renderFence;

		DeletionQueue deletionQueue;
	};

	bool _isInitialized{ false };
	int _frameNumber{ 0 };
	bool _stopRendering{ false };
	VkExtent2D _windowExtent{ 1700, 900 };
	struct SDL_Window* _window{ nullptr };
	bool _windowResized{ false };
	static VulkanEngine& Get();
	static constexpr unsigned int FRAME_OVERLAP = 2;
	static constexpr bool bUseValidationLayers = true;


	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debugMessenger; //debug output handle
	VkPhysicalDevice _physicalDevice; //GPU chosen as default physical device
	VkDevice _device; //logical device for commands
	VkSurfaceKHR _surface; //window surface

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	FrameData _frames[FRAME_OVERLAP];
	FrameData& getCurrentFrame() { return _frames[_frameNumber % FRAME_OVERLAP]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	DeletionQueue _mainDeletionQueue;

	//draw resources
	VmaAllocator _allocator;
	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float _renderScale = 1.f;

	//shader descriptors
	DescriptorAllocator globalDescriptorAllocator;
	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	//pipelines
	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;
	
	std::vector<ComputeEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

	//immediate submit structures
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	//triangle pipeline
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;

	//mesh pipeline
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	GPUMeshBuffers _rectangle;

	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	


	//initializes everything in engine
	void init();

	//shuts down engine
	void cleanup();

	//draw loop
	void draw();
	void drawBackground(VkCommandBuffer cmd);
	void drawImGui(VkCommandBuffer cmd, VkImageView targetImageView);
	void drawGeometry(VkCommandBuffer cmd);

	//run main loop
	void run();

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	//buffers
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroyBuffer(const AllocatedBuffer& buffer);

	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
private:

	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();

	void createSwapchain(uint32_t width, uint32_t height, VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR);
	void destroySwapchain();
	void resizeSwapchain();

	void initDescriptors();

	void initPipelines();
	void initBackgroundPipelines();

	void initImGui();

	void initTrianglePipeline();

	void initMeshPipeline();
	void initDefaultData();
};