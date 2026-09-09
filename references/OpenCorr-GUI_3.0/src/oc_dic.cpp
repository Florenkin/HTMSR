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

#include "oc_dic.h"

// 模块概览：2D DIC 和 3D DVC 求解器的抽象基类及共享辅助工具。
namespace opencorr
{
	//DIC
	DIC::DIC() {}

	// 更新当前对象使用的配置、输入或运行时状态。
	void DIC::setImages(Image2D& ref_img, Image2D& tar_img)
	{
		// 算法说明：求解器保存的是图像指针而不是副本，因为预处理和逐个 POI
		// 计算都会反复访问同一份图像数据，避免拷贝更高效。
		this->ref_img = &ref_img;
		this->tar_img = &tar_img;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void DIC::setSubset(int radius_x, int radius_y)
	{
		subset_radius_x = radius_x;
		subset_radius_y = radius_y;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void DIC::setSelfAdaptive(bool is_self_adaptive)
	{
		// 算法说明：自适应模式允许上游初始化模块按点调整子区尺寸，
		// 以应对纹理质量或局部变形在空间上的不均匀分布。
		self_adaptive = is_self_adaptive;
	}


	//DVC
	DVC::DVC() {}

	// 更新当前对象使用的配置、输入或运行时状态。
	void DVC::setImages(Image3D& ref_img, Image3D& tar_img)
	{
		// 算法说明：体相关求解器沿用了与 2D 相同的所有权模式，
		// 这样大型 3D 图像不会被无谓复制，内存开销更可控。
		this->ref_img = &ref_img;
		this->tar_img = &tar_img;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void DVC::setSubset(int radius_x, int radius_y, int radius_z)
	{
		subset_radius_x = radius_x;
		subset_radius_y = radius_y;
		subset_radius_z = radius_z;
	}


	// 按 ZNCC 从高到低排序候选结果。
	bool sortByZNCC(const POI2D& p1, const POI2D& p2)
	{
		// 算法说明：更高的 ZNCC 代表更好的局部灰度匹配，因此它被用作
		// 候选筛选时默认的优先级排序准则。
		return p1.result.zncc > p2.result.zncc;
	}

	// 按距离从近到远排序候选关键点。
	bool sortByDistance(const KeypointIndex& kp1, const KeypointIndex& kp2)
	{
		return kp1.distance < kp2.distance;
	}

}//namespace opencorr



