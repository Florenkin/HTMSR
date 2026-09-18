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

#include <numeric>
#include <omp.h>

// 模块概览：在高精度迭代细化前，基于特征引导的局部仿射初始化。
#include "oc_feature_affine.h"

namespace opencorr
{
	// 返回当前对象中的配置值、派生数据或运行时状态。
	std::unique_ptr<NearestNeighbor>& FeatureAffine2D::getInstance(int tid)
	{
		if (tid >= (int)instance_pool.size())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("CPU thread ID over limit");
		}

		return instance_pool[tid];
	}

	// 构造 FeatureAffine2D 对象，并初始化后续步骤需要的状态。
	FeatureAffine2D::FeatureAffine2D(int radius_x, int radius_y, int thread_number)
	{
		this->subset_radius_x = radius_x;
		this->subset_radius_y = radius_y;
		neighbor_search_radius = sqrt((float)(radius_x * radius_x + radius_y * radius_y));
		neighbor_number_min = 7;
		ransac_config.error_threshold = 1.5f;
		ransac_config.sample_mumber = 3;
		ransac_config.trial_number = 20;
		this->thread_number = thread_number;

		self_adaptive = false;
		subset_feature_min = 14;
		subset_radius_min = 10;

		instance_pool.resize(thread_number);
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i] = std::make_unique<NearestNeighbor>();
		}
	}

	// 释放 FeatureAffine2D 持有的缓冲区、计划或动态资源。
	FeatureAffine2D::~FeatureAffine2D()
	{
		for (auto& instance : instance_pool)
		{
			instance.reset();
		}
		std::vector<std::unique_ptr<NearestNeighbor>>().swap(instance_pool);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	RansacConfig FeatureAffine2D::getRansacConfig() const
	{
		return ransac_config;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	float FeatureAffine2D::getSearchRadius() const
	{
		return neighbor_search_radius;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int FeatureAffine2D::getNeighborMin() const
	{
		return neighbor_number_min;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine2D::setSearch(float neighbor_search_radius, int neighbor_number_min)
	{
		this->neighbor_search_radius = neighbor_search_radius;
		this->neighbor_number_min = neighbor_number_min;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine2D::setRansacConfig(RansacConfig ransac_config)
	{
		this->ransac_config = ransac_config;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine2D::setSubsetAdjustment(int feature_min, int radius_min)
	{
		subset_feature_min = feature_min;
		subset_radius_min = radius_min;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine2D::setKeypointPair(std::vector<Point2D>& ref_kp, std::vector<Point2D>& tar_kp)
	{
		this->ref_kp = ref_kp;
		this->tar_kp = tar_kp;
	}

	// 执行主计算前所需的预处理。
	void FeatureAffine2D::prepare()
	{
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i]->clear();

			instance_pool[i]->assignPoints(ref_kp);
			instance_pool[i]->setSearchRadius(neighbor_search_radius);
			instance_pool[i]->setSearchK(neighbor_number_min);
			instance_pool[i]->constructKdTree();
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void FeatureAffine2D::compute(POI2D* poi)
	{
		// 根据线程编号获取当前线程对应的实例。
		std::unique_ptr<NearestNeighbor>& neighbor_search = getInstance(omp_get_thread_num());

		Point3D current_point(poi->x, poi->y, 0.f);
		std::vector<Point2D> ref_candidates, tar_candidates;

		int neighbor_num = 0;

		// 算法说明：在自适应模式下，还会利用邻近特征的几何分布来调整 POI 位置和子区尺寸。
		// 算法说明：这样可以让子区向纹理更明显的区域扩展或偏移。
		if (self_adaptive)
		{
			float x_min = ref_img->width;
			float x_max = -1.f;
			float y_min = ref_img->height;
			float y_max = -1.f;

			// 在给定半径内搜索邻近关键点。
			std::vector<uint32_t> k_neighbor_idx;
			std::vector<float> k_squared_distance;

			neighbor_num = neighbor_search->knnSearch(current_point, subset_feature_min, k_neighbor_idx, k_squared_distance);

			if (neighbor_num < ransac_config.sample_mumber)
			{
				poi->result.zncc = -1.f;
				return;
			}
			else
			{
				for (int i = 0; i < neighbor_num; i++)
				{
					ref_candidates.push_back(ref_kp[k_neighbor_idx[i]]);
					tar_candidates.push_back(tar_kp[k_neighbor_idx[i]]);

					x_min = ref_candidates[i].x < x_min ? ref_candidates[i].x : x_min;
					x_max = ref_candidates[i].x > x_max ? ref_candidates[i].x : x_max;
					y_min = ref_candidates[i].y < y_min ? ref_candidates[i].y : y_min;
					y_max = ref_candidates[i].y > y_max ? ref_candidates[i].y : y_max;
				}

				// 调整 POI 位置与子区尺寸。
				if (poi->x >= x_min && poi->x <= x_max && poi->y >= y_min && poi->y <= y_max)
				{
					// 如果 POI 位于包围矩形内，则按到最远边界的距离设置子区半径。
					poi->subset_radius.x = abs(x_max - poi->x) > abs(poi->x - x_min) ? (int)abs(x_max - poi->x) : (int)abs(poi->x - x_min);
					poi->subset_radius.y = abs(y_max - poi->y) > abs(poi->y - y_min) ? (int)abs(y_max - poi->y) : (int)abs(poi->y - y_min);
				}
				else
				{
					// 如果 POI 落在包围矩形外，则将矩形中心作为新的 POI。
					poi->x = (int)(0.5f * (x_max + x_min));
					poi->y = (int)(0.5f * (y_max + y_min));
					poi->subset_radius.x = (int)(0.5f * (x_max - x_min));
					poi->subset_radius.y = (int)(0.5f * (y_max - y_min));
				}

				// 检查半径是否低于预设最小值。
				poi->subset_radius.x = poi->subset_radius.x < subset_radius_min ? subset_radius_min : poi->subset_radius.x;
				poi->subset_radius.y = poi->subset_radius.y < subset_radius_min ? subset_radius_min : poi->subset_radius.y;
			}
		}
		else
		{
			// 在给定半径内搜索邻近关键点。
			std::vector<nanoflann::ResultItem<uint32_t, float>> current_matches;
			neighbor_num = neighbor_search->radiusSearch(current_point, current_matches);

			if (neighbor_num < ransac_config.sample_mumber)
			{
				poi->result.zncc = -1.f;
				return;
			}
			else
			{
				ref_candidates.resize(neighbor_num);
				tar_candidates.resize(neighbor_num);

				if (neighbor_num >= neighbor_number_min)
				{
					for (int i = 0; i < neighbor_num; i++)
					{
						ref_candidates[i] = ref_kp[current_matches[i].first];
						tar_candidates[i] = tar_kp[current_matches[i].first];
					}
				}
				else //如果半径搜索得到的邻居数量不足，则退回到 KNN 搜索
				{
					std::vector<Point2D>().swap(ref_candidates);
					std::vector<Point2D>().swap(tar_candidates);

					std::vector<uint32_t> k_neighbor_idx;
					std::vector<float> k_squared_distance;

					neighbor_num = neighbor_search->knnSearch(current_point, k_neighbor_idx, k_squared_distance);

					ref_candidates.resize(neighbor_num);
					tar_candidates.resize(neighbor_num);
					for (int i = 0; i < neighbor_num; i++)
					{
						ref_candidates[i] = ref_kp[k_neighbor_idx[i]];
						tar_candidates[i] = tar_kp[k_neighbor_idx[i]];
					}
				}
			}
		}

		// 将全局坐标转换为以 POI 为中心的局部坐标。
		for (int i = 0; i < neighbor_num; i++)
		{
			ref_candidates[i] = ref_candidates[i] - (Point2D)*poi;
			tar_candidates[i] = tar_candidates[i] - (Point2D)*poi;
		}

		// 算法说明：这里使用 RANSAC 在拟合仿射模型前剔除错误匹配或不一致的特征对。
		// 算法说明：只有重投影误差足够低的一致集才会被接受。
		// 执行 RANSAC 流程。
		std::vector<int> candidate_index(neighbor_num);
		// 实现当前类型中的一个独立处理步骤或辅助过程。
		std::iota(candidate_index.begin(), candidate_index.end(), 0); //用升序整数初始化候选索引

		int trial_counter = 0; //试验计数器
		float location_mean_error;
		std::vector<int> max_set;

		// 随机数生成器。
		std::random_device rd;
		std::mt19937_64 gen64(rd());
		do
		{
			// 随机抽取样本。
			std::shuffle(candidate_index.begin(), candidate_index.end(), gen64);

			Eigen::MatrixXf ref_neighbors(ransac_config.sample_mumber, 3);
			Eigen::MatrixXf tar_neighbors(ransac_config.sample_mumber, 3);
			for (int j = 0; j < ransac_config.sample_mumber; j++)
			{
				ref_neighbors(j, 0) = ref_candidates[candidate_index[j]].x;
				ref_neighbors(j, 1) = ref_candidates[candidate_index[j]].y;
				ref_neighbors(j, 2) = 1.f;
				tar_neighbors(j, 0) = tar_candidates[candidate_index[j]].x;
				tar_neighbors(j, 1) = tar_candidates[candidate_index[j]].y;
				tar_neighbors(j, 2) = 1.f;
			}
			// 这里满足 ref * affine = tar，因此 affine 与文献中 Ax=x' 的矩阵写法互为排列形式。
			// 算法说明：每次试验都会估计一个从参考侧局部坐标到目标侧局部坐标的仿射映射。
			// 算法说明：之后会在最佳一致集上再次执行同样的最小二乘求解，以得到最终初始化结果。
			Eigen::Matrix3f affine_matrix = ref_neighbors.colPivHouseholderQr().solve(tar_neighbors);

			// 计算一致集。
			std::vector<int> trial_set;
			location_mean_error = 0;
			float delta_x, delta_y, estimation_error;
			for (int j = 0; j < neighbor_num; j++)
			{
				delta_x = (float)(ref_candidates[candidate_index[j]].x * affine_matrix(0, 0)
					+ ref_candidates[candidate_index[j]].y * affine_matrix(1, 0) + affine_matrix(2, 0))
					- tar_candidates[candidate_index[j]].x;
				delta_y = (float)(ref_candidates[candidate_index[j]].x * affine_matrix(0, 1)
					+ ref_candidates[candidate_index[j]].y * affine_matrix(1, 1) + affine_matrix(2, 1))
					- tar_candidates[candidate_index[j]].y;
				Point2D cur_point(delta_x, delta_y);
				estimation_error = cur_point.vectorNorm();
				// 若误差可接受，则保留该“好点”。
				if (estimation_error < ransac_config.error_threshold)
				{
					trial_set.push_back(candidate_index[j]);
					location_mean_error += estimation_error;
				}
			}
			// 如果当前试验集更大，则用它替换历史最大一致集。
			if (trial_set.size() > max_set.size())
			{
				max_set.assign(trial_set.begin(), trial_set.end());
			}

			trial_counter++;
			location_mean_error /= trial_set.size();
		} while (trial_counter < ransac_config.trial_number &&
			(max_set.size() < neighbor_number_min || location_mean_error > ransac_config.error_threshold / neighbor_number_min));

		// 算法说明：RANSAC 结束后，会使用所有内点而不是仅采样子集重新计算最终仿射模型。
		// 算法说明：这样能在把仿射参数转换为一阶变形向量前提高稳定性。
		// 根据一致集结果重新计算仿射矩阵。
		int max_set_size = (int)max_set.size();
		if (max_set_size < 3) //求解方程所需的基本条件
		{
			poi->result.zncc = -2.f;
			return;
		}
		else
		{
			Eigen::MatrixXf ref_neighbors(max_set_size, 3);
			Eigen::MatrixXf tar_neighbors(max_set_size, 3);

			for (int i = 0; i < max_set_size; i++)
			{
				ref_neighbors(i, 0) = ref_candidates[max_set[i]].x;
				ref_neighbors(i, 1) = ref_candidates[max_set[i]].y;
				ref_neighbors(i, 2) = 1.f;
				tar_neighbors(i, 0) = tar_candidates[max_set[i]].x;
				tar_neighbors(i, 1) = tar_candidates[max_set[i]].y;
				tar_neighbors(i, 2) = 1.f;
			}
			// 使用最小二乘法求解。
			Eigen::Matrix3f affine_matrix = ref_neighbors.colPivHouseholderQr().solve(tar_neighbors);

			// 根据仿射矩阵与一阶形函数之间的等价关系，计算一阶变形参数。
			// 算法说明：仿射矩阵会被转换成后续 ICGN 求解器使用的变形参数形式。
			// 算法说明：这样 FeatureAffine 的结果就能直接作为迭代求解器的初始猜测。
			poi->deformation.u = affine_matrix(2, 0);
			poi->deformation.ux = affine_matrix(0, 0) - 1.f;
			poi->deformation.uy = affine_matrix(1, 0);
			poi->deformation.v = affine_matrix(2, 1);
			poi->deformation.vx = affine_matrix(0, 1);
			poi->deformation.vy = affine_matrix(1, 1) - 1.f;

			// 保存 RANSAC 结果。
			poi->result.iteration = (float)trial_counter;
			poi->result.feature = (float)max_set_size;

			poi->result.zncc = 0.f;
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void FeatureAffine2D::compute(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}



	// 返回当前对象中的配置值、派生数据或运行时状态。
	std::unique_ptr<NearestNeighbor>& FeatureAffine3D::getInstance(int tid)
	{
		if (tid >= (int)instance_pool.size())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("CPU thread ID over limit");
		}

		return instance_pool[tid];
	}

	// 构造 FeatureAffine3D 对象，并初始化后续步骤需要的状态。
	FeatureAffine3D::FeatureAffine3D(int radius_x, int radius_y, int radius_z, int thread_number)
	{
		this->subset_radius_x = radius_x;
		this->subset_radius_y = radius_y;
		neighbor_search_radius = sqrt((float)(radius_x * radius_x + radius_y * radius_y + radius_z * radius_z));
		neighbor_number_min = 16;
		ransac_config.error_threshold = 3.2f;
		ransac_config.sample_mumber = 4;
		ransac_config.trial_number = 32;
		this->thread_number = thread_number;

		instance_pool.resize(thread_number);
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i] = std::make_unique<NearestNeighbor>();
		}
	}

	// 释放 FeatureAffine3D 持有的缓冲区、计划或动态资源。
	FeatureAffine3D::~FeatureAffine3D()
	{
		for (auto& instance : instance_pool)
		{
			instance.reset();
		}
		std::vector<std::unique_ptr<NearestNeighbor>>().swap(instance_pool);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	RansacConfig FeatureAffine3D::getRansacConfig() const
	{
		return ransac_config;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	float FeatureAffine3D::getSearchRadius() const
	{
		return neighbor_search_radius;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int FeatureAffine3D::getNeighborMin() const
	{
		return neighbor_number_min;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine3D::setSearch(float neighbor_search_radius, int neighbor_number_min)
	{
		this->neighbor_search_radius = neighbor_search_radius;
		this->neighbor_number_min = neighbor_number_min;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine3D::setRansacConfig(RansacConfig ransac_config)
	{
		this->ransac_config = ransac_config;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void FeatureAffine3D::setKeypointPair(std::vector<Point3D>& ref_kp, std::vector<Point3D>& tar_kp)
	{
		this->ref_kp = ref_kp;
		this->tar_kp = tar_kp;
	}

	// 执行主计算前所需的预处理。
	void FeatureAffine3D::prepare()
	{
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i]->clear();

			instance_pool[i]->assignPoints(ref_kp);
			instance_pool[i]->setSearchRadius(neighbor_search_radius);
			instance_pool[i]->setSearchK(neighbor_number_min);
			instance_pool[i]->constructKdTree();
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void FeatureAffine3D::compute(POI3D* poi)
	{
		// 根据线程编号获取当前线程对应的实例。
		std::unique_ptr<NearestNeighbor>& neighbor_search = getInstance(omp_get_thread_num());

		Point3D current_point(poi->x, poi->y, poi->z);
		std::vector<Point3D> ref_candidates, tar_candidates;

		// 在给定半径内搜索邻近关键点。
		std::vector<nanoflann::ResultItem<uint32_t, float>> current_matches;
		int neighbor_num = neighbor_search->radiusSearch(current_point, current_matches);

		if (neighbor_num < ransac_config.sample_mumber)
		{
			poi->result.zncc = -1.f;
			return;
		}
		else
		{
			ref_candidates.resize(neighbor_num);
			tar_candidates.resize(neighbor_num);

			if (neighbor_num >= neighbor_number_min)
			{
				for (int i = 0; i < neighbor_num; i++)
				{
					ref_candidates[i] = ref_kp[current_matches[i].first];
					tar_candidates[i] = tar_kp[current_matches[i].first];
				}
			}
			else //如果半径搜索得到的邻居数量不足，则退回到 KNN 搜索
			{
				std::vector<Point3D>().swap(ref_candidates);
				std::vector<Point3D>().swap(tar_candidates);

				std::vector<uint32_t> k_neighbor_idx;
				std::vector<float> k_squared_distance;

				neighbor_num = neighbor_search->knnSearch(current_point, k_neighbor_idx, k_squared_distance);

				ref_candidates.resize(neighbor_num);
				tar_candidates.resize(neighbor_num);
				for (int i = 0; i < neighbor_num; i++)
				{
					ref_candidates[i] = ref_kp[k_neighbor_idx[i]];
					tar_candidates[i] = tar_kp[k_neighbor_idx[i]];
				}
			}

			// 将全局坐标转换为以 POI 为中心的局部坐标。
			for (int i = 0; i < neighbor_num; i++)
			{
				ref_candidates[i] = ref_candidates[i] - (Point3D)*poi;
				tar_candidates[i] = tar_candidates[i] - (Point3D)*poi;
			}

			// 执行 RANSAC 流程。
			std::vector<int> candidate_index(neighbor_num);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::iota(candidate_index.begin(), candidate_index.end(), 0); //用升序整数初始化候选索引

			int trial_counter = 0; //试验计数器
			float location_mean_error;
			std::vector<int> max_set;

			// 随机数生成器。
			std::random_device rd;
			std::mt19937_64 gen64(rd());
			do
			{
				// 随机抽取样本。
				std::shuffle(candidate_index.begin(), candidate_index.end(), gen64);

				Eigen::MatrixXf tar_neighbors(ransac_config.sample_mumber, 4);
				Eigen::MatrixXf ref_neighbors(ransac_config.sample_mumber, 4);
				for (int j = 0; j < ransac_config.sample_mumber; j++) {
					tar_neighbors(j, 0) = tar_candidates[candidate_index[j]].x;
					tar_neighbors(j, 1) = tar_candidates[candidate_index[j]].y;
					tar_neighbors(j, 2) = tar_candidates[candidate_index[j]].z;
					tar_neighbors(j, 3) = 1.f;

					ref_neighbors(j, 0) = ref_candidates[candidate_index[j]].x;
					ref_neighbors(j, 1) = ref_candidates[candidate_index[j]].y;
					ref_neighbors(j, 2) = ref_candidates[candidate_index[j]].z;
					ref_neighbors(j, 3) = 1.f;
				}
				// 这里满足 ref * affine = tar，因此 affine 与文献中 Ax=x' 的矩阵写法互为排列形式。
				Eigen::Matrix4f affine_matrix = ref_neighbors.colPivHouseholderQr().solve(tar_neighbors);

				// 计算一致集。
				std::vector<int> trial_set;
				location_mean_error = 0;
				float delta_x, delta_y, delta_z, estimation_error;
				for (int j = 0; j < neighbor_num; j++) {
					delta_x = (float)(ref_candidates[candidate_index[j]].x * affine_matrix(0, 0)
						+ ref_candidates[candidate_index[j]].y * affine_matrix(1, 0)
						+ ref_candidates[candidate_index[j]].z * affine_matrix(2, 0) + affine_matrix(3, 0))
						- tar_candidates[candidate_index[j]].x;
					delta_y = (float)(ref_candidates[candidate_index[j]].x * affine_matrix(0, 1)
						+ ref_candidates[candidate_index[j]].y * affine_matrix(1, 1)
						+ ref_candidates[candidate_index[j]].z * affine_matrix(2, 1) + affine_matrix(3, 1))
						- tar_candidates[candidate_index[j]].y;
					delta_z = (float)(ref_candidates[candidate_index[j]].x * affine_matrix(0, 2)
						+ ref_candidates[candidate_index[j]].y * affine_matrix(1, 2)
						+ ref_candidates[candidate_index[j]].z * affine_matrix(2, 2) + affine_matrix(3, 2))
						- tar_candidates[candidate_index[j]].z;
					Point3D point(delta_x, delta_y, delta_z);
					estimation_error = point.vectorNorm();
					// 若误差可接受，则保留该“好点”。
					if (estimation_error < ransac_config.error_threshold) {
						trial_set.push_back(candidate_index[j]);
						location_mean_error += estimation_error;
					}
				}
				// 如果当前试验集更大，则用它替换历史最大一致集。
				if (trial_set.size() > max_set.size()) {
					max_set.assign(trial_set.begin(), trial_set.end());
				}
				trial_counter++;
				location_mean_error /= trial_set.size();
			} while (trial_counter < ransac_config.trial_number &&
				(max_set.size() < neighbor_number_min || location_mean_error > ransac_config.error_threshold / neighbor_number_min));

			// 根据一致集结果重新计算仿射矩阵。
			int max_set_size = (int)max_set.size();
			if (max_set_size < 4) //求解方程所需的基本条件
			{
				poi->result.zncc = -2.f;
				return;
			}

			Eigen::MatrixXf tar_neighbors(max_set_size, 4);
			Eigen::MatrixXf ref_neighbors(max_set_size, 4);

			for (int i = 0; i < max_set_size; i++)
			{
				ref_neighbors(i, 0) = ref_candidates[max_set[i]].x;
				ref_neighbors(i, 1) = ref_candidates[max_set[i]].y;
				ref_neighbors(i, 2) = ref_candidates[max_set[i]].z;
				ref_neighbors(i, 3) = 1.f;
				tar_neighbors(i, 0) = tar_candidates[max_set[i]].x;
				tar_neighbors(i, 1) = tar_candidates[max_set[i]].y;
				tar_neighbors(i, 2) = tar_candidates[max_set[i]].z;
				tar_neighbors(i, 3) = 1.f;
			}
			// 使用最小二乘法求解。
			Eigen::Matrix4f affine_matrix = ref_neighbors.colPivHouseholderQr().solve(tar_neighbors);

			// 根据仿射矩阵与一阶形函数之间的等价关系，计算一阶变形参数。
			poi->deformation.u = affine_matrix(3, 0);
			poi->deformation.ux = affine_matrix(0, 0) - 1.f;
			poi->deformation.uy = affine_matrix(1, 0);
			poi->deformation.uz = affine_matrix(2, 0);
			poi->deformation.v = affine_matrix(3, 1);
			poi->deformation.vx = affine_matrix(0, 1);
			poi->deformation.vy = affine_matrix(1, 1) - 1.f;
			poi->deformation.vz = affine_matrix(2, 1);
			poi->deformation.w = affine_matrix(3, 2);
			poi->deformation.wx = affine_matrix(0, 2);
			poi->deformation.wy = affine_matrix(1, 2);
			poi->deformation.wy = affine_matrix(2, 2) - 1.f;

			// 保存 RANSAC 结果。
			poi->result.iteration = (float)trial_counter;
			poi->result.feature = (float)max_set_size;

			poi->result.zncc = 0.f;
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void FeatureAffine3D::compute(std::vector<POI3D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}

}//namespace opencorr



