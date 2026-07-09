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

// 模块概览：利用极线约束缩小双目匹配候选范围的搜索模块。
#include "oc_epipolar_search.h"

namespace opencorr
{
	// 构造 EpipolarSearch 对象，并初始化后续步骤需要的状态。
	EpipolarSearch::EpipolarSearch(Calibration& view1_cam, Calibration& view2_cam, int thread_number)
	{
		this->view1_cam = view1_cam;
		this->view2_cam = view2_cam;
		this->thread_number = thread_number;
	}

	// 释放 EpipolarSearch 持有的缓冲区、计划或动态资源。
	EpipolarSearch::~EpipolarSearch()
	{
		destoryICGN();
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int EpipolarSearch::getSearchRadius() const
	{
		return search_radius;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int EpipolarSearch::getSearchStep() const
	{
		return search_step;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void EpipolarSearch::setSearch(int search_radius, int search_step)
	{
		if (search_radius < search_step)
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("Search radius is less than search step");
			return;
		}
		this->search_radius = search_radius;
		this->search_step = search_step;
	}

	// 创建并配置当前流程要使用的辅助对象。
	void EpipolarSearch::createICGN(int subset_radius_x, int subset_radius_y, float conv_criterion, float stop_condition)
	{
		icgn1 = std::make_unique<ICGN2D1>(subset_radius_x, subset_radius_y, conv_criterion, stop_condition, thread_number);
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	void EpipolarSearch::prepareICGN()
	{
		icgn1->setImages(*ref_img, *tar_img);
		icgn1->prepare();
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	void EpipolarSearch::destoryICGN()
	{
		if (icgn1 != nullptr)
		{
			icgn1.reset();
		}
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void EpipolarSearch::setParallax(Point2D parallax)
	{
		this->parallax = parallax;

		parallax_x[0] = 0;
		parallax_x[1] = 0;
		parallax_x[2] = parallax.x;

		parallax_y[0] = 0;
		parallax_y[1] = 0;
		parallax_y[2] = parallax.y;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void EpipolarSearch::setParallax(float coefficient_x[3], float coefficient_y[3])
	{
		parallax_x[0] = coefficient_x[0];
		parallax_x[1] = coefficient_x[1];
		parallax_x[2] = coefficient_x[2];

		parallax_y[0] = coefficient_y[0];
		parallax_y[1] = coefficient_y[1];
		parallax_y[2] = coefficient_y[2];
	}

	// Refreshes derived matrices, caches, or internal state after parameter changes.
	void EpipolarSearch::updateCameras(Calibration& view1_cam, Calibration& view2_cam)
	{
		this->view1_cam = view1_cam;
		this->view2_cam = view2_cam;
	}

	// Refreshes derived matrices, caches, or internal state after parameter changes.
	void EpipolarSearch::updateFundementalMatrix()
	{
		// 算法说明：基础矩阵会利用两台相机的标定参数，把视图 1 中的点映射成视图 2 中对应的极线。
		//creat transposed inverse intrinsic matrix of right camera
		Eigen::Matrix3f right_invK_t = view2_cam.intrinsic_matrix.inverse().transpose();

		//create an anti-symmetric matrix of translation vector of right camera
		Eigen::Matrix3f right_t_antisymmetric;
		right_t_antisymmetric << 0, -view2_cam.translation_vector(2), view2_cam.translation_vector(1),
			view2_cam.translation_vector(2), 0, -view2_cam.translation_vector(0),
			-view2_cam.translation_vector(1), view2_cam.translation_vector(0), 0;

		//create essential matrix of right camera
		Eigen::Matrix3f right_E = right_t_antisymmetric * view2_cam.rotation_matrix;

		//creat inversed intrinsic matrix of left camera
		Eigen::Matrix3f left_K = view1_cam.intrinsic_matrix.inverse();

		fundamental_matrix = right_invK_t * right_E * left_K;
	}

	// 执行主计算前所需的预处理。
	void EpipolarSearch::prepare()
	{
		view1_cam.updateMatrices();
		view2_cam.updateMatrices();

		updateFundementalMatrix();

		prepareICGN();
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void EpipolarSearch::compute(POI2D* poi)
	{
		// 算法说明：一个简单的线性视差模型能够在极线细化前，为对应点大致位置提供实用初猜。
		//estimate parallax
		parallax.x = parallax_x[0] * (poi->x - int(ref_img->width / 2)) + parallax_x[1] * (poi->y - int(ref_img->height / 2)) + parallax_x[2];
		parallax.y = parallax_y[0] * (poi->x - int(ref_img->width / 2)) + parallax_y[1] * (poi->y - int(ref_img->height / 2)) + parallax_y[2];

		//convert locatoin of left POI to a vector
		Eigen::Vector3f view1_vector;
		view1_vector << (poi->x + poi->deformation.u), (poi->y + poi->deformation.v), 1;

		// 算法说明：基础矩阵定义了候选极线，而视差估计则给出围绕其建立一维搜索的中心点。
		//get the projection of POI in the primary view on the epipolar line in the secondary view
		Eigen::Vector3f view2_epipolar = fundamental_matrix * view1_vector;
		float line_slope = -view2_epipolar(0) / view2_epipolar(1);
		float line_intercept = -view2_epipolar(2) / view2_epipolar(1);
		int x_view2 = (int)((line_slope * (poi->y + poi->deformation.v + parallax.y - line_intercept) + poi->x + poi->deformation.u + parallax.x) / (line_slope * line_slope + 1));
		int y_view2 = (int)(line_slope * x_view2 + line_intercept);

		// 算法说明：中心候选点是在任何局部相关细化前，由极线几何和视差共同预测得到的投影匹配。
		//get the center of searching region
		std::vector<POI2D> poi_candidates;
		POI2D current_poi(poi->x, poi->y);
		current_poi.deformation.u = x_view2 - poi->x;
		current_poi.deformation.v = y_view2 - poi->y;
		poi_candidates.push_back(current_poi);

		// 算法说明：其他候选点会沿极线对称采样，这样搜索就能保持为有界的一维问题。
		//get the other trial locations in searching region
		int x_trial, y_trial;
		for (int i = search_step; i < search_radius; i += search_step)
		{
			x_trial = x_view2 + i;
			y_trial = (int)(line_slope * x_trial + line_intercept);
			current_poi.deformation.u = x_trial - poi->x;
			current_poi.deformation.v = y_trial - poi->y;
			if (x_trial - icgn1->subset_radius_x > 0 && x_trial + icgn1->subset_radius_x < icgn1->ref_img->width - 1
				&& y_trial - icgn1->subset_radius_y > 0 && y_trial + icgn1->subset_radius_y < icgn1->ref_img->height - 1)
			{
				poi_candidates.push_back(current_poi);
			}

			x_trial = x_view2 - i;
			y_trial = (int)(line_slope * x_trial + line_intercept);
			current_poi.deformation.u = x_trial - poi->x;
			current_poi.deformation.v = y_trial - poi->y;
			if (x_trial - icgn1->subset_radius_x > 0 && x_trial + icgn1->subset_radius_x < icgn1->ref_img->width - 1
				&& y_trial - icgn1->subset_radius_y > 0 && y_trial + icgn1->subset_radius_y < icgn1->ref_img->height - 1)
			{
				poi_candidates.push_back(current_poi);
			}
		}

		// 算法说明：每个候选点都会先经过 ICGN 局部细化，因此最终比较不再只是像素级，而是亚像素且考虑变形的。
		//coarse check using ICGN1
		auto queue_size = poi_candidates.size();
#pragma omp parallel for
		for (int i = 0; i < queue_size; i++)
		{
			icgn1->compute(&poi_candidates[i]);
		}

		// 算法说明：最终最佳候选是根据细化后的相关质量来选择的，这样可以剔除几何上成立但灰度质量差的匹配。
		//take the one with the highest ZNCC value
		std::sort(poi_candidates.begin(), poi_candidates.end(), sortByZNCC);

		poi->deformation = poi_candidates[0].deformation;
		poi->result = poi_candidates[0].result;
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void EpipolarSearch::compute(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
		//CAUTION: no need to use OMP parallel for, as the parallelism has been implemented in the processing of each POI
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}

}//namespace opencorr



