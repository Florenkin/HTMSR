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

#ifndef _FEATURE_H_
#define _FEATURE_H_

#include <opencv2/features2d.hpp>

// 模块概览：2D 和 3D 特征提取与匹配模块的通用接口。
#include "oc_image.h"
#include "oc_point.h"

namespace opencorr
{
	// 2D 特征提取与匹配模块的抽象接口。
	class Feature2D
	{
	protected:
		// 算法说明：基于特征的匹配会提供稀疏对应点，
		// 这类对应点通常比单纯的局部稠密相关更能容忍大变形。
		Image2D* ref_img = nullptr;
		Image2D* tar_img = nullptr;

	public:
		virtual ~Feature2D() = default;

		inline void setImages(Image2D& ref_img, Image2D& tar_img)
		{
			this->ref_img = &ref_img;
			this->tar_img = &tar_img;
		}

		virtual void prepare() = 0;
		virtual void compute() = 0;
	};

	// 3D 特征提取与匹配模块的抽象接口。
	class Feature3D
	{
	protected:
		// 算法说明：3D 特征接口的作用是为体匹配提供初始稀疏种子，
		// 然后再进入代价更高的稠密 DVC 细化阶段。
		Image3D* ref_img = nullptr;
		Image3D* tar_img = nullptr;

	public:
		virtual ~Feature3D() = default;

		inline void setImages(Image3D& ref_img, Image3D& tar_img)
		{
			this->ref_img = &ref_img;
			this->tar_img = &tar_img;
		}
		virtual void prepare() = 0;
		virtual void compute() = 0;
	};

}//namespace opencorr

#endif //_FEATURE_H_




