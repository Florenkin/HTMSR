#include"StereoScan.h"

using namespace cv;
using namespace std;
using namespace Eigen;


/*
	函数功能：将旋转矩阵和平移向量组合成 4×4 齐次变换矩阵
	输入：
		R：3×3 旋转矩阵
		t：3×1 平移向量
	输出：
		返回值：4×4 齐次变换矩阵 H
*/
Mat StereoScan::RttoH(Mat R, Mat t)
{
	// 将旋转矩阵 R 和平移向量 t 组合成 4x4 齐次变换矩阵。
	Mat H = Mat::zeros(4, 4, CV_64FC1);
	H.at<double>(0, 0) = R.at<double>(0, 0);
	H.at<double>(0, 1) = R.at<double>(0, 1);
	H.at<double>(0, 2) = R.at<double>(0, 2);
	H.at<double>(1, 0) = R.at<double>(1, 0);
	H.at<double>(1, 1) = R.at<double>(1, 1);
	H.at<double>(1, 2) = R.at<double>(1, 2);
	H.at<double>(2, 0) = R.at<double>(2, 0);
	H.at<double>(2, 1) = R.at<double>(2, 1);
	H.at<double>(2, 2) = R.at<double>(2, 2);
	H.at<double>(0, 3) = t.at<double>(0, 0);
	H.at<double>(1, 3) = t.at<double>(1, 0);
	H.at<double>(2, 3) = t.at<double>(2, 0);
	H.at<double>(3, 3) = 1;
	return H;
}

/*
	函数功能：将图像中的二维像素点转换为相机坐标系下的归一化三维方向向量
	输入：
		pix2d：二维像素坐标点 (u, v)
		MM：相机内参矩阵
	输出：
		Vec：归一化相机坐标系中的方向向量 (x, y, 1)
*/
void StereoScan::PixtoVec(
	Eigen::Vector2d pix2d,
	Eigen::Matrix3d MM,
	Eigen::Vector3d& Vec)
{
	Vec = Eigen::Vector3d((pix2d[0] - MM(0, 2)) / MM(0, 0),
		(pix2d[1] - MM(1, 2)) / MM(1, 1),
		1);
}


/*
	函数功能：根据两条空间直线求解它们最近点之间的中点，作为三维重建点
	输入：
		line1：第一条空间直线，格式为 [vx, vy, vz, px, py, pz]
		line2：第二条空间直线，格式为 [vx, vy, vz, px, py, pz]
	输出：
		p3d：两条空间直线最近点中点对应的三维坐标
*/
void StereoScan::GetPointFrom2Lines(
	Eigen::Matrix<double, 6, 1> line1,
	Eigen::Matrix<double, 6, 1> line2,
	Eigen::Vector3d& p3d)
{
	Vector3d p1 = Vector3d(line1[3], line1[4], line1[5]);
	Vector3d p2 = Vector3d(line2[3], line2[4], line2[5]);
	Vector3d v1 = Vector3d(line1[0], line1[1], line1[2]);
	Vector3d v2 = Vector3d(line2[0], line2[1], line2[2]);
	Matrix<double, 3, 2> A;
	A << v1[0], -v2[0],
		v1[1], -v2[1],
		v1[2], -v2[2];
	Vector3d b = p2 - p1;
	Vector2d x = A.colPivHouseholderQr().solve(b);//求解两条直线上的参数
	Vector3d p3 = p1 + v1 * x[0];
	Vector3d p4 = p2 + v2 * x[1];
	Vector3d p5 = (p3 + p4) / 2;
	p3d = p5;
}
