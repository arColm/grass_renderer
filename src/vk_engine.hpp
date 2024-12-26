#pragma once

#include "vk_types.hpp"
#include "vk_loader.hpp"
#include "vk_descriptors.hpp"
#include "player.hpp"


class VulkanEngine
{
public:
	struct EngineStats
	{
		float frameTime;
		float fps;
	};

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
	struct DrawQueue
	{
		typedef std::function<void(VkDescriptorSet sceneDataDescriptorSet, GPUDrawPushConstants drawPushConstants)> drawCall;
		//this structure stores a queue of functions which delete vulkan objects
		//note: this is not optimal for large number of objects!
		//		better is to store arrays of each object type (e.g. VkImage), and iterate through those and delete.
		std::deque<drawCall> deletors;

		void pushFunction(drawCall&& function)
		{
			deletors.push_back(function);
		}

		void flush(VkDescriptorSet sceneDataDescriptorSet, GPUDrawPushConstants drawPushConstants)
		{
			//reverse iterate to execute all functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
			{
				(*it)(sceneDataDescriptorSet,drawPushConstants);
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
		DrawQueue drawQueue; // flushed at end of every frame to commit draws

		DescriptorAllocatorGrowable descriptorAllocator;

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
	AllocatedImage _noiseImage;
	VkExtent2D _drawExtent;
	float _renderScale = 1.f;
	VkSampler _defaultSampler;

	//shader descriptors
	DescriptorAllocatorGrowable _globalDescriptorAllocator;
	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	//immediate submit structures
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	//background pipeline
	VkPipelineLayout _backgroundPipelineLayout;
	VkPipeline _backgroundPipeline;

	//mesh pipeline
	VkDescriptorSetLayout _sceneDataDescriptorLayout;
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	

	//scene
	Player _player;
	SceneData _sceneData;
	glm::vec3 _sunPosition;
	float _time = 0;

	EngineStats _engineStats;
	std::shared_ptr<MeshAsset> _groundMesh;
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>> _meshAssets;

	//grass
	bool _settingsChanged = true;
	int _grassCount = 0;
	int _maxGrassDistance = 5;
	int _grassDensity = 1;
	VkPipelineLayout _grassPipelineLayout;
	VkPipeline _grassPipeline;
	VkPipelineLayout _grassComputePipelineLayout;
	VkPipeline _grassComputePipeline;
	VkDescriptorSetLayout _grassDataDescriptorLayout;
	VkDescriptorSet _grassDataDescriptorSet;
	AllocatedBuffer _grassDataBuffer;
	std::shared_ptr<MeshAsset> _grassMesh;

	//terrain
	static const int RENDER_DISTANCE;
	static const int HEIGHT_MAP_SIZE;
	AllocatedImage _heightMapImage;
	VkPipelineLayout _heightMapComputePipelineLayout;
	VkPipeline _heightMapComputePipeline;
	VkDescriptorSetLayout _heightMapDescriptorLayout;
	VkDescriptorSet _heightMapDescriptorSet;
	VkImageView _heightMapDebugImageView;
	VkDescriptorSet _heightMapSamplerDescriptorSet;

	//shadowmap
	static const int SHADOWMAP_RESOLUTION;
	VkDescriptorSetLayout _shadowMapDescriptorLayout;
	VkDescriptorSet _shadowMapDescriptorSet;
	AllocatedImage _shadowMapImage;
	SceneData _shadowMapSceneData;
	VkPipelineLayout _shadowMeshPipelineLayout;
	VkPipeline _shadowMeshPipeline; 
	VkPipelineLayout _shadowGrassPipelineLayout;
	VkPipeline _shadowGrassPipeline; 


	//wind
	AllocatedImage _windMapImage;
	VkPipelineLayout _windMapComputePipelineLayout; 
	VkPipeline _windMapComputePipeline;
	VkDescriptorSetLayout _windMapDescriptorLayout;
	VkDescriptorSet _windMapDescriptorSet;

	VkImageView _windMapDebugImageView;
	VkDescriptorSet _windMapSamplerDescriptorSet;

	//skybox
	std::shared_ptr<MeshAsset> _skyboxMesh;
	VkPipelineLayout _skyboxPipelineLayout; //TODO
	VkPipeline _skyboxPipeline; //TODO


	//initializes everything in engine
	void init();

	//shuts down engine
	void cleanup();

	//draw loop
	void draw();
	void drawBackground(VkCommandBuffer cmd);
	void drawImGui(VkCommandBuffer cmd, VkImageView targetImageView);
	void drawGeometry(VkCommandBuffer cmd);
	void drawShadowMap(VkCommandBuffer cmd);
	void drawSkybox(VkCommandBuffer cmd);

	void updateGrassData(VkCommandBuffer cmd);

	//run main loop
	void run();

	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	//buffers
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroyBuffer(const AllocatedBuffer& buffer);

	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	//scene
	void updateScene(float deltaTime);
	void updateWindMap(VkCommandBuffer cmd);
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

	void initSampler();

	void initImGui();

	void initMeshPipeline();
	void initGrassPipeline();

	void initDefaultData();
	void initSceneData();

	//scene
	void initGround();
	void initGrass();
	void initHeightMap();
	void initShadowMapResources();
	void initWindMap();
	void initSkybox();

};