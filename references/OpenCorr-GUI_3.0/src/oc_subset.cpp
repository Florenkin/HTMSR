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

#include <omp.h>

// 模块概览：DIC 和 DVC 中用作相关窗口的局部子区容器。
#include "oc_subset.h"

namespace opencorr
{
	// 构造 Subset2D 对象，并初始化后续步骤需要的状态。
	Subset2D::Subset2D(Point2D center, int radius_x, int radius_y)
	{
		if (radius_x < 1 || radius_y < 1)
		{
			std::cerr << "Too small radius:" << radius_x << ", " << radius_y << std::endl;
		}

		this->center = center;
		this->radius_x = radius_x;
		this->radius_y = radius_y;
		width = radius_x * 2 + 1;
		height = radius_y * 2 + 1;
		size = height * width;

		eg_mat = Eigen::MatrixXf::Zero(height, width);
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	void Subset2D::fill(Image2D* image)
	{
		// 算法说明：fill 会先把局部窗口物化出来，这样后续归一化和残差计算就能在连续致密数据上进行。
		int upperleft_y = center.y - radius_y;
		int upperleft_x = center.x - radius_x;
		eg_mat << image->eg_mat.block(upperleft_y, upperleft_x, height, width);
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	float Subset2D::zeroMeanNorm()
	{
		// 算法说明：减去局部均值可以抑制亮度偏置，返回的范数随后还会用于构造 ZNCC 或 ZNSSD 一类指标。
		float subset_mean = eg_mat.mean();
		eg_mat.array() -= subset_mean;
		float subset_sum = eg_mat.squaredNorm();

		return sqrt(subset_sum);
	}


	// 构造 Subset3D 对象，并初始化后续步骤需要的状态。
	Subset3D::Subset3D(Point3D center, int radius_x, int radius_y, int radius_z)
	{
		if (radius_x < 1 || radius_y < 1 || radius_z < 1)
		{
			std::cerr << "Too small radius:" << radius_x << ", " << radius_y << ", " << radius_z << std::endl;
		}

		if (vol_mat != nullptr)
		{
			delete3D(vol_mat);
		}

		this->center = center;
		this->radius_x = radius_x;
		this->radius_y = radius_y;
		this->radius_z = radius_z;
		dim_x = radius_x * 2 + 1;
		dim_y = radius_y * 2 + 1;
		dim_z = radius_z * 2 + 1;
		size = dim_z * dim_y * dim_x;

		vol_mat = new3D(dim_z, dim_y, dim_x);
	}

	// 释放 Subset3D 持有的缓冲区、计划或动态资源。
	Subset3D::~Subset3D()
	{
		if (vol_mat != nullptr)
		{
			delete3D(vol_mat);
		}
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	void Subset3D::fill(Image3D* image)
	{
		// 算法说明：体数据窗口会复制到专用缓冲区中，因为重复的变形采样在紧凑局部块上更容易处理。
		Point3D start_point(center.x - radius_x, center.y - radius_y, center.z - radius_z);
		for (int i = 0; i < dim_z; i++)
		{
			for (int j = 0; j < dim_y; j++)
			{
				for (int k = 0; k < dim_x; k++)
				{
					vol_mat[i][j][k] = image->vol_mat[int(start_point.z + i)][int(start_point.y + j)][int(start_point.x + k)];
				}
			}
		}
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	float Subset3D::zeroMeanNorm()
	{
		// 算法说明：零均值归一化在 3D 中也起着与 2D 相同的鲁棒性作用。
		// 算法说明：它能让局部匹配对整体亮度偏置不那么敏感。
		// 计算灰度值均值。
		float mean_value = 0;
		for (int i = 0; i < dim_z; i++)
		{
			for (int j = 0; j < dim_y; j++)
			{
				for (int k = 0; k < dim_x; k++)
				{
					mean_value += vol_mat[i][j][k];
				}
			}
		}
		mean_value /= size;

		// 将灰度分布调整为零均值。
		float subset_sum = 0;
		for (int i = 0; i < dim_z; i++)
		{
			for (int j = 0; j < dim_y; j++)
			{
				for (int k = 0; k < dim_x; k++)
				{
					vol_mat[i][j][k] -= mean_value;
					subset_sum += (vol_mat[i][j][k] * vol_mat[i][j][k]);
				}
			}
		}

		return sqrt(subset_sum);
	}

}//namespace opencorr



