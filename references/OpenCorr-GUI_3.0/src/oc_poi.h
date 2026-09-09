/*
 * This file is part of OpenCorr, an open source C++ library for
 * study and development of 2D, 3D/stereo and volumetric
 * digital image correlation.
 *
 * Copyright (C) 2021-2024, Zhenyu Jiang <zhenyujiang@scut.edu.cn>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one from http://mozilla.org/MPL/2.0/.
 *
 * More information about OpenCorr can be found at https://www.opencorr.org/
 */

#pragma once

#ifndef _POI_H_
#define _POI_H_

#include "oc_deformation.h"

// 模块概览：用于 2D DIC、Stereo DIC 和 DVC 的统一兴趣点数据容器。
namespace opencorr
{
	// POI 内部会复用到的数据结构。
	union DeformationVector2D
	{
		struct
		{
			float u, ux, uy, uxx, uxy, uyy;
			float v, vx, vy, vxx, vxy, vyy;
		};
		// 算法说明：同一套紧凑存储同时支持一阶和二阶 2D 模型，
		// 因而不同求解器可以共享同一种 POI 内存布局。
		float p[12]; //order: u ux uy uxx uxy uyy v vx vy vxx vxy vyy
	};

	// 2D 应变分量的紧凑存储，同时支持命名字段和数组访问。
	union StrainVector2D
	{
		struct
		{
			float exx, eyy, exy;
		};
		float e[3]; //order: exx, eyy, exy
	};

	// 每个 2D POI 的求解元数据，例如初值、质量和迭代状态。
	union Result2D
	{
		struct
		{
			float u0, v0, zncc, iteration, convergence, feature;
		};
		// 算法说明：`result` 同时保存初始化痕迹和求解质量指标，
		// 后续模块据此即可筛选或复用 POI，而不需要额外侧表。
		float r[6];
	};

	// 每个 POI 在多个双目匹配阶段中的元数据。
	union Result2DS
	{
		struct
		{
			float r1r2_zncc, r1t1_zncc, r1t2_zncc, r2_x, r2_y, t1_x, t1_y, t2_x, t2_y;
		};
		// 算法说明：双目流程通常需要同时记录多组匹配质量和中间图像坐标，
		// 这样后续三维重建时可以直接复用这一份复合结果。
		float r[9];
	};

	// 3D 变形参数的紧凑存储，同时支持命名字段和数组访问。
	union DeformationVector3D
	{
		struct
		{
			float u, ux, uy, uz;
			float v, vx, vy, vz;
			float w, wx, wy, wz;
		};
		float p[12]; //order: u ux uy uz v vx vy vz w wx wy wz
	};

	// 用于 Stereo DIC 输出的 3D 位移分量紧凑存储。
	union DisplacementVector3D
	{
		struct
		{
			float u, v, w;
		};
		float p[3];
	};

	// 3D 应变分量的紧凑存储，同时支持命名字段和数组访问。
	union StrainVector3D
	{
		struct
		{
			float exx, eyy, ezz;
			float exy, eyz, ezx;
		};
		float e[6]; //order: exx, eyy, ezz, exy, eyz, ezx
	};

	// 每个 3D POI 的求解元数据，例如初值、质量和迭代状态。
	union Result3D
	{
		struct
		{
			float u0, v0, w0, zncc, iteration, convergence, feature;
		};
		float r[7];
	};

	// 2D DIC 使用的 POI 类型。
	class POI2D : public Point2D
	{
	public:
		// 算法说明：POI2D 会贯穿初始化、迭代相关、应变计算和导出阶段，
		// 因而它承担了 2D 流水线中的主要数据承载职责。
		DeformationVector2D deformation;
		Result2D result;
		StrainVector2D strain;
		Point2D subset_radius;

		inline POI2D(int x, int y) :Point2D(x, y)
		{
			clear();
		}

		inline POI2D(float x, float y) : Point2D(x, y)
		{
			clear();
		}

		inline POI2D(Point2D location) : Point2D(location)
		{
			clear();
		}

		inline ~POI2D() {}

		// 重置除位置外的全部数据。
		inline void clear()
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(result.r), std::end(result.r), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(strain.e), std::end(strain.e), 0.f);
			subset_radius.x = 0.f;
			subset_radius.y = 0.f;
		}
	};


	// Stereo DIC 使用的 POI 类型。
	class POI2DS : public Point2D
	{
	public:
		// 算法说明：POI2DS 同时保留图像域和重建三维域信息，
		// 这样 Stereo DIC 就能把匹配过程与物理量测连接起来。
		DisplacementVector3D deformation;
		Result2DS result;
		Point3D ref_coor, tar_coor;
		StrainVector3D strain;
		Point2D subset_radius;

		inline POI2DS(int x, int y) :Point2D(x, y)
		{
			clear();
		}

		inline POI2DS(float x, float y) : Point2D(x, y)
		{
			clear();
		}

		inline POI2DS(Point2D location) : Point2D(location)
		{
			clear();
		}

		inline ~POI2DS() {}

		// 重置除位置外的全部数据。
		inline void clear()
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(result.r), std::end(result.r), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(strain.e), std::end(strain.e), 0.f);

			ref_coor.x = 0.f;
			ref_coor.y = 0.f;
			ref_coor.z = 0.f;
			tar_coor.x = 0.f;
			tar_coor.y = 0.f;
			tar_coor.z = 0.f;

			subset_radius.x = 0.f;
			subset_radius.y = 0.f;
		}
	};


	// DVC 使用的 POI 类型。
	class POI3D : public Point3D
	{
	public:
		// 算法说明：POI3D 延续了 2D 的设计思路，使体相关流程也能
		// 复用同样的管线理念，只是坐标和邻域都扩展到了三维。
		DeformationVector3D deformation;
		Result3D result;
		StrainVector3D strain;
		Point3D subset_radius;

		inline POI3D(int x, int y, int z) :Point3D(x, y, z)
		{
			clear();
		}

		inline POI3D(float x, float y, float z) : Point3D(x, y, z)
		{
			clear();
		}

		inline POI3D(Point3D location) : Point3D(location)
		{
			clear();
		}

		inline ~POI3D() {}

		// 重置除位置外的全部数据。
		inline void clear()
		{
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(deformation.p), std::end(deformation.p), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(result.r), std::end(result.r), 0.f);
			// 实现当前类型中的一个独立处理步骤或辅助过程。
			std::fill(std::begin(strain.e), std::end(strain.e), 0.f);
			subset_radius.x = 0.f;
			subset_radius.y = 0.f;
			subset_radius.z = 0.f;
		}
	};

}//namespace opencorr

#endif //_POI_H_



