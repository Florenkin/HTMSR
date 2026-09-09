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

// 模块概览：为迭代求解器提供图像和体数据梯度计算。
#include "oc_gradient.h"

namespace opencorr
{
	const float first_factor = 1.f / 12.f;
	const float second_factor = 2.f / 3.f;

	// 一阶导数，四阶精度。
	Gradient2D4::Gradient2D4(Image2D& image)
	{
		grad_img = &image;
	}

	// 释放 Gradient2D4 持有的缓冲区、计划或动态资源。
	Gradient2D4::~Gradient2D4() {}

	// 更新当前对象使用的配置、输入或运行时状态。
	void Gradient2D4::setImage(Image2D& image)
	{
		grad_img = &image;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient2D4::getGradientX()
	{
		// 算法说明：四阶有限差分模板能够在不过度扩大邻域的前提下提升梯度精度，
		// 兼顾数值稳定性和局部性。
		int height = grad_img->height;
		int width = grad_img->width;

		gradient_x = Eigen::MatrixXf::Zero(height, width);

#pragma omp parallel for
		for (int r = 0; r < height; r++)
		{
			for (int c = 2; c < width - 2; c++)
			{
				float result = 0.0f;
				result -= grad_img->eg_mat(r, c + 2) * first_factor;
				result += grad_img->eg_mat(r, c + 1) * second_factor;
				result -= grad_img->eg_mat(r, c - 1) * second_factor;
				result += grad_img->eg_mat(r, c - 2) * first_factor;
				gradient_x(r, c) = result;
			}
		}
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient2D4::getGradientY()
	{
		// 算法说明：x 和 y 方向导数分别计算，这样求解器就能按各自需求
		// 组合出对应 warp 模型所需的 Jacobian 形式。
		int height = grad_img->height;
		int width = grad_img->width;

		gradient_y = Eigen::MatrixXf::Zero(height, width);

#pragma omp parallel for
		for (int r = 2; r < height - 2; r++)
		{
			for (int c = 0; c < width; c++)
			{
				float result = 0.0f;
				result -= grad_img->eg_mat(r + 2, c) * first_factor;
				result += grad_img->eg_mat(r + 1, c) * second_factor;
				result -= grad_img->eg_mat(r - 1, c) * second_factor;
				result += grad_img->eg_mat(r - 2, c) * first_factor;
				gradient_y(r, c) = result;
			}
		}
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient2D4::getGradientXY()
	{
		// 算法说明：混合导数主要服务于高阶模型以及需要曲率信息的后处理步骤。
		int height = grad_img->height;
		int width = grad_img->width;

		gradient_xy = Eigen::MatrixXf::Zero(height, width);

		if (gradient_x.rows() != height || gradient_x.cols() != width)
		{
			getGradientX();
		}

#pragma omp parallel for
		for (int r = 2; r < height - 2; r++)
		{
			for (int c = 0; c < width; c++)
			{
				float result = 0.0f;
				result -= gradient_x(r + 2, c) * first_factor;
				result += gradient_x(r + 1, c) * second_factor;
				result -= gradient_x(r - 1, c) * second_factor;
				result += gradient_x(r - 2, c) * first_factor;
				gradient_xy(r, c) = result;
			}
		}
	}


	// 3D 情况下的一阶导数，四阶精度。
	Gradient3D4::Gradient3D4(Image3D& image)
	{
		grad_img = &image;
	}

	// 释放 Gradient3D4 持有的缓冲区、计划或动态资源。
	Gradient3D4::~Gradient3D4()
	{
		clear();
	}

	// 在保留点位置或可复用结构的前提下，重置缓存结果值。
	void Gradient3D4::clear()
	{
		if (gradient_x != nullptr)
		{
			delete3D(gradient_x);
		}

		if (gradient_y != nullptr)
		{
			delete3D(gradient_y);
		}

		if (gradient_z != nullptr)
		{
			delete3D(gradient_z);
		}
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void Gradient3D4::setImage(Image3D& image)
	{
		grad_img = &image;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient3D4::getGradientX()
	{
		// 算法说明：同样的四阶差分思想被逐轴扩展到了 3D 体数据，
		// 使体相关求解器能够获得更平滑且更准确的梯度场。
		int dim_x = grad_img->dim_x;
		int dim_y = grad_img->dim_y;
		int dim_z = grad_img->dim_z;

		if (gradient_x != nullptr)
		{
			delete3D(gradient_x);
		}
		gradient_x = new3D(dim_z, dim_y, dim_x);

#pragma omp parallel for
		for (int i = 0; i < dim_z; i++)
		{
			for (int j = 0; j < dim_y; j++)
			{
				for (int k = 2; k < dim_x - 2; k++)
				{
					float result = 0.0f;
					result -= grad_img->vol_mat[i][j][k + 2] * first_factor;
					result += grad_img->vol_mat[i][j][k + 1] * second_factor;
					result -= grad_img->vol_mat[i][j][k - 1] * second_factor;
					result += grad_img->vol_mat[i][j][k - 2] * first_factor;
					gradient_x[i][j][k] = result;
				}
			}
		}
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient3D4::getGradientY()
	{
		int dim_x = grad_img->dim_x;
		int dim_y = grad_img->dim_y;
		int dim_z = grad_img->dim_z;

		if (gradient_y != nullptr)
		{
			delete3D(gradient_y);
		}
		gradient_y = new3D(dim_z, dim_y, dim_x);

#pragma omp parallel for
		for (int k = 0; k < dim_x; k++)
		{
			for (int i = 0; i < dim_z; i++)
			{
				for (int j = 2; j < dim_y - 2; j++)
				{
					float result = 0.0f;
					result -= grad_img->vol_mat[i][j + 2][k] * first_factor;
					result += grad_img->vol_mat[i][j + 1][k] * second_factor;
					result -= grad_img->vol_mat[i][j - 1][k] * second_factor;
					result += grad_img->vol_mat[i][j - 2][k] * first_factor;
					gradient_y[i][j][k] = result;
				}
			}
		}
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	void Gradient3D4::getGradientZ()
	{
		int dim_x = grad_img->dim_x;
		int dim_y = grad_img->dim_y;
		int dim_z = grad_img->dim_z;

		if (gradient_z != nullptr)
		{
			delete3D(gradient_z);
		}
		gradient_z = new3D(dim_z, dim_y, dim_x);

#pragma omp parallel for
		for (int j = 0; j < dim_y; j++)
		{
			for (int k = 0; k < dim_x; k++)
			{
				for (int i = 2; i < dim_z - 2; i++)
				{
					float result = 0.0f;
					result -= grad_img->vol_mat[i + 2][j][k] * first_factor;
					result += grad_img->vol_mat[i + 1][j][k] * second_factor;
					result -= grad_img->vol_mat[i - 1][j][k] * second_factor;
					result += grad_img->vol_mat[i - 2][j][k] * first_factor;
					gradient_z[i][j][k] = result;
				}
			}
		}
	}

} //namespcae opencorr



