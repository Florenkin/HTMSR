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

#ifndef _FEATURE_AFFINE_H_
#define _FEATURE_AFFINE_H_

#include "oc_dic.h"
#include "oc_nearest_neighbor.h"

// 模块概览：在高精度迭代细化前，基于特征引导的局部仿射初始化。
namespace opencorr
{
	// RANSAC 的参数配置。
	struct RansacConfig
	{
		int trial_number; //RANSAC 的最大试验次数
		int sample_mumber; //每次试验抽取的样本数
		float error_threshold; //RANSAC 的误差阈值
	};

	// 本模块的 2D 部分实现了下述文献中的方法。
	//J. Yang et al, Optics and Lasers in Engineering (2020) 127: 105964.
	//https://doi.org/10.1016/j.optlaseng.2019.105964

	// 算法说明：FeatureAffine 会根据每个 POI 周围的匹配关键点估计局部仿射变换。
	// 算法说明：它的输出不是最终结果，而是为后续迭代相关提供稳健的亚像素初值。
	class FeatureAffine2D : public DIC
	{
	private:
		std::vector<std::unique_ptr<NearestNeighbor>> instance_pool;
		std::unique_ptr<NearestNeighbor>& getInstance(int tid);

	protected:
		float neighbor_search_radius; //POI 周围匹配关键点的搜索半径
		int neighbor_number_min; //RANSAC 所需的最少邻居数量
		RansacConfig ransac_config;

		int subset_feature_min; //自适应子区中包含的最少邻居数
		int subset_radius_min; //自适应子区允许的最小半径

	public:
		std::vector<Point2D> ref_kp; //参考图像中的匹配关键点
		std::vector<Point2D> tar_kp; //目标图像中的匹配关键点

		FeatureAffine2D(int radius_x, int radius_y, int thread_number);
		~FeatureAffine2D();

		RansacConfig getRansacConfig() const;
		float getSearchRadius() const;
		int getNeighborMin() const;

		void setSearch(float neighbor_search_radius, int neighbor_number_min);
		void setRansacConfig(RansacConfig ransac_config);
		void setSubsetAdjustment(int feature_min, int radius_min); //设置自适应子区调整参数

		void setKeypointPair(std::vector<Point2D>& ref_kp, std::vector<Point2D>& tar_kp);
		void prepare();
		void compute(POI2D* poi);
		void compute(std::vector<POI2D>& poi_queue);
	};


	// 本模块的 3D 部分实现了下述文献中的方法。
	//J. Yang et al, Optics and Lasers in Engineering (2021) 136: 106323.
	//https://doi.org/10.1016/j.optlaseng.2020.106323

	// 算法说明：3D 版本使用匹配的三维关键点，在体空间中拟合局部仿射运动模型。
	// 算法说明：当大变形或复杂变形下 FFT 初始化不可靠时，这一点尤其有价值。
	class FeatureAffine3D : public DVC
	{
	private:
		std::vector<std::unique_ptr<NearestNeighbor>> instance_pool;
		std::unique_ptr<NearestNeighbor>& getInstance(int tid);

	protected:
		float neighbor_search_radius; //POI 周围匹配关键点的搜索半径
		int neighbor_number_min; //RANSAC 所需的最少邻居数量
		RansacConfig ransac_config;

	public:
		std::vector<Point3D> ref_kp; //参考体数据中的匹配关键点
		std::vector<Point3D> tar_kp; //目标体数据中的匹配关键点

		FeatureAffine3D(int radius_x, int radius_y, int radius_z, int thread_number);
		~FeatureAffine3D();

		RansacConfig getRansacConfig() const;
		float getSearchRadius() const;
		int getNeighborMin() const;

		void setSearch(float neighbor_search_radius, int neighbor_number_min);
		void setRansacConfig(RansacConfig ransac_config);

		void setKeypointPair(std::vector<Point3D>& ref_kp, std::vector<Point3D>& tar_kp);
		void prepare();
		void compute(POI3D* poi);
		void compute(std::vector<POI3D>& poi_queue);
	};

}//namespace opencorr

#endif //_FEATURE_AFFINE_H_


