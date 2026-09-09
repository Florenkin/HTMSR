#pragma once

#include <Eigen/Core>
#include <Eigen/Dense>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
#include <string>
#include <vector>

// StereoScan 封装了双目标定、激光中心线提取和三维重建的完整流程。
class StereoScan
{
private:
	// 左右相机的内参与畸变参数。
	cv::Mat K1;
	cv::Mat K2;
	cv::Mat D1;
	cv::Mat D2;
	cv::Mat P1;
	cv::Mat P2;
	// 双目外参与极线几何相关矩阵。
	cv::Mat R;
	cv::Mat t;
	cv::Rect roi;
	cv::Mat E;
	cv::Mat F;
	double CalibrateOneCamera(std::vector<cv::Mat> img, cv::Size board_size, cv::Size square_size, cv::Mat& M, cv::Mat& D);
	double CalibrateTwoCamera(
		std::vector<cv::Mat> img_left, std::vector<cv::Mat> img_right, cv::Size board_size, cv::Size square_size,
		cv::Mat M11, cv::Mat M22, cv::Mat D11, cv::Mat D22,
		cv::Mat& RR, cv::Mat& tt, cv::Mat& EE, cv::Mat& FF);
	cv::Mat RttoH(cv::Mat R, cv::Mat t);
	void GetPointFrom2Lines(Eigen::Matrix<double, 6, 1> line1, Eigen::Matrix<double, 6, 1> line2, Eigen::Vector3d& p3d);
	void PixtoVec(Eigen::Vector2d pix2d, Eigen::Matrix3d MM, Eigen::Vector3d& Vec);
	void imreadtwo(std::vector<cv::Mat>& img1, std::vector<cv::Mat>& img2, std::string path1, std::string path2, int img_num);
	void imgPreProcess(cv::Mat img, int thre, cv::Mat& dstImg, std::vector<cv::Vec4i>& dstRanges);
	void GetImageRanges(cv::Mat& bwImg, std::vector<cv::Vec4i>& dstRanges, double areaThr, int borderexten);
	void GrayCenterLineExtrationOnceNew(cv::Mat srcImg, cv::Rect rect, std::vector<Eigen::Vector2d>& dstPoints, cv::Mat& imgline);
	void findLaserCenterSteger_v2(cv::Mat& inputImage, cv::Rect rect, std::vector<Eigen::Vector2d>& dstPoints, cv::Mat& imgLine);
	void My3DReconstruction(cv::Mat img1, cv::Mat img2, std::vector<Eigen::Vector3d>& p3d);
	void WriteCalibrationFile(const std::string& filename, double rms);
	bool ReadCalibrationFile(const std::string& filename);
	void threshold_RGB(cv::Mat srcImg, int value, std::string color, cv::Mat& dstImg);
	void RGBtoGray(cv::Mat srcImg, std::string color, cv::Mat& dstImg);
	void domainSmooth(cv::Mat srcImg, cv::Mat& dstImg);
public:
	StereoScan(std::string& cablfile, std::string& cabrfile, cv::Size board_size, cv::Size square_size);
	void ReconstructImages(
		const std::string& left_path,
		const std::string& right_path,
		int img_begin,
		int img_end,
		std::vector<std::vector<Eigen::Vector3d>>& points3d);
	
};

// 运行期的路径与输入输出配置。
struct Variable
{
	int img_num = 20;
	std::string cablfile = "F:/Data630/cal/L/";
	std::string cabrfile = "F:/Data630/cal/R/";
	bool is_calibrate = true;
	std::string calibration_file = "stereo_calibration.yml";
	std::string reconstruction_left_path = "F:/StereoData20260702/1/L/";
	std::string reconstruction_right_path = "F:/StereoData20260702/1/R/";
	int reconstruction_img_begin = -1;
	int reconstruction_img_end = -1;
	std::string point_cloud_txt = "point_cloud.txt";
};

inline const bool isrename = false;

// 算法参数配置。
struct Parameters
{
	cv::Size board_size = cv::Size(11, 8);
	cv::Size square_size = cv::Size(15, 15);
	cv::Rect rect1 = cv::Rect(0, 0, 3072, 2048);
	cv::Rect rect2 = cv::Rect(0, 0, 3072, 2048);
	int key = 0;//key=1 时使用 steger 算法提取激光中心线，否则使用灰度质心法提取激光中心线
	// 后处理参数
	int RemoveNum = 10;
	bool isRemoveEndPoints = false;//是否去除端点
	// graycenter
	int thre_set = 150;//二值化阈值
	int min_gray = 30;//最小灰度
	// steger
	std::string Color_Laser = "BLUE";
	double bwThr = 100;//二值化阈值
	double sltThr = 200;
	double stripeWidth = 5;//激光线条宽度
	int filter = 1;
	// 3D reconstruction
	double min_distance = 0.1;//最小距离
};

inline const Parameters parameters;
inline Variable variable;
