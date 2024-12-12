#include "Application.hpp"
#include "src/vk_engine.hpp"



void Application::run()
{
	VulkanEngine engine;

	engine.init();
	engine.run();

	engine.cleanup();
}
