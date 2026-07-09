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

// 模块概览：相机标定数据、畸变处理和坐标转换工具。
#include "oc_calibration.h"

namespace opencorr
{
	// 构造 Calibration 对象，并初始化后续步骤需要的状态。
	Calibration::Calibration()
	{
		convergence = 0.001f;
		iteration = 40;
	}

	// 构造 Calibration 对象，并初始化后续步骤需要的状态。
	Calibration::Calibration(CameraIntrinsics& intrinsics, CameraExtrinsics& extrinsics)
	{
		updateCalibration(intrinsics, extrinsics);
		convergence = 0.001f;
		iteration = 40;
	}

	// 释放 Calibration 持有的缓冲区、计划或动态资源。
	Calibration::~Calibration() {}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateIntrinsicMatrix()
	{
		// 算法说明：内参矩阵负责把归一化图像坐标映射到传感器坐标，
		// 其中包含焦距、主点位置以及坐标轴斜切项。
		intrinsic_matrix.setIdentity();
		intrinsic_matrix(0, 0) = intrinsics.fx;
		intrinsic_matrix(0, 1) = intrinsics.fs;
		intrinsic_matrix(0, 2) = intrinsics.cx;
		intrinsic_matrix(1, 1) = intrinsics.fy;
		intrinsic_matrix(1, 2) = intrinsics.cy;
		if (intrinsic_matrix.isIdentity())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("Null intrinsics matrix");
		}
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateRotationMatrix()
	{
		// 算法说明：存储的 Rodrigues 旋转向量会被转换成矩阵形式，
		// 以供投影和双目几何计算直接使用。
		Eigen::Vector3f rotation_vector;
		rotation_vector << extrinsics.rx, extrinsics.ry, extrinsics.rz;

		float theta = rotation_vector.norm();
		rotation_vector.normalize();
		Eigen::AngleAxisf r_v(theta, rotation_vector);

		rotation_matrix = r_v.toRotationMatrix();
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateTranslationVector()
	{
		translation_vector(0) = extrinsics.tx;
		translation_vector(1) = extrinsics.ty;
		translation_vector(2) = extrinsics.tz;
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateProjectionMatrix()
	{
		// 算法说明：投影矩阵把内参与外参合并在一起，
		// 这样三维世界点就能被直接映射到图像测量坐标。
		Eigen::MatrixXf rt_mat(3, 4);

		rt_mat.block(0, 0, 3, 3) << rotation_matrix;
		rt_mat.col(3) << translation_vector;

		projection_matrix = intrinsic_matrix * rt_mat;
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateMatrices()
	{
		updateIntrinsicMatrix();
		updateRotationMatrix();
		updateTranslationVector();
		updateProjectionMatrix();
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void Calibration::updateCalibration(CameraIntrinsics& intrinsics, CameraExtrinsics& extrinsics)
	{
		this->intrinsics = intrinsics;
		this->extrinsics = extrinsics;

		updateMatrices();
	}

	// 在保留点位置或可复用结构的前提下，重置缓存结果值。
	void Calibration::clear()
	{
		// 实现当前类型中的一个独立处理步骤或辅助过程。
		std::fill(std::begin(intrinsics.cam_i), std::end(intrinsics.cam_i), 0.f);
		// 实现当前类型中的一个独立处理步骤或辅助过程。
		std::fill(std::begin(extrinsics.cam_e), std::end(extrinsics.cam_e), 0.f);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	float Calibration::getConvergence() const
	{
		return convergence;
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	int Calibration::getIteration() const
	{
		return iteration;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void Calibration::setUndistortion(float convergence, int iteration)
	{
		this->convergence = convergence;
		this->iteration = iteration;
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	Point2D Calibration::image_to_sensor(Point2D& point)
	{
		// 算法说明：这里的图像坐标是归一化相机平面坐标，
		// 传感器坐标则是施加内参后的像素式坐标。
		float sensor_y = point.y * intrinsics.fy + intrinsics.cy;
		float sensor_x = point.x * intrinsics.fx + point.y * intrinsics.fs + intrinsics.cx;

		Point2D sensor_coordinate(sensor_x, sensor_y);
		return sensor_coordinate;
	}

	// 实现当前类型中的一个独立处理步骤或辅助过程。
	Point2D Calibration::sensor_to_image(Point2D& point)
	{
		// 算法说明：这是内参映射的逆过程，
		// 畸变建模和反向查表构造时都要用到它。
		float image_y = (point.y - intrinsics.cy) / intrinsics.fy;
		float image_x = (point.x - intrinsics.cx - intrinsics.fs * image_y) / intrinsics.fx;

		Point2D image_coordinate(image_x, image_y);
		return image_coordinate;
	}

	// 对归一化图像坐标中的点施加畸变。
	Point2D Calibration::distort(Point2D& point)
	{
		// 算法说明：畸变模型由有理式径向项和切向项共同组成，
		// 这与常见的相机镜头标定模型保持一致。
		// 预先计算中间变量。
		float image_xx = point.x * point.x;
		float image_yy = point.y * point.y;
		float image_xy = point.x * point.y;
		float distortion_r2 = image_xx + image_yy;
		float distrotion_r4 = distortion_r2 * distortion_r2;
		float distortion_r6 = distortion_r2 * distrotion_r4;

		// 算法说明：径向项会根据点到光轴的距离对坐标做放大或收缩。
		// 施加径向畸变。
		float radial_factor = (1 + intrinsics.k1 * distortion_r2 + intrinsics.k2 * distrotion_r4 + intrinsics.k3 * distortion_r6)
			/ (1 + intrinsics.k4 * distortion_r2 + intrinsics.k5 * distrotion_r4 + intrinsics.k6 * distortion_r6);
		float distorted_y = point.y * radial_factor;
		float distorted_x = point.x * radial_factor;

		// 算法说明：切向项用于补偿镜头或传感器装调误差造成的不对称偏移。
		// 施加切向畸变。
		distorted_y += intrinsics.p1 * (distortion_r2 + 2 * image_yy) + 2 * intrinsics.p2 * image_xy;
		distorted_x += 2 * intrinsics.p1 * image_xy + intrinsics.p2 * (distortion_r2 + 2 * image_xx);

		// 返回畸变后的坐标。
		Point2D distorted_coordinate(distorted_x, distorted_y);
		return distorted_coordinate;
	}

	// 执行主计算前所需的预处理。
	void Calibration::prepare(int height, int width)
	{
		// 算法说明：第一步会先给每个传感器像素赋一个归一化图像坐标初值，
		// 然后再在此基础上迭代校正。
		// 初始化归一化图像坐标映射表。
		map_x = Eigen::MatrixXf::Zero(height, width);
		map_y = Eigen::MatrixXf::Zero(height, width);

		// 初始化校正映射。
#pragma omp parallel for
		for (int r = 0; r < height; r++)
		{
			for (int c = 0; c < width; c++)
			{
				Point2D sensor_coordinate(c, r);
				Point2D image_coordinate(sensor_to_image(sensor_coordinate));
				map_x(r, c) = image_coordinate.x;
				map_y(r, c) = image_coordinate.y;
			}
		}

#pragma omp parallel for
		for (int r = 0; r < height; r++)
		{
			for (int c = 0; c < width; c++)
			{
				// 算法说明：这个类似不动点迭代的过程会离线逐像素求解反畸变问题，
				// 并把结果写入查找表中。
				float deviation_y;
				float deviation_x;
				bool stop_iteration = false;
				int i = 0;
				Point2D image_coordinate(map_x(r, c), map_y(r, c));

				while (i < iteration && stop_iteration == false)
				{
					i++;
					Point2D distorted_coordinate(distort(image_coordinate));
					Point2D sensor_coordinate(image_to_sensor(distorted_coordinate));
					deviation_y = r - sensor_coordinate.y;
					deviation_x = c - sensor_coordinate.x;
					if (std::isinf(deviation_x) || std::isinf(deviation_y))
					{
						stop_iteration = true;
						image_coordinate.y = map_y(r, c);
						image_coordinate.x = map_x(r, c);
					}
					// 算法说明：更新是在归一化图像空间中进行的，
					// 这样修正量始终与当前内参定义保持一致。
					if (fabs(deviation_x) > convergence || fabs(deviation_y) > convergence)
					{
						deviation_y /= intrinsics.fy;
						image_coordinate.y += deviation_y;
						image_coordinate.x += (deviation_x - deviation_y * intrinsics.fs) / intrinsics.fx;
					}
					else
					{
						stop_iteration = true;
					}
				}
				map_y(r, c) = image_coordinate.y;
				map_x(r, c) = image_coordinate.x;
			}
		}
	}

	// 对输入坐标应用反畸变逻辑。
	Point2D Calibration::undistort(Point2D& point)
	{
		// 算法说明：运行时反畸变会退化成在预计算反查表上的双线性插值，
		// 相比逐点迭代求逆要快得多。
		// 处理靠近边界的点。
		if (point.x < 0)
		{
			point.x = 0;
		}
		if (point.y < 0)
		{
			point.y = 0;
		}
		if (point.x > map_x.cols() - 2)
		{
			point.x = (float)map_x.cols() - 2.f;
		}
		if (point.y > map_y.rows() - 2)
		{
			point.y = (float)map_y.rows() - 2.f;
		}

		// 提取坐标的整数部分和小数部分。
		int y_integral = (int)floor(point.y);
		int x_integral = (int)floor(point.x);

		float y_decimal = point.y - y_integral;
		float x_decimal = point.x - x_integral;

		// 算法说明：双线性插值可以保证亚像素位置的反畸变结果连续平滑，
		// 而不是简单落到最近的整数表项上。
		// 在反查表中定位该点。
		float corrected_y = map_y(y_integral, x_integral) * (1 - y_decimal) * (1 - x_decimal)
			+ map_y(y_integral + 1, x_integral) * y_decimal * (1 - x_decimal)
			+ map_y(y_integral, x_integral + 1) * (1 - y_decimal) * x_decimal
			+ map_y(y_integral + 1, x_integral + 1) * y_decimal * x_decimal;

		float corrected_x = map_x(y_integral, x_integral) * (1 - y_decimal) * (1 - x_decimal)
			+ map_x(y_integral + 1, x_integral) * y_decimal * (1 - x_decimal)
			+ map_x(y_integral, x_integral + 1) * (1 - y_decimal) * x_decimal
			+ map_x(y_integral + 1, x_integral + 1) * y_decimal * x_decimal;

		Point2D image_coordinate(corrected_x, corrected_y);

		// 转回传感器坐标系。
		Point2D undistorted_coordinate(image_to_sensor(image_coordinate));
		return undistorted_coordinate;
	}

}//namespace opencorr



