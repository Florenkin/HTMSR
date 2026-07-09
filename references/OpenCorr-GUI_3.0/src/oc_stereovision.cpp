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
 
// 模块概览：基于匹配图像点和相机标定的双目重建模块。
#include "oc_stereovision.h"

namespace opencorr
{
	// 构造 Stereovision 对象，并初始化后续步骤需要的状态。
	Stereovision::Stereovision(Calibration* view1_cam, Calibration* view2_cam, int thread_number)
	{
		this->view1_cam = view1_cam;
		this->view2_cam = view2_cam;
		this->thread_number = thread_number;
	}

	// 释放 Stereovision 持有的缓冲区、计划或动态资源。
	Stereovision::~Stereovision() {}

	// Refreshes derived matrices, caches, or internal state after parameter changes.
	void Stereovision::updateCameras(Calibration* view1_cam, Calibration* view2_cam)
	{
		this->view1_cam = view1_cam;
		this->view2_cam = view2_cam;
	}

	// Refreshes derived matrices, caches, or internal state after parameter changes.
	void Stereovision::updateFundementalMatrix()
	{
		// 算法说明：这里使用的标定几何与极线搜索中使用的是同一套
		// also stored here for downstream stereo validation or diagnostics.
		//creat transposed inverse intrinsic matrix of right camera
		Eigen::Matrix3f right_invK_t = view2_cam->intrinsic_matrix.inverse().transpose();

		//create an anti-symmetric matrix of translation vector of right camera
		Eigen::Matrix3f right_t_antisymmetric;
		right_t_antisymmetric << 0, -view2_cam->translation_vector(2), view2_cam->translation_vector(1),
			view2_cam->translation_vector(2), 0, -view2_cam->translation_vector(0),
			-view2_cam->translation_vector(1), view2_cam->translation_vector(0), 0;

		//create essential matrix of right camera
		Eigen::Matrix3f right_E = right_t_antisymmetric * view2_cam->rotation_matrix;

		//creat inversed intrinsic matrix of left camera
		Eigen::Matrix3f left_K = view1_cam->intrinsic_matrix.inverse();

		fundamental_matrix = right_invK_t * right_E * left_K;
	}

	// 执行主计算前所需的预处理。
	void Stereovision::prepare() {
		view1_cam->updateIntrinsicMatrix();
		view1_cam->updateRotationMatrix();
		view1_cam->updateTranslationVector();
		view1_cam->updateProjectionMatrix();

		view2_cam->updateIntrinsicMatrix();
		view2_cam->updateRotationMatrix();
		view2_cam->updateTranslationVector();
		view2_cam->updateProjectionMatrix();

		updateFundementalMatrix();
	}

	// 根据匹配图像点和相机几何关系重建三维坐标。
	Point3D Stereovision::reconstruct(Point2D& view1_2d_point, Point2D& view2_2d_point)
	{
		if (std::isnan(view1_2d_point.x) || std::isnan(view1_2d_point.y) || std::isnan(view2_2d_point.x) || std::isnan(view2_2d_point.y))
		{
			Point3D zero_pt;
			return zero_pt;
		}
		else
		{
			// 算法说明：重建是在去畸变坐标上进行的，因此
			// the projection model matches the linear pinhole camera matrices.
			Point2D view1_coor = view1_cam->undistort(view1_2d_point);
			Point2D view2_coor = view2_cam->undistort(view2_2d_point);

			float x_view1 = view1_coor.x;
			float y_view1 = view1_coor.y;
			float x_view2 = view2_coor.x;
			float y_view2 = view2_coor.y;

			// 算法说明：每个相机都会贡献两条形如以下形式的线性方程
			// x * P_3 - P_1 and y * P_3 - P_2, yielding an overdetermined 4x3 system.
			//left side of equations
			Eigen::MatrixXf left_matrix(4, 3);
			//column 1
			left_matrix(0, 0) = x_view1 * view1_cam->projection_matrix(2, 0) - view1_cam->projection_matrix(0, 0);
			left_matrix(1, 0) = y_view1 * view1_cam->projection_matrix(2, 0) - view1_cam->projection_matrix(1, 0);
			left_matrix(2, 0) = x_view2 * view2_cam->projection_matrix(2, 0) - view2_cam->projection_matrix(0, 0);
			left_matrix(3, 0) = y_view2 * view2_cam->projection_matrix(2, 0) - view2_cam->projection_matrix(1, 0);

			//column 2
			left_matrix(0, 1) = x_view1 * view1_cam->projection_matrix(2, 1) - view1_cam->projection_matrix(0, 1);
			left_matrix(1, 1) = y_view1 * view1_cam->projection_matrix(2, 1) - view1_cam->projection_matrix(1, 1);
			left_matrix(2, 1) = x_view2 * view2_cam->projection_matrix(2, 1) - view2_cam->projection_matrix(0, 1);
			left_matrix(3, 1) = y_view2 * view2_cam->projection_matrix(2, 1) - view2_cam->projection_matrix(1, 1);

			//column 3
			left_matrix(0, 2) = x_view1 * view1_cam->projection_matrix(2, 2) - view1_cam->projection_matrix(0, 2);
			left_matrix(1, 2) = y_view1 * view1_cam->projection_matrix(2, 2) - view1_cam->projection_matrix(1, 2);
			left_matrix(2, 2) = x_view2 * view2_cam->projection_matrix(2, 2) - view2_cam->projection_matrix(0, 2);
			left_matrix(3, 2) = y_view2 * view2_cam->projection_matrix(2, 2) - view2_cam->projection_matrix(1, 2);

			//right side of equations
			Eigen::Vector4f right_matrix;
			right_matrix(0) = view1_cam->projection_matrix(0, 3) - x_view1 * view1_cam->projection_matrix(2, 3);
			right_matrix(1) = view1_cam->projection_matrix(1, 3) - y_view1 * view1_cam->projection_matrix(2, 3);
			right_matrix(2) = view2_cam->projection_matrix(0, 3) - x_view2 * view2_cam->projection_matrix(2, 3);
			right_matrix(3) = view2_cam->projection_matrix(1, 3) - y_view2 * view2_cam->projection_matrix(2, 3);

			// 算法说明：当两条成像射线因噪声不能严格相交时，QR 用来求解最小二乘三角化问题
			// the two image rays do not intersect exactly because of measurement noise.
			//determine the world coordinates by solving the equations
			Eigen::Vector3f world_coor = left_matrix.colPivHouseholderQr().solve(right_matrix);

			Point3D space_3d_point;
			space_3d_point.x = world_coor(0);
			space_3d_point.y = world_coor(1);
			space_3d_point.z = world_coor(2);

			return space_3d_point;
		}
	}

	// 根据匹配图像点和相机几何关系重建三维坐标。
	void Stereovision::reconstruct(std::vector<Point2D>& view1_2d_point_queue, std::vector<Point2D>& view2_2d_point_queue, std::vector<Point3D>& space_3d_point_queue)
	{
		auto queue_length = view1_2d_point_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++) {
			space_3d_point_queue[i] = reconstruct(view1_2d_point_queue[i], view2_2d_point_queue[i]);
		}
	}

}//namespace opencorr



