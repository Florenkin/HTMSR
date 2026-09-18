#include "StereoScan.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;


/*
	函数功能：扫描指定目录中的图像文件，按文件名排序，并在需要时按顺序重命名
	输入：
		path：图像所在目录路径
	输出：
		files：排序后的图像路径列表
*/
static void sort_renameFilesByName(const std::string& path, std::vector<fs::path>& files);


/*
	函数功能：从左右两个目录中读取图像，并按顺序组成左右图像对
	输入：
		path1：左相机图像目录路径
		path2：右相机图像目录路径
		img_num：最多读取的图像对数量
	输出：
		img1：左相机图像集合
		img2：右相机图像集合
*/
void StereoScan::imreadtwo(
	std::vector<cv::Mat>& img1,
	std::vector<cv::Mat>& img2,
	std::string path1,
	std::string path2,
	int img_num)
{
	// 读取左右目录下的图像，并按排序后的顺序一一配对。
	std::vector<fs::path> paths1, paths2;
	sort_renameFilesByName(path1, paths1);
	sort_renameFilesByName(path2, paths2);

	cv::Mat imgl, imgr;
	for (int i = 0; (i < std::min(paths1.size(), paths2.size())) && (i < img_num); ++i)
	{
		imgl = cv::imread(paths1[i].string(), 0);
		imgr = cv::imread(paths2[i].string(), 0);
		if (imgl.empty() || imgr.empty())
			continue;

		img1.push_back(imgl);
		img2.push_back(imgr);
	}
}


/*
	函数功能：扫描指定目录，筛选 bmp/png 图像文件，按文件名排序，并可选重命名为连续编号
	输入：
		path：图像所在目录路径
	输出：
		files：收集并排序后的图像路径列表
*/
static void sort_renameFilesByName(const std::string& path, std::vector<fs::path>& files)
{
	// 收集目录下的 bmp/png 文件，并按文件名排序。
	fs::path folderPath(path);
	if (fs::exists(folderPath) && fs::is_directory(folderPath))
	{
		for (const fs::path& path : fs::directory_iterator(folderPath))
		{
			if (fs::is_regular_file(path))
			{
				if (path.extension() == ".bmp" || path.extension() == ".png")
					files.push_back(path);
			}
		}

		std::sort(files.begin(), files.end());
		if (isrename)
		{
			// 若开启重命名开关，则将文件重命名为连续编号。
			fs::path newpath;
			for (int i = 0; i < files.size(); ++i)
			{
				newpath = files[i].parent_path() / (std::to_string(i + 1) + files[i].extension().string());
				fs::rename(files[i], newpath);
			}
		}
	}
}
