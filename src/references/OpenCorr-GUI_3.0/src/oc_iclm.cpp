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

// 模块概览：通过加入阻尼来提升困难情况下鲁棒性的 ICLM 求解器。
#include "oc_iclm.h"

namespace opencorr
{
	// 实现当前类型中的一个独立处理步骤或辅助过程。
	std::unique_ptr<ICLM2D1_> ICLM2D1_::allocate(int subset_radius_x, int subset_radius_y)
	{
		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		std::unique_ptr<ICLM2D1_> ICLM_instance = std::make_unique<ICLM2D1_>();
		ICLM_instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		ICLM_instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		ICLM_instance->error_img = Eigen::MatrixXf::Zero(subset_height, subset_width);
		ICLM_instance->sd_img = new3D(subset_height, subset_width, 6);

		return ICLM_instance;
	}

	// 显式释放当前对象持有的大型动态资源。
	void ICLM2D1_::release(std::unique_ptr<ICLM2D1_>& instance)
	{
		if (instance->sd_img != nullptr)
		{
			delete3D(instance->sd_img);
		}
		instance->ref_subset.reset();
		instance->ref_subset.reset();
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void ICLM2D1_::update(std::unique_ptr<ICLM2D1_>& instance, int subset_radius_x, int subset_radius_y)
	{
		release(instance);

		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->error_img.resize(subset_height, subset_width);
		instance->sd_img = new3D(subset_height, subset_width, 6);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	std::unique_ptr<ICLM2D1_>& ICLM2D1::getInstance(int tid)
	{
		if (tid >= (int)instance_pool.size())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("CPU thread ID over limit");
		}

		return instance_pool[tid];
	}

	// 构造 ICLM2D1 对象，并初始化后续步骤需要的状态。
	ICLM2D1::ICLM2D1(int subset_radius_x, int subset_radius_y, float conv_criterion, float stop_condition, int thread_number)
		: ref_gradient(nullptr), tar_interp(nullptr)
	{
		this->subset_radius_x = subset_radius_x;
		this->subset_radius_y = subset_radius_y;
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
		this->thread_number = thread_number;

		self_adaptive = false;

		instance_pool.resize(thread_number);
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i] = ICLM2D1_::allocate(subset_radius_x, subset_radius_y);
		}
	}

	// 释放 ICLM2D1 持有的缓冲区、计划或动态资源。
	ICLM2D1::~ICLM2D1()
	{
		ref_gradient.reset();
		tar_interp.reset();

		for (auto& instance : instance_pool)
		{
			// Explicitly frees large dynamic resources held by the current object.
			ICLM2D1_::release(instance);
			instance.reset();
		}
		std::vector<std::unique_ptr<ICLM2D1_>>().swap(instance_pool);
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D1::setIteration(float conv_criterion, float stop_condition)
	{
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D1::setIteration(POI2D* poi)
	{
		conv_criterion = poi->result.convergence;
		stop_condition = (int)poi->result.iteration;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D1::setDamping(float lambda, float alpha, float beta)
	{
		damping.lambda = lambda;
		damping.alpha = alpha;
		damping.beta = beta;
	}

	// 预计算由参考图像导出的数据。
	void ICLM2D1::prepareRef()
	{
		if (ref_gradient != nullptr)
		{
			ref_gradient.reset();
		}

		ref_gradient = std::make_unique<Gradient2D4>(*ref_img);
		ref_gradient->getGradientX();
		ref_gradient->getGradientY();
	}

	// 预计算由目标图像导出的数据。
	void ICLM2D1::prepareTar()
	{
		if (tar_interp != nullptr)
		{
			tar_interp.reset();
		}

		tar_interp = std::make_unique<BicubicBspline>(*tar_img);
		tar_interp->prepare();
	}

	// 执行主计算前所需的预处理。
	void ICLM2D1::prepare()
	{
		prepareRef();
		prepareTar();
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void ICLM2D1::compute(POI2D* poi)
	{
		// 根据线程编号获取当前线程对应的实例。
		std::unique_ptr<ICLM2D1_>& iclm = getInstance(omp_get_thread_num());

		int subset_rx = subset_radius_x;
		int subset_ry = subset_radius_y;

		if (self_adaptive)
		{
			// 按当前 POI 的子区尺寸更新线程实例。
			ICLM2D1_::update(iclm, poi->subset_radius.x, poi->subset_radius.y);
			subset_rx = poi->subset_radius.x;
			subset_ry = poi->subset_radius.y;
		}

		if (poi->y - subset_ry < 0 || poi->x - subset_rx < 0
			|| poi->y + subset_ry > ref_img->height - 1 || poi->x + subset_rx > ref_img->width - 1
			|| fabs(poi->deformation.u) >= ref_img->width || fabs(poi->deformation.v) >= ref_img->height
			|| poi->result.zncc < 0 || std::isnan(poi->deformation.u) || std::isnan(poi->deformation.v))
		{
			poi->result.zncc = poi->result.zncc >= 0 ? -3.f : poi->result.zncc;
			return;
		}

		// 设置当前子区尺寸。
		int subset_width = 2 * subset_rx + 1;
		int subset_height = 2 * subset_ry + 1;

		// 构造参考子区。
		iclm->ref_subset->center = (Point2D)*poi;
		iclm->ref_subset->fill(ref_img);
		float ref_mean_norm = iclm->ref_subset->zeroMeanNorm();

		// 算法说明：与 ICGN 一样，最速下降图像和 Hessian 都只在参考子区上构建一次，
		// 因此后续每一步迭代的代价都能保持较低。
		// 构建 Hessian 矩阵。
		iclm->hessian.setZero();
		for (int r = 0; r < subset_height; r++)
		{
			for (int c = 0; c < subset_width; c++)
			{
				int x_local = c - subset_rx;
				int y_local = r - subset_ry;
				int x_global = (int)poi->x + x_local;
				int y_global = (int)poi->y + y_local;
				float ref_gradient_x = ref_gradient->gradient_x(y_global, x_global);
				float ref_gradient_y = ref_gradient->gradient_y(y_global, x_global);

				iclm->sd_img[r][c][0] = ref_gradient_x;
				iclm->sd_img[r][c][1] = ref_gradient_x * x_local;
				iclm->sd_img[r][c][2] = ref_gradient_x * y_local;
				iclm->sd_img[r][c][3] = ref_gradient_y;
				iclm->sd_img[r][c][4] = ref_gradient_y * x_local;
				iclm->sd_img[r][c][5] = ref_gradient_y * y_local;

				for (int i = 0; i < 6; i++)
				{
					for (int j = 0; j < i + 1; j++)
					{
						iclm->hessian(i, j) += (iclm->sd_img[r][c][i] * iclm->sd_img[r][c][j]);
						iclm->hessian(j, i) = iclm->hessian(i, j);
					}
				}
			}
		}

		// 构造目标子区。
		iclm->tar_subset->center = (Point2D)*poi;

		// 读取初始猜测。
		Deformation2D1 p_initial(poi->deformation.u, poi->deformation.ux, poi->deformation.uy, poi->deformation.v, poi->deformation.vx, poi->deformation.vy);

		// 算法说明：ICLM 用带阻尼的最小二乘步替代了朴素的反向组合更新，
		// 当残差上升时它可以自动退让，从而提升鲁棒性。
		// IC-LM 迭代。
		int iteration_counter = 0; // 初始化迭代计数器
		Deformation2D1 p_current, p_increment;
		p_current.setDeformation(p_initial);
		Point2D local_coor, warped_coor, global_coor;
		float dp_norm_max, znssd, current_lambda;
		float znssd0 = 4.f;
		Matrix6f identity_mat;
		identity_mat.setIdentity();

		do
		{
			iteration_counter++;

			// 重建目标子区。
			for (int r = 0; r < subset_height; r++)
			{
				for (int c = 0; c < subset_width; c++)
				{
					int x_local = c - subset_rx;
					int y_local = r - subset_ry;
					local_coor.x = x_local;
					local_coor.y = y_local;
					warped_coor = p_current.warp(local_coor);
					global_coor = iclm->tar_subset->center + warped_coor;
					iclm->tar_subset->eg_mat(r, c) = tar_interp->compute(global_coor);
				}
			}

			float tar_mean_norm = iclm->tar_subset->zeroMeanNorm();

			// 计算残差图像。
			iclm->error_img = iclm->tar_subset->eg_mat * (ref_mean_norm / tar_mean_norm) - (iclm->ref_subset->eg_mat);

			// 计算 ZNSSD。
			znssd = iclm->error_img.squaredNorm() / (ref_mean_norm * ref_mean_norm);

			// 算法说明：初始阻尼大小与当前残差相关联，
			// 因而较差的初值会自动以更保守的步长起步。
			// 如果这是第一步迭代。
			if (iteration_counter == 1)
			{
				// 计算初始 lambda。
				current_lambda = powf(damping.lambda, znssd / znssd0) - 1.f;
			}

			// 算法说明：加入 `lambda * I` 后，lambda 大时更接近梯度下降，
			// lambda 小时则更接近 Gauss-Newton。
			// 计算阻尼后的逆 Hessian。
			iclm->inv_hessian = (iclm->hessian + current_lambda * identity_mat).inverse();

			// 计算增量方程右端项。
			float numerator[6] = { 0.f };
			for (int r = 0; r < subset_height; r++)
			{
				for (int c = 0; c < subset_width; c++)
				{
					for (int i = 0; i < 6; i++)
					{
						numerator[i] += (iclm->sd_img[r][c][i] * iclm->error_img(r, c));
					}
				}
			}

			// 计算参数增量 dp。
			float dp[6] = { 0.f };
			for (int i = 0; i < 6; i++)
			{
				for (int j = 0; j < 6; j++)
				{
					dp[i] += (iclm->inv_hessian(i, j) * numerator[j]);
				}
			}
			p_increment.setDeformation(dp);

			// 算法说明：只有当归一化残差得到改善时才接受本次更新，
			// 否则就拒绝该步并增大阻尼。
			// 根据当前 znssd 更新 lambda 和形变向量。
			if (znssd < znssd0)
			{
				// 算法说明：当 `alpha < 1` 时，成功步会减小阻尼，
				// 求解器就能逐步回到更快的 Gauss-Newton 区域。
				// 更新 lambda。
				current_lambda = current_lambda * damping.alpha;

				// 算法说明：被接受的增量会以逆组合方式与当前 warp 结合，
				// 从而保持反向组合更新结构不变。
				// 更新 warp。
				p_current.warp_matrix = p_current.warp_matrix * p_increment.warp_matrix.inverse();

				// 回写显式参数。
				p_current.setDeformation();

				// 更新参考残差 znssd0。
				znssd0 = znssd;
			}
			else
			{
				// 算法说明：当 `beta > 1` 时，失败步会增大阻尼，
				// 让下一次更新更小，从而提升鲁棒性。
				current_lambda = current_lambda * damping.beta;
			}

			// 检查收敛性。
			int subset_rx2 = subset_rx * subset_rx;
			int subset_ry2 = subset_ry * subset_ry;

			dp_norm_max = p_increment.u * p_increment.u
				+ p_increment.ux * p_increment.ux * subset_rx2
				+ p_increment.uy * p_increment.uy * subset_ry2
				+ p_increment.v * p_increment.v
				+ p_increment.vx * p_increment.vx * subset_rx2
				+ p_increment.vy * p_increment.vy * subset_ry2;

			dp_norm_max = sqrt(dp_norm_max);
		} while (iteration_counter < stop_condition && dp_norm_max >= conv_criterion);

		// 保存最终结果。
		poi->deformation.u = p_current.u;
		poi->deformation.ux = p_current.ux;
		poi->deformation.uy = p_current.uy;
		poi->deformation.v = p_current.v;
		poi->deformation.vx = p_current.vx;
		poi->deformation.vy = p_current.vy;

		// 保存输出所需参数。
		poi->result.u0 = p_initial.u;
		poi->result.v0 = p_initial.v;
		poi->result.zncc = 0.5f * (2 - znssd);
		poi->result.iteration = (float)iteration_counter;
		poi->result.convergence = dp_norm_max;

		// 保存子区尺寸。
		poi->subset_radius.x = subset_rx;
		poi->subset_radius.y = subset_ry;

		// 检查是否在要求范围内收敛。
		if (poi->result.convergence >= conv_criterion && poi->result.iteration >= stop_condition)
		{
			poi->result.zncc = -4.f;
		}

		// 检查 ZNCC 或位移中是否出现 NaN。
		if (std::isnan(poi->result.zncc) || std::isnan(poi->deformation.u) || std::isnan(poi->deformation.v))
		{
			poi->deformation.u = poi->result.u0;
			poi->deformation.v = poi->result.v0;
			poi->result.zncc = -5.f;
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void ICLM2D1::compute(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}




	// 实现当前类型中的一个独立处理步骤或辅助过程。
	std::unique_ptr<ICLM2D2_> ICLM2D2_::allocate(int subset_radius_x, int subset_radius_y)
	{
		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		std::unique_ptr<ICLM2D2_> ICLM_instance = std::make_unique<ICLM2D2_>();
		ICLM_instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		ICLM_instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		ICLM_instance->error_img = Eigen::MatrixXf::Zero(subset_height, subset_width);
		ICLM_instance->sd_img = new3D(subset_height, subset_width, 12);

		return ICLM_instance;
	}

	// 显式释放当前对象持有的大型动态资源。
	void ICLM2D2_::release(std::unique_ptr<ICLM2D2_>& instance)
	{
		if (instance->sd_img != nullptr)
		{
			delete3D(instance->sd_img);
		}
		instance->ref_subset.reset();
		instance->tar_subset.reset();
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void ICLM2D2_::update(std::unique_ptr<ICLM2D2_>& instance, int subset_radius_x, int subset_radius_y)
	{
		release(instance);

		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->error_img.resize(subset_height, subset_width);
		instance->sd_img = new3D(subset_height, subset_width, 12);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	std::unique_ptr<ICLM2D2_>& ICLM2D2::getInstance(int tid)
	{
		if (tid >= (int)instance_pool.size())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("CPU thread ID over limit");
		}

		return instance_pool[tid];
	}

	// 构造 ICLM2D2 对象，并初始化后续步骤需要的状态。
	ICLM2D2::ICLM2D2(int subset_radius_x, int subset_radius_y, float conv_criterion, float stop_condition, int thread_number)
		: ref_gradient(nullptr), tar_interp(nullptr)
	{
		this->subset_radius_x = subset_radius_x;
		this->subset_radius_y = subset_radius_y;
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
		this->thread_number = thread_number;

		self_adaptive = false;

		instance_pool.resize(thread_number);
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i] = ICLM2D2_::allocate(subset_radius_x, subset_radius_y);
		}
	}

	// 释放 ICLM2D2 持有的缓冲区、计划或动态资源。
	ICLM2D2::~ICLM2D2()
	{
		ref_gradient.reset();
		tar_interp.reset();

		for (auto& instance : instance_pool)
		{
			// Explicitly frees large dynamic resources held by the current object.
			ICLM2D2_::release(instance);
			instance.reset();
		}
		std::vector<std::unique_ptr<ICLM2D2_>>().swap(instance_pool);
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D2::setIteration(float conv_criterion, float stop_condition)
	{
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D2::setIteration(POI2D* poi)
	{
		conv_criterion = poi->result.convergence;
		stop_condition = poi->result.iteration;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void ICLM2D2::setDamping(float lambda, float alpha, float beta)
	{
		damping.lambda = lambda;
		damping.alpha = alpha;
		damping.beta = beta;
	}

	// 预计算由参考图像导出的数据。
	void ICLM2D2::prepareRef()
	{
		if (ref_gradient != nullptr)
		{
			ref_gradient.reset();
		}

		ref_gradient = std::make_unique<Gradient2D4>(*ref_img);
		ref_gradient->getGradientX();
		ref_gradient->getGradientY();
	}

	// 预计算由目标图像导出的数据。
	void ICLM2D2::prepareTar()
	{
		if (tar_interp != nullptr)
		{
			tar_interp.reset();
		}

		tar_interp = std::make_unique<BicubicBspline>(*tar_img);
		tar_interp->prepare();
	}

	// 执行主计算前所需的预处理。
	void ICLM2D2::prepare()
	{
		prepareRef();
		prepareTar();
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void ICLM2D2::compute(POI2D* poi)
	{
		// 根据线程编号获取当前线程对应的实例。
		std::unique_ptr<ICLM2D2_>& iclm = getInstance(omp_get_thread_num());

		int subset_rx = subset_radius_x;
		int subset_ry = subset_radius_y;

		if (self_adaptive)
		{
			// 按当前 POI 的子区尺寸更新线程实例。
			ICLM2D2_::update(iclm, poi->subset_radius.x, poi->subset_radius.y);
			subset_rx = poi->subset_radius.x;
			subset_ry = poi->subset_radius.y;
		}

		if (poi->y - subset_ry < 0 || poi->x - subset_rx < 0
			|| poi->y + subset_ry > ref_img->height - 1 || poi->x + subset_rx > ref_img->width - 1
			|| fabs(poi->deformation.u) >= ref_img->width || fabs(poi->deformation.v) >= ref_img->height
			|| poi->result.zncc < 0 || std::isnan(poi->deformation.u) || std::isnan(poi->deformation.v))
		{
			poi->result.zncc = poi->result.zncc >= 0 ? -3.f : poi->result.zncc;
			return;
		}
		int subset_width = 2 * subset_rx + 1;
		int subset_height = 2 * subset_ry + 1;

		// 构造参考子区。
		iclm->ref_subset->center = (Point2D)*poi;
		iclm->ref_subset->fill(ref_img);
		float ref_mean_norm = iclm->ref_subset->zeroMeanNorm();

		// 算法说明：二阶求解器仍然只在参考侧预计算一次 Hessian，
		// 只是此时参数向量已经扩展到包含二次 warp 项。
		// 构建 Hessian 矩阵。
		iclm->hessian.setZero();
		for (int r = 0; r < subset_height; r++)
		{
			for (int c = 0; c < subset_width; c++)
			{
				int x_local = c - subset_rx;
				int y_local = r - subset_ry;
				float xx_local = (x_local * x_local) * 0.5f;
				float xy_local = (float)(x_local * y_local);
				float yy_local = (y_local * y_local) * 0.5f;
				int x_global = (int)poi->x + x_local;
				int y_global = (int)poi->y + y_local;
				float ref_gradient_x = ref_gradient->gradient_x(y_global, x_global);
				float ref_gradient_y = ref_gradient->gradient_y(y_global, x_global);

				iclm->sd_img[r][c][0] = ref_gradient_x;
				iclm->sd_img[r][c][1] = ref_gradient_x * x_local;
				iclm->sd_img[r][c][2] = ref_gradient_x * y_local;
				iclm->sd_img[r][c][3] = ref_gradient_x * xx_local;
				iclm->sd_img[r][c][4] = ref_gradient_x * xy_local;
				iclm->sd_img[r][c][5] = ref_gradient_x * yy_local;

				iclm->sd_img[r][c][6] = ref_gradient_y;
				iclm->sd_img[r][c][7] = ref_gradient_y * x_local;
				iclm->sd_img[r][c][8] = ref_gradient_y * y_local;
				iclm->sd_img[r][c][9] = ref_gradient_y * xx_local;
				iclm->sd_img[r][c][10] = ref_gradient_y * xy_local;
				iclm->sd_img[r][c][11] = ref_gradient_y * yy_local;

				for (int i = 0; i < 12; i++)
				{
					for (int j = 0; j < i + 1; j++)
					{
						iclm->hessian(i, j) += (iclm->sd_img[r][c][i] * iclm->sd_img[r][c][j]);
						iclm->hessian(j, i) = iclm->hessian(i, j);
					}
				}
			}
		}

		// 构造目标子区。
		iclm->tar_subset->center = (Point2D)*poi;

		// 读取初始猜测。
		Deformation2D1 p_initial(poi->deformation.u, poi->deformation.ux, poi->deformation.uy, poi->deformation.v, poi->deformation.vx, poi->deformation.vy);

		// 算法说明：这个循环整体沿用一阶 ICLM 的逻辑，
		// 只是阻尼现在用来稳定更大的二阶 warp 参数空间。
		// IC-LM 迭代。
		int iteration_counter = 0; // 初始化迭代计数器
		Deformation2D2 p_current, p_increment;
		p_current.setDeformation(p_initial);
		Point2D local_coor, warped_coor, global_coor;
		float dp_norm_max, znssd, current_lambda;
		float znssd0 = 4.f;
		Matrix12f identity_mat;
		identity_mat.setIdentity();

		do
		{
			iteration_counter++;
			// 重建目标子区。
			for (int r = 0; r < subset_height; r++)
			{
				for (int c = 0; c < subset_width; c++)
				{
					int x_local = c - subset_rx;
					int y_local = r - subset_ry;
					local_coor.x = x_local;
					local_coor.y = y_local;
					warped_coor = p_current.warp(local_coor);
					global_coor = iclm->tar_subset->center + warped_coor;
					iclm->tar_subset->eg_mat(r, c) = tar_interp->compute(global_coor);
				}
			}
			float tar_mean_norm = iclm->tar_subset->zeroMeanNorm();

			// 计算残差图像。
			iclm->error_img = iclm->tar_subset->eg_mat * (ref_mean_norm / tar_mean_norm) - (iclm->ref_subset->eg_mat);

			// 计算 ZNSSD。
			znssd = iclm->error_img.squaredNorm() / (ref_mean_norm * ref_mean_norm);

			// 算法说明：第一步残差同样会为二阶模型初始化阻尼尺度。
			// 如果这是第一步迭代。
			if (iteration_counter == 1)
			{
				// 计算初始 lambda。
				current_lambda = powf(damping.lambda, znssd / znssd0) - 1;
			}

			// 算法说明：阻尼逆矩阵可以避免在局部拟合不可靠时，
			// 二次 warp 更新变得过于激进。
			// 计算阻尼后的逆 Hessian。
			iclm->inv_hessian = (iclm->hessian + current_lambda * identity_mat).inverse();

			// 计算增量方程右端项。
			float numerator[12] = { 0.f };
			for (int r = 0; r < subset_height; r++)
			{
				for (int c = 0; c < subset_width; c++)
				{
					for (int i = 0; i < 12; i++)
					{
						numerator[i] += (iclm->sd_img[r][c][i] * iclm->error_img(r, c));
					}
				}
			}

			// 计算参数增量 dp。
			float dp[12] = { 0.f };
			for (int i = 0; i < 12; i++)
			{
				for (int j = 0; j < 12; j++)
				{
					dp[i] += (iclm->inv_hessian(i, j) * numerator[j]);
				}
			}
			p_increment.setDeformation(dp);

			// 算法说明：成功步会被接受并让求解器更大胆，
			// 失败步则会被拒绝，同时提高阻尼。
			// 根据当前 znssd 更新 lambda 和形变向量。
			if (znssd < znssd0)
			{
				// 算法说明：当残差改善后减小 lambda，
				// 方法会更接近无阻尼的反向组合解。
				// 更新 lambda。
				current_lambda = current_lambda * damping.alpha;

				// 算法说明：更新依然通过逆 warp 组合完成，
				// 而不是直接把参数增量相加。
				// 更新 warp。
				p_current.warp_matrix = p_current.warp_matrix * p_increment.warp_matrix.inverse();

				// 回写显式参数。
				p_current.setDeformation();

				// 更新参考残差 znssd0。
				znssd0 = znssd;
			}
			else
			{
				// 算法说明：增大 lambda 会抑制不稳定的二阶增量，
				// 直到残差重新开始下降。
				current_lambda = current_lambda * damping.beta;
			}

			// 检查收敛性。
			int subset_rx2 = subset_rx * subset_rx;
			int subset_ry2 = subset_ry * subset_ry;
			int subset_radius_xy2 = subset_rx2 * subset_ry2;

			dp_norm_max = p_increment.u * p_increment.u
				+ p_increment.ux * p_increment.ux * subset_rx2
				+ p_increment.uy * p_increment.uy * subset_ry2
				+ p_increment.uxx * p_increment.uxx * subset_rx2 * subset_rx2 * 0.25f
				+ p_increment.uyy * p_increment.uyy * subset_ry2 * subset_ry2 * 0.25f
				+ p_increment.uxy * p_increment.uxy * subset_radius_xy2
				+ p_increment.v * p_increment.v
				+ p_increment.vx * p_increment.vx * subset_rx2
				+ p_increment.vy * p_increment.vy * subset_ry2
				+ p_increment.vxx * p_increment.vxx * subset_rx2 * subset_rx2 * 0.25f
				+ p_increment.vyy * p_increment.vyy * subset_ry2 * subset_ry2 * 0.25f
				+ p_increment.vxy * p_increment.vxy * subset_radius_xy2;

			dp_norm_max = sqrt(dp_norm_max);
		} while (iteration_counter < stop_condition && dp_norm_max >= conv_criterion);

		// 保存最终结果。
		poi->deformation.u = p_current.u;
		poi->deformation.ux = p_current.ux;
		poi->deformation.uy = p_current.uy;
		poi->deformation.uxx = p_current.uxx;
		poi->deformation.uxy = p_current.uxy;
		poi->deformation.uyy = p_current.uyy;

		poi->deformation.v = p_current.v;
		poi->deformation.vx = p_current.vx;
		poi->deformation.vy = p_current.vy;
		poi->deformation.vxx = p_current.vxx;
		poi->deformation.vxy = p_current.vxy;
		poi->deformation.vyy = p_current.vyy;

		// 保存输出所需参数。
		poi->result.u0 = p_initial.u;
		poi->result.v0 = p_initial.v;
		poi->result.zncc = 0.5f * (2 - znssd);
		poi->result.iteration = (float)iteration_counter;
		poi->result.convergence = dp_norm_max;

		// 保存子区尺寸。
		poi->subset_radius.x = subset_rx;
		poi->subset_radius.y = subset_ry;

		// 检查是否在要求范围内收敛。
		if (poi->result.convergence >= conv_criterion && poi->result.iteration >= stop_condition)
		{
			poi->result.zncc = -4.f;
		}

		// 检查 ZNCC 或位移中是否出现 NaN。
		if (std::isnan(poi->result.zncc) || std::isnan(poi->deformation.u) || std::isnan(poi->deformation.v))
		{
			poi->deformation.u = poi->result.u0;
			poi->deformation.v = poi->result.v0;
			poi->result.zncc = -5.f;
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void ICLM2D2::compute(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}

}//namespace opencorr



