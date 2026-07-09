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

// 模块概览：基于 KD 树的最近邻搜索，服务于特征和应变处理流程。
#include "oc_nearest_neighbor.h"

namespace opencorr
{
	// 构造 NearestNeighbor 对象，并初始化后续步骤需要的状态。
	NearestNeighbor::NearestNeighbor() {}

	// 释放 NearestNeighbor 持有的缓冲区、计划或动态资源。
	NearestNeighbor::~NearestNeighbor()
	{
		if (kdt_index != nullptr)
		{
			delete kdt_index;
		}
	}

	// 接收外部点集，并构建用于邻域搜索的点云视图。
	void NearestNeighbor::assignPoints(std::vector<Point2D>& point_queue)
	{
		auto queue_length = point_queue.size();
		point_cloud.pts.resize(queue_length);
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			point_cloud.pts[i].x = point_queue[i].x;
			point_cloud.pts[i].y = point_queue[i].y;
			point_cloud.pts[i].z = 0.f;
		}
	}

	// 接收外部点集，并构建用于邻域搜索的点云视图。
	void NearestNeighbor::assignPoints(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
		point_cloud.pts.resize(queue_length);
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			point_cloud.pts[i].x = poi_queue[i].x;
			point_cloud.pts[i].y = poi_queue[i].y;
			point_cloud.pts[i].z = 0.f;
		}
	}

	// 接收外部点集，并构建用于邻域搜索的点云视图。
	void NearestNeighbor::assignPoints(std::vector<Point3D>& point_queue)
	{
		auto queue_length = point_queue.size();
		point_cloud.pts.resize(queue_length);
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			point_cloud.pts[i].x = point_queue[i].x;
			point_cloud.pts[i].y = point_queue[i].y;
			point_cloud.pts[i].z = point_queue[i].z;
		}
	}

	// 接收外部点集，并构建用于邻域搜索的点云视图。
	void NearestNeighbor::assignPoints(std::vector<POI3D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
		point_cloud.pts.resize(queue_length);
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			point_cloud.pts[i].x = poi_queue[i].x;
			point_cloud.pts[i].y = poi_queue[i].y;
			point_cloud.pts[i].z = poi_queue[i].z;
		}
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	float NearestNeighbor::getSearchRadius() const
	{
		return search_radius;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int NearestNeighbor::getSearchK() const
	{
		return search_k;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void NearestNeighbor::setSearchRadius(float search_radius)
	{
		this->search_radius = search_radius;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void NearestNeighbor::setSearchK(int search_k)
	{
		this->search_k = search_k;
	}

	// 构建后续邻域查询要使用的 KD 树索引。
	void NearestNeighbor::constructKdTree()
	{
		// 算法说明：KD 树会基于当前点集构建一次，
		// 之后就能被大量局部查询反复高效复用。
		// 构建 KD 树索引。
		using kdTree = nanoflann::KDTreeSingleIndexAdaptor<nanoflann::L2_Simple_Adaptor<float, PointCloud>, PointCloud, 3>;

		kdt_index = new kdTree(3 /*dim*/, point_cloud, { 10 /* max leaf */ });
	}

	// 在保留点位置或可复用结构的前提下，重置缓存结果值。
	void NearestNeighbor::clear()
	{
		if (!point_cloud.pts.empty())
		{
			std::vector<Point>().swap(point_cloud.pts);
		}
		if (kdt_index != nullptr)
		{
			delete kdt_index;
			kdt_index = nullptr;
		}
	}

	// 查询给定搜索半径内的所有邻近点。
	int NearestNeighbor::radiusSearch(Point3D query_point, std::vector<nanoflann::ResultItem<uint32_t, float>>& matches)
	{
		// 算法说明：半径搜索更符合物理意义上的局部邻域定义，
		// 因而它通常很适合用于应变估计。
		float squared_radius = search_radius * search_radius;

		query_coor[0] = query_point.x;
		query_coor[1] = query_point.y;
		query_coor[2] = query_point.z;

		nanoflann::SearchParameters params;
		params.sorted = false;

		int num_matches = (int)kdt_index->radiusSearch(&query_coor[0], squared_radius, matches, params);

		return num_matches;
	}

	// 查询给定搜索半径内的所有邻近点。
	int NearestNeighbor::radiusSearch(Point3D query_point, float search_radius, std::vector<nanoflann::ResultItem<uint32_t, float>>& matches)
	{
		float squared_radius = search_radius * search_radius;

		query_coor[0] = query_point.x;
		query_coor[1] = query_point.y;
		query_coor[2] = query_point.z;

		nanoflann::SearchParameters params;
		params.sorted = false;

		int num_matches = (int)kdt_index->radiusSearch(&query_coor[0], squared_radius, matches, params);

		return num_matches;
	}

	// 查询当前点周围的 K 个最近邻。
	int NearestNeighbor::knnSearch(Point3D query_point, std::vector<uint32_t>& k_neighbor_idx, std::vector<float>& k_squared_distance)
	{
		// 算法说明：KNN 搜索可以保证最小样本数，
		// 因而当半径搜索拿不到足够有效邻居时，它就是很好的回退方案。
		k_neighbor_idx.resize(search_k);
		k_squared_distance.resize(search_k);

		query_coor[0] = query_point.x;
		query_coor[1] = query_point.y;
		query_coor[2] = query_point.z;

		int num_matches = (int)kdt_index->knnSearch(&query_coor[0], search_k, &k_neighbor_idx[0], &k_squared_distance[0]);

		// 如果树中的可用点少于请求数量，则按实际数量收缩结果。
		k_neighbor_idx.resize(num_matches);
		k_squared_distance.resize(num_matches);

		return num_matches;
	}

	// 查询当前点周围的 K 个最近邻。
	int NearestNeighbor::knnSearch(Point3D query_point, int search_k, std::vector<uint32_t>& k_neighbor_idx, std::vector<float>& k_squared_distance)
	{
		k_neighbor_idx.resize(search_k);
		k_squared_distance.resize(search_k);

		query_coor[0] = query_point.x;
		query_coor[1] = query_point.y;
		query_coor[2] = query_point.z;

		int num_matches = (int)kdt_index->knnSearch(&query_coor[0], search_k, &k_neighbor_idx[0], &k_squared_distance[0]);

		// 如果树中的可用点少于请求数量，则按实际数量收缩结果。
		k_neighbor_idx.resize(num_matches);
		k_squared_distance.resize(num_matches);

		return num_matches;
	}

}//namespace opencorr



