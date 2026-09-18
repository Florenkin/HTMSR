 #include "StereoScan.h"
#include "Util.h"

#include <opencv2/highgui/highgui.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace std;
using namespace Eigen;

int main()
{
	try
	{
		// 程序启动后等待 1000ms，按下 c/C 则进入标定流程，否则尝试直接重建。
		cout << "Input c in 1000 ms to calibrate, otherwise reconstruct." << endl;

		int key = cv::waitKey(1000);

		variable.is_calibrate = (key == 'c' || key == 'C');
		if (!variable.is_calibrate && !std::filesystem::exists(variable.calibration_file))
		{
			cout << "Calibration file not found, start calibration." << endl;
			variable.is_calibrate = true;
		}

		if (variable.is_calibrate)
			cout << "Start calibration." << endl;
		else
			cout << "Read calibration file and reconstruct." << endl;

		StereoScan stereo_scan(
			variable.cablfile,
			variable.cabrfile,
			parameters.board_size,
			parameters.square_size);

		// 外层 vector 表示每一帧，内层 vector 保存该帧重建得到的三维点。
		vector<vector<Vector3d>> points3d;
		stereo_scan.ReconstructImages(
			variable.reconstruction_left_path,
			variable.reconstruction_right_path,
			variable.reconstruction_img_begin,
			variable.reconstruction_img_end,
			points3d);

		// 将逐帧点集合并成一个点云数组，便于统一导出。
		vector<Vector3d> point_cloud;
		for (int i = 0; i < points3d.size(); i++)
		{
			for (int j = 0; j < points3d[i].size(); j++)
			{
				point_cloud.push_back(points3d[i][j]);
			}
		}

		string filename = variable.point_cloud_txt;
		WriteData3d(point_cloud, filename);
		cout << "Write point cloud txt: " << filename << endl;

		// 若重建成功，则弹出点云可视化窗口。
		if (!point_cloud.empty())
			visualization_points(points3d);
	}
	catch (const exception& e)
	{
		cerr << e.what() << endl;
		return -1;
	}

	return 0;
}
