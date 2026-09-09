#include "StereoScan.h"
#include <algorithm>
#include <iostream>


using namespace std;
using namespace cv;
using namespace Eigen;



/*
	函数功能：对输入图像进行预处理，包括阈值分割、轮廓筛选、区域平滑，并输出候选区域范围
	输入：
		img：输入图像
		thre：二值化阈值
	输出：
		dstImg：预处理后的图像结果
		dstRanges：候选激光区域范围，每个元素格式为 (top, bottom, left, right)
*/
void StereoScan::imgPreProcess(cv::Mat img, int thre, cv::Mat& dstImg, std::vector<cv::Vec4i>& dstRanges)
{
	// 图像预处理：阈值分割、轮廓筛选、区域平滑，并输出候选范围。
	Mat proImg = img.clone();
	Mat bwImg;
	if (proImg.channels() > 1)
	{
		threshold_RGB(proImg, thre, "BLUE", bwImg);
		cvtColor(proImg, proImg, COLOR_BGR2GRAY);
		//namedWindow("proImg", WINDOW_NORMAL);
		//imshow("proImg", proImg);
		//waitKey(100);
		//cout << proImg << endl;
	}
	else
	{
		threshold(proImg, bwImg, thre, 255, THRESH_BINARY);
	}
	//cout << "otsu: " << imgp.GetOtsuThresh(proImg) << endl;
	//threshold(proImg, bwImg, thre, 20, THRESH_BINARY);//50,100,GetOtsuThresh(img),阈值
	//threshold_RGB(proImg, 150, "BLUE", bwImg);
	//namedWindow("bwImg", WINDOW_NORMAL);
	//imshow("bwImg", bwImg);
	//waitKey(0);
	//找出各个边缘
	std::vector<std::vector<cv::Point>> contours;
	std::vector<std::vector<cv::Point>> contoursOr;
	std::vector<std::vector<cv::Point>>::iterator it;
	std::vector<cv::Vec4i>hierarchy;
	cv::findContours(bwImg, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
	contoursOr = contours;
	vector<vector<Point2i>> finalContourSet;
	double areaThr = 10;//50,200
	Mat Temp = cv::Mat::zeros(bwImg.rows, bwImg.cols, bwImg.type());
	vector<Vec4i> ranges;
	//cout << " contours.size() " << contours.size() << endl;
	for (int i = 0; i < contours.size(); i++)
	{
		double area = contourArea(contours[i]);
		//cout << " area " << area << endl;
		if (area > areaThr)//面积小于 areaThr 就不要
		{
			//cout << "area:  " << area << endl;
			finalContourSet.push_back(contours[i]);
			Point2i leftP = *min_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			Point2i RightP = *max_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			Point2i topP = *min_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			Point2i botP = *max_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			//cout << "leftP: " << leftP << endl;
			//cout << "RightP: " << RightP << endl;
			//cout << "topP: " << topP << endl;
			//cout << "botP: " << botP << endl;
			int left = 1; int right = img.cols - 1; int r = 5;
			if (leftP.x - r >= 1)
				left = leftP.x - r;
			else
				left = 1;
			if (RightP.x + r <= img.cols - 1)
				right = RightP.x + r;
			else
				right = img.cols - 1;
			int top = topP.y;
			int bot = botP.y;

			//sort(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			//int left = contours[i][0].x;
			//int right = contours[i][contours[i].size() - 1].x;
			//sort(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			//int top = contours[i][0].y;
			//int bot = contours[i][contours[i].size() - 1].y;

			//if (bot - top < (right - left) * 1.0)
			//	continue;
			cv::drawContours(Temp, contoursOr, i, cv::Scalar(255), cv::FILLED);
			namedWindow("Temp", WINDOW_NORMAL);
			resizeWindow("Temp", 1200, 800);
			imshow("Temp", Temp);
			waitKey(1);
			ranges.push_back(Vec4i(top, bot, left, right));
		}
	}
	morphologyEx(Temp, Temp, MORPH_OPEN, getStructuringElement(MORPH_RECT, Size(3, 3)));
	domainSmooth(Temp, Temp);//区域平滑
	Mat dstImg0 = proImg.mul(Temp / 255);
	GaussianBlur(dstImg0, dstImg0, Size(0, 0), 2, 2);
	//namedWindow("dstImg", WINDOW_NORMAL);
	//imshow("dstImg", dstImg0);
	//waitKey(1);
	dstImg = dstImg0.clone();
	dstRanges = ranges;
}


/*
	函数功能：在输入图像中提取可能包含激光条纹的候选区域范围
	输入：
		proImg：输入图像，函数内部可能将其转换为指定颜色通道灰度图
		areaThr：轮廓面积阈值，小于该值的区域会被忽略
		borderexten：候选区域边界向外扩展的像素数
	输出：
		dstRanges：候选激光区域范围，每个元素格式为 (top, bottom, left, right)
*/
void StereoScan::GetImageRanges(cv::Mat& proImg, std::vector<cv::Vec4i>& dstRanges, double areaThr, int borderexten)
{
	Mat bwImg;
	// 提取激光灰度图并二值化，可按 BGR 通道提取或直接二值化。
	if (proImg.channels() > 1)
	{
		threshold_RGB(proImg, parameters.bwThr, parameters.Color_Laser, bwImg);
		//cvtColor(proImg, proImg, COLOR_BGR2GRAY);
		RGBtoGray(proImg, parameters.Color_Laser, proImg);
		//namedWindow("proImg", WINDOW_NORMAL);
		//imshow("proImg", proImg);
		//waitKey(0);
		//cout << " proImg " << endl << proImg << endl;
	}
	else
	{
		threshold(proImg, bwImg, parameters.bwThr, 255, THRESH_BINARY);
	}
	//namedWindow("bwImg", WINDOW_NORMAL);
	//imshow("bwImg", bwImg);
	//waitKey(1);

	// 开运算，减少噪声影响。
	morphologyEx(bwImg, bwImg, MORPH_OPEN, getStructuringElement(MORPH_RECT, Size(3, 3)));
	//找出各个边缘
	std::vector<std::vector<cv::Point>> contours;
	std::vector<std::vector<cv::Point>> contoursOr;
	std::vector<std::vector<cv::Point>>::iterator it;
	std::vector<cv::Vec4i>hierarchy;
	cv::findContours(bwImg, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
	contoursOr = contours;
	//vector<vector<Point2i>> finalContourSet;
	Mat Temp = cv::Mat::zeros(bwImg.rows, bwImg.cols, bwImg.type());
	vector<Vec4i> ranges;
	for (int i = 0; i < contours.size(); i++)
	{
		double area = contourArea(contours[i]);
		if (area > areaThr)//面积小于 areaThr 就不要
		{
			//cout << "area:  " << area << endl;
			//finalContourSet.push_back(contours[i]);
			Point2i leftP = *min_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			Point2i RightP = *max_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			Point2i topP = *min_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			Point2i botP = *max_element(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			//cout << "leftP: " << leftP << endl;
			//cout << "RightP: " << RightP << endl;
			//cout << "topP: " << topP << endl;
			//cout << "botP: " << botP << endl;
			int left = leftP.x;
			int right = RightP.x;
			int top = topP.y;
			int bot = botP.y;
			// 边界扩展
			if (left - borderexten < 0)
				left = 0;
			else
				left -= borderexten;
			if (top - borderexten < 0)
				top = 0;
			else
				top -= borderexten;
			if (right + borderexten > bwImg.cols - 1)
				right = bwImg.cols - 1;
			else
				right += borderexten;
			if (bot + borderexten > bwImg.rows - 1)
				bot = bwImg.rows - 1;
			else
				bot += borderexten;

			//sort(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.x < b.x; });
			//int left = contours[i][0].x;
			//int right = contours[i][contours[i].size() - 1].x;
			//sort(contours[i].begin(), contours[i].end(), [&](Point2i a, Point2i b) {return a.y < b.y; });
			//int top = contours[i][0].y;
			//int bot = contours[i][contours[i].size() - 1].y;

			//if (bot - top < (right - left) * 1.0)
			//	continue;
			//cv::drawContours(Temp, contoursOr, i, cv::Scalar(255), cv::FILLED);
			//namedWindow("Temp", WINDOW_NORMAL);
			//imshow("Temp", Temp);
			//waitKey(0);
			ranges.push_back(Vec4i(top, bot, left, right));

		}
	}
	dstRanges = ranges;
}


/*
	函数功能：按照指定颜色通道对图像进行阈值分割，生成二值图像
	输入：
		srcImg：输入彩色图像
		value：阈值
		color：指定颜色通道，可为 "BLUE"、"GREEN" 或默认按红色通道处理
	输出：
		dstImg：输出二值图像，满足阈值条件的像素置为 255，否则置为 0
*/
void StereoScan::threshold_RGB(Mat srcImg, int value, string color, Mat& dstImg)
{
	// 按指定颜色通道进行阈值分割，用于提取激光条纹区域。
	if (srcImg.channels() > 1)
	{
		cvtColor(srcImg, dstImg, COLOR_BGR2GRAY);
	}
	for (int i = 0; i < srcImg.rows; i++)
	{
		for (int j = 0; j < srcImg.cols; j++)
		{
			if (color == "BLUE")
			{
				if (srcImg.at<Vec3b>(i, j)[0] > value)
				{
					dstImg.at<uchar>(i, j) = 255;
				}
				else
				{
					dstImg.at<uchar>(i, j) = 0;
				}
			}
			else if (color == "GREEN")
			{
				if (srcImg.at<Vec3b>(i, j)[1] > value)
				{
					dstImg.at<uchar>(i, j) = 255;
				}
				else
				{
					dstImg.at<uchar>(i, j) = 0;
				}
			}
			else
			{
				if (srcImg.at<Vec3b>(i, j)[2] > value)
				{
					dstImg.at<uchar>(i, j) = 255;
				}
				else
				{
					dstImg.at<uchar>(i, j) = 0;
				}
			}
		}
	}
}


/*
	函数功能：从彩色图像中提取指定颜色通道，并输出为单通道灰度图
	输入：
		srcImg：输入彩色图像
		color：指定颜色通道，可为 "BLUE"、"GREEN" 或 "RED"
	输出：
		dstImg：对应颜色通道的单通道灰度图
*/
void StereoScan::RGBtoGray(Mat srcImg, string color, Mat& dstImg)
{
	// 提取指定颜色通道作为灰度图。
	vector<Mat> channels;
	split(srcImg, channels);
	if (color == "BLUE")
	{
		dstImg = channels[0];
	}
	if (color == "GREEN")
	{
		dstImg = channels[1];
	}
	if (color == "RED")
	{
		dstImg = channels[2];
	}
}


/*
	函数功能：对二值区域图进行平滑处理，减弱孤立毛刺、边缘噪声和小范围不连续
	输入：
		srcImg：输入的二值区域图
	输出：
		dstImg：平滑处理后的二值区域图
*/
void StereoScan::domainSmooth(Mat srcImg, Mat& dstImg)
{
	// 对二值区域做简单平滑，减弱孤立毛刺和边缘噪声。
	//cv::imshow("srcImg", srcImg);
	//imwrite("src.png", srcImg);
	//waitKey(5);
	Mat orImg = srcImg.clone();
	int sizeHalf = 3;
	dilate(orImg, orImg, getStructuringElement(MORPH_RECT, Size(sizeHalf, sizeHalf)));
	Mat temp = orImg.clone();

	Mat filMat = Mat::ones(2 * sizeHalf + 1, 2 * sizeHalf + 1, CV_64F);
	filMat /= (2 * sizeHalf + 1) * (2 * sizeHalf + 1);
	filter2D(orImg, temp, CV_8U, filMat);
	temp = temp >= 255 / 2;
	erode(temp, temp, getStructuringElement(MORPH_RECT, Size(sizeHalf, sizeHalf)));
	dstImg = temp.clone();
	//cv::imshow("or", orImg);
	//cv::imshow("temp", temp);
	//waitKey(5);
}
