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

#ifndef _SUBSET_H_
#define _SUBSET_H_

#include "oc_image.h"
#include "oc_point.h"

// 模块概览：DIC 和 DVC 中用作相关窗口的局部子区容器。
namespace opencorr
{
	// 以 2D POI 为中心的相关子区。
	class Subset2D
	{
	public:
		// 算法说明：子区是相关算法真正优化的局部灰度窗口，
		// 而不是直接在整幅图像上做全局优化。
		Point2D center;
		int radius_x, radius_y;
		int height, width;
		int size;

		Eigen::MatrixXf eg_mat;

		Subset2D(Point2D center, int radius_x, int radius_y);
		~Subset2D() = default;

		void fill(Image2D* image);
		float zeroMeanNorm();
	};

	// 以 3D POI 为中心的相关子区。
	class Subset3D
	{
	public:
		// 算法说明：3D 子区就是 2D 窗口在体数据中的对应物，
		// 它是 DVC 匹配、归一化和残差计算的基本局部单元。
		Point3D center;
		int radius_x, radius_y, radius_z;
		int dim_x, dim_y, dim_z;
		int size;

		float*** vol_mat = nullptr;

		Subset3D(Point3D center, int radius_x, int radius_y, int radius_z);
		~Subset3D();

		void fill(Image3D* image);
		float zeroMeanNorm();
	};

}//namespace opencorr

#endif //_SUBSET_H_



