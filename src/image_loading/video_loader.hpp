#pragma once

#include <opencv2/opencv.hpp>
#include <glm/ext/vector_float4.hpp>
#include <filesystem>



namespace VideoLoader
{
	std::vector<glm::vec4>& getVideoFrame(const std::string& filepath, int frame);
	std::vector<float> getVideoFrameBW(const std::filesystem::path filepath, int frame);

};