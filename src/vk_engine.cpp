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

const int VulkanEngine::HEIGHT_MAP_SIZE = 2048;
const int VulkanEngine::SHADOWMAP_RESOLUTION = 2048;
const int VulkanEngine::RENDER_DISTANCE = 600;
const float VulkanEngine::CSM_SCALE = 3.5f;

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }

void VulkanEngine::init()
{
	assert(loadedEngine == nullptr); //enforce only 1 engine
	loadedEngine = this;

	//initialize SDL window
	SDL_Init(SDL_INIT_VIDEO);
	SDL_SetRelativeMouseMode(SDL_TRUE);

	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	_window = SDL_CreateWindow(
		"Grass buh",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		_windowExtent.width, _windowExtent.height,
		windowFlags
	);

	initVulkan();
	initSwapchain();
	initCommands();
	initSyncStructures();
	initSampler();
	initDescriptors();
	initShadowMapResources();
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

		destroyBuffer(_grassDataBuffer);

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
	UI_triangleCount = 0;

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

	vkutil::transitionImage(cmd, _windMapImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	updateWindMap(cmd);
	updateGrassData(cmd);

	//calculate shadow map
	vkutil::transitionImage(cmd, _shadowMapImageArray.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	drawShadowMap(cmd);
	vkutil::transitionImage(cmd, _shadowMapImageArray.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
		

	//make the draw image into writable mode before rendering
	//note: for read-only iamge or rasterized image, GENERAL is not optimal
	//		but for compute-shader written images, GENERAL is the best
	//vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); 

	//render
	//drawBackground(cmd);

	//todo write a transitionImages function to transition the all the colors together
	vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	
	vkutil::transitionImage(cmd, _normalsImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd, _specularMapImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd, _positionsImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	
	//calculate grass positions
	drawGeometry(cmd);

	//prepare to do deferred rendering
	vkutil::transitionImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _depthImage.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	vkutil::transitionImage(cmd, _normalsImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _specularMapImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _positionsImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transitionImage(cmd, _finalDrawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	drawDeferred(cmd);

	//prepare to copy final image to swapchain image for presenting
	vkutil::transitionImage(cmd, _finalDrawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	
	vkutil::copyImageToImage(cmd, _finalDrawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

	//prepare swapchain image to draw GUI
	//	note: we use COLOR_ATTACHMENT_OPTIMAL when calling rendering commands
	vkutil::transitionImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	//vkutil::transitionImage(cmd, _windMapImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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
	VkDescriptorSet sceneDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _sceneDataDescriptorLayout, nullptr);
	{
		//	Scene Data
		AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		getCurrentFrame().deletionQueue.pushFunction(
			[=, this]() {
				destroyBuffer(sceneDataBuffer);
			}
		);
		SceneData* sceneUniformData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
		*sceneUniformData = _sceneData; //write scene data to buffer

		DescriptorWriter writer;
		writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		writer.updateSet(_device, sceneDataDescriptorSet);
	}
	
	//begin a render pass connected to draw image
	//TODO if we want to point to no color attachment, make the imageview nullptr!!! and use VK_ATTACHMENT_UNUSED
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkClearColorValue normalClearValue{ .float32 = { 0.f,0.f,0.f,0.f } };
	VkRenderingAttachmentInfo normalsAttachment = vkinit::attachmentInfo(_normalsImage.imageView, (VkClearValue*)&normalClearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkClearColorValue specularMapClearValue{ .float32 = {0} };
	VkRenderingAttachmentInfo specularMapAttachment = vkinit::attachmentInfo(_specularMapImage.imageView, (VkClearValue*)&specularMapClearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkClearColorValue positionsClearValue{ .float32 = {0} };
	VkRenderingAttachmentInfo positionsAttachment = vkinit::attachmentInfo(_positionsImage.imageView, (VkClearValue*)&positionsClearValue, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo renderingAttachments[] = {
		colorAttachment,
		normalsAttachment,
		specularMapAttachment,
		positionsAttachment
	};

	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	//VkRenderingInfo renderInfo = vkinit::renderingInfo(_drawExtent, &colorAttachment, &depthAttachment);
	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.pNext = nullptr;

	renderingInfo.renderArea = VkRect2D{ VkOffset2D{0,0}, _drawExtent };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 4;
	renderingInfo.pColorAttachments = renderingAttachments;
	renderingInfo.pDepthAttachment = &depthAttachment;
	renderingInfo.pStencilAttachment = nullptr;

	vkCmdBeginRendering(cmd, &renderingInfo);

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


	GPUDrawPushConstants pushConstants{};
	pushConstants.worldMatrix = glm::translate(glm::vec3(0));
	pushConstants.playerPosition = glm::vec4(_player._position.x, _player._position.y, _player._position.z, 0);
	//draw skybox
	pushConstants.vertexBuffer = _skyboxMesh->meshBuffers.vertexBufferAddress;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipelineLayout, 0, 1, &sceneDataDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd,_skyboxPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _skyboxMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _skyboxMesh->surfaces[0].count, 1, _skyboxMesh->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += _skyboxMesh->surfaces[0].count / 3 * 1;
	//draw clouds
	pushConstants.vertexBuffer = _cloudMesh->meshBuffers.vertexBufferAddress;
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _cloudPipeline);
	//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _cloudPipelineLayout, 0, 1, &sceneDataDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _cloudPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _cloudMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _cloudMesh->surfaces[0].count, 1, _cloudMesh->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += _cloudMesh->surfaces[0].count / 3 * 1;

	//draw mesh
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

	//draw loaded test mesh
	pushConstants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;

	{
		VkDescriptorSet sets[] = {
			sceneDataDescriptorSet,
			_shadowMapDescriptorSet
		};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 2, sets, 0, nullptr);
		//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 1, 1, &_shadowMapDescriptorSet, 0, nullptr);
	}
	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, testMeshes[2]->surfaces[0].count, 1, testMeshes[2]->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += testMeshes[2]->surfaces[0].count / 3 * 1;


	//draw ground
	pushConstants.vertexBuffer = _groundMesh->meshBuffers.vertexBufferAddress;
	vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _groundMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _groundMesh->surfaces[0].count, 1, _groundMesh->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += _groundMesh->surfaces[0].count / 3 * 1;

	//draw grass
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _grassPipeline);

	pushConstants.vertexBuffer = _grassMesh->meshBuffers.vertexBufferAddress;

	VkDescriptorSet sets[] = {
		sceneDataDescriptorSet,
		_shadowMapDescriptorSet,
		_grassDataDescriptorSet
	};
	//TODO maybe no need to rebind scenedata!!!
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _grassPipelineLayout, 0, 3, sets, 0, nullptr);
	vkCmdPushConstants(cmd, _grassPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _grassMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, _grassMesh->surfaces[0].count, _grassCount, _grassMesh->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += _grassMesh->surfaces[0].count / 3 * _grassCount;

	//
	//	TRANSPARENT MESHES
	//
	
	//water
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _waterPipeline);
	{
		VkDescriptorSet sets[] = {
			sceneDataDescriptorSet,
			_shadowMapDescriptorSet,
			_waterDataDescriptorSet
		};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _waterPipelineLayout, 0, 3, sets, 0, nullptr);
		//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 1, 1, &_shadowMapDescriptorSet, 0, nullptr);
	}
	pushConstants.vertexBuffer = _waterMesh->meshBuffers.vertexBufferAddress;
	vkCmdPushConstants(cmd, _waterPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
	vkCmdBindIndexBuffer(cmd, _waterMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd, _waterMesh->surfaces[0].count, 1, _waterMesh->surfaces[0].startIndex, 0, 0);
	UI_triangleCount += _waterMesh->surfaces[0].count / 3 * 1;

	vkCmdEndRendering(cmd);
}

void VulkanEngine::drawShadowMap(VkCommandBuffer cmd)
{
	for (int i = 0; i < CSM_COUNT; i++)
	{
		//IDK if theres a better way to do this (there probably is) but for now
		// everything is just copypasted from drawGeometry
		//PER FRAME DATA
		//	Scene Data
		AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		getCurrentFrame().deletionQueue.pushFunction(
			[=, this]() {
				destroyBuffer(sceneDataBuffer);
			}
		);
		SceneData* sceneUniformData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
		*sceneUniformData = _shadowMapSceneData[i]; //write scene data to buffer

		VkDescriptorSet sceneDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _sceneDataDescriptorLayout, nullptr);
		DescriptorWriter writer;
		writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		writer.updateSet(_device, sceneDataDescriptorSet);

		//begin a render pass connected to draw image
		//TODO if we want to point to no color attachment, make the imageview nullptr!!! and use VK_ATTACHMENT_UNUSED
		VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(_shadowMapImageArray.imageViews[i], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.pNext = nullptr;

		renderingInfo.renderArea = VkRect2D{ VkOffset2D{0,0}, VkExtent2D(_shadowMapImageArray.imageExtent.width,_shadowMapImageArray.imageExtent.height)};
		renderingInfo.layerCount = 1;
		renderingInfo.colorAttachmentCount = 0;
		renderingInfo.pDepthAttachment = &depthAttachment;
		renderingInfo.pStencilAttachment = nullptr;
		vkCmdBeginRendering(cmd, &renderingInfo);

		//set dynamic viewport and scissor
		VkViewport viewport{};
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = _shadowMapImageArray.imageExtent.width;
		viewport.height = _shadowMapImageArray.imageExtent.height;
		viewport.minDepth = 0.f;
		viewport.maxDepth = 1.f;

		vkCmdSetViewport(cmd, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.extent.width = _shadowMapImageArray.imageExtent.width;
		scissor.extent.height = _shadowMapImageArray.imageExtent.height;
		scissor.offset.x = 0;
		scissor.offset.y = 0;

		vkCmdSetScissor(cmd, 0, 1, &scissor);

		//draw mesh
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowMeshPipeline);

		GPUDrawPushConstants pushConstants{};
		pushConstants.worldMatrix = glm::translate(glm::vec3(0));
		pushConstants.playerPosition = glm::vec4(_player._position.x, _player._position.y, _player._position.z, 0);
		//pushConstants.playerPosition = glm::vec4(_sunPosition.x, _sunPosition.y, _sunPosition.z, 0);

		//draw loaded test mesh
		pushConstants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;

		{

			VkDescriptorSet sets[] = {
				sceneDataDescriptorSet,
				_shadowMapDescriptorSet
			};
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowMeshPipelineLayout, 0, 2, sets, 0, nullptr);
		}
		vkCmdPushConstants(cmd, _shadowMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
		vkCmdBindIndexBuffer(cmd, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd, testMeshes[2]->surfaces[0].count, 1, testMeshes[2]->surfaces[0].startIndex, 0, 0);


		//draw ground
		pushConstants.vertexBuffer = _groundMesh->meshBuffers.vertexBufferAddress;
		vkCmdPushConstants(cmd, _shadowMeshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
		vkCmdBindIndexBuffer(cmd, _groundMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, _groundMesh->surfaces[0].count, 1, _groundMesh->surfaces[0].startIndex, 0, 0);

		//draw grass
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowGrassPipeline);

		pushConstants.vertexBuffer = _grassMesh->meshBuffers.vertexBufferAddress;

		VkDescriptorSet sets[] = {
			sceneDataDescriptorSet,
			_shadowMapDescriptorSet,
			_grassDataDescriptorSet
		};

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowGrassPipelineLayout, 0, 3, sets, 0, nullptr);
		vkCmdPushConstants(cmd, _shadowGrassPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
		vkCmdBindIndexBuffer(cmd, _lowQualityGrassMesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexed(cmd, _lowQualityGrassMesh->surfaces[0].count, _grassCount, _lowQualityGrassMesh->surfaces[0].startIndex, 0, 0);


		vkCmdEndRendering(cmd);
	}
}

void VulkanEngine::drawDeferred(VkCommandBuffer cmd)
{
	//todo probably shouldnt repeat this but whatever
	VkDescriptorSet sceneDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _sceneDataDescriptorLayout, nullptr);
	{
		//	Scene Data
		AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		getCurrentFrame().deletionQueue.pushFunction(
			[=, this]() {
				destroyBuffer(sceneDataBuffer);
			}
		);
		SceneData* sceneUniformData = (SceneData*)sceneDataBuffer.allocation->GetMappedData();
		*sceneUniformData = _sceneData; //write scene data to buffer

		DescriptorWriter writer;
		writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		writer.updateSet(_device, sceneDataDescriptorSet);
	}
	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_player._position.x, _player._position.y, _player._position.z, 1);

	//bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _deferredPipeline);

	//bind the descriptor set containing the draw image for the compute pipeline
	VkDescriptorSet descriptors[] = {
		sceneDataDescriptorSet,
		_drawImageDescriptors
	};
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _deferredPipelineLayout, 0, 2, descriptors, 0, nullptr);
	vkCmdPushConstants(cmd, _deferredPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);

	//execute compute pipeline dispatch
	vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
}

void VulkanEngine::updateGrassData(VkCommandBuffer cmd)
{
	if (_settingsChanged)
	{
		AllocatedBuffer deletedBuffer = _grassDataBuffer;
		//deletion
		getCurrentFrame().deletionQueue.pushFunction(
			[=, this]() {
				destroyBuffer(deletedBuffer);
			}
		);
		_settingsChanged = false;
		_grassDensity = UI_grassDensity;
		_maxGrassDistance = UI_maxGrassDistance;
		//TODO only allocate new buffer when grass count changes
		_grassCount = (_maxGrassDistance * 2 * _grassDensity + 1) * (_maxGrassDistance * 2 * _grassDensity + 1);;
		_grassDataBuffer = createBuffer(sizeof(GrassData) * _grassCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	}
	//TODO mayb we should just create a buffer every frame and fill it instead of storing in FrameData hmmm
	//		then we dont have to update the framedata buffer AND this buffer when _grassCount changes.
	_grassDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _grassDataDescriptorLayout, nullptr);
	DescriptorWriter writer;
	writer.writeBuffer(0, _grassDataBuffer.buffer, sizeof(GrassData) * _grassCount, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	writer.writeImage(1, _windMapImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.updateSet(_device, _grassDataDescriptorSet);


	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_player._position.x, _player._position.y, _player._position.z, 1);
	pushConstants.data2 = glm::vec4(_grassCount, _maxGrassDistance, _grassDensity, 0);

	VkDescriptorSet descriptorSets[] = {
		_grassDataDescriptorSet,
		_heightMapDescriptorSet
	};
	//bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _grassComputePipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _grassComputePipelineLayout, 0, 2, descriptorSets, 0, nullptr);
	vkCmdPushConstants(cmd, _grassComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	//execute compute pipeline dispatch
	vkCmdDispatch(cmd, std::ceil((float)(_grassCount) / 64.0),
		1, 1);

	vkutil::bufferBarrier(cmd, _grassDataBuffer.buffer, VK_WHOLE_SIZE, 0,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR);
}

void VulkanEngine::run()
{
	SDL_Event e;
	bool bQuit = false;

	auto lastTickTime = std::chrono::high_resolution_clock::now();
	while (!bQuit)
	{
		auto tickTime = std::chrono::high_resolution_clock::now();
		auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(tickTime-lastTickTime);
		lastTickTime = tickTime;

		float deltaTime = elapsedTime.count() / 1000000.f;

		_engineStats.frameTime = deltaTime * 1000.f;
		_engineStats.fps = 1 / deltaTime;

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

		if (ImGui::Begin("grass"))
		{
			ImGui::SliderInt("density", &UI_grassDensity, 1, 40);
			ImGui::SliderInt("distance", &UI_maxGrassDistance, 1, 300);
			ImGui::Text("grassCount: %d", _grassCount);
			ImGui::Text("tris: %d", UI_triangleCount);

			if (ImGui::Button("Apply Changes"))
				_settingsChanged = true;

			ImGui::Checkbox("Day/Night Cycle", &_isSunMoving);
			ImGui::End();
		}

		if (ImGui::Begin("stats"))
		{
			ImGui::Text("frame time: %f ms", _engineStats.frameTime);
			ImGui::Text("fps: %f", _engineStats.fps);

			ImGui::End();
		}

		if (ImGui::Begin("wind map"))
		{
			ImGui::Image((ImTextureID)_windMapSamplerDescriptorSet, ImVec2(600,600));
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
	_time += deltaTime;
	_player.update(deltaTime);
	_sceneData.view = _player.getViewMatrix();
	_sceneData.viewProj = _sceneData.proj * _sceneData.view;
	if (_isSunMoving)
	{
		_sceneData.sunlightDirection = glm::rotate(_time * 0.3f, glm::vec3(1, 0, 0)) * glm::vec4(1, 1, 0, 1);
		_sceneData.sunlightDirection.w = -_sceneData.sunlightDirection.y;
	}



	_sunPosition = glm::vec3(-50 * _sceneData.sunlightDirection.x + _player._position.x,
		-50 * _sceneData.sunlightDirection.y,
		-50 * _sceneData.sunlightDirection.z + _player._position.z);

	for (int i = 0; i < CSM_COUNT; i++)
	{
		_shadowMapSceneData[i].view = glm::lookAt(_sunPosition,
			glm::vec3(_player._position.x, 2, _player._position.z), glm::vec3(0, 1, 0));
		_shadowMapSceneData[i].viewProj = _shadowMapSceneData[i].proj * _shadowMapSceneData[i].view;
		_sceneData.sunViewProj[i] = _shadowMapSceneData[i].viewProj;
	}
	_sceneData.time = glm::vec4(_time, _time / 2, 0, 0);
}

void VulkanEngine::updateWindMap(VkCommandBuffer cmd)
{
	int numCells = _windMapImage.imageExtent.width * _windMapImage.imageExtent.height;


	ComputePushConstants pushConstants;
	pushConstants.data1 = glm::vec4(_time, 1, 1, 1);

	//bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _windMapComputePipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _windMapComputePipelineLayout, 0, 1, &_windMapDescriptorSet, 0, nullptr);
	vkCmdPushConstants(cmd, _windMapComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pushConstants);
	//execute compute pipeline dispatch
	vkCmdDispatch(cmd, std::ceil((float)(numCells) / 64.0),
		1, 1);

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

	VkImageUsageFlags drawImageUsageFlags{};
	drawImageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;		//can copy from image
	drawImageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;		//can copy to image
	drawImageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
	drawImageUsageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;	//allows us to draw geometry on it through graphics pipeline

	//allocate draw image from gpu memory
	VmaAllocationCreateInfo rimgAllocInfo{};
	rimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
	rimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

	//allocate and create image
	//hardcoding draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = drawImageExtent;
	VkImageCreateInfo drawImageInfo = vkinit::imageCreateInfo(_drawImage.imageFormat, drawImageUsageFlags, drawImageExtent);
	vmaCreateImage(_allocator, &drawImageInfo, &rimgAllocInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

	_normalsImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_normalsImage.imageExtent = drawImageExtent;
	VkImageCreateInfo normalsImageInfo = vkinit::imageCreateInfo(_normalsImage.imageFormat, drawImageUsageFlags, drawImageExtent);
	vmaCreateImage(_allocator, &normalsImageInfo, &rimgAllocInfo, &_normalsImage.image, &_normalsImage.allocation, nullptr);

	_positionsImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_positionsImage.imageExtent = drawImageExtent;
	VkImageCreateInfo positionsImageInfo = vkinit::imageCreateInfo(_positionsImage.imageFormat, drawImageUsageFlags, drawImageExtent);
	vmaCreateImage(_allocator, &positionsImageInfo, &rimgAllocInfo, &_positionsImage.image, &_positionsImage.allocation, nullptr);

	_specularMapImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_specularMapImage.imageExtent = drawImageExtent;
	VkImageCreateInfo specularMapImageInfo = vkinit::imageCreateInfo(_specularMapImage.imageFormat, drawImageUsageFlags, drawImageExtent);
	vmaCreateImage(_allocator, &specularMapImageInfo, &rimgAllocInfo, &_specularMapImage.image, &_specularMapImage.allocation, nullptr);

	_finalDrawImage.imageFormat = _drawImage.imageFormat;
	_finalDrawImage.imageExtent = drawImageExtent;
	VkImageCreateInfo finalDrawImageInfo = vkinit::imageCreateInfo(_finalDrawImage.imageFormat, drawImageUsageFlags, drawImageExtent);
	vmaCreateImage(_allocator, &finalDrawImageInfo, &rimgAllocInfo, &_finalDrawImage.image, &_finalDrawImage.allocation, nullptr);

	//build image view for the draw image to use for rendering
	
	{
		VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_drawImage.imageView));
	}
	{
		VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_normalsImage.imageFormat, _normalsImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_normalsImage.imageView));
	}
	{
		VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_positionsImage.imageFormat, _positionsImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_positionsImage.imageView));
	}
	{
		VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_specularMapImage.imageFormat, _specularMapImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_specularMapImage.imageView));
	}
	{
		VkImageViewCreateInfo rviewInfo = vkinit::imageViewCreateInfo(_finalDrawImage.imageFormat, _finalDrawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
		VK_CHECK(vkCreateImageView(_device, &rviewInfo, nullptr, &_finalDrawImage.imageView));
	}

	//add to deletion queues
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _drawImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation); //note that VMA allocated objects are deleted with VMA
			vkDestroyImageView(_device, _normalsImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _normalsImage.image, _normalsImage.allocation);
			vkDestroyImageView(_device, _positionsImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _positionsImage.image, _positionsImage.allocation);
			vkDestroyImageView(_device, _specularMapImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _specularMapImage.image, _specularMapImage.allocation); 
			vkDestroyImageView(_device, _finalDrawImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _finalDrawImage.image, _finalDrawImage.allocation);
		});

	//DEPTH IMAGE
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawImageExtent;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

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
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
		builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		builder.addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		_sceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_grassDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
	}
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_heightMapDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	//allocate a descriptor set for draw image
	_drawImageDescriptors = _globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

	DescriptorWriter writer;
	writer.writeImage(0, _finalDrawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.writeImage(1, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.writeImage(2, _depthImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
	writer.writeImage(3, _normalsImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.writeImage(4, _specularMapImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.writeImage(5, _positionsImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
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
		vkDestroyDescriptorSetLayout(_device, _heightMapDescriptorLayout, nullptr);
	});
}

void VulkanEngine::initPipelines()
{
	initBackgroundPipelines();
	initDeferredPipelines();
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

void VulkanEngine::initDeferredPipelines()
{
	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;

	VkDescriptorSetLayout layouts[] = {
		_sceneDataDescriptorLayout,
		_drawImageDescriptorLayout,
	};
	computeLayout.pSetLayouts = layouts;
	computeLayout.setLayoutCount = 2;

	VkPushConstantRange pushConstant{};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_deferredPipelineLayout));

	//load shaders

	VkShaderModule deferredReflectionShader;
	if (!vkutil::loadShaderModule("./shaders/deferred.comp.spv", _device, &deferredReflectionShader))
	{
		fmt::print("Error when building deferred reflections compute shader \n");
	}

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.pNext = nullptr;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.pName = "main"; //name of entrypoint function
	stageInfo.module = deferredReflectionShader;

	//create pipelines
	VkComputePipelineCreateInfo computePipelineCreateInfo{};
	computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.layout = _deferredPipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_deferredPipeline));


	//deletion
	vkDestroyShaderModule(_device, deferredReflectionShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _deferredPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _deferredPipeline, nullptr);
		});
}

void VulkanEngine::initSampler()
{
	//Sampler
	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.pNext = nullptr;
	samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
	samplerCreateInfo.minFilter = VK_FILTER_NEAREST;

	vkCreateSampler(_device, &samplerCreateInfo, nullptr, &_defaultSampler);

	_mainDeletionQueue.pushFunction(
		[&] {
			vkDestroySampler(_device, _defaultSampler, nullptr);
		}
	);
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
	VkDescriptorSetLayout layouts[] = {
		_sceneDataDescriptorLayout,
		_shadowMapDescriptorLayout
	};
	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		_drawImage.imageFormat,
		_normalsImage.imageFormat,
		_specularMapImage.imageFormat,
		_positionsImage.imageFormat
	};


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
	pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
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

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
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
		_shadowMapDescriptorLayout,
		_grassDataDescriptorLayout
	};
	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 3;
	pipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_grassPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		_drawImage.imageFormat,
		_normalsImage.imageFormat,
		_specularMapImage.imageFormat,
		_positionsImage.imageFormat
	};
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

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
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
	VkDescriptorSetLayout computeLayout[] = {
		_grassDataDescriptorLayout,
		_heightMapDescriptorLayout
	};

	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pPushConstantRanges = &computeBufferRange;
	computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.setLayoutCount = 2;
	computePipelineLayoutInfo.pSetLayouts = computeLayout;

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
	initHeightMap();
	initGround();
	initGrass();
	initWindMap();
	initSkybox();
	initClouds();
	initWater();
}

void VulkanEngine::initSceneData()
{
	_player._position = glm::vec3(0, 5, 0);

	_sceneData = SceneData{};
	_sceneData.view = _player.getViewMatrix();
	_sceneData.proj = glm::perspective(
			glm::radians(70.f),
			(float)_windowExtent.width / (float)_windowExtent.height,
			//10000.f,0.1f); //reverse depth for better precision near 0? (TODO: not working?)
			0.1f, 300.f);
	_sceneData.proj[1][1] *= -1;

	_sceneData.ambientColor = glm::vec4(0.1f, 0.1f, 0.1f, 0.1f);
	_sceneData.sunlightDirection = glm::vec4(2, -2, 0, 1);
	_sceneData.sunlightColor = glm::vec4(1, 1, 1, 1);
	_sceneData.viewProj = _sceneData.proj*_sceneData.view;

	for (int i = 0; i < CSM_COUNT; i++)
	{
		_sceneData.sunViewProj[i] = _sceneData.viewProj;
	}


	_sunPosition = glm::vec3(-30 * _sceneData.sunlightDirection.x + std::floor(_player._position.x),
		-30 * _sceneData.sunlightDirection.y,
		-30 * _sceneData.sunlightDirection.z + std::floor(_player._position.z));

	_sceneData.time = glm::vec4(_time, _time / 2, 0, 0);
}

void VulkanEngine::initGround()
{
	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	uint32_t numVerticesPerSide = (RENDER_DISTANCE * 2 + 2);
	const size_t vertexBufferSize = numVerticesPerSide * numVerticesPerSide;
	const size_t indexBufferSize = (numVerticesPerSide - 1) * (numVerticesPerSide - 1) * 6;
	meshBuffers.vertexBuffer = createBuffer(vertexBufferSize * sizeof(Vertex),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	meshBuffers.indexBuffer = createBuffer(indexBufferSize * sizeof(uint32_t),
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
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &vertexBufferAddressInfo);

	VkBufferDeviceAddressInfo indexBufferAddressInfo{};
	indexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	indexBufferAddressInfo.pNext = nullptr;
	indexBufferAddressInfo.buffer = meshBuffers.indexBuffer.buffer;
	VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddress(_device, &indexBufferAddressInfo);
	
	//VERTEX
	{
		//	PIPELINE
		VkShaderModule computeVertexShader;
		if (!vkutil::loadShaderModule("./shaders/ground_mesh_vertices.comp.spv", _device, &computeVertexShader))
		{
			fmt::print("error when building ground mesh vertex compute shader module\n");
		}
		else
		{
			fmt::print("ground mesh vertex compute shader loaded\n");
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
		computePipelineLayoutInfo.pSetLayouts = &_heightMapDescriptorLayout;
		computePipelineLayoutInfo.setLayoutCount = 1;
		
		VkPipelineLayout computeVertexPipelineLayout;
		VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &computeVertexPipelineLayout));

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
		VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeVertexPipeline));

		ComputeVertexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide,2,0,0);
		pushConstants.vertexBuffer = meshBuffers.vertexBufferAddress;

		////clean structures
		vkDestroyShaderModule(_device, computeVertexShader, nullptr);

		//_mainDeletionQueue.pushFunction([&]() {
		//	});

		immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeVertexPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeVertexPipelineLayout, 0, 1, &_heightMapDescriptorSet,
					0, nullptr);
				vkCmdPushConstants(cmd, computeVertexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeVertexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil(numVerticesPerSide / 16.0f), std::ceil(numVerticesPerSide / 16.0f), 1);
			}
		);
		vkDestroyPipelineLayout(_device, computeVertexPipelineLayout, nullptr);
		vkDestroyPipeline(_device, computeVertexPipeline, nullptr);
	}


	//INDICES
	{
		//	PIPELINE
		VkShaderModule computeIndexShader;
		if (!vkutil::loadShaderModule("./shaders/ground_mesh_indices.comp.spv", _device, &computeIndexShader))
		{
			fmt::print("error when building ground mesh indices compute shader module\n");
		}
		else
		{
			fmt::print("ground mesh index compute shader loaded\n");
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
		VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &computeIndexPipelineLayout));

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
		VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeIndexPipeline));

		ComputeIndexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide-1, 0, 0, 0);
		pushConstants.indexBuffer = indexBufferAddress;

		////clean structures
		vkDestroyShaderModule(_device, computeIndexShader, nullptr);

		//_mainDeletionQueue.pushFunction([&]() {
		//	});

		immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeIndexPipeline);
				vkCmdPushConstants(cmd, computeIndexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeIndexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil((float)indexBufferSize/(6*64)), 1, 1);
			}
		);
		vkDestroyPipelineLayout(_device, computeIndexPipelineLayout, nullptr);
		vkDestroyPipeline(_device, computeIndexPipeline, nullptr);
	}
	//TODO OPTIMIZATION make the vertices and indices in same buffer with offset to improve performance
	meshAsset.name = "ground";
	meshAsset.surfaces = surfaces;
	//meshAsset.meshBuffers = uploadMesh(indices,vertices);
	meshAsset.meshBuffers = meshBuffers;

	_groundMesh = std::make_shared<MeshAsset>(std::move(meshAsset));



	//vkDestroyShaderModule(_device, vertexComputeShader, nullptr);
	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_groundMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_groundMesh->meshBuffers.indexBuffer);

		}
	);
	//_mainDeletionQueue.pushFunction(
	//	[&]() {
	//		destroyBuffer(meshBuffers.vertexBuffer);
	//		destroyBuffer(meshBuffers.indexBuffer);
	//	}
	//);
}

void VulkanEngine::initGrass()
{
	{
		MeshAsset meshAsset{};

		glm::vec4 bottomColor{ 0.14f,0.32f,0.08f,1.0f };
		glm::vec4 topColor{ 0.38f,0.56f,0.25f,1.0f };
		glm::vec3 normal(0, 0, -1);
		float grassWidth = 0.03f;

		std::array<Vertex, 9> vertices{};
		vertices[0] = { glm::vec3(grassWidth, 0,0), 1, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		vertices[1] = { glm::vec3(-grassWidth,0,0), 0, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		vertices[2] = { glm::vec3(grassWidth, 0.3f, 0), 1, normal, 1, glm::mix(bottomColor,topColor,0.3f) };
		vertices[3] = { glm::vec3(-grassWidth,0.3f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,0.3f) };
		vertices[4] = { glm::vec3(grassWidth, 0.6f, 0), 1, normal, 1, glm::mix(bottomColor,topColor,0.6f) };
		vertices[5] = { glm::vec3(-grassWidth,0.6f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,0.6f) };
		vertices[6] = { glm::vec3(grassWidth, 0.9f, 0), 1, normal, 1, glm::mix(bottomColor,topColor,0.9f) };
		vertices[7] = { glm::vec3(-grassWidth,0.9f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,0.9f) };
		vertices[8] = { glm::vec3(0, 1.1f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,1.f) };

		std::vector<uint32_t> indices{
			0,3,1,
			0,2,3,
			2,5,3,
			2,4,5,
			4,7,5,
			4,6,7,
			6,8,7
		};

		//std::array<Vertex, 3> vertices{};
		//vertices[0] = { glm::vec3(grassWidth, 0,0), 1, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		//vertices[1] = { glm::vec3(-grassWidth,0,0), 0, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		//vertices[2] = { glm::vec3(0, 1.1f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,1.f) };
		//std::vector<uint32_t> indices{
		//	0,1,2
		//};

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
	}
	{
		MeshAsset meshAsset{};

		glm::vec4 bottomColor{ 0.14f,0.32f,0.08f,1.0f };
		glm::vec4 topColor{ 0.38f,0.56f,0.25f,1.0f };
		glm::vec3 normal(0, 0, -1);
		float grassWidth = 0.03f;

		std::array<Vertex, 3> vertices{};
		vertices[0] = { glm::vec3(grassWidth, 0,0), 1, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		vertices[1] = { glm::vec3(-grassWidth,0,0), 0, normal, 1, glm::mix(bottomColor,topColor,0.f) };
		vertices[2] = { glm::vec3(0, 1.1f, 0), 0, normal, 1, glm::mix(bottomColor,topColor,1.f) };
		std::vector<uint32_t> indices{
			0,1,2
		};

		std::vector<GeoSurface> surfaces;
		surfaces.resize(1);
		surfaces[0].count = static_cast<uint32_t>(indices.size());
		surfaces[0].startIndex = static_cast<uint32_t>(0);


		meshAsset.name = "low quality grass";
		meshAsset.surfaces = surfaces;
		meshAsset.meshBuffers = uploadMesh(indices, vertices);


		_lowQualityGrassMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

		_mainDeletionQueue.pushFunction(
			[&]() {
				destroyBuffer(_lowQualityGrassMesh->meshBuffers.vertexBuffer);
				destroyBuffer(_lowQualityGrassMesh->meshBuffers.indexBuffer);
			}
		);
	}

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

void VulkanEngine::initHeightMap()
{
	//create image
	VkExtent3D imageExtent = {
		HEIGHT_MAP_SIZE,
		HEIGHT_MAP_SIZE,
		1
	};

	//hardcoding draw format to 32 bit float
	_heightMapImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_heightMapImage.imageExtent = imageExtent;

	VkImageUsageFlags imageUsageFlags{};
	imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
	if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_heightMapImage.imageFormat, imageUsageFlags, imageExtent);

	//allocate draw image from gpu memory
	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
	imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

	//allocate and create image
	vmaCreateImage(_allocator, &imgInfo, &imgAllocInfo, &_heightMapImage.image, &_heightMapImage.allocation, nullptr);

	//build image view for the draw image to use for rendering
	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_heightMapImage.imageFormat, _heightMapImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &_heightMapImage.imageView));

	//add to deletion queues
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _heightMapImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _heightMapImage.image, _heightMapImage.allocation); //note that VMA allocated objects are deleted with VMA
		});

	// DESCRIPTORS
	//	descriptor layout
	{
		//	writing to descriptor set
		_heightMapDescriptorSet = _globalDescriptorAllocator.allocate(_device, _heightMapDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _heightMapImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_device, _heightMapDescriptorSet);
	}


	//	PIPELINE
	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("./shaders/heightmap.comp.spv", _device, &computeShader))
	{
		fmt::print("error when building heightmap compute shader module\n");
	}
	else
	{
		fmt::print("heightmap compute shader loaded\n");
	}

	//push constant range
	//VkPushConstantRange computeBufferRange{};
	//computeBufferRange.offset = 0;
	//computeBufferRange.size = sizeof(ComputePushConstants);
	//computeBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	//sets

	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	//computePipelineLayoutInfo.pPushConstantRanges = &computeBufferRange;
	//computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_heightMapDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &_heightMapComputePipelineLayout));

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
	computePipelineCreateInfo.layout = _heightMapComputePipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_heightMapComputePipeline));

	//clean structures
	vkDestroyShaderModule(_device, computeShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _heightMapComputePipelineLayout, nullptr);
		vkDestroyPipeline(_device, _heightMapComputePipeline, nullptr);
		});

	immediateSubmit(
		[&](VkCommandBuffer cmd) {
			vkutil::transitionImage(cmd, _heightMapImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _heightMapComputePipeline);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _heightMapComputePipelineLayout, 0, 1, &_heightMapDescriptorSet, 0, nullptr);
			vkCmdDispatch(cmd, std::ceil(HEIGHT_MAP_SIZE / 16.0f), std::ceil(HEIGHT_MAP_SIZE / 16.0f), 1);
		}
	);

	if(false) 
	{
		//create debug image view 
		VkImageViewCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = nullptr;

		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.image = _heightMapImage.image;
		info.format = _heightMapImage.imageFormat;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.components = {
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A
		};
		vkCreateImageView(_device, &info, nullptr, &_heightMapDebugImageView);
		_mainDeletionQueue.pushFunction(
			[=]() {
				vkDestroyImageView(_device, _heightMapDebugImageView, nullptr);
			});
		//create debug descriptor set
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		VkDescriptorSetLayout layout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);

		_heightMapSamplerDescriptorSet = _globalDescriptorAllocator.allocate(_device, layout, nullptr);
		DescriptorWriter writer;
		writer.writeImage(0, _heightMapDebugImageView, _defaultSampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(_device, _heightMapSamplerDescriptorSet);

		vkDestroyDescriptorSetLayout(_device, layout, nullptr);
	}
}

void VulkanEngine::initShadowMapResources()
{
	//IMAGE
	VkExtent3D imageExtent = {
		SHADOWMAP_RESOLUTION,
		SHADOWMAP_RESOLUTION,
		1
	};
	//hardcoding depth to be same as regular depth test image
	_shadowMapImageArray.imageFormat = _depthImage.imageFormat;
	_shadowMapImageArray.imageExtent = imageExtent;

	VkImageUsageFlags imageUsageFlags{};
	imageUsageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;

	imgInfo.imageType = VK_IMAGE_TYPE_2D;

	imgInfo.format = _shadowMapImageArray.imageFormat;
	imgInfo.extent = imageExtent;

	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = CSM_COUNT;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	//TILING lets the gpu shuffle the data as it sees fit
	//LINEAR is needed for images read from cpu, which turns the image into a 2d array
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = imageUsageFlags;


	//allocate draw image from gpu memory
	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
	imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

	//allocate and create image
	VkResult result = vmaCreateImage(_allocator, &imgInfo, &imgAllocInfo, &_shadowMapImageArray.image, &_shadowMapImageArray.allocation, nullptr);
	_mainDeletionQueue.pushFunction(
		[=]() {
			vmaDestroyImage(_allocator, _shadowMapImageArray.image, _shadowMapImageArray.allocation); //note that VMA allocated objects are deleted with VMA
		});

	//build image view for the image to use for all cascades
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.pNext = nullptr;

	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	viewInfo.image = _shadowMapImageArray.image;
	viewInfo.format = _shadowMapImageArray.imageFormat;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = CSM_COUNT;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &_shadowMapImageArray.fullImageView));
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _shadowMapImageArray.fullImageView, nullptr);
		});

	float scale = 1;
	_shadowMapImageArray.imageViews.resize(CSM_COUNT);
	for (int i = 0; i < CSM_COUNT; i++)
	{
		glm::mat4 projection =
			glm::ortho(-(10.0) * scale, 10.0 * scale,
				(10.0) * scale, -10.0 * scale,
				-1050.0, 1050.0);
		//200.0, 0.0);
		_shadowMapSceneData[i].proj = projection;

		scale *= CSM_SCALE;

		//build image views for the drawing to individual cascades
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.pNext = nullptr;

		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.image = _shadowMapImageArray.image;
		viewInfo.format = _shadowMapImageArray.imageFormat;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = i;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &_shadowMapImageArray.imageViews[i]));

		//add to deletion queues
		_mainDeletionQueue.pushFunction(
			[=]() {
				vkDestroyImageView(_device, _shadowMapImageArray.imageViews[i], nullptr);
			});
	}


	//DESCRIPTOR SET LAYOUTS
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		_shadowMapDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
	}
	//	deletion
	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyDescriptorSetLayout(_device, _shadowMapDescriptorLayout, nullptr);
		});
	//DESCRIPTOR SETS
	{
		//	writing to descriptor set
		_shadowMapDescriptorSet = _globalDescriptorAllocator.allocate(_device, _shadowMapDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _shadowMapImageArray.fullImageView, _defaultSampler, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(_device, _shadowMapDescriptorSet);
	}
	

	//PIPELINES
	{

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

		//sets TODO might have to make a new layout because no shadow map descriptor
		{
			VkDescriptorSetLayout layouts[] = {
				_sceneDataDescriptorLayout,
				_shadowMapDescriptorLayout,
				_grassDataDescriptorLayout
			};
			//build pipeline layout that controls the input/outputs of shader
			//	note: no descriptor sets or other yet.
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
			pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.setLayoutCount = 3;
			pipelineLayoutInfo.pSetLayouts = layouts;

			VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_shadowGrassPipelineLayout));
		}

		//CREATE PIPELINE
		vkutil::PipelineBuilder pipelineBuilder;

		//	pipeline layout
		pipelineBuilder._pipelineLayout = _shadowGrassPipelineLayout;
		//	connect vertex and fragment shaders to pipeline
		pipelineBuilder.setVertexShader(meshVertShader);
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
		};
		pipelineBuilder.setBlendingModes(modes);
		// depth testing
		pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

		//connect image format we will draw to, from draw image
		pipelineBuilder.setDepthFormat(_shadowMapImageArray.imageFormat);

		//build pipeline
		_shadowGrassPipeline = pipelineBuilder.buildPipeline(_device);

		//clean structures
		vkDestroyShaderModule(_device, meshVertShader, nullptr);

		_mainDeletionQueue.pushFunction([&]() {
			vkDestroyPipelineLayout(_device, _shadowGrassPipelineLayout, nullptr);
			vkDestroyPipeline(_device, _shadowGrassPipeline, nullptr);
			});
	}

	{

		VkShaderModule meshVertShader;
		if (!vkutil::loadShaderModule("./shaders/mesh.vert.spv", _device, &meshVertShader))
		{
			fmt::print("error when building mesh vertex shader module");
		}
		else
		{
			fmt::print("mesh vertex shader loaded");
		}

		//push constant range
		VkPushConstantRange bufferRange{};
		bufferRange.offset = 0;
		bufferRange.size = sizeof(GPUDrawPushConstants);
		bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		//sets

		//build pipeline layout that controls the input/outputs of shader
		//	note: no descriptor sets or other yet.
		{
			VkDescriptorSetLayout layouts[] = {
				_sceneDataDescriptorLayout,
				_shadowMapDescriptorLayout
			};
			VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
			pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
			pipelineLayoutInfo.pushConstantRangeCount = 1;
			pipelineLayoutInfo.setLayoutCount = 2;
			pipelineLayoutInfo.pSetLayouts = layouts;

			VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_shadowMeshPipelineLayout));
		}

		//CREATE PIPELINE
		vkutil::PipelineBuilder pipelineBuilder;

		//	pipeline layout
		pipelineBuilder._pipelineLayout = _shadowMeshPipelineLayout;
		//	connect vertex and fragment shaders to pipeline
		pipelineBuilder.setVertexShader(meshVertShader);
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
		};
		pipelineBuilder.setBlendingModes(modes);
		// depth testing
		pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

		//connect image format we will draw to, from draw image
		pipelineBuilder.setDepthFormat(_shadowMapImageArray.imageFormat);

		//build pipeline
		_shadowMeshPipeline = pipelineBuilder.buildPipeline(_device);

		//clean structures
		vkDestroyShaderModule(_device, meshVertShader, nullptr);

		_mainDeletionQueue.pushFunction([&]() {
			vkDestroyPipelineLayout(_device, _shadowMeshPipelineLayout, nullptr);
			vkDestroyPipeline(_device, _shadowMeshPipeline, nullptr);
			});
	}

	{
		//create debug image view 
		VkImageViewCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = nullptr;

		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.image = _shadowMapImageArray.image;
		info.format = _shadowMapImageArray.imageFormat;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		info.components = {
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A,
			VK_COMPONENT_SWIZZLE_A
		};


		vkCreateImageView(_device, &info, nullptr, &_heightMapDebugImageView);
		_mainDeletionQueue.pushFunction(
			[=]() {
				vkDestroyImageView(_device, _heightMapDebugImageView, nullptr);
			});
		//create debug descriptor set
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		VkDescriptorSetLayout layout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);

		_heightMapSamplerDescriptorSet = _globalDescriptorAllocator.allocate(_device, layout, nullptr);
		DescriptorWriter writer;
		writer.writeImage(0, _heightMapDebugImageView, _defaultSampler, VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(_device, _heightMapSamplerDescriptorSet);

		vkDestroyDescriptorSetLayout(_device, layout, nullptr);
	}
}

void VulkanEngine::initWindMap()
{
	//IMAGE
	VkExtent3D imageExtent{
		RENDER_DISTANCE*2,
		RENDER_DISTANCE*2,
		1
	};

	//hardcoding draw format to 16 bit float
	_windMapImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;	
	_windMapImage.imageExtent = imageExtent;

	VkImageUsageFlags imageUsageFlags{};
	imageUsageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;			//compute shader can write to image
	if (bUseValidationLayers) imageUsageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(_windMapImage.imageFormat, imageUsageFlags, imageExtent);

	//allocate draw image from gpu memory
	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY; //never accessed from cpu
	imgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); //only gpu-side VRAM, fastest access

	//allocate and create image
	vmaCreateImage(_allocator, &imgInfo, &imgAllocInfo, &_windMapImage.image, &_windMapImage.allocation, nullptr);

	//build image view for the draw image to use for rendering
	VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(_windMapImage.imageFormat, _windMapImage.image, VK_IMAGE_ASPECT_COLOR_BIT);

	VK_CHECK(vkCreateImageView(_device, &viewInfo, nullptr, &_windMapImage.imageView));

	//add to deletion queues
	_mainDeletionQueue.pushFunction(
		[=]() {
			vkDestroyImageView(_device, _windMapImage.imageView, nullptr);
			vmaDestroyImage(_allocator, _windMapImage.image, _windMapImage.allocation); //note that VMA allocated objects are deleted with VMA
		});
	//DESCRIPTORS

	//DESCRIPTOR SET LAYOUTS
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		_windMapDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT);
	}
	//	deletion
	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyDescriptorSetLayout(_device, _windMapDescriptorLayout, nullptr);
		});
	//DESCRIPTOR SETS
	{
		//	writing to descriptor set
		_windMapDescriptorSet = _globalDescriptorAllocator.allocate(_device, _windMapDescriptorLayout);
		DescriptorWriter writer;
		writer.writeImage(0, _windMapImage.imageView, nullptr , VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_device, _windMapDescriptorSet);
	}
	//{
	//	//	writing to GRASSDATA descriptor set
	//	DescriptorWriter writer;
	//	writer.writeImage(1, _windMapImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	//	writer.updateSet(_device, _grassDataDescriptorSet);
	//}
	//PIPELINE

	VkShaderModule computeShader;
	if (!vkutil::loadShaderModule("./shaders/windmap.comp.spv", _device, &computeShader))
	{
		fmt::print("error when building windmap compute shader module\n");
	}
	else
	{
		fmt::print("windmap compute shader loaded\n");
	}

	//push constant range
	VkPushConstantRange computeBufferRange{};
	computeBufferRange.offset = 0;
	computeBufferRange.size = sizeof(ComputePushConstants);
	computeBufferRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	//sets

	//build pipeline layout that controls the input/outputs of shader
	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	computePipelineLayoutInfo.pPushConstantRanges = &computeBufferRange;
	computePipelineLayoutInfo.pushConstantRangeCount = 1;
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &_windMapDescriptorLayout;

	VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &_windMapComputePipelineLayout));

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
	computePipelineCreateInfo.layout = _windMapComputePipelineLayout;
	computePipelineCreateInfo.stage = stageInfo;

	VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_windMapComputePipeline));

	//clean structures
	vkDestroyShaderModule(_device, computeShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _windMapComputePipelineLayout, nullptr);
		vkDestroyPipeline(_device, _windMapComputePipeline, nullptr);
		});

	{
		//create debug image view 
		VkImageViewCreateInfo info{};

		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.pNext = nullptr;

		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.image = _windMapImage.image;
		info.format = _windMapImage.imageFormat;
		info.subresourceRange.baseMipLevel = 0;
		info.subresourceRange.levelCount = 1;
		info.subresourceRange.baseArrayLayer = 0;
		info.subresourceRange.layerCount = 1;
		info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		info.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_ONE
		};
		vkCreateImageView(_device, &info, nullptr, &_windMapDebugImageView);
		_mainDeletionQueue.pushFunction(
			[=]() {
				vkDestroyImageView(_device, _windMapDebugImageView, nullptr);
			});
		//create debug descriptor set
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		VkDescriptorSetLayout layout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);

		_windMapSamplerDescriptorSet = _globalDescriptorAllocator.allocate(_device, layout, nullptr);
		DescriptorWriter writer;
		writer.writeImage(0, _windMapImage.imageView, _defaultSampler, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		writer.updateSet(_device, _windMapSamplerDescriptorSet);

		vkDestroyDescriptorSetLayout(_device, layout, nullptr);
	}
}

void VulkanEngine::initSkybox()
{
	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	const std::vector<glm::vec4> vertices {
		glm::vec4(-1.f, -1.f, -1.f,1.0f),
		glm::vec4(1.f, -1.f, -1.f,1.0f),
		glm::vec4(-1.f, 1.f, -1.f,1.0f),
		glm::vec4(1.f, 1.f, -1.f,1.0f),
		glm::vec4(-1.f, -1.f, 1.f,1.0f),
		glm::vec4(1.f, -1.f, 1.f,1.0f),
		glm::vec4(-1.f, 1.f, 1.f,1.0f),
		glm::vec4(1.f, 1.f, 1.f,1.0f)
	};
	const std::vector<uint32_t> indices({
		0,1,2,
		1,3,2,
		0,5,1,
		0,4,5,
		0,2,4,
		2,6,4,
		5,7,3,
		5,3,1,
		4,6,7,
		4,7,5,
		2,3,6,
		3,7,6
	});

	const size_t vertexBufferSize = vertices.size() * sizeof(glm::vec4);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	meshBuffers.vertexBuffer = createBuffer(
		vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//find address of vertex buffer
	VkBufferDeviceAddressInfo deviceAddressInfo{};
	deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAddressInfo.pNext = nullptr;
	deviceAddressInfo.buffer = meshBuffers.vertexBuffer.buffer;
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

	//create index buffer
	meshBuffers.indexBuffer = createBuffer(
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
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.indexBuffer.buffer, 1, &indexCopy);
		});

	destroyBuffer(staging);


	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].startIndex = static_cast<uint32_t>(0);
	surfaces[0].count = static_cast<uint32_t>(indices.size());

	meshAsset.name = "skybox";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = meshBuffers;

	_skyboxMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

	//vkDestroyShaderModule(_device, vertexComputeShader, nullptr);
	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_skyboxMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_skyboxMesh->meshBuffers.indexBuffer);

		}
	);


	VkShaderModule fragShader;
	if (!vkutil::loadShaderModule("./shaders/skybox.frag.spv", _device, &fragShader))
	{
		fmt::print("error when building skybox fragmentshader module");
	}
	else
	{
		fmt::print("skybox fragment shader loaded");
	}
	VkShaderModule vertShader;
	if (!vkutil::loadShaderModule("./shaders/skybox.vert.spv", _device, &vertShader))
	{
		fmt::print("error when building skybox vertex shader module");
	}
	else
	{
		fmt::print("skybox vertex shader loaded");
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

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_skyboxPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		_drawImage.imageFormat,
		_normalsImage.imageFormat,
		_specularMapImage.imageFormat,
		_positionsImage.imageFormat
	};
	//CREATE PIPELINE
	vkutil::PipelineBuilder pipelineBuilder;

	//	pipeline layout
	pipelineBuilder._pipelineLayout = _skyboxPipelineLayout;
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
	//pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);
	pipelineBuilder.disableDepthTest();

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
	pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

	//build pipeline
	_skyboxPipeline = pipelineBuilder.buildPipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, fragShader, nullptr);
	vkDestroyShaderModule(_device, vertShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
		});

}

void VulkanEngine::initClouds()
{

	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	const std::vector<glm::vec4> vertices{
		glm::vec4(-RENDER_DISTANCE, 50.9f, -RENDER_DISTANCE ,1.0f),
		glm::vec4(RENDER_DISTANCE , 50.9f, -RENDER_DISTANCE ,1.0f),
		glm::vec4(-RENDER_DISTANCE , 50.9f, RENDER_DISTANCE ,1.0f),
		glm::vec4(RENDER_DISTANCE , 50.9f, RENDER_DISTANCE ,1.0f)
	};
	const std::vector<uint32_t> indices({
		0,1,2,
		1,3,2
		});

	const size_t vertexBufferSize = vertices.size() * sizeof(glm::vec4);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	meshBuffers.vertexBuffer = createBuffer(
		vertexBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);

	//find address of vertex buffer
	VkBufferDeviceAddressInfo deviceAddressInfo{};
	deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAddressInfo.pNext = nullptr;
	deviceAddressInfo.buffer = meshBuffers.vertexBuffer.buffer;
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

	//create index buffer
	meshBuffers.indexBuffer = createBuffer(
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
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;
		vkCmdCopyBuffer(cmd, staging.buffer, meshBuffers.indexBuffer.buffer, 1, &indexCopy);
		});

	destroyBuffer(staging);


	std::vector<GeoSurface> surfaces;
	surfaces.resize(1);
	surfaces[0].startIndex = static_cast<uint32_t>(0);
	surfaces[0].count = static_cast<uint32_t>(indices.size());

	meshAsset.name = "clouds";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = meshBuffers;

	_cloudMesh = std::make_shared<MeshAsset>(std::move(meshAsset));

	//vkDestroyShaderModule(_device, vertexComputeShader, nullptr);
	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_cloudMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_cloudMesh->meshBuffers.indexBuffer);

		}
	);


	VkShaderModule fragShader;
	if (!vkutil::loadShaderModule("./shaders/cloud.frag.spv", _device, &fragShader))
	{
		fmt::print("error when building cloud fragmentshader module");
	}
	else
	{
		fmt::print("cloud fragment shader loaded");
	}
	VkShaderModule vertShader;
	if (!vkutil::loadShaderModule("./shaders/cloud.vert.spv", _device, &vertShader))
	{
		fmt::print("error when building cloud vertex shader module");
	}
	else
	{
		fmt::print("cloud vertex shader loaded");
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

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_cloudPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		_drawImage.imageFormat,
		_normalsImage.imageFormat,
		_specularMapImage.imageFormat,
		_positionsImage.imageFormat
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
	pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

	//build pipeline
	_cloudPipeline = pipelineBuilder.buildPipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, fragShader, nullptr);
	vkDestroyShaderModule(_device, vertShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _cloudPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _cloudPipeline, nullptr);
		});

}

void VulkanEngine::initWater()
{
	static const int WATER_DISTANCE = 60;
	static const int WATER_QUALITY = 4;
	GPUMeshBuffers meshBuffers{};
	MeshAsset meshAsset{};

	uint32_t numVerticesPerSide = (WATER_DISTANCE * WATER_QUALITY + 2);
	const size_t vertexBufferSize = numVerticesPerSide * numVerticesPerSide;
	const size_t indexBufferSize = (numVerticesPerSide - 1) * (numVerticesPerSide - 1) * 6;
	meshBuffers.vertexBuffer = createBuffer(vertexBufferSize * sizeof(Vertex),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY);
	meshBuffers.indexBuffer = createBuffer(indexBufferSize * sizeof(uint32_t),
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
	meshBuffers.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &vertexBufferAddressInfo);

	VkBufferDeviceAddressInfo indexBufferAddressInfo{};
	indexBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	indexBufferAddressInfo.pNext = nullptr;
	indexBufferAddressInfo.buffer = meshBuffers.indexBuffer.buffer;
	VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddress(_device, &indexBufferAddressInfo);

	//VERTEX
	{
		//	PIPELINE
		VkShaderModule computeVertexShader;
		if (!vkutil::loadShaderModule("./shaders/water_mesh_vertices.comp.spv", _device, &computeVertexShader))
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
		computePipelineLayoutInfo.pSetLayouts = &_heightMapDescriptorLayout;
		computePipelineLayoutInfo.setLayoutCount = 1;

		VkPipelineLayout computeVertexPipelineLayout;
		VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &computeVertexPipelineLayout));

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
		VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeVertexPipeline));

		ComputeVertexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide, WATER_QUALITY, 0, 0);
		pushConstants.vertexBuffer = meshBuffers.vertexBufferAddress;

		////clean structures
		vkDestroyShaderModule(_device, computeVertexShader, nullptr);

		immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeVertexPipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeVertexPipelineLayout, 0, 1, &_heightMapDescriptorSet,
					0, nullptr);
				vkCmdPushConstants(cmd, computeVertexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeVertexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil(numVerticesPerSide / 16.0f), std::ceil(numVerticesPerSide / 16.0f), 1);
			}
		);
		vkDestroyPipelineLayout(_device, computeVertexPipelineLayout, nullptr);
		vkDestroyPipeline(_device, computeVertexPipeline, nullptr);
	}


	//INDICES
	{
		//	PIPELINE
		VkShaderModule computeIndexShader;
		if (!vkutil::loadShaderModule("./shaders/water_mesh_indices.comp.spv", _device, &computeIndexShader))
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
		VK_CHECK(vkCreatePipelineLayout(_device, &computePipelineLayoutInfo, nullptr, &computeIndexPipelineLayout));

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
		VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &computeIndexPipeline));

		ComputeIndexPushConstants pushConstants{};
		pushConstants.data = glm::vec4(numVerticesPerSide - 1, 0, 0, 0);
		pushConstants.indexBuffer = indexBufferAddress;

		vkDestroyShaderModule(_device, computeIndexShader, nullptr);

		immediateSubmit(
			[&](VkCommandBuffer cmd) {
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computeIndexPipeline);
				vkCmdPushConstants(cmd, computeIndexPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
					sizeof(ComputeIndexPushConstants), &pushConstants);
				vkCmdDispatch(cmd, std::ceil((float)indexBufferSize / (6 * 64)), 1, 1);
			}
		);
		vkDestroyPipelineLayout(_device, computeIndexPipelineLayout, nullptr);
		vkDestroyPipeline(_device, computeIndexPipeline, nullptr);
	}
	//TODO OPTIMIZATION make the vertices and indices in same buffer with offset to improve performance
	meshAsset.name = "water";
	meshAsset.surfaces = surfaces;
	meshAsset.meshBuffers = meshBuffers;

	_waterMesh = std::make_shared<MeshAsset>(std::move(meshAsset));



	_mainDeletionQueue.pushFunction(
		[&]() {
			destroyBuffer(_waterMesh->meshBuffers.vertexBuffer);
			destroyBuffer(_waterMesh->meshBuffers.indexBuffer);

		}
	);
	//
	// DESCRIPTORS
	// 
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); // wind map
		_waterDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT);
	}

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyDescriptorSetLayout(_device, _waterDataDescriptorLayout, nullptr);
		});

	{
		_waterDataDescriptorSet = getCurrentFrame().descriptorAllocator.allocate(_device, _waterDataDescriptorLayout, nullptr);
		DescriptorWriter writer;
		writer.writeImage(0, _windMapImage.imageView, nullptr, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		writer.updateSet(_device, _waterDataDescriptorSet);
	}

	// 
	// PIPELINE
	//


	VkShaderModule waterFragShader;
	if (!vkutil::loadShaderModule("./shaders/water.frag.spv", _device, &waterFragShader))
	{
		fmt::print("error when building water fragmentshader module");
	}
	else
	{
		fmt::print("water fragment shader loaded");
	}
	VkShaderModule waterVertShader;
	if (!vkutil::loadShaderModule("./shaders/water.vert.spv", _device, &waterVertShader))
	{
		fmt::print("error when building water vertex shader module");
	}
	else
	{
		fmt::print("water vertex shader loaded");
	}

	//push constant range
	VkPushConstantRange bufferRange{};
	bufferRange.offset = 0;
	bufferRange.size = sizeof(GPUDrawPushConstants);
	bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	//sets
	VkDescriptorSetLayout layouts[] = {
		_sceneDataDescriptorLayout,
		_shadowMapDescriptorLayout,
		_waterDataDescriptorLayout
	};
	//build pipeline layout that controls the input/outputs of shader
	//	note: no descriptor sets or other yet.
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipelineLayoutCreateInfo();
	pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.setLayoutCount = 3;
	pipelineLayoutInfo.pSetLayouts = layouts;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_waterPipelineLayout));

	//color attachment formats
	std::vector<VkFormat> colorAttachmentFormats = {
		_drawImage.imageFormat,
		_normalsImage.imageFormat,
		_specularMapImage.imageFormat,
		_positionsImage.imageFormat
	};
	//CREATE PIPELINE
	vkutil::PipelineBuilder pipelineBuilder;

	//	pipeline layout
	pipelineBuilder._pipelineLayout = _waterPipelineLayout;
	//	connect vertex and fragment shaders to pipeline
	pipelineBuilder.setShaders(waterVertShader, waterFragShader);
	//	input topology
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	//	polygon mode
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	//	cull mode
	pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
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

	//connect image format we will draw to, from draw image
	pipelineBuilder.setColorAttachmentFormats(colorAttachmentFormats);
	pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

	//build pipeline
	_waterPipeline = pipelineBuilder.buildPipeline(_device);

	//clean structures
	vkDestroyShaderModule(_device, waterFragShader, nullptr);
	vkDestroyShaderModule(_device, waterVertShader, nullptr);

	_mainDeletionQueue.pushFunction([&]() {
		vkDestroyPipelineLayout(_device, _waterPipelineLayout, nullptr);
		vkDestroyPipeline(_device, _waterPipeline, nullptr);
		});
}
