/*
 * This file is part of OpenCorr, an open source C++ library for
 * study and development of 2D, 3D/stereo and volumetric
 * digital image correlation.
 *
 * Copyright (C) 2021-2025, Zhenyu Jiang <zhenyujiang@scut.edu.cn>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one from http://mozilla.org/MPL/2.0/.
 *
 * More information about OpenCorr can be found at https://www.opencorr.org/
 */

#include <fstream>
#include <omp.h>

// 模块概览：读取和存储 2D 图像、3D 体数据和彩色图像。
#include "oc_image.h"

namespace opencorr
{
	// 构造 Image2D 对象，并初始化后续步骤需要的状态。
	Image2D::Image2D(int width, int height)
	{
		cv_mat = cv::Mat::zeros(height, width, CV_8UC1);
		eg_mat = Eigen::MatrixXf::Zero(height, width);
		this->width = width;
		this->height = height;
		size = height * width;
	}

	// 构造 Image2D 对象，并初始化后续步骤需要的状态。
	Image2D::Image2D(std::string file_path) : height(0), width(0)
	{
		load(file_path);
	}

	// 从文件或外部数据源加载数据到当前对象。
	void Image2D::load(std::string file_path)
	{
		// 算法说明：2D 图像先通过 OpenCV 读入以保证兼容性，随后再同步到 Eigen，因为求解器主要在矩阵上工作。
		cv_mat = cv::imread(file_path, cv::IMREAD_GRAYSCALE);

		if (!cv_mat.data)
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("Fail to load file: " + file_path);
		}

		this->file_path = file_path;

		if (width != cv_mat.cols || height != cv_mat.rows)
		{
			width = cv_mat.cols;
			height = cv_mat.rows;
			size = height * width;
			eg_mat.resize(height, width);
		}

		// 实现当前类型中的一个独立处理步骤或辅助过程。
		cv::cv2eigen(cv_mat, eg_mat);
	}



	// 构造 Image3D 对象，并初始化后续步骤需要的状态。
	Image3D::Image3D(int dim_x, int dim_y, int dim_z)
	{
		vol_mat = new3D(dim_z, dim_y, dim_x);
		this->dim_x = dim_x;
		this->dim_y = dim_y;
		this->dim_z = dim_z;
		size = dim_z * dim_y * dim_x;
	}

	// 构造 Image3D 对象，并初始化后续步骤需要的状态。
	Image3D::Image3D(std::string file_path)
	{
		load(file_path);
	}

	// 从文件或外部数据源加载数据到当前对象。
	void Image3D::loadBin(std::string file_path)
	{
		// 算法说明：自定义 bin 格式通过“简单头信息 + 连续 float 数据块”的方式，
		// 针对大体数据提供了更快的加载路径。
		if (vol_mat != nullptr)
		{
			delete3D(vol_mat);
		}

		std::ifstream file_in;
		file_in.open(file_path, std::ios::in | std::ios::binary);

		if (!file_in.is_open())
		{
			std::cerr << "Failed to open bin file: " << file_path << std::endl;
		}

		// 获取数据长度。
		file_in.seekg(0, file_in.end);
		int file_length = file_in.tellg();
		file_in.seekg(0, file_in.beg);
		int data_length = file_length - sizeof(int) * 3;

		// 头信息是一个 `int[3]` 数组，依次表示 x、y、z 三个维度。
		int img_dimension[3];
		file_in.read((char*)img_dimension, sizeof(int) * 3);
		dim_x = img_dimension[0];
		dim_y = img_dimension[1];
		dim_z = img_dimension[2];
		size = dim_z * dim_y * dim_x;

		// 创建 3D 矩阵，并填充二进制文件中的 float 数据。
		vol_mat = new3D(dim_z, dim_y, dim_x);
		file_in.read((char*)**vol_mat, sizeof(float) * size);

		file_in.close();
	}

	// 从文件或外部数据源加载数据到当前对象。
	void Image3D::loadTiff(std::string file_path)
	{
		// 算法说明：支持多页 TIFF 后，就更容易处理来自 CT、显微成像等实验流程
		// 导出的图像栈数据。
		if (vol_mat != nullptr)
		{
			delete3D(vol_mat);
		}

		// 读取多页 TIFF，并把每一页存入 `cv::Mat` 向量。
		std::vector<cv::Mat> tiff_mat;
		if (!cv::imreadmulti(file_path, tiff_mat, cv::IMREAD_GRAYSCALE))
		{
			std::cerr << "Fail to load multi-page tiff: " + file_path << std::endl;
		}

		// 获取 3D 图像的三个维度。
		dim_x = tiff_mat[0].cols;
		dim_y = tiff_mat[0].rows;
		dim_z = (int)tiff_mat.size();
		size = dim_z * dim_y * dim_x;

		// 创建 3D 矩阵，并填充 TIFF 数据。
		vol_mat = new3D(dim_z, dim_y, dim_x);

#pragma omp parallel for
		for (int i = 0; i < dim_z; i++)
		{
			for (int j = 0; j < dim_y; j++)
			{
				for (int k = 0; k < dim_x; k++) {
					vol_mat[i][j][k] = (float)tiff_mat[i].at<uchar>(j, k);
				}
			}
		}
	}

	// 从文件或外部数据源加载数据到当前对象。
	void Image3D::load(std::string file_path)
	{
		// 算法说明：按扩展名分发加载逻辑，可以把底层数据格式差异
		// 从 DVC 其余流程中隔离出去。
		this->file_path = file_path;

		// 判断文件是 bin 还是 tiff。
		size_t dot_pos = file_path.find_last_of(".");
		std::string file_ext = file_path.substr(dot_pos + 1);
		if (file_ext == "bin" || file_ext == "BIN")
		{
			loadBin(file_path);
		}
		else if (file_ext == "tif" || file_ext == "TIF" || file_ext == "tiff" || file_ext == "TIFF")
		{
			loadTiff(file_path);
		}
		else
		{
			std::cerr << "Not binary file or multi-page tiff" << std::endl;
		}
	}

	// 显式释放当前对象持有的大型动态资源。
	void Image3D::release()
	{
		if (vol_mat != nullptr)
		{
			delete3D(vol_mat);
		}
	}


	// 彩色 2D 图像部分。
	ColorfulImage2D::ColorfulImage2D(int width, int height)
	{
		cv_mat = cv::Mat::zeros(height, width, CV_8UC3);
		b_eg_mat = Eigen::MatrixXf::Zero(height, width);
		g_eg_mat = Eigen::MatrixXf::Zero(height, width);
		r_eg_mat = Eigen::MatrixXf::Zero(height, width);
		this->width = width;
		this->height = height;
		size = height * width;
	}

	// 构造 ColorfulImage2D 对象，并初始化后续步骤需要的状态。
	ColorfulImage2D::ColorfulImage2D(std::string file_path)
	{
		load(file_path);
	}

	// 从文件或外部数据源加载数据到当前对象。
	void ColorfulImage2D::load(std::string file_path)
	{
		// 算法说明：加载时一次性拆分通道，
		// 可以避免后续需要单独颜色平面时再重复提取。
		cv_mat = cv::imread(file_path, cv::IMREAD_COLOR);

		if (!cv_mat.data)
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("Fail to load file: " + file_path);
		}

		this->file_path = file_path;

		// 清空旧通道缓存。
		std::vector<cv::Mat>().swap(cv_channels);
		// 拆分 B、G、R 三个颜色通道。
		split(cv_mat, cv_channels);

		if (width != cv_mat.cols || height != cv_mat.rows)
		{
			width = cv_mat.cols;
			height = cv_mat.rows;
			size = height * width;
			b_eg_mat.resize(height, width);
			g_eg_mat.resize(height, width);
			r_eg_mat.resize(height, width);
		}

		// 实现当前类型中的一个独立处理步骤或辅助过程。
		cv::cv2eigen(cv_channels[0], b_eg_mat);
		// 实现当前类型中的一个独立处理步骤或辅助过程。
		cv::cv2eigen(cv_channels[1], g_eg_mat);
		// 实现当前类型中的一个独立处理步骤或辅助过程。
		cv::cv2eigen(cv_channels[2], r_eg_mat);
	}

}//namespace opencorr




