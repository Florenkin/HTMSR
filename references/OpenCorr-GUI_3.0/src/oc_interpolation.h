/*
 * This file is part of OpenCorr, an open source C++ library for
 * study and development of 2D, 3D/stereo and volumetric
 * digital image correlation.
 *
 * Copyright (C) 2021-2024, Zhenyu Jiang <zhenyujiang@scut.edu.cn>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one from http://mozilla.org/MPL/2.0/.
 *
 * More information about OpenCorr can be found at https://www.opencorr.org/
 */

#pragma once

#ifndef _INTERPOLATION_H_
#define _INTERPOLATION_H_

#include "oc_image.h"
#include "oc_point.h"

// 模块概览：2D 和 3D 插值后端的通用接口。
namespace opencorr
{
	// 2D 插值接口。
	class Interpolation2D
	{
	protected:
		// 算法说明：迭代相关依赖稳定的亚像素灰度求值，
		// 因此所有具体插值后端都遵循这一套最小接口。
		Image2D* interp_img = nullptr;

	public:
		int width, height;
		virtual ~Interpolation2D() = default;

		virtual void prepare() = 0;
		virtual float compute(Point2D& location) = 0;
	};

	// 3D 插值接口。
	class Interpolation3D
	{
	protected:
		// 算法说明：3D 接口存在的原因与 2D 相同，
		// 只是服务对象变成了体相关中的亚体素采样。
		Image3D* interp_img = nullptr;

	public:
		int dim_x, dim_y, dim_z;
		virtual ~Interpolation3D() = default;

		virtual void prepare() = 0;
		virtual float compute(Point3D& location) = 0;
	};

}//namespace opencorr

#endif //_INTERPOLATION_H_



