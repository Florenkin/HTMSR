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

// 模块概览：经典 Newton-Raphson 求解器，主要用于对比和参考。
#include "oc_nr.h"

namespace opencorr
{
	// 实现当前类型中的一个独立处理步骤或辅助过程。
	std::unique_ptr<NR2D1_> NR2D1_::allocate(int subset_radius_x, int subset_radius_y)
	{
		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		std::unique_ptr<NR2D1_> NR_instance = std::make_unique<NR2D1_>();
		NR_instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		NR_instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		NR_instance->tar_gradient_x = Eigen::MatrixXf::Zero(subset_height, subset_width);
		NR_instance->tar_gradient_y = Eigen::MatrixXf::Zero(subset_height, subset_width);
		NR_instance->error_img = Eigen::MatrixXf::Zero(subset_height, subset_width);
		NR_instance->sd_img = new3D(subset_height, subset_width, 6);

		return NR_instance;
	}

	// 显式释放当前对象持有的大型动态资源。
	void NR2D1_::release(std::unique_ptr<NR2D1_>& instance)
	{
		if (instance->sd_img != nullptr)
		{
			delete3D(instance->sd_img);
		}

		instance->ref_subset.reset();
		instance->tar_subset.reset();
	}

	// 参数变化后，刷新派生矩阵、缓存或内部状态。
	void NR2D1_::update(std::unique_ptr<NR2D1_>& instance, int subset_radius_x, int subset_radius_y)
	{
		release(instance);

		int subset_width = 2 * subset_radius_x + 1;
		int subset_height = 2 * subset_radius_y + 1;
		Point2D subset_center(0, 0);

		instance->ref_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->tar_subset = std::make_unique<Subset2D>(subset_center, subset_radius_x, subset_radius_y);
		instance->tar_gradient_x.resize(subset_height, subset_width);
		instance->tar_gradient_y.resize(subset_height, subset_width);
		instance->error_img.resize(subset_height, subset_width);
		instance->sd_img = new3D(subset_height, subset_width, 6);
	}

	// 返回当前对象中的配置值、派生数据或运行时状态。
	std::unique_ptr<NR2D1_>& NR2D1::getInstance(int tid)
	{
		if (tid >= (int)instance_pool.size())
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			throw std::string("CPU thread ID over limit");
		}

		return instance_pool[tid];
	}

	// 构造 NR2D1 对象，并初始化后续步骤需要的状态。
	NR2D1::NR2D1(int subset_radius_x, int subset_radius_y, float conv_criterion, float stop_condition, int thread_number)
		: tar_gradient(nullptr), tar_interp(nullptr), tar_interp_x(nullptr), tar_interp_y(nullptr)
	{
		this->subset_radius_x = subset_radius_x;
		this->subset_radius_y = subset_radius_y;
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
		this->thread_number = thread_number;

		instance_pool.resize(thread_number);
#pragma omp parallel for
		for (int i = 0; i < thread_number; i++)
		{
			instance_pool[i] = NR2D1_::allocate(subset_radius_x, subset_radius_y);
		}
	}

	// 释放 NR2D1 持有的缓冲区、计划或动态资源。
	NR2D1::~NR2D1()
	{
		tar_gradient.reset();
		tar_interp.reset();
		tar_interp_x.reset();
		tar_interp_y.reset();

		for (auto& instance : instance_pool)
		{
			// 显式释放当前对象持有的大型动态资源。
			NR2D1_::release(instance);
			instance.reset();
		}
		std::vector<std::unique_ptr<NR2D1_>>().swap(instance_pool);
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void NR2D1::setIteration(float conv_criterion, float stop_condition)
	{
		this->conv_criterion = conv_criterion;
		this->stop_condition = stop_condition;
	}

	// 更新当前对象使用的配置、输入或运行时状态。
	void NR2D1::setIteration(POI2D* poi)
	{
		conv_criterion = poi->result.convergence;
		stop_condition = (int)poi->result.iteration;
	}

	// 执行主计算前所需的预处理。
	void NR2D1::prepare()
	{
		// 算法说明：NR 不仅需要对灰度做插值，还需要对
		// 目标图像梯度做插值，因为这些梯度会在每次迭代中被重新采样。
		// 构建目标图像的梯度图。
		if (tar_gradient != nullptr)
		{
			tar_gradient.reset();
		}
		tar_gradient = std::make_unique<Gradient2D4>(*tar_img);
		tar_gradient->getGradientX();
		tar_gradient->getGradientY();

		// 构建目标图像的插值系数表。
		if (tar_interp != nullptr)
		{
			tar_interp.reset();
		}
		tar_interp = std::make_unique<BicubicBspline>(*tar_img);
		tar_interp->prepare();

		// 构建 x 方向梯度图的插值系数表。
		Image2D gradient_img(tar_img->width, tar_img->height);
		gradient_img.eg_mat = tar_gradient->gradient_x;

		if (tar_interp_x != nullptr)
		{
			tar_interp_x.reset();
		}
		tar_interp_x = std::make_unique<BicubicBspline>(gradient_img);
		tar_interp_x->prepare();

		// 构建 y 方向梯度图的插值系数表。
		gradient_img.eg_mat = tar_gradient->gradient_y;

		if (tar_interp_y != nullptr)
		{
			tar_interp_y.reset();
		}
		tar_interp_y = std::make_unique<BicubicBspline>(gradient_img);
		tar_interp_y->prepare();
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void NR2D1::compute(POI2D* poi)
	{
		// 根据线程编号获取当前线程对应的实例。
		std::unique_ptr<NR2D1_>& fa_nr = getInstance(omp_get_thread_num());

		if (poi->y - subset_radius_y < 0 || poi->x - subset_radius_x < 0
			|| poi->y + subset_radius_y > ref_img->height - 1 || poi->x + subset_radius_x > ref_img->width - 1
			|| fabs(poi->deformation.u) >= ref_img->width || fabs(poi->deformation.v) >= ref_img->height
			|| poi->result.zncc < 0 || std::isnan(poi->deformation.u) || std::isnan(poi->deformation.v))
		{
			poi->result.zncc = poi->result.zncc < -1 ? poi->result.zncc : -1;
		}
		else
		{
			int subset_width = 2 * subset_radius_x + 1;
			int subset_height = 2 * subset_radius_y + 1;

			// 构造参考子区。
			fa_nr->ref_subset->center = (Point2D)*poi;
			fa_nr->ref_subset->fill(ref_img);
			float ref_mean_norm = fa_nr->ref_subset->zeroMeanNorm();

			// 构造目标子区。
			fa_nr->tar_subset->center = (Point2D)*poi;

			// 读取初始猜测。
			Deformation2D1 p_initial(poi->deformation.u, poi->deformation.ux, poi->deformation.uy, poi->deformation.v, poi->deformation.vx, poi->deformation.vy);

			// 算法说明：这是前向加性形式的 Newton-Raphson 变体，
			// 因此每次 warp 更新后都要重新构造目标子区及其梯度。
			// Newton-Raphson 迭代。
			int iteration_counter = 0; // 初始化迭代计数器
			Deformation2D1 p_current, p_increment;
			p_current.setDeformation(p_initial);
			float dp_norm_max, znssd;
			do
			{
				iteration_counter++;
				// 算法说明：在循环内部重新采样目标梯度是其关键差异
				// 这正是它与 ICGN 的主要差别，也是单次迭代成本更高的原因。
				// 重建形变后的目标子区及其对应的梯度矩阵。
				for (int r = 0; r < subset_height; r++)
				{
					for (int c = 0; c < subset_width; c++)
					{
						int x_local = c - subset_radius_x;
						int y_local = r - subset_radius_y;
						Point2D local_coor(x_local, y_local);
						Point2D warped_coor = p_current.warp(local_coor);
						Point2D global_coor = fa_nr->tar_subset->center + warped_coor;

						fa_nr->tar_subset->eg_mat(r, c) = tar_interp->compute(global_coor);
						fa_nr->tar_gradient_x(r, c) = tar_interp_x->compute(global_coor);
						fa_nr->tar_gradient_y(r, c) = tar_interp_y->compute(global_coor);
					}
				}
				float tar_mean_norm = fa_nr->tar_subset->zeroMeanNorm();

				// 算法说明：Hessian 依赖当前变形后的目标
				// 梯度，因此在 NR 中每次迭代都必须重新构建。
				// 构建 Hessian 矩阵。
				fa_nr->hessian.setZero();
				for (int r = 0; r < subset_height; r++)
				{
					for (int c = 0; c < subset_width; c++)
					{
						int x_local = c - subset_radius_x;
						int y_local = r - subset_radius_y;
						float tar_grad_x = fa_nr->tar_gradient_x(r, c);
						float tar_grad_y = fa_nr->tar_gradient_y(r, c);

						fa_nr->sd_img[r][c][0] = tar_grad_x;
						fa_nr->sd_img[r][c][1] = tar_grad_x * x_local;
						fa_nr->sd_img[r][c][2] = tar_grad_x * y_local;
						fa_nr->sd_img[r][c][3] = tar_grad_y;
						fa_nr->sd_img[r][c][4] = tar_grad_y * x_local;
						fa_nr->sd_img[r][c][5] = tar_grad_y * y_local;

						for (int i = 0; i < 6; i++)
						{
							for (int j = 0; j < 6; j++)
							{
								fa_nr->hessian(i, j) += (fa_nr->sd_img[r][c][i] * fa_nr->sd_img[r][c][j]);
							}
						}
					}
				}

				// 计算 Hessian 的逆矩阵。
				fa_nr->inv_hessian = fa_nr->hessian.inverse();

				// 计算残差图像。
				fa_nr->error_img = fa_nr->ref_subset->eg_mat * (tar_mean_norm / ref_mean_norm) - (fa_nr->tar_subset->eg_mat);

				// 计算 ZNSSD。
				znssd = fa_nr->error_img.squaredNorm() / (tar_mean_norm * tar_mean_norm);

				// 计算增量方程右端项。
				float numerator[6] = { 0.f };
				for (int r = 0; r < subset_height; r++)
				{
					for (int c = 0; c < subset_width; c++)
					{
						for (int i = 0; i < 6; i++)
						{
							numerator[i] += (fa_nr->sd_img[r][c][i] * fa_nr->error_img(r, c));
						}
					}
				}

				// 计算参数增量 dp。
				float dp[6] = { 0.f };
				for (int i = 0; i < 6; i++)
				{
					for (int j = 0; j < 6; j++)
					{
						dp[i] += (fa_nr->inv_hessian(i, j) * numerator[j]);
					}
				}
				p_increment.setDeformation(dp);

				// 算法说明：NR 会把求出的增量直接加到
				// 参数向量上，而不是像 ICGN 那样做逆 warp 组合。
				// 更新参数 p。
				p_current.setDeformation(p_current.u + p_increment.u, p_current.ux + p_increment.ux, p_current.uy + p_increment.uy,
					p_current.v + p_increment.v, p_current.vx + p_increment.vx, p_current.vy + p_increment.vy);

				// 检查收敛性。
				int subset_radius_x2 = subset_radius_x * subset_radius_x;
				int subset_radius_y2 = subset_radius_y * subset_radius_y;

				dp_norm_max = 0.f;
				dp_norm_max += p_increment.u * p_increment.u;
				dp_norm_max += p_increment.ux * p_increment.ux * subset_radius_x2;
				dp_norm_max += p_increment.uy * p_increment.uy * subset_radius_y2;
				dp_norm_max += p_increment.v * p_increment.v;
				dp_norm_max += p_increment.vx * p_increment.vx * subset_radius_x2;
				dp_norm_max += p_increment.vy * p_increment.vy * subset_radius_y2;

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
		}

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
			poi->result.zncc = -5;
		}
	}

	// 执行当前模块的核心计算，并将结果写回输入对象。
	void NR2D1::compute(std::vector<POI2D>& poi_queue)
	{
		auto queue_length = poi_queue.size();
#pragma omp parallel for
		for (int i = 0; i < queue_length; i++)
		{
			compute(&poi_queue[i]);
		}
	}

}//namespace opencorr



