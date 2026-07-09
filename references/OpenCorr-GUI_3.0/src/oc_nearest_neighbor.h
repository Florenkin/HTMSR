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

#ifndef _NEAREST_NEIGHBOR_H_
#define _NEAREST_NEIGHBOR_H_

#include <nanoflann.hpp>

// 模块概览：基于 KD 树的最近邻搜索，服务于特征和应变处理流程。
#include "oc_poi.h"

namespace opencorr
{
	// 最近邻适配器使用的最小点结构。
	struct Point
	{
		float x, y, z;
	};

	// 将项目数据暴露给 nanoflann 的点云适配器。
	struct PointCloud
	{
		using coord_t = float;  // 每个坐标分量的数据类型

		std::vector<Point> pts;

		// 返回点数量。
		inline size_t kdtree_get_point_count() const
		{
			// 算法说明：nanoflann 会直接访问这个适配器，
			// 因而项目内部数据无需先转换成另一套点云结构。
			return pts.size();
		}

		// 返回第 `idx` 个点在第 `dim` 维上的坐标值。
		inline float kdtree_get_pt(const size_t idx, const size_t dim) const
		{
			if (dim == 0)
			{
				return pts[idx].x;
			}
			else if (dim == 1)
			{
				return pts[idx].y;
			}
			else
			{
				return pts[idx].z;
			}
		}

		// 可选的包围盒计算接口，返回 false 表示使用默认实现。
		template <class BBOX>
		bool kdtree_get_bbox(BBOX& /* bb */) const
		{
			return false;
		}
	};

	// 基于 KD 树的最近邻搜索封装。
	class NearestNeighbor
	{
	protected:
		// 算法说明：邻域搜索是每个 POI 周围做局部仿射初始化、
		// 应变拟合和局部统计时的基础设施。
		PointCloud point_cloud;
		float search_radius;
		int search_k;
		float query_coor[3] = { 0.f };

		nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, PointCloud>, PointCloud, 3 /* dim */>* kdt_index = nullptr;

	public:
		NearestNeighbor();
		~NearestNeighbor();

		void assignPoints(std::vector<Point2D>& point_queue);
		void assignPoints(std::vector<POI2D>& poi_queue);
		void assignPoints(std::vector<Point3D>& point_queue);
		void assignPoints(std::vector<POI3D>& poi_queue);

		float getSearchRadius() const;
		int getSearchK() const;
		void setSearchRadius(float search_radius);
		void setSearchK(int search_k);

		void constructKdTree();
		void clear(); //clear point cloud and KD_tree

		int radiusSearch(Point3D query_point, std::vector<nanoflann::ResultItem<uint32_t, float>>& matches);
		int radiusSearch(Point3D query_point, float search_radius, std::vector<nanoflann::ResultItem<uint32_t, float>>& matches);

		int knnSearch(Point3D query_point, std::vector<uint32_t>& k_neighbor_idx, std::vector<float>& k_squared_distance);
		int knnSearch(Point3D query_point, int search_k, std::vector<uint32_t>& k_neighbor_idx, std::vector<float>& k_squared_distance);
	};

}//namespace opencorr

#endif //_NEAREST_NEIGHBOR_H_



