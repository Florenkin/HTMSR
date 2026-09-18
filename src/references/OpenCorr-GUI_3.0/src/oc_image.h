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

#pragma once

#ifndef _IMAGE_H_
#define _IMAGE_H_

#include <Eigen>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>

// 模块概览：读取和存储 2D 图像、3D 体数据和彩色图像。
#include "oc_array.h"

namespace opencorr
{
	// 同时以 OpenCV 和 Eigen 两种形式存储灰度图像，供后续处理使用。
	class Image2D
	{
	public:
		// 算法说明：同时保留 OpenCV 和 Eigen 两种形式，
		// 可以兼顾特征提取流程与基于矩阵的数值求解效率。
		int height, width;
		unsigned int size;

		std::string file_path;

		cv::Mat cv_mat;
		Eigen::MatrixXf eg_mat;

		Image2D(int width, int height);
		Image2D(std::string file_path);
		~Image2D() = default;

		void load(std::string file_path);
	};

	// 存储从项目二进制格式或多页 TIFF 文件加载的 3D 体数据。
	class Image3D
	{
	public:
		// 算法说明：体数据以原始 float 网格形式存储，
		// 因为插值和梯度计算都需要直接访问体素值。
		int dim_x, dim_y, dim_z;
		unsigned long size;

		std::string file_path;

		float*** vol_mat = nullptr;

		Image3D(int dim_x, int dim_y, int dim_z);
		Image3D(std::string file_path);
		~Image3D() = default;

		void loadBin(std::string file_path);
		void loadTiff(std::string file_path);
		void load(std::string file_path);

		void release();
	};

	// 处理彩色图像的辅助类。
	class ColorfulImage2D
	{
	public:
		// 算法说明：颜色通道会被分别保留，
		// 方便后续模块按通道分析纹理或生成可视化结果。
		int height, width;
		unsigned int size;

		std::string file_path;

		cv::Mat cv_mat;
		std::vector<cv::Mat> cv_channels;
		Eigen::MatrixXf b_eg_mat, g_eg_mat, r_eg_mat;

		ColorfulImage2D(int width, int height);
		ColorfulImage2D(std::string file_path);
		~ColorfulImage2D() = default;

		void load(std::string file_path);
	};

}//namespace opencorr

#endif //_IMAGE_H_



