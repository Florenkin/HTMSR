#include <Eigen/Core>
#include <iostream>
#include <fstream>

// 保存3d数据到txt
void WriteData3d(
	std::vector<Eigen::Vector3d>& Data, 
	std::string& filename);

// 读取3d数据从txt
void ReadData3d(
	std::vector<Eigen::Vector3d>& Data,
	std::string& filename);

// 保存2d数据到txt
void WriteData2d(
	std::vector<Eigen::Vector2d>& Data, 
	std::string& filename);

// 读取2d数据从txt
void ReadData2d(
	std::vector<Eigen::Vector2d>& Data, 
	std::string& filename);

// 点云可视化显示函数。
void visualization_points(
	std::vector<std::vector<Eigen::Vector3d>>& points3d_in_cam);

// 读取图像路径，可按给定索引范围截取子集。
void ReadImgPath(
	std::string path,
	int idx_begin,
	int idx_end,
	std::vector<std::string>& path_final);
