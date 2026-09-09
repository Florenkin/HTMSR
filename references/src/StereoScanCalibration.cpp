#include"StereoScan.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace std;
using namespace cv;


/*
	函数功能：构造并初始化 StereoScan 双目系统对象，根据配置决定读取历史标定结果还是重新执行标定
	输入：
		cablfile：左相机标定图像目录路径
		cabrfile：右相机标定图像目录路径
		board_size：棋盘格内角点数量
		square_size：棋盘格每个小格的实际尺寸
	输出：
		无（函数执行后会初始化对象内部的左右相机参数、双目外参及相关几何矩阵）
*/
StereoScan::StereoScan(std::string& cablfile, std::string& cabrfile, cv::Size board_size, cv::Size square_size)
{
	// 非标定模式下优先读取历史标定结果，避免重复标定。
	if (!variable.is_calibrate)
	{
		if (!ReadCalibrationFile(variable.calibration_file))
			throw std::runtime_error("Failed to read calibration file: " + variable.calibration_file);

		cout << "Read calibration file: " << variable.calibration_file << endl;
		return;
	}

	// 标定模式下读取左右相机棋盘图像，分别完成单目标定和双目标定。
	cv::Mat K1, K2, D1, D2, R, t, E, F;
	vector<cv::Mat> L_imgs, R_imgs;
	imreadtwo(L_imgs, R_imgs, cablfile, cabrfile, variable.img_num);
	CalibrateOneCamera(L_imgs, board_size, square_size, K1, D1);
	CalibrateOneCamera(R_imgs, board_size, square_size, K2, D2);
	this->K1 = K1;
	this->K2 = K2;
	this->D1 = D1;
	this->D2 = D2;
	double rms = CalibrateTwoCamera(
		L_imgs, R_imgs, board_size, square_size,
		K1, K2, D1, D2,
		R, t, E, F);
	this->R = R;
	this->t = t;
	this->E = E;
	this->F = F;
	this->P1 = K1.clone();
	this->P2 = K2.clone();

	WriteCalibrationFile(variable.calibration_file, rms);
}


/*
	函数功能：将标定得到的左右相机参数和双目几何参数写入 yml 文件
	输入：
		filename：标定结果输出文件名或文件路径
		rms：双目标定的均方根误差
	输出：
		无（函数执行后会在磁盘上生成或覆盖对应的 yml 标定文件）
*/
void StereoScan::WriteCalibrationFile(const std::string& filename, double rms)
{
	// 将标定得到的关键矩阵写入 yml 文件，供后续重建直接使用。
	cv::FileStorage fs(filename, cv::FileStorage::WRITE);
	if (!fs.isOpened())
		throw std::runtime_error("Failed to write calibration file: " + filename);

	fs << "rms" << rms;
	fs << "K1" << K1;
	fs << "D1" << D1;
	fs << "K2" << K2;
	fs << "D2" << D2;
	fs << "P1" << P1;
	fs << "P2" << P2;
	fs << "R" << R;
	fs << "t" << t;
	fs << "E" << E;
	fs << "F" << F;
	fs.release();

	cout << "Write calibration file: " << filename << endl;
}


/*
	函数功能：从 yml 文件中读取左右相机标定参数和双目外参
	输入：
		filename：标定结果文件名或文件路径
	输出：
		返回值：若关键标定参数读取成功且有效则返回 true，否则返回 false
*/
bool StereoScan::ReadCalibrationFile(const std::string& filename)
{
	// 从 yml 文件中恢复左右相机参数以及双目外参。
	cv::FileStorage fs(filename, cv::FileStorage::READ);
	if (!fs.isOpened())
		return false;

	fs["K1"] >> K1;
	fs["D1"] >> D1;
	fs["K2"] >> K2;
	fs["D2"] >> D2;
	fs["P1"] >> P1;
	fs["P2"] >> P2;
	fs["R"] >> R;
	fs["t"] >> t;
	fs["E"] >> E;
	fs["F"] >> F;
	fs.release();

	if (P1.empty())
		P1 = K1.clone();
	if (P2.empty())
		P2 = K2.clone();

	return !K1.empty() && !D1.empty() && !K2.empty() && !D2.empty() && !R.empty() && !t.empty();
}


/*
	函数功能：对单台相机执行标定，求解其内参矩阵和畸变参数，并计算重投影误差
	输入：
		img：该相机拍摄的一组棋盘格标定图像
		board_size：棋盘格内角点数量
		square_size：棋盘格每个小格的实际尺寸
	输出：
		M：标定得到的相机内参矩阵
		D：标定得到的相机畸变参数
		返回值：平均重投影误差，用于评价标定精度
*/
double StereoScan::CalibrateOneCamera(vector<Mat> img, Size board_size, Size square_size, Mat& M, Mat& D)
{
	// 单目标定流程：提取角点、执行标定，并计算重投影误差。
	int img_num = img.size();
	Size image_size;
	vector<Point2f> image_points_buf;
	vector<vector<Point2f>> image_points_seq;
	for (int i = 0; i < img_num; i++)
	{
		Mat imageInput = img[i];

		//equalizeHist(imageInput, imageInput);
		if (i == 0)  //读入第一张图片时获取图像宽高信息
		{
			image_size.width = imageInput.cols;
			image_size.height = imageInput.rows;
			cout << "image_size.width = " << image_size.width << endl;
			cout << "image_size.height = " << image_size.height << endl;
		}
		namedWindow("imageInput", WINDOW_NORMAL);
		imshow("imageInput", imageInput);//显示图片
		waitKey(20);//暂停0.1S		

		/* 提取角点 */
		if (
			//0 == findChessboardCorners(imageInput, board_size, image_points_buf, CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE)
			0 == findChessboardCornersSB(imageInput, board_size, image_points_buf, cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY)
			)
		{
			cout << "Image" << i + 1 << " can not find chessboard corners!\n"; //找不到角点
			waitKey(20);
			continue;
		}
		else
		{
			cout << "Image" << i + 1 << " can find chessboard corners!\n"; //找到角点
			/* 亚像素精确化 */
			// 对粗提取的角点继续做亚像素优化，提高标定精度。
			//find4QuadCornerSubpix(imageInput, image_points_buf, Size(5, 5)); //对粗提取的角点进行精确化
			cornerSubPix(imageInput, image_points_buf, Size(15, 15), Size(-1, -1), TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));
			image_points_seq.push_back(image_points_buf);  //保存亚像素角点
			/* 在图像上显示角点位置 */
			drawChessboardCorners(imageInput, board_size, image_points_buf, true); //用于在图片中标记角点
			namedWindow("Camera Calibration", WINDOW_NORMAL);
			imshow("Camera Calibration", imageInput);//显示图片
			waitKey(100);//暂停0.5S		
		}
	}

	//以下是摄像机标定
	cout << "Start one camera calibration" << endl;
	/*棋盘三维信息*/
	vector<vector<Point3f>> object_points; /* 保存标定板上角点的三维坐标 */
	/*内外参数*/
	Mat cameraMatrix = Mat(3, 3, CV_32FC1, Scalar::all(0)); /* 摄像机内参数矩阵 */
	vector<int> point_counts;  // 每幅图像中角点的数量
	Mat distCoeffs = Mat(1, 5, CV_32FC1, Scalar::all(0)); /* 摄像机的5个畸变系数：k1,k2,p1,p2,k3 */
	vector<Mat> tvecsMat;  /* 每幅图像的旋转向量 */
	vector<Mat> rvecsMat; /* 每幅图像的平移向量 */
	/* 初始化标定板上角点的三维坐标 */
	int i, j, t;
	for (t = 0; t < img_num; t++)
	{
		vector<Point3f> tempPointSet;
		for (i = 0; i < board_size.height; i++)
		{
			for (j = 0; j < board_size.width; j++)
			{
				Point3f realPoint;
				/* 假设标定板放在世界坐标系中z=0的平面上 */
				realPoint.x = i * square_size.width;
				realPoint.y = j * square_size.height;
				realPoint.z = 0;
				tempPointSet.push_back(realPoint);
			}
		}
		object_points.push_back(tempPointSet);
	}
	/* 初始化每幅图像中的角点数量，假定每幅图像中都可以看到完整的标定板 */
	for (i = 0; i < img_num; i++)
	{
		point_counts.push_back(board_size.width * board_size.height);
	}
	/* 开始标定 */
	double error = calibrateCamera(object_points, image_points_seq, image_size, cameraMatrix, distCoeffs, rvecsMat, tvecsMat, 0);
	cout << "标定完成！\n";
	//cout << " error " << error;

	//对标定结果进行评价
	cout << "开始评价标定结果………………\n";
	//double total_err = 0.0; /* 所有图像的平均误差的总和 */
	//double err = 0.0; /* 每幅图像的平均误差 */
	vector<Point2f> image_points2; /* 保存重新计算得到的投影点 */
	cout << "\t每幅图像的标定误差：\n";
	double err = 0;
	for (int i = 0; i < img_num; i++)
	{
		vector<Point3f> tempPointSet = object_points[i];
		projectPoints(tempPointSet, rvecsMat[i], tvecsMat[i], cameraMatrix, distCoeffs, image_points2);
		vector<Point2f> tempImagePoint = image_points_seq[i];
		double erri = 0;
		for (int j = 0; j < tempImagePoint.size(); j++)
		{
			Point2f temp_point = image_points2[j] - tempImagePoint[j];
			double errij = sqrt(temp_point.x * temp_point.x + temp_point.y * temp_point.y);
			erri = erri + errij;
		}
		err = err + erri;
		erri = erri / tempImagePoint.size();
		std::cout << "Image " << i + 1 << " average error: " << erri << " pixel" << endl;
	}
	err = err / (img_num * object_points[0].size());
	std::cout << "Total average error: " << err << " pixel" << endl;
	std::cout << "Evaluation done" << endl;

	M = cameraMatrix.clone();
	D = distCoeffs.clone();
	return err;
}


/*
	函数功能：在左右相机单目标定结果已知的基础上，执行双目标定并求解双目外参与极线几何参数
	输入：
		img_left：左相机拍摄的一组棋盘格标定图像
		img_right：右相机拍摄的一组棋盘格标定图像
		board_size：棋盘格内角点数量
		square_size：棋盘格每个小格的实际尺寸
		M11：左相机内参矩阵
		M22：右相机内参矩阵
		D11：左相机畸变参数
		D22：右相机畸变参数
	输出：
		RR：左右相机之间的旋转矩阵
		tt：左右相机之间的平移向量
		EE：本质矩阵
		FF：基础矩阵
		返回值：双目标定的 RMS 误差
*/
double StereoScan::CalibrateTwoCamera(vector<Mat> img_left, vector<Mat> img_right, Size board_size, Size square_size,
	Mat M11, Mat M22, Mat D11, Mat D22,Mat& RR, Mat& tt, Mat& EE, Mat& FF)
{
	// 双目标定流程：基于左右图角点求解双目外参与极线约束。
	int nframes = img_left.size();//有几对图片
	Size imageSize = img_left[0].size();
	int CornerNum = board_size.width * board_size.height;//角点个数
	int N = board_size.width * board_size.height;

	vector<vector<Point3f>> objectPoints;//记录左右眼所有的角点的实际位置
	vector<vector<Point2f>> pointsl, pointsr;//记录左右眼所有角点的像素坐标

	for (int k = 0; k < img_left.size(); k++)
	{
		vector<Point3f> tempPointSet;//缓存棋盘格点的世界坐标
		for (int i = 0; i < board_size.height; i++)
		{
			for (int j = 0; j < board_size.width; j++)
			{
				Point3f realPoint;
				//假设标定板放在世界坐标系中z=0的平面上
				realPoint.x = j * square_size.width;
				realPoint.y = i * square_size.height;
				realPoint.z = 0;
				tempPointSet.push_back(realPoint);
				//cout << realPoint << " ";
			}
		}
		objectPoints.push_back(tempPointSet);
	}

	for (int i = 0; i < img_left.size(); i++)
	{
		if (img_left[i].channels() > 1)
			cvtColor(img_left[i], img_left[i], COLOR_BGR2GRAY);
		//namedWindow("grayimg_lefti", WINDOW_NORMAL);
		///imshow("grayimg_lefti", img_left[i]);
		//waitKey(1);
		if (img_right[i].channels() > 1)
			cvtColor(img_right[i], img_right[i], COLOR_BGR2GRAY);

		vector<Point2f> image_points_bufl, image_points_bufr;
		bool foundl = findChessboardCornersSB(img_left[i], board_size, image_points_bufl, cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
		bool foundr = findChessboardCornersSB(img_right[i], board_size, image_points_bufr, cv::CALIB_CB_EXHAUSTIVE | cv::CALIB_CB_ACCURACY);
		//bool foundl = findChessboardCorners(img_left[i], board_size, image_points_bufl, CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE);
		//bool foundr = findChessboardCorners(img_right[i], board_size, image_points_bufr, CALIB_CB_ADAPTIVE_THRESH | CALIB_CB_NORMALIZE_IMAGE);

		if (foundl && foundr)
		{
			cout << "Image " << i + 1 << " found corners" << endl;
			Mat view_grayl = img_left[i].clone();
			cornerSubPix(view_grayl, image_points_bufl, Size(15, 15), Size(-1, -1), TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));
			//find4QuadCornerSubpix(view_grayl, image_points_bufl, Size(11, 11)); //对粗提取的角点进行精确化
			drawChessboardCorners(view_grayl, board_size, image_points_bufl, true); //用于在图片中标记角点
			namedWindow("Cornersl", WINDOW_NORMAL);
			imshow("Cornersl", view_grayl);//显示图片
			waitKey(10);//暂停0.5S

			Mat view_grayr = img_right[i].clone();
			cornerSubPix(view_grayr, image_points_bufr, Size(15, 15), Size(-1, -1), TermCriteria(TermCriteria::EPS + TermCriteria::COUNT, 30, 0.1));
			//find4QuadCornerSubpix(view_grayr, image_points_bufr, Size(11, 11)); //对粗提取的角点进行精确化
			// 在图像上显示角点位置 
			drawChessboardCorners(view_grayr, board_size, image_points_bufr, true); //用于在图片中标记角点
			namedWindow("Cornersr", WINDOW_NORMAL);
			imshow("Cornersr", view_grayr);//显示图片
			waitKey(10);//暂停0.5S	

			pointsl.push_back(image_points_bufl);
			pointsr.push_back(image_points_bufr);
		}
		else
		{
			cout << "Image " << i << " corner extraction failed" << endl;
		}
	}

	cout << "Start stereo calibration" << endl;
	cout << " pointsl.size() " << pointsl.size() << " pointsr.size() " << pointsr.size() << " objectPoints.size() " << objectPoints.size() << endl;
	double rms = stereoCalibrate(objectPoints, pointsl, pointsr,
		M11, D11,
		M22, D22,
		imageSize, RR, tt, EE, FF,
		CALIB_FIX_INTRINSIC,
		//CALIB_USE_INTRINSIC_GUESS,
		//0,
		TermCriteria(TermCriteria::COUNT, 30, 1e-6));

	cout << "双目标定结束" << endl;
	cout << "done with RMS error=" << rms << endl;
	cout << " M11 " << endl << M11 << endl;
	cout << " D11 " << endl << D11 << endl;
	cout << " M22 " << endl << M22 << endl;
	cout << " D22 " << endl << D22 << endl;
	cout << " RR " << endl << RR << endl;
	cout << " tt " << endl << tt << endl;
	cout << " EE " << endl << EE << endl;
	cout << " FF " << endl << FF << endl;

	return rms;
}
