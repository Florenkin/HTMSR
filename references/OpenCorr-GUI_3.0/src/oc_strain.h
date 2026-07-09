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

#ifndef _STRAIN_H_
#define _STRAIN_H_

#include "oc_array.h"
#include "oc_nearest_neighbor.h"

// 模块概览：面向 2D、双目和体数据场景的局部位移拟合与应变计算。
namespace opencorr
{
	// 应变拟合时用于记录邻居 POI 索引和距离的结构。
	struct PointIndex // 暴力搜索时使用的辅助结构
	{
		int poi_idx; // POI 队列中的索引
		float distance; // 到当前处理 POI 的欧氏距离
	};

	// 负责局部位移拟合与 Green-Lagrange / Cauchy 应变计算。
	// 算法说明：Strain 是后处理阶段，它会在每个 POI 周围拟合局部位移场。
	// 算法说明：它先从邻域 POI 中估计位移梯度，再把这些梯度转换成应变分量。
	class Strain
	{
	private:
		std::vector<NearestNeighbor*> instance_pool;
		NearestNeighbor* getInstance(int tid);

	protected:
		float subregion_radius; // 邻域子区半径
		int neighbor_number_min; // 拟合所需的最少邻居 POI 数量
		float zncc_threshold; // 高于该阈值的 POI 才视为有效
		int description; // 应变描述方式，1 为 Lagrangian，2 为 Eulerian
		int approximation; // 应变近似方式，1 为 Cauchy，2 为 Green
		int thread_number; // CPU 线程数

	public:

		Strain(float subregion_radius, int neighbor_number_min, int thread_number);
		~Strain();

		float getSubregionRadius() const;
		int getNeighborMin() const;
		float getZnccThreshold() const;

		void setSubregionRadius(float subregion_radius);
		void setNeighborMin(int neighbor_number_min);
		void setZnccThreshold(float zncc_threshold);
		void setDescription(int description); // 1 为 Lagrangian，2 为 Eulerian
		void setApproximation(int approximation); // 1 为 Cauchy，2 为 Green

		void prepare(std::vector<POI2D>& poi_queue);
		void prepare(std::vector<POI2DS>& poi_queue);
		void prepare(std::vector<POI3D>& poi_queue);

		void compute(POI2D* poi, std::vector<POI2D>& poi_queue);
		void compute(POI2DS* poi, std::vector<POI2DS>& poi_queue);
		void compute(POI3D* poi, std::vector<POI3D>& poi_queue);

		void compute(std::vector<POI2D>& poi_queue);
		void compute(std::vector<POI2DS>& poi_queue);
		void compute(std::vector<POI3D>& poi_queue);
	};


	// 按距离从近到远排序候选邻居。
	bool sortByDistance(const PointIndex& p1, const PointIndex& p2);

}//namespace opencorr

#endif //_STRAIN_H_


