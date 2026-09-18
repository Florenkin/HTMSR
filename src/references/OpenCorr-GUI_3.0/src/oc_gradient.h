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

#ifndef _GRADIENT_H_
#define _GRADIENT_H_

#include "oc_image.h"

// 模块概览：为迭代求解器提供图像和体数据梯度计算。
namespace opencorr
{
	// 本模块实现参考以下文献。
	//B. Fornberg, Mathematics of Computation (1988) 184(51): 699-706.
	//https://doi.org/10.1090/S0025-5718-1988-0935077-0

	class Gradient2D4
	{
	protected:
		// 算法说明：迭代求解器依赖图像梯度来构建 Jacobian、Hessian
		// 以及最速下降图像，它们共同决定每一步参数更新。
		Image2D* grad_img = nullptr;

	public:
		Eigen::MatrixXf gradient_x;
		Eigen::MatrixXf gradient_y;
		Eigen::MatrixXf gradient_xy;

		Gradient2D4(Image2D& image);
		~Gradient2D4();

		void setImage(Image2D& image); //set image to process

		void getGradientX(); //create an array of gradient_x
		void getGradientY(); //create an array of gradient_y
		void getGradientXY(); //create an array of gradient_xy
	};

	// 为 3D 体数据计算一阶梯度。
	class Gradient3D4
	{
	protected:
		// 算法说明：3D 梯度场在 DVC 中承担着与 2D 梯度在 DIC 中相同的职责，
		// 只是现在需要同时覆盖三个空间方向。
		Image3D* grad_img = nullptr;

	public:
		float*** gradient_x = nullptr;
		float*** gradient_y = nullptr;
		float*** gradient_z = nullptr;

		Gradient3D4(Image3D& image);
		~Gradient3D4();

		void clear(); //clear all data
		void setImage(Image3D& image); //set image to process

		void getGradientX(); //create an array of gradient_x
		void getGradientY(); //create an array of gradient_y
		void getGradientZ(); //create an array of gradient_z
	};

}//namespace opencorr

#endif //_GRADIENT_H_



