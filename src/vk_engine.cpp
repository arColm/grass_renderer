#include "vk_engine.hpp"

#include <SDL/SDL.h>
#include <SDL/SDL_vulkan.h>

#include <thread>
#include <chrono>

#include <VkBootstrap/VkBootstrap.h>
#include <iostream>
#include "vk_initializers.hpp"
#include "vk_images.hpp"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_descriptors.hpp"
#include "vk_pipelines.hpp"

#include "../thirdparty/imgui/imgui.h"
#include "../thirdparty/imgui/imgui_impl_vulkan.h"
#include "../thirdparty/imgui/imgui_impl_sdl2.h"

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtx/transform.hpp>
#include "noise.hpp"
#include "vk_buffers.hpp"

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }

void VulkanEngine::init()
{
	assert(loadedEngine == nullptr); //enforce only 1 engine
	loadedEngine = this;

	//initialize SDL window
	SDL_Init(SDL_INIT_VIDEO);

	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow(
		"vkGuide",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width, _windowExtent.height,
		windowFlags
	);

	initVulkan();
	initSwapchain();
	initCommands();
	initSyncStructures();
	initDescriptors();
	initPipelines();
	
	initDefaultData();
	initSceneData();

	initImGui();


	_isInitialized = true;
}

void VulkanEngine::cleanup()
{
	if (_isInitialized)
	{
		vkDeviceWaitIdle(_device);

		for (int i = 0; i < FRAME_OVERLAP; i++)
		{
			vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);
			//destroying command pool destroys all its command buffers

			//destroy sync objects
			vkDestroyFence(_device, _frames[i].renderFence, nullptr);
			vkDestroySemaphore(_device, _frames[i].renderSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);

			_frames[i].deletionQueue.flush();
		}

		for (auto& mesh : testMeshes)
		{
			destroyBuffer(mesh->meshBuffers.indexBuffer);
			destroyBuffer(mesh->meshBuffers.vertexBuffer);
		}

		_mainDeletionQueue.flush();


		destroySwapchain();

		vkDestroySurfaceKHR(_instance, _surface, nullptr);
		vkDestroyDevice(_device, nullptr);
		vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
		vkDestroyInstance(_instance, nullptr);

		SDL_DestroyWindow(_window);
	}

	loadedEngine = nullptr;
}

void VulkanEngine::draw()
{
	static const uint64_t timeout = 1000000000;

	//wait until gpu has finished rendering the last frame, timeout of 1 second
	VK_CHECK(vkWaitForFences(_device, 1, &getCurrentFrame().renderFence, true, timeout));

	getCurrentFrame().deletionQueue.flush();

	VK_CHECK(vkResetFences(_device, 1, &getCurrentFrame().renderFence));

	//request image from swapchain
	uint32_t swapchainImageIndex;
	//	check for window resize
	VkResult e = vkAcquireNextImageKHR(_device, _swapchain, timeout, getCurrentFrame().swapchainSemaphore, nullptr, &swapchainImageIndex);
	if (e == VK_ERROR_OUT_OF_DATE_KHR)
	{
		_windowResized = true;
		return;
	}
	VkCommandBuffer cmd = getCurrentFrame().mainCommandBuffer;

	//reset command buffer now that commands are finished executing
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	_drawExtent.width = std::min(_drawImage.imageExtent.width,_swapchainExtent.width) * _renderScale;
	_drawExtent.height = std::min(_drawImage.imageExtent.height, _swapchainExtent.height) * _renderScale;

	//begin recording command buffer
	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); //reset after executing once
	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	//make the draw image into writable mode before rendering
	//note: for read-only iamge or rasterized image, GENERAL is not optimal
	//		but for compute-shader written images, GENERAL is the best
	vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); 

	//render
	drawBackground(cmd);

	vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	
	//calculate grass positions
	calculateGrassData(cmd);
	drawGeometry(cmd);

	//prepare to copy drawimage to swapchain image
	vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	
	vkutil::copyImageToImage(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

	//prepare swapchain image to draw GUI
	//	note: we use COLOR_ATTACHMENT_OPTIMAL when calling rendering commands
	vkutil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawImGui(cmd, _swapchainImageViews[swapchainImageIndex]);

	//prepare swapchain image for presenting
	vkutil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);



	//finalize command buffer
	VK_CHECK(vkEndCommandBuffer(cmd));

	//prepare submission to queue
	//wait on presentSemaphore, as the semaphore is signalled when the swapchain is ready
	//signal renderSemaphore, to signal that rendering is finished

	VkCommandBufferSubmitInfo cmdInfo = vkinit::commandBufferSubmitInfo(cmd);

	VkSemaphoreSubmitInfo waitInfo = vkinit::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, getCurrentFrame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkinit::semaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, getCurrentFrame().renderSemaphore);

	VkSubmitInfo2 submit = vkinit::submitInfo(&cmdInfo, &signalInfo, &waitInfo);

	//submit command buffer to queue and execute it
	// renderFrame will now block until graphics commands finish execution
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, getCurrentFrame().renderFence));

	
	//Prepare presenting of image
	// put the image we rendered into visible window
	// wait on renderSemaphore, as all drawing commands must be finished before the image is displayed
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &getCurrentFrame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	//	check if window resized
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		_windowResized = true;
	}

	//finally increase frame number
	_frameNumber++;
}

void VulkanEngine::drawBackground(VkCommandBuffer cmd)
{
	//make a clear-color from frame number. 
	VkClearColorValue clearValue;
	float flash = std::abs(std::sin(_frameNumber / 120.f));
	clearValue = { {0.0f,0.0f,flash,1.0f} };

	VkImageSubresourceRange clearRange = vkinit::imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT); //defines what part of the Image to clear

	//bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _backgroundPipeline);

	//bind the descriptor set containing the draw image for the compute pipeline
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _backgroundPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

	//execute compute pipeline dispatch
	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void VulkanEngine::drawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderingInfo = vkinit::renderingInfo(_swapchainExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void VulkanEngine::drawGeometry(VkCommandBuffer cmd)
{
	//PER FRAME DATA
	//	Scene Data
	AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	getCurrentFrame().deletionQueue.pushFunction(
		[=, this]() {
			destroyBuffer(sceneDataBuffer);
		}
	);
	SceneData* sceneUniformData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
	*sceneUniformData = _sceneData; //write scene data to buffer

	VkDescriptorSet sceneDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _sceneDataDescriptorLayout, nullptr);
	DescriptorWriter writer;
	writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(_device, sceneDataDescriptorSet);
	
	//begin a render pass connected to draw image
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkinit::renderingInfo(_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	//set dynamic viewport and scissor
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _drawExtent.width;
	viewport.height = _drawExtent.height;
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent.width = _drawExtent.width;
	scissor.extent.height = _drawExtent.height;
	scissor.offset.x = 0;
	scissor.offset.y = 0;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//draw mesh
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

	GPUDrawPushConstants pushConstants;
	glm::mat4 view = _player.getViewMatrix();
	glm::mat4 projection = glm::perspective(
		glm::radians(70.f),
		(float)_drawExtent.width / (float)_drawExtent.height,
		//10000.f,0.1f); //reverse depth for better precision near 0? (TODO: not working?)
		0.1f,10000.f);
	projection[1][1] *= -1; //invert y -axis
	//pushConstants.worldMatrix = projection * view;
	pushConstants.worldMatrix = glm::translate(glm::vec3(0));

	//draw loaded test mesh
	pushConstants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &sceneDataDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, testMeshes[2]->surfaces[0].count, 1, testMeshes[2]->surfaces[0].startIndex, 0, 0);


	//draw ground
	pushConstants.vertexBuffer = _groundMesh->meshBuffers.vertexBufferAddress;
	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _groundMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _groundMesh->surfaces[0].count, 1, _groundMesh->surfaces[0].startIndex, 0, 0);

	getCurrentFrame().drawQueue.flush(sceneDataDescriptorSet,pushConstants);

	vkCmdEndRendering(cmd);
}

void VulkanEngine::calculateGrassData(VkCommandBuffer cmd)
{
	uint32_t grassCount = (_maxGrassDistance * 2 * _grassDensity + 1);
	grassCount *= grassCount; //squared
	AllocatedBuffer grassDataBuffer = createBuffer(sizeof(GrassData) * grassCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	//deletion
	getCurrentFrame().deletionQueue.pushFunction(
		[=,this]() {
			destroyBuffer(grassDataBuffer);
		}
	);

	//bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _grassComputePipeline);

	//TODO mayb we should just create a buffer every frame and fill it instead of storing in FrameData hmmm
	//		then we dont have to update the framedata buffer AND this buffer when _grassCount changes.
	VkDescriptorSet grassDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _grassDataDescriptorLayout, nullptr);
	DescriptorWriter writer;
	writer.writeBuffer(0, grassDataBuffer.buffer, sizeof(GrassData)*grassCount, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	writer.updateSet(_device, grassDataDescriptorSet);


	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_player._position.x, _player._position.y, _player._position.z, 1);
	pushConstants.data2 = glm::vec4(grassCount, _maxGrassDistance, _grassDensity, 0);


	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _grassComputePipelineLayout, 0, 1, &grassDataDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _grassComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	//execute compute pipeline dispatch
	vkCmdDispatch(cmd, std::ceil((_maxGrassDistance * 2 * _grassDensity + 1) / 64.0),
		1, 1);

	vkutil::bufferBarrier(cmd, grassDataBuffer.buffer, VK_WHOLE_SIZE, 0,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
	getCurrentFrame().drawQueue.pushFunction(
		[=, this](VkDescriptorSet sceneDataDescriptorSet, GPUDrawPushConstants drawPushConstants) {

			//grass rendering
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _grassPipeline);

			drawPushConstants.vertexBuffer = _grassMesh->meshBuffers.vertexBufferAddress;
			//drawPushConstants.worldMatrix = glm::translate(glm::vec3(1, -2, 0));

			VkDescriptorSet sets[] = {
				sceneDataDescriptorSet,
				grassDataDescriptorSet
			};

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _grassPipelineLayout, 0, 2, sets, 0, nullptr);
			vkCmdPushConstants(cmd, _grassPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &drawPushConstants);
			vkCmdBindIndexBuffer(cmd, _grassMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			//draw _grassCount instances
			vkCmdDrawIndexed(cmd, _grassMesh->surfaces[0].count, grassCount, _grassMesh->surfaces[0].startIndex, 0, 0);

		}
	);
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	auto lastTickTime = std::chrono::high_resolution_clock::now();

	while (!bQuit)
	{
		auto tickTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(tickTime-lastTickTime);
		lastTickTime = tickTime;

		float deltaTime = elapsedTime.count() / 1000.f;

		_engineStats.frameTime = elapsedTime.count();
		_engineStats.fps = 1000.f / _engineStats.frameTime;

		while (SDL_PollEvent(&e) != 0)
		{
			if (e.type == SDL_QUIT)
				bQuit = true;
			if (e.type == SDL_WINDOWEVENT)
			{
				if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
					_stopRendering = true;
				if (e.window.event == SDL_WINDOWEVENT_RESTORED)
					_stopRendering = false;
			}

			_player.processSDLEvent(e, deltaTime);
			ImGui_ImplSDL2_ProcessEvent(&e); //ImGui event handling
		}

		if (_stopRendering)
		{
			//throttle speed
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (_windowResized)
		{
			resizeSwapchain();
		}

		//imgui
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();;

		if (ImGui::Begin("stats"))
		{
			ImGui::Text("frame time: %f ms", _engineStats.frameTime);
			ImGui::Text("fps: %f", _engineStats.fps);

			ImGui::End();
		}


		ImGui::Render();

		//draw frame
		draw();

		updateScene(deltaTime);
	}
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkinit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkinit::commandBufferSubmitInfo(cmd);
	VkSubmitInfo2 submit = vkinit::submitInfo(&cmdInfo, nullptr, nullptr);

	//submit command buffer to the queue and execute it
	//	_renderFence blocks until graphic command finishes execution
	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

AllocatedBuffer VulkanEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	//allocate buffer
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo{};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; //maps pointer so we can write to the memory
	
	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.allocationInfo));
	return newBuffer;
}

void VulkanEngine::destroyBuffer(const AllocatedBuffer& buffer)
{
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	GPUMeshBuffers newSurface;

	//create vertex buffer
	newSurface.vertexBuffer = createBuffer(
		vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//find address of vertex buffer
	VkBufferDeviceAddressInfo deviceAddressInfo{};
	deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAddressInfo.pNext = nullptr;
	deviceAddressInfo.buffer = newSurface.vertexBuffer.buffer;
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

	//create index buffer
	newSurface.indexBuffer = createBuffer(
		indexBufferSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	AllocatedBuffer staging = createBuffer(
		vertexBufferSize + indexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_CPU_ONLY);

	void* data = staging.allocation->GetMappedData();

	//copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	//copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	//	note:	with this we have to wait for GPU commmands to finish before uploading
	//			usually we have a DEDICATED BACKGROUND THREAD/COMMAND BUFFER to handle transfers
	immediateSubmit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
		//std::cout << vertexCopy.size << '-' << indexCopy.size << '\n';
		});

	destroyBuffer(staging);


	return newSurface;
}

void VulkanEngine::updateScene(float deltaTime)
{
	_player.update(deltaTime);
	_sceneData.view = _player.getViewMatrix();
	_sceneData.viewProj = _sceneData.proj * _sceneData.view;
}

void VulkanEngine::initVulkan()
{
	vkb::InstanceBuilder instanceBuilder;

	//make the vulkan instance with basic debug features
	auto instRet = instanceBuilder.set_app_name("VkGuide")
		.request_validation_layers(bUseValidationLayers)
		.use_default_debug_messenger()
		.require_api_version(1, 3, 0) //vulkan 1.3
		.build(); //returns a VkbInstance optional

	vkb::Instance vkbInst = instRet.value();

	_instance = vkbInst.instance;
	_debugMessenger = vkbInst.debug_messenger;

	SDL_Vulkan_CreateSurface(_window, _instance, &_surface); //creates and attaches VkSurface

	VkPhysicalDeviceVulkan13Features features13{}; //vulkan 1.3 features
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = true;
	features13.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features12{}; //vulkan 1.2 features
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = true;
	features12.descriptorIndexing = true;

	//use vkbootstrap to select GPU
	//gpu must be able to write to SDL surface and support vk 1.3
	vkb::PhysicalDeviceSelector selector{ vkbInst };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 3)
		.set_required_features_13(features13)
		.set_required_features_12(features12)
		.set_surface(_surface)
		.select()
		.value();

	//create final vulkan logical device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;

	//graphics queue
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	//memory allocator
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; //lets us use GPU pointers later
	vmaCreateAllocator(&allocatorInfo, &_allocator);
	_mainDeletionQueue.pushFunction([&]() {
		vmaDestroyAllocator(_allocator);
	});
}

void VulkanEngine::initSwapchain()
{
	createSwapchain(_windowExtent.width, _windowExtent.height);// , VK_PRESENT_MODE_FIFO_KHR); //hard vsync

	VkExtent3D drawImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	//hardcoding draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;

	VkImageUsageFlags drawImageUsageFlags{};
	drawImageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;		//can copy from image
	drawImageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;		//can copy to image
	drawImageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
	drawImageUsageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;	//allows us to draw geometry on it through graphics pipeline

	VkImageCreateInfo rimgInfo = vkinit::imageCreateInfo(_drawImage.imageFormat, drawImageUsageFlags, drawImageExtent);

	//allocate draw image from gpu memory
	VmaAllocationCreateInfo rimgAllocInfo{};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

	//allocate and create image
	vmaCreateImage(_allocator, &rimgInfo, &rimgAllocInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	//build image view for the draw image to use for rendering
	VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_drawImage.imageView));

	//add to deletion queues
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _drawImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation); //note that VMA allocated objects are deleted with VMA
		});

	//DEPTH IMAGE
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageCreateInfo dimgInfo = vkinit::imageCreateInfo(_depthImage.imageFormat, depthImageUsages, _depthImage.imageExtent);

	vmaCreateImage(_allocator, &dimgInfo, &rimgAllocInfo, &_depthImage.image, &_depthImage.allocation, nullptr);

	VkImageViewCreateInfo dviewInfo = vkinit::imageViewCreateInfo(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dviewInfo, nullptr, &_depthImage.imageView));

	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _depthImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
		});

	_noiseImage = Noise::generatePerlinNoiseImage(_device, _allocator, 1024, 1024, VK_FORMAT_R8_UNORM);
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _noiseImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _noiseImage.image, _noiseImage.allocation);
		}
	);
}

void VulkanEngine::initCommands()
{
	//tell vulkan we expect to reset individual command buffers in pool (vs reset all at once)
	VkCommandPoolCreateInfo commandPoolInfo = vkinit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i].commandPool));

		//allocate default command buffer that we use for rendering
		VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::commandBufferAllocateInfo(_frames[i].commandPool, 1);
		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].mainCommandBuffer));
	}

	//immediate commands
	VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo immCmdAllocInfo = vkinit::commandBufferAllocateInfo(_immCommandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &immCmdAllocInfo, &_immCommandBuffer));

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);
	});
}

void VulkanEngine::initSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = vkinit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT); //initialize as signalled
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphoreCreateInfo(0);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));
	}

	//immediate command fence
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyFence(_device, _immFence, nullptr);
	});

	VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_grassSemaphore));

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroySemaphore(_device, _grassSemaphore, nullptr);
		});
}

void VulkanEngine::createSwapchain(uint32_t width, uint32_t height, VkPresentModeKHR presentMode /*= VK_PRESENT_MODE_FIFO_KHR*/)
{
	vkb::SwapchainBuilder swapchainBuilder(_physicalDevice, _device, _surface);

	_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	VkSurfaceFormatKHR surfaceFormat{};
	surfaceFormat.format = _swapchainImageFormat;
	surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.set_desired_format(surfaceFormat)
		.set_desired_present_mode(presentMode)
		.set_desired_extent(width, height)
		.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.build()
		.value();

	_swapchainExtent = vkbSwapchain.extent;
	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::destroySwapchain()
{
	vkDestroySwapchainKHR(_device, _swapchain, nullptr);

	for (int i = 0; i < _swapchainImageViews.size(); i++)
	{
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

void VulkanEngine::resizeSwapchain()
{
	vkDeviceWaitIdle(_device);

	destroySwapchain();

	int w, h;
	SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width = w;
	_windowExtent.height = h;

	createSwapchain(_windowExtent.width, _windowExtent.height);

	_windowResized = false;
}

void VulkanEngine::initDescriptors()
{
	//create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
	};

	_globalDescriptorAllocator.init(_device, 10, sizes);

	//DESCRIPTOR LAYOUTS
	//make the descriptor set layout for our compute draw
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		_sceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		_grassDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
	}

	//allocate a descriptor set for draw image
	_drawImageDescriptors = _globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

	DescriptorWriter writer;
	writer.writeImage(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.updateSet(_device, _drawImageDescriptors);

	//frame descriptors
	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frameSizes = {
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
		};

		_frames[i].descriptorAllocator = DescriptorAllocatorGrowable{};
		_frames[i].descriptorAllocator.init(_device, 1000, frameSizes);

		//deletion
		_mainDeletionQueue.pushFunction(
			[&, i]() {
				_frames[i].descriptorAllocator.destroyPools(_device);
			}
		);
	}


	//deletion
	_mainDeletionQueue.pushFunction([&]() {
		_globalDescriptorAllocator.destroyPools(_device);
		vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _sceneDataDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _grassDataDescriptorLayout, nullptr);
	});
}

void VulkanEngine::initPipelines()
{
	initBackgroundPipelines();
	initMeshPipeline();
	initGrassPipeline();
}

void VulkanEngine::initBackgroundPipelines()
{

	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;

	computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_backgroundPipelineLayout));

	//load shaders

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule("./shaders/sky.comp.spv", _device, &skyShader))
	{
		fmt::print("Error when building compute shader \n");
	}

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.pName = "main"; //name of entrypoint function
	stageInfo.module = skyShader;

	//create pipelines
	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _backgroundPipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_backgroundPipeline));


	//deletion
	vkDestroyShaderModule(_device, skyShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _backgroundPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _backgroundPipeline, nullptr);
	});
}

void VulkanEngine::initImGui()
{
	// 1:	create descriptor pool for imgui
	//		note: the size of the pool is oversized
	VkDescriptorPoolSize poolSizes[] = {
		{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

	// 2.	initialize imgui library
	ImGui::CreateContext(); //initializes core structures of imgui
	ImGui_ImplSDL2_InitForVulkan(_window); //initializes imgui for SDL2

	// initialize imgui for vulkan
	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.Instance = _instance;
	initInfo.PhysicalDevice = _physicalDevice;
	initInfo.Device = _device;
	initInfo.Queue = _graphicsQueue;
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = 3;
	initInfo.ImageCount = 3;
	initInfo.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	initInfo.PipelineRenderingCreateInfo = {};
	initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo);
	ImGui_ImplVulkan_CreateFontsTexture();

	// destroy
	_mainDeletionQueue.pushFunction([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
	});
}

void VulkanEngine::initMeshPipeline()
{
	VkShaderModule meshFragShader;
	if (!vkutil::loadShaderModule("./shaders/mesh.frag.spv", _device, &meshFragShader))
	{
		fmt::print("error when building triangle fragmentshader module");
	}
	else
	{
		fmt::print("triangle fragment shader loaded");
	}
	VkShaderModule meshVertShader;
	if (!vkutil::loadShaderModule("./shaders/mesh.vert.spv", _device, &meshVertShader))
	{
		fmt::print("error when building triangle vertex shader module");
	}
	else
	{
		fmt::print("triangle vertex shader loaded");
	}
	
	//push constant range
	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	//sets

	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &_sceneDataDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	//CREATE PIPELINE
	vkutil::PipelineBuilder pipelineBuilder;

	//	pipeline layout
	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	//	connect vertex and fragment shaders to pipeline
	pipelineBuilder.setShaders(meshVertShader, meshFragShader);
	//	input topology
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//	polygon mode
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	//	cull mode
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	//	disable multisampling
	pipelineBuilder.setMultisamplingNone();
	//	disable blending
	pipelineBuilder.enableBlendingAlphaBlend();
	// depth testing
	pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormat(_drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

	//build pipeline
	_meshPipeline = pipelineBuilder.buildPipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, meshFragShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _meshPipeline, nullptr);
		});
}

void VulkanEngine::initGrassPipeline()
{
	VkShaderModule meshFragShader;
	if (!vkutil::loadShaderModule("./shaders/mesh.frag.spv", _device, &meshFragShader))
	{
		fmt::print("error when building grass fragmentshader module");
	}
	else
	{
		fmt::print("grass fragment shader loaded");
	}
	VkShaderModule meshVertShader;
	if (!vkutil::loadShaderModule("./shaders/grass.vert.spv", _device, &meshVertShader))
	{
		fmt::print("error when building grass vertex shader module");
	}
	else
	{
		fmt::print("grass vertex shader loaded");
	}

	//push constant range
	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	//sets
	VkDescriptorSetLayout layouts[] = {
		_sceneDataDescriptorLayout,
		_grassDataDescriptorLayout
	};
	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_grassPipelineLayout));

	//CREATE PIPELINE
	vkutil::PipelineBuilder pipelineBuilder;

	//	pipeline layout
	pipelineBuilder._pipelineLayout = _grassPipelineLayout;
	//	connect vertex and fragment shaders to pipeline
	pipelineBuilder.setShaders(meshVertShader, meshFragShader);
	//	input topology
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//	polygon mode
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	//	cull mode
	pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	//	disable multisampling
	pipelineBuilder.setMultisamplingNone();
	//	disable blending
	pipelineBuilder.enableBlendingAlphaBlend();
	// depth testing
	pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormat(_drawImage.imageFormat);
	pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

	//build pipeline
	_grassPipeline = pipelineBuilder.buildPipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, meshFragShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _grassPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _grassPipeline, nullptr);
		});

	//COMPUTE

	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("./shaders/grass_data.comp.spv", _device, &computeShader))
	{
		fmt::print("error when building grass compute shader module\n");
	}
	else
	{
		fmt::print("grasscompute shader loaded\n");
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
	computePipelineLayoutInfo.pSetLayouts = &_grassDataDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &_grassComputePipelineLayout));

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
	computePipelineCreateInfo.layout = _grassComputePipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_grassComputePipeline));

	//clean structures
	vkDestroyShaderModule(_device, computeShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _grassComputePipelineLayout, nullptr);
		vkDestroyPipeline(_device, _grassComputePipeline, nullptr);
		});
}

void VulkanEngine::initDefaultData()
{
	//load meshes
	testMeshes = loadGltfMeshes(this, "./assets/basicmesh.glb").value();
	initGround();
	initGrass();
}

void VulkanEngine::initSceneData()
{
	_sceneData = SceneData{};
	_sceneData.view = _player.getViewMatrix();
	_sceneData.proj = glm::perspective(
			glm::radians(70.f),
			(float)_windowExtent.width / (float)_windowExtent.height,
			//10000.f,0.1f); //reverse depth for better precision near 0? (TODO: not working?)
			0.1f, 10000.f);
	_sceneData.proj[1][1] *= -1;

	_sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.1f);
	_sceneData.sunlightDirection = glm::vec4(2, -2, 2, 1);
	_sceneData.sunlightColor = glm::vec4(1, 1, 1, 1);
	_sceneData.viewProj = _sceneData.proj*_sceneData.view;
}

void VulkanEngine::initGround()
{
	MeshAsset meshAsset{};

	std::array<Vertex, 4> vertices{};
	glm::vec4 color{ 0.07f,0.15f,0.09f,1.0f };
	
	vertices[0] = { glm::vec3(30, -2, 30), 1, glm::vec3(0, 1, 0), 1, color };
	vertices[1] = { glm::vec3(-30,-2,30), 0, glm::vec3(0,1,0), 1, color };
	vertices[2] = { glm::vec3(30,-2,-30), 1, glm::vec3(0,1,0), 0, color };
	vertices[3] = { glm::vec3(-30,-2,-30), 0, glm::vec3(0,1,0), 0, color };
	
	std::vector<uint32_t> indices{
		0,3,1,
		0,2,3
	};

	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].count = static_cast<uint32_t>(indices.size());
	surfaces[0].startIndex = static_cast<uint32_t>(0);


	meshAsset.name = "ground";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = uploadMesh(indices,vertices);


	//_meshAssets["ground"] = std::make_shared<MeshAsset>(std::move(meshAsset));
	_groundMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_groundMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_groundMesh->meshBuffers.indexBuffer);
		}
	);
}

void VulkanEngine::initGrass()
{
	MeshAsset meshAsset{};

	std::array<Vertex, 9> vertices{};
	glm::vec4 color{ 0.07f,0.23f,0.09f,1.0f };

	vertices[0] = { glm::vec3(0.05f, 0,0), 1, glm::vec3(0, 0, -1), 1, color * 1.2f };
	vertices[1] = { glm::vec3(-0.05f,0,0), 0, glm::vec3(0, 0, -1), 1, color * 1.3f };
	vertices[2] = { glm::vec3(0.05f, 0.3f, 0), 1, glm::vec3(0, 0, -1), 1, color * 1.4f };
	vertices[3] = { glm::vec3(-0.05f,0.3f, 0), 0, glm::vec3(0, 0, -1), 1, color * 1.5f };
	vertices[4] = { glm::vec3(0.05f, 0.6f, 0), 1, glm::vec3(0, 0, -1), 1, color * 1.6f };
	vertices[5] = { glm::vec3(-0.05f,0.6f, 0), 0, glm::vec3(0, 0, -1), 1, color * 1.7f };
	vertices[6] = { glm::vec3(0.05f, 0.9f, 0), 1, glm::vec3(0, 0, -1), 1, color * 1.8f };
	vertices[7] = { glm::vec3(-0.05f,0.9f, 0), 0, glm::vec3(0, 0, -1), 1, color * 1.9f };
	vertices[8] = { glm::vec3(0, 1.1f, 0), 0, glm::vec3(0, 0, -1), 1, color*2.f };

	std::vector<uint32_t> indices{
		0,3,1,
		0,2,3,
		2,5,3,
		2,4,5,
		4,7,5,
		4,6,7,
		6,8,7
	};

	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].count = static_cast<uint32_t>(indices.size());
	surfaces[0].startIndex = static_cast<uint32_t>(0);


	meshAsset.name = "grass";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = uploadMesh(indices, vertices);


	_grassMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_grassMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_grassMesh->meshBuffers.indexBuffer);
		}
	);

	//grass data buffers
	//for (int i = 0; i < FRAME_OVERLAP; i++)
	//{
	//	_frames[i].grassDataBuffer = createBuffer(sizeof(GrassData) * _grassCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	//	//deletion
	//	_mainDeletionQueue.pushFunction(
	//		[&, i]() {
	//			destroyBuffer(_frames[i].grassDataBuffer);
	//		}
	//	);
	//}
}
