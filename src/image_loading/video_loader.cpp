#include "video_loader.hpp"

std::vector<glm::vec4>& VideoLoader::getVideoFrame(const std::string& filepath, int frame)
{
	std::vector<glm::vec4> res;
	return res;
}

std::vector<float> VideoLoader::getVideoFrameBW(const std::filesystem::path filepath, int frame)
{
	//std::cout << cv::getBuildInformation() << std::endl;
	std::vector<float> res;
	cv::VideoCapture capture("D:\\vulkan\\projects\\grass_renderer\\assets\\badapple.mp4", cv::CAP_FFMPEG);
	cv::Mat frameData;
	cv::Mat1b singleChannelFrame;
	
	//capture.open("D:\\vulkan\\projects\\grass_renderer\\assets\\badapple.mp4", cv::CAP_FFMPEG);
	if (!capture.isOpened())
	{
		std::cout << filepath.generic_string();
		throw "error when reading file";
	}

	int totalFrames = capture.get(cv::CAP_PROP_FRAME_COUNT);
	if (frame > totalFrames)
		return res;
	capture.set(cv::CAP_PROP_POS_FRAMES, frame);
	capture.read(frameData);
	cv::extractChannel(frameData, singleChannelFrame, 0); 
	if (singleChannelFrame.isContinuous()) {
		// array.assign(mat.datastart, mat.dataend); // <- has problems for sub-matrix like mat = big_mat.row(i)
		res.assign(singleChannelFrame.data, singleChannelFrame.data + singleChannelFrame.total() * singleChannelFrame.channels());
	}
	else {
		for (int i = 0; i < singleChannelFrame.rows; ++i) {
			res.insert(res.end(), singleChannelFrame.ptr<uchar>(i), singleChannelFrame.ptr<uchar>(i) + singleChannelFrame.cols * singleChannelFrame.channels());
		}
	}
	return res;
}
