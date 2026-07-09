#pragma warning(disable:4996)

#include "Util.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/visualization/cloud_viewer.h>

#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;
using namespace Eigen;

/*
	函数功能：将三维点数据保存到txt文本文件
	输入：
		Data：待保存的三维点集合，每个点包含 x、y、z 三个坐标
		filename：输出txt文件名或文件路径
	输出：
		无（函数执行后会在磁盘上生成或覆盖对应的txt文件）
*/
void WriteData3d(
	vector<Vector3d>&Data, 
	string & filename)
{
	fstream ClearTXT(filename, ios::out);
	fstream WriteTXT;
	WriteTXT.open(filename);
	for (int i = 0; i < Data.size(); i++)
	{
		WriteTXT << Data[i][0] << " " << Data[i][1] << " " << Data[i][2] << endl;
	}
	WriteTXT.close();
}

/*
	函数功能：从txt文本文件中读取三维点数据
	输入：
		filename：输入txt文件名或文件路径
	输出：
		Data：读取后的三维点集合，每一行数据对应一个三维点
*/
void ReadData3d(
	vector<Vector3d>& Data, 
	string& filename)
{
	fstream ReadDataTxt;
	float X, Y, Z;
	Vector3d param;
	ReadDataTxt.open(filename);
	while (ReadDataTxt >> X >> Y >> Z)
	{
		param[0] = X;
		param[1] = Y;
		param[2] = Z;
		Data.push_back(param);
	}
	ReadDataTxt.close();
}


/*
	函数功能：将二维点数据保存到txt文本文件
	输入：
		Data：待保存的二维点集合，每个点包含 x、y 两个坐标
		filename：输出txt文件名或文件路径
	输出：
		无（函数执行后会在磁盘上生成或覆盖对应的txt文件）
*/
void WriteData2d(
	vector<Vector2d>& Data, 
	string& filename)
{
	fstream ClearTXT(filename, ios::out);
	fstream WriteTXT;
	WriteTXT.open(filename);
	for (int i = 0; i < Data.size(); i++)
	{
		WriteTXT << Data[i][0] << " " << Data[i][1] << endl;
	}
	WriteTXT.close();
}


/*
	函数功能：从txt文本文件中读取二维点数据
	输入：
		filename：输入txt文件名或文件路径
	输出：
		Data：读取后的二维点集合，每一行数据对应一个二维点
*/
void ReadData2d(
	vector<Vector2d>& Data, 
	string& filename)
{
	fstream ReadDataTxt;
	float X, Y;
	Vector2d param;
	ReadDataTxt.open(filename);
	while (ReadDataTxt >> X >> Y)
	{
		param[0] = X;
		param[1] = Y;
		Data.push_back(param);
	}
	ReadDataTxt.close();
}

/*
	函数功能：将多帧重建得到的三维点集合并成一个点云，并使用PCL进行可视化显示
	输入：
		points3d_in_cam：按帧存储的三维点集合，外层表示不同帧，内层表示每帧中的所有三维点
	输出：
		无（函数执行后会弹出点云可视化窗口显示结果）
*/
void visualization_points(vector<vector<Vector3d>>& points3d_in_cam)
{
	// 将多帧三维点拼接为一个点云，再交给 PCL 统一显示。
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
	cloud->height = 1;
	cloud->is_dense = true;

	// 预计算总点数
	size_t total = 0;
	for (auto& vec : points3d_in_cam)
		total += vec.size();
	cloud->points.reserve(total);

	// 填充点云
	for (auto& vec : points3d_in_cam) {
		for (auto& p : vec) {
			cloud->points.emplace_back(
				(float)p.x(),
				(float)p.y(),
				(float)p.z()
			);
		}
	}
	cloud->width = cloud->points.size();

	// 可视化
	pcl::visualization::PCLVisualizer::Ptr viewer(new pcl::visualization::PCLVisualizer("3D Viewer"));
	viewer->setBackgroundColor(1, 1, 1);
	pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> color(cloud, 0, 0, 255);
	viewer->addPointCloud(cloud, color, "cloud");
	viewer->setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 1.0, "cloud");
	viewer->addCoordinateSystem(1.0);
	viewer->spin();
}

/*
	函数功能：收集并筛选要处理的图像路径列表
	输入：
		path：图像所在目录路径
		idx_begin：起始索引（包含）
		idx_end：结束索引（包含）
	输出：
		path_final：最终筛选后的图像路径列表
*/
void ReadImgPath(string path,
	int idx_begin,
	int idx_end,
	vector<string>& path_final)
{
	path_final.clear();

	vector<string> all_paths;

	vector<string> exts = {
		".jpg", ".jpeg", ".png",
		".bmp", ".tif", ".tiff"
	};

	// 递归扫描目录，收集所有可用图像路径。
	for (const auto& entry : std::filesystem::recursive_directory_iterator(path))
	{
		if (!entry.is_regular_file())
			continue;

		string ext = entry.path().extension().string();

		transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (find(exts.begin(), exts.end(), ext) != exts.end())
		{
			all_paths.push_back(
				std::filesystem::absolute(entry.path()).string()
			);
		}
	}

	// 排序
	sort(all_paths.begin(), all_paths.end());

	// (-1, -1) 表示保留全部
	if (idx_begin == -1 && idx_end == -1)
	{
		path_final = std::move(all_paths);
		return;
	}

	if (all_paths.empty())
		return;

	// 边界检查
	idx_begin = max(0, idx_begin);
	idx_end = min(idx_end, static_cast<int>(all_paths.size()) - 1);

	if (idx_begin > idx_end)
		return;

	// 提取 [idx_begin, idx_end]
	path_final.assign(
		all_paths.begin() + idx_begin,
		all_paths.begin() + idx_end + 1
	);
}
