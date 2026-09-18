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

#ifndef _CALIBRATION_H_
#define _CALIBRATION_H_

#include "oc_array.h"
#include "oc_point.h"

// 模块概览：相机标定数据、畸变处理和坐标转换工具。
namespace opencorr
{
	// 相机内参和畸变参数容器。
	union CameraIntrinsics
	{
		struct
		{
			float fx, fy, fs;
			float cx, cy;
			float k1, k2, k3, k4, k5, k6;
			float p1, p2;
		};
		float cam_i[13];
	};

	// 相机姿态和位移参数容器。
	union CameraExtrinsics
	{
		struct
		{
			float tx, ty, tz;
			float rx, ry, rz;
		};
		float cam_e[6];
	};

	// 包含矩阵、映射和坐标转换辅助能力的相机标定对象。
	class Calibration
	{
	public:
		// 算法说明：对象同时保存原始标定参数，以及由它们派生出来的矩阵和查找表，
		// 便于下游模块直接高效复用。
		CameraIntrinsics intrinsics;
		CameraExtrinsics extrinsics;

		Eigen::Matrix3f intrinsic_matrix; // 相机内参矩阵
		Eigen::Matrix3f rotation_matrix; // 相机旋转矩阵
		Eigen::Vector3f translation_vector; // 相机平移向量
		Eigen::MatrixXf projection_matrix; // 相机投影矩阵

		float convergence; // 反畸变迭代的收敛阈值
		int iteration; // 反畸变迭代的停止步数

		// 传感器整数像素坐标对应的图像坐标反查表。
		Eigen::MatrixXf map_x;
		Eigen::MatrixXf map_y;

		Calibration();
		Calibration(CameraIntrinsics& intrinsics, CameraExtrinsics& extrinsics);
		~Calibration();

		void updateIntrinsicMatrix();
		void updateRotationMatrix();
		void updateTranslationVector();
		void updateProjectionMatrix();
		void updateMatrices();
		void updateCalibration(CameraIntrinsics& intrinsics, CameraExtrinsics& extrinsics);

		// 将内参与外参全部清零。
		void clear();

		// 获取和设置反畸变的收敛条件与迭代上限。
		float getConvergence() const;
		int getIteration() const;
		void setUndistortion(float convergence, int iteration);

		// 在归一化图像坐标系与传感器像素坐标系之间转换。
		Point2D image_to_sensor(Point2D& point);
		Point2D sensor_to_image(Point2D& point);

		/* 为传感器坐标系中的整数像素位置建立其在归一化图像坐标系中的
		反查映射表。*/
		// 算法说明：这张反查表会把重复的反畸变查询变成快速插值查表，
		// 运行时无需再做逐点迭代求解。
		void prepare(int height, int width);

		// 对归一化图像坐标中的点施加畸变。
		Point2D distort(Point2D& point);

		// 在图像-传感器坐标映射表上通过插值执行反畸变。
		Point2D undistort(Point2D& point);
	};

} //opencorr
#endif //_CALIBRATION_H_



