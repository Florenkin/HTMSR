#include "StereoScan.h"
#include "LineLaser.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace cv;
using namespace std;
using namespace Eigen;


/*
	函数功能：批量读取左右重建图像，并对每一对左右图像执行一次三维重建
	输入：
		left_path：左相机重建图像目录
		right_path：右相机重建图像目录
		img_begin：开始读取的图像索引（包含）
		img_end：结束读取的图像索引（包含）
	输出：
		points3d：按帧保存的三维点集合，外层表示图像对，内层表示该图像对重建得到的三维点
*/
void StereoScan::ReconstructImages(
	const std::string& left_path,
	const std::string& right_path,
	int img_begin,
	int img_end,
	std::vector<std::vector<Eigen::Vector3d>>& points3d)
{
	if (!std::filesystem::is_directory(left_path))
		throw runtime_error("Left reconstruction path does not exist: " + left_path);
	if (!std::filesystem::is_directory(right_path))
		throw runtime_error("Right reconstruction path does not exist: " + right_path);

	// 读取左右图像序列路径，并按顺序组织成重建输入。
	vector<string> img_path_L, img_path_R;
	ReadImgPath(left_path, img_begin, img_end, img_path_L);
	ReadImgPath(right_path, img_begin, img_end, img_path_R);

	int img_num = static_cast<int>(min(img_path_L.size(), img_path_R.size()));
	if (img_num == 0)
		throw runtime_error("No reconstruction images found.");

	points3d.clear();

	for (int i = 0; i < img_num; i++)
	{
		cout << "第 " << i + 1 << " 根" << endl;

		// 对每一对左右图像独立执行三维重建。
		Mat imgli = imread(img_path_L[i]);
		Mat imgri = imread(img_path_R[i]);
		if (imgli.empty() || imgri.empty())
			continue;

		vector<Vector3d> p3di;
		My3DReconstruction(imgli, imgri, p3di);
		points3d.push_back(p3di);

		cout << "p3di.size() " << p3di.size() << endl;
		cout << endl;
	}

	cout << "p.size(): " << points3d.size() << endl;
}


/*
	函数功能：对单对左右图像执行完整的三维重建流程，包括激光中心线提取、去畸变、左右匹配和三维点恢复
	输入：
		img1：左相机图像
		img2：右相机图像
	输出：
		p3d：该对左右图像重建得到的三维点集合
*/
void StereoScan::My3DReconstruction(cv::Mat img1, cv::Mat img2, std::vector<Eigen::Vector3d>& p3d)
{
	// 将 OpenCV 相机矩阵转换成 Eigen 矩阵，便于后续几何计算。
	Matrix3d M11E, M22E;
	cv2eigen(K1, M11E);
	cv2eigen(K2, M22E);

	vector<Eigen::Vector2d> dstPoints1E, dstPoints2E;
	Mat imgline1, imgline2;

	if (parameters.key == 1)
	{
		//使用Steger法
		findLaserCenterSteger_v2(img1, parameters.rect1, dstPoints1E, imgline1);//100 200 7 1
		findLaserCenterSteger_v2(img2, parameters.rect2, dstPoints2E, imgline2);//100 200 7 1
	}
	else
	{
		//使用灰度重心法
		GrayCenterLineExtrationOnceNew(img1, parameters.rect1, dstPoints1E, imgline1);
		GrayCenterLineExtrationOnceNew(img2, parameters.rect2, dstPoints2E, imgline2);
	}

	if (dstPoints1E.size() == 0 || dstPoints2E.size() == 0)
	{
		return;
	}

	//Vector2d 转 Point2d
	vector<Point2d> dstPoints1, dstPoints2;
	for (int i = 0; i < dstPoints1E.size(); i++)
	{
		dstPoints1.push_back(Point2d(dstPoints1E[i][0], dstPoints1E[i][1]));
	}
	for (int i = 0; i < dstPoints2E.size(); i++)
	{
		dstPoints2.push_back(Point2d(dstPoints2E[i][0], dstPoints2E[i][1]));
	}

	vector<Point2d> undistortedPoints1, undistortedPoints2;
	undistortPoints(dstPoints1, undistortedPoints1, K1, D1, noArray(), P1);
	undistortPoints(dstPoints2, undistortedPoints2, K2, D2, noArray(), P2);

	// 在左右激光中心线上寻找最匹配的点对。
	vector<Eigen::Vector2d> pairpoint1, pairpoint2;
	Eigen::Vector3d ttE = Eigen::Vector3d(t.at<double>(0), t.at<double>(1), t.at<double>(2));
	Eigen::Vector3d vec_R_L = ttE;
	vec_R_L.normalize();
	Eigen::Matrix3d RRE;
	cv::cv2eigen(R, RRE);
	if (undistortedPoints1.size() < undistortedPoints2.size())
	{
		for (int i = 0; i < undistortedPoints1.size(); i++)
		{
			Eigen::Vector2d pix_L_i = Eigen::Vector2d(undistortedPoints1[i].x, undistortedPoints1[i].y);
			Eigen::Vector3d v_L_i;
			PixtoVec(pix_L_i, M11E, v_L_i);
			v_L_i.normalize();
			Eigen::Vector3d v_LR_i = RRE * v_L_i;

			double error_min = 100000;
			double t = 0;
			for (int j = 0; j < undistortedPoints2.size(); j++)
			{
				Eigen::Vector2d pix_R_j = Eigen::Vector2d(undistortedPoints2[j].x, undistortedPoints2[j].y);
				Eigen::Vector3d v_R_j;
				PixtoVec(pix_R_j, M22E, v_R_j);
				v_R_j.normalize();
				double error_ij = abs((v_LR_i.cross(v_R_j)).dot(vec_R_L));
				if (error_ij < error_min)
				{
					error_min = error_ij;
					t = j;
				}
				//cout << error_ij << " , ";
			}
			//cout << endl;
			if (error_min < parameters.min_distance)
			{
				pairpoint1.push_back(Eigen::Vector2d(undistortedPoints1[i].x, undistortedPoints1[i].y));
				pairpoint2.push_back(Eigen::Vector2d(undistortedPoints2[t].x, undistortedPoints2[t].y));
			}
		}
	}
	else
	{
		for (int i = 0; i < undistortedPoints2.size(); i++)
		{
			Eigen::Vector2d pix_R_i = Eigen::Vector2d(undistortedPoints2[i].x, undistortedPoints2[i].y);
			Eigen::Vector3d v_R_i;
			PixtoVec(pix_R_i, M22E, v_R_i);
			v_R_i.normalize();

			double error_min = 100000;
			double t = 0;
			for (int j = 0; j < undistortedPoints1.size(); j++)
			{
				Eigen::Vector2d pix_L_j = Eigen::Vector2d(undistortedPoints1[j].x, undistortedPoints1[j].y);
				Eigen::Vector3d v_L_j;
				PixtoVec(pix_L_j, M11E, v_L_j);
				v_L_j.normalize();
				Eigen::Vector3d v_LR_j = RRE * v_L_j;

				double error_ij = abs((v_LR_j.cross(v_R_i)).dot(vec_R_L));
				if (error_ij < error_min)
				{
					error_min = error_ij;
					t = j;
				}
				//cout << error_ij << " , ";
			}
			//cout << endl;
			if (error_min < parameters.min_distance)
			{
				pairpoint1.push_back(Eigen::Vector2d(undistortedPoints1[t].x, undistortedPoints1[t].y));
				pairpoint2.push_back(Eigen::Vector2d(undistortedPoints2[i].x, undistortedPoints2[i].y));
			}
		}
	}

	// 对每一组匹配点构造两条空间射线，并求它们的最近点中点作为三维点。
	for (int i = 0; i < pairpoint1.size(); i++)
	{
		Eigen::Vector3d v_L_i;
		PixtoVec(pairpoint1[i], M11E, v_L_i);
		v_L_i.normalize();
		Eigen::Vector3d v_LR_i = RRE * v_L_i;
		Eigen::Matrix<double, 6, 1> line_L_i;
		line_L_i << v_LR_i[0], v_LR_i[1], v_LR_i[2], ttE[0], ttE[1], ttE[2];

		Eigen::Vector3d v_R_i;
		PixtoVec(pairpoint2[i], M22E, v_R_i);
		v_R_i.normalize();
		Eigen::Matrix<double, 6, 1> line_R_i;
		line_R_i << v_R_i[0], v_R_i[1], v_R_i[2], 0, 0, 0;

		Eigen::Vector3d p3d_R_i;
		GetPointFrom2Lines(line_L_i, line_R_i, p3d_R_i);
		Eigen::Vector3d p3d_L_i = RRE.inverse() * (p3d_R_i - ttE);
		p3d.push_back(p3d_L_i);
	}
}


/*
	函数功能：使用灰度重心法在指定 ROI 区域内提取激光条纹中心线
	输入：
		srcImg：输入图像
		rect：待处理的 ROI 区域
	输出：
		dstPoints：提取出的激光中心点集合，坐标为整张图中的位置
		imgline：绘制了中心线点的可视化结果图
*/
void StereoScan::GrayCenterLineExtrationOnceNew(cv::Mat srcImg, cv::Rect rect, std::vector<Eigen::Vector2d>& dstPoints, cv::Mat& imgline)
{
	// 灰度重心法：在每一行内按灰度加权计算激光中心位置。
	Size imgsize = srcImg.size();
	Mat imgrgb = srcImg.clone();
	if (srcImg.channels() == 1)
		cvtColor(imgrgb, imgrgb, COLOR_GRAY2BGR);
	Mat roi = srcImg(rect);
	//namedWindow("roi", WINDOW_FREERATIO);
	//imshow("roi", roi);//显示图片
	//waitKey(1);
	Mat dstImg;
	vector<Vec4i> ranges;
	int thre = parameters.thre_set;
	imgPreProcess(roi, thre, dstImg, ranges);
	namedWindow("dstImg", WINDOW_FREERATIO);
	imshow("dstImg", dstImg);//显示图片
	//imwrite("dst.png", dstImg);
	waitKey(1);

	vector<Eigen::Vector2d> linePoints;
	cout << " ranges " << ranges.size() << endl;
	for (int k = 0; k < ranges.size(); k++)
	{
		for (int m = ranges[k][0]; m < ranges[k][1]; m++)
		{
			double sum_gv = 0;
			double sum_gv_pos = 0;
			double thresh = parameters.min_gray;//30,10,0
			for (int n = ranges[k][2]; n < ranges[k][3]; n++)
			{
				double tempV = dstImg.at<uchar>(m, n);
				if (tempV > thresh)
				{
					sum_gv += tempV;
					sum_gv_pos += tempV * n;
				}
				else continue;
				if (m >= ranges[k][0] + 2 && m < ranges[k][1] - 2) {
					sum_gv += dstImg.at<uchar>(m - 2, n) + dstImg.at<uchar>(m - 1, n) + dstImg.at<uchar>(m + 1, n) + dstImg.at<uchar>(m + 2, n);
					sum_gv_pos += (dstImg.at<uchar>(m - 2, n) + dstImg.at<uchar>(m - 1, n) + dstImg.at<uchar>(m + 1, n) + dstImg.at<uchar>(m + 2, n)) * n;
				}
			}
			if (sum_gv < 200)
				continue;
			double x = sum_gv_pos / sum_gv;
			cv::circle(imgrgb, Point2f(x + rect.tl().x, m + rect.tl().y), 1, Scalar(0, 0, 255));
			linePoints.push_back(Eigen::Vector2d(x + rect.tl().x, m + rect.tl().y));
		}
	}

	dstPoints = linePoints;
	imgline = imgrgb;
	namedWindow("imgline", WINDOW_FREERATIO);
	imshow("imgline", imgline);//显示图片
	waitKey(1);

	if (parameters.isRemoveEndPoints)
	{
		int ptNum = dstPoints.size();
		if (ptNum > 2 * parameters.RemoveNum)
		{
			//dstPoints.erase(dstPoints.begin(), dstPoints.begin() + parameters.RemoveNum);
			//dstPoints.erase(dstPoints.end() - parameters.RemoveNum, dstPoints.end());
		}
	}
}


/*
	函数功能：使用 Steger 法在指定 ROI 区域内提取激光条纹的亚像素中心线
	输入：
		inputImage：输入图像
		rect：待处理的 ROI 区域
	输出：
		dstPoints：提取出的激光中心点集合，坐标为整张图中的位置
		imgLine：绘制了中心线点的可视化结果图
*/
void StereoScan::findLaserCenterSteger_v2(cv::Mat& inputImage, cv::Rect rect, std::vector<Eigen::Vector2d>& dstPoints, cv::Mat& imgLine)
{
	// Steger 法：利用高斯导数与 Hessian 信息做亚像素激光中心提取。
	Size imgSize = inputImage.size();
	imgLine = inputImage.clone();
	Mat proImg = inputImage(rect);//提取矩形框内图像进行高斯模糊(目前为全图)
	Mat bwImg;
	if (proImg.channels() > 1)
	{
		threshold_RGB(proImg, parameters.bwThr, parameters.Color_Laser, bwImg);
		RGBtoGray(proImg, parameters.Color_Laser, proImg);
	}
	else
	{
		threshold(proImg, bwImg, parameters.bwThr, 255, THRESH_BINARY);
	}
	morphologyEx(bwImg, bwImg, MORPH_OPEN, getStructuringElement(MORPH_RECT, Size(3, 3)));

	//提取Roi：有效矩形区域
	vector<Vec4i> Rois;
	GetImageRanges(bwImg, Rois, 500, 10);//500:亮度阈值，10：边界修正量（+）

	//void cv::GaussianBlur(
	//	cv::InputArray src,     // 输入图像
	//	cv::OutputArray dst,    // 输出图像
	//	cv::Size ksize,         // 高斯滤波核的大小，通常为奇数
	//	double sigmaX,          // X方向的高斯标准差
	//	double sigmaY = 0,      // Y方向的高斯标准差（如果为0，则默认为与sigmaX相同）
	//	int borderType = cv::BORDER_DEFAULT  // 边界处理方式，默认为BORDER_DEFAULT
	//);
	// 这里手动构造高斯导数核，便于后续直接计算 Hessian 矩阵。

	//cout << " Rois " << Rois.size() << endl;
	//求解高斯核：二维权重矩阵，总和为1
	// 定义网格范围
	double sigma = parameters.stripeWidth / sqrt(3.0);//stripeWidth:激光线条宽度；经验公式：σ=B/（根号3）
	int range = static_cast<int>(round(3 * sigma));//核半径r；经验公式：r=3σ
	int size = 2 * range + 1;
	// 创建坐标网格，
	Mat X(size, size, CV_64F), Y(size, size, CV_64F);
	for (int i = -range; i <= range; ++i) {
		for (int j = -range; j <= range; ++j) {
			X.at<double>(i + range, j + range) = j;
			Y.at<double>(i + range, j + range) = i;
		}
	}
	//X.mul(X)+Y.mul(Y)表示网格上每个网格到原点距离
	// X² + Y² 算出来是：
	 /* 8  5  4  5  8
		5  2  1  2  5
		4  1  0  1  4
		5  2  1  2  5
		8  5  4  5  8*/
		// 计算 exp(-(X^2 + Y^2) / (2 * sigma^2))
	Mat X2_Y2 = (X.mul(X) + Y.mul(Y)) / (2 * sigma * sigma);//离散高斯分布矩阵
	Mat expTerm;
	cv::exp(-X2_Y2, expTerm); // OpenCV 的 exp 函数：expTerm=exp(X^2+Y^2)/2σ^2;
	// 计算高斯导数
	Mat DGaussx = (1 / (2 * CV_PI * pow(sigma, 4))) * (-X).mul(expTerm);//expTerm对x偏导
	Mat DGaussy = DGaussx.t(); // 转置DGaussx = 水平方向边缘检测；DGaussy = 垂直方向边缘检测；因为高斯对称，所以 y 导数 = x 导数 转置（旋转90)
	Mat DGaussxx = (1 / (2 * CV_PI * pow(sigma, 4))) * ((X.mul(X) / (sigma * sigma)) - 1).mul(expTerm);
	Mat DGaussxy = (1 / (2 * CV_PI * pow(sigma, 6))) * (X.mul(Y)).mul(expTerm);
	Mat DGaussyy = DGaussxx.t(); // 转置
	// OpenCV 卷积 = 核先翻转再相乘
	cv::flip(DGaussx, DGaussx, -1);
	cv::flip(DGaussy, DGaussy, -1);
	cv::flip(DGaussxx, DGaussxx, -1);
	cv::flip(DGaussxy, DGaussxy, -1);
	//计算各个Roi的Hessian矩阵
	vector<Point2d> linePoints;
	//统计该元素是否被访问
	Mat visited = Mat::zeros(proImg.size(), CV_8UC1);
	for (int k = 0; k < Rois.size(); k++) {
		Vec4i roi = Rois[k];//Rois[i]={顶，底，左，右}
		Mat roiImg = proImg(Range(roi[0], roi[1] + 1), Range(roi[2], roi[3] + 1)).clone();//之前右，底减一；
		//Mat tempBwimg = bwImg(Range(roi[0], roi[1] + 1), Range(roi[2], roi[3] + 1));
		//Mat drawproImg;
		//cvtColor(proImg, drawproImg, COLOR_GRAY2BGR);
		//rectangle(drawproImg, Rect(roi[2], roi[0], roi[3] - roi[2], roi[1] - roi[0]), Scalar(0, 0, 255), 2);
		//namedWindow("drawproImg", WINDOW_NORMAL);
		//imshow("drawproImg", drawproImg);
		//namedWindow("roiImg", WINDOW_NORMAL);
		//imshow("roiImg", roiImg);
		//namedWindow("tempBwimg", WINDOW_NORMAL);
		//imshow("tempBwimg", tempBwimg);
		//waitKey(0);
		roiImg.convertTo(roiImg, CV_64F);
		Mat Dx, Dy, Dxx, Dxy, Dyy;
		// 应用卷积
		filter2D(roiImg, Dx, CV_64F, DGaussx, Point(-1, -1), 0, BORDER_CONSTANT);//水平亮度变化
		filter2D(roiImg, Dy, CV_64F, DGaussy, Point(-1, -1), 0, BORDER_CONSTANT);//竖直亮度变化
		filter2D(roiImg, Dxx, CV_64F, DGaussxx, Point(-1, -1), 0, BORDER_CONSTANT);
		filter2D(roiImg, Dxy, CV_64F, DGaussxy, Point(-1, -1), 0, BORDER_CONSTANT);
		filter2D(roiImg, Dyy, CV_64F, DGaussyy, Point(-1, -1), 0, BORDER_CONSTANT);
		//求解特征值
		Mat tmp;
		//角点检测标准公式
		//Mat response = (Dxx.mul(Dyy) - Dxy.mul(Dxy)) - 0.04 * (Dxx + Dyy).mul(Dxx + Dyy);
		sqrt((Dxx - Dyy).mul(Dxx - Dyy) + 4.0 * Dxy.mul(Dxy), tmp);
		Mat v1x = 2.0 * Dxy;
		Mat v1y = Dyy - Dxx + tmp;
		// Normalize
		Mat mag;
		magnitude(v1x, v1y, mag);
		//for (int i = 0; i < v1x.rows; i++) {
		//	for (int j = 0; j < v1x.cols; j++) {
		//		if (mag.at<double>(i, j) > 0) {
		//			v1x.at<double>(i, j) /= mag.at<double>(i, j);
		//			v1y.at<double>(i, j) /= mag.at<double>(i, j);
		//		}
		//	}
		//}
		v1x /= mag;
		v1y /= mag;
		//第二个特征向量
		Mat v2x = -v1y;
		Mat v2y = v1x;
		// Compute eigenvalues
		Mat Lambda1 = 0.5 * (Dxx + Dyy + tmp);
		Mat Lambda2 = 0.5 * (Dxx + Dyy - tmp);
		// Sort eigenvalues by absolute value
		Mat check = abs(Lambda1) < abs(Lambda2);
		Mat Ix = v1x.clone();
		Mat Iy = v1y.clone();
		v2x.copyTo(Ix, check);
		v2y.copyTo(Iy, check);
		Mat t = -(Dx.mul(Ix) + Dy.mul(Iy)) / (Dxx.mul(Ix.mul(Ix)) + 2.0 * Dxy.mul(Ix.mul(Iy)) + Dyy.mul(Iy.mul(Iy)));
		Mat px = t.mul(Ix);
		Mat py = t.mul(Iy);
		vector<Point2d> laserPoints;
		vector<Point2i> candidatePoints;
		if (parameters.filter) {
			for (int y = 0; y < px.rows; ++y) {
				for (int x = 0; x < px.cols; ++x) {
					if (roiImg.at<double>(y, x) <= parameters.sltThr || visited.at<uchar>(y + roi[0], x + roi[2]) > 0 || bwImg.at<uchar>(y + roi[0], x + roi[2]) == 0)
						//if (roiImg.at<double>(y, x) <= sltThr)
						continue;
					else if (abs(px.at<double>(y, x)) <= 0.5 && abs(py.at<double>(y, x)) <= 0.5) {
						laserPoints.emplace_back(x + px.at<double>(y, x) + roi[2], y + py.at<double>(y, x) + roi[0]);
						candidatePoints.emplace_back(x, y);
						visited.at<uchar>(y + roi[0], x + roi[2]) = 255;
					}
				}
			}
			// 对点进行筛选
			vector<Point2d> filteredPoints;  // 存储筛选后的点
			int n = laserPoints.size();
			vector<bool> processed(n, false); // 标记是否已处理过该点
			for (int i = 0; i < n; i++) {
				//cout << i << "\t";
				if (processed[i])
					continue;  // 如果当前点已处理，则跳过
				else {
					processed[i] = true;  // 标记当前点已处理
				}

				// 当前点的 y 坐标
				int x = candidatePoints[i].x;
				int y = candidatePoints[i].y;
				// 找到所有相邻的y坐标相同的点
				int minIndex = i;
				////按照px+py的进行筛选
				//double value = abs(px.at<double>(y, x)) + abs(py.at<double>(y, x));
				//for (int j = i + 1; j < n; j++) {
				//	if (candidatePoints[j].y != y) {
				//		break;  // 如果下一个点的 y 不相等，直接跳出循环
				//	}
				//	if (candidatePoints[j].y == y && !processed[j]) {
				//		// 选择对应 value 值最小的点
				//		if (abs(px.at<double>(candidatePoints[j].y, candidatePoints[j].x)) + abs(py.at<double>(candidatePoints[j].y, candidatePoints[j].x)) < value) {
				//			minIndex = j;
				//			value = abs(px.at<double>(candidatePoints[j].y, candidatePoints[j].x)) + abs(py.at<double>(candidatePoints[j].y, candidatePoints[j].x));
				//		}
				//	}
				//	processed[j] = true;
				//}
				// 将最小值对应的点加入到结果中

				//按照x的位置进行筛选
				int num = 1;
				for (int j = i + 1; j < n; j++) {
					if (candidatePoints[j].y != y) {
						break;  // 如果下一个点的 y 不相等，直接跳出循环
					}
					if (candidatePoints[j].y == y && !processed[j]) {
						num++;
					}
					processed[j] = true;
				}
				if (num > 2)
					minIndex = i + (num - 1) / 2;
				filteredPoints.push_back(laserPoints[minIndex]);
				//cout << minIndex << "\t" << fixed << setprecision(4) << laserPoints[minIndex].x << "\t" << laserPoints[minIndex].y << endl;
			}
			laserPoints = filteredPoints;
		}
		else {
			for (int y = 0; y < px.rows; ++y) {
				for (int x = 0; x < px.cols; ++x) {
					//cout << x << "\t" << y << endl;
					if (roiImg.at<double>(y, x) <= parameters.sltThr || visited.at<uchar>(y + roi[0], x + roi[2]) > 0 || bwImg.at<uchar>(y + roi[0], x + roi[2]) == 0)
						//if (roiImg.at<double>(y, x) <= sltThr)
						continue;
					else if (abs(px.at<double>(y, x)) <= 0.5 && abs(py.at<double>(y, x)) <= 0.5) {
						laserPoints.emplace_back(x + px.at<double>(y, x) + roi[2], y + py.at<double>(y, x) + roi[0]);
						//candidatePoints.emplace_back(x, y);
						visited.at<uchar>(y + roi[0], x + roi[2]) = 255;
					}
				}
			}
		}
		linePoints.insert(linePoints.end(), laserPoints.begin(), laserPoints.end());
	}
	//cout << " linePoints " << linePoints.size() << endl;
	for (int i = 0; i < linePoints.size(); i++)
	{
		dstPoints.push_back(Eigen::Vector2d(linePoints[i].x + rect.tl().x, linePoints[i].y + rect.tl().y));
	}
	for (int i = 0; i < dstPoints.size(); i++)
	{
		cv::circle(imgLine, Point2f(dstPoints[i][0], dstPoints[i][1]), 1, Scalar(0, 0, 255));
	}
	namedWindow("imgLine", WINDOW_NORMAL);
	imshow("imgLine", imgLine);
	waitKey(1);
	if (parameters.isRemoveEndPoints)
	{
		if (dstPoints.size() > 2 * parameters.RemoveNum) 
		{
			// 去除首尾若干点，减少端点区域的检测波动。
			dstPoints.erase(dstPoints.begin(), dstPoints.begin() + parameters.RemoveNum);
			dstPoints.erase(dstPoints.end() - parameters.RemoveNum, dstPoints.end());
		}
	}
}
