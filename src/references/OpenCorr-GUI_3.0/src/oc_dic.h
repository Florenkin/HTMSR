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

#ifndef _DIC_H_
#define _DIC_H_

#include<memory>

// 模块概览：2D DIC 和 3D DVC 求解器的抽象基类及共享辅助工具。
#include "oc_poi.h"
#include "oc_subset.h"

namespace opencorr
{

	// ZNCC 异常状态码定义。
	// 0：重置后可继续进入后续处理。
	// -1：子区内特征数量不足（FeatureAffine）。
	// -2：RANSAC 中未找到一致集（FeatureAffine）。
	// -3：在迭代开始前即终止（ICGN）。
	// -4：迭代未收敛（ICGN）。
	// -5：结果中出现 NaN（ICGN）。

	// 暴力搜索时使用的关键点索引与距离记录。
	struct KeypointIndex
	{
		int kp_idx; // 关键点队列中的索引
		float distance; // 到当前 POI 的欧氏距离
	};

	// 2D DIC 求解器共享接口的抽象基类。
	class DIC
	{
	public:
		// 算法说明：所有 2D 求解器共享同样的输入图像、子区尺寸控制和
		// `prepare/compute` 生命周期，这样不同算法就可以按统一方式串接。
		Image2D* ref_img = nullptr;
		Image2D* tar_img = nullptr;

		int subset_radius_x, subset_radius_y;
		int thread_number; //OpenMP thread number
		bool self_adaptive;

		DIC();
		virtual ~DIC() = default;

		void setImages(Image2D& ref_img, Image2D& tar_img);
		void setSubset(int radius_x, int radius_y);
		void setSelfAdaptive(bool is_self_adaptive); //select if the subset is automatically set or manually set

		virtual void prepare() = 0;
		virtual void compute(POI2D* poi) = 0;
		virtual void compute(std::vector<POI2D>& poi_queue) = 0;

	};

	// 3D DVC 求解器共享接口的抽象基类。
	class DVC
	{
	public:
		// 算法说明：3D 接口与 DIC 的设计高度一致，这使得平面相关和体相关
		// 在库架构层面保持统一，便于复用调用流程与上层组织方式。
		Image3D* ref_img = nullptr;
		Image3D* tar_img = nullptr;

		int subset_radius_x, subset_radius_y, subset_radius_z;
		int thread_number; //OpenMP thread number

		DVC();
		virtual ~DVC() = default;

		void setImages(Image3D& ref_img, Image3D& tar_img);
		void setSubset(int radius_x, int radius_y, int radius_z);

		virtual void prepare() = 0;
		virtual void compute(POI3D* POI) = 0;
		virtual void compute(std::vector<POI3D>& poi_queue) = 0;
	};

	// 按 ZNCC 从高到低排序候选结果。
	bool sortByZNCC(const POI2D& p1, const POI2D& p2);

	// 按距离从近到远排序候选关键点。
	bool sortByDistance(const KeypointIndex& kp1, const KeypointIndex& kp2);

}//namespace opencorr

#endif //_DIC_H_



