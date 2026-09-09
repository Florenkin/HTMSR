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

#ifndef _ARRAY_H_
#define _ARRAY_H_

#include <Eigen/Eigen>

// 模块概览：项目中通用的 Eigen 类型别名和动态数组辅助工具。
typedef Eigen::Matrix<float, 6, 6> Matrix6f;
typedef Eigen::Matrix<float, 12, 12> Matrix12f;
typedef Eigen::Matrix<float, 6, 1> Vector6f;
typedef Eigen::Matrix<float, 4, 1> Vector4f;
// 算法说明：这些定长类型别名让 DIC 求解器中的小规模稠密矩阵系统
// 表达更明确，也更容易让编译器做高效优化。

namespace opencorr
{
	// 2D 数组的创建与释放。
	template <class Real = float>
	Real** new2D(int dimension1, int dimension2) //array[dimension1][dimension2]
	{
		// 算法说明：这些辅助数组既保留了便捷的多级索引方式，
		// 又通过连续内存布局提升数值核心的缓存友好性。
		Real** ptr = nullptr;
		Real* ptr1d = (Real*)calloc(dimension1 * dimension2, sizeof(Real));
		ptr = (Real**)malloc(dimension1 * sizeof(Real*));

		for (int i = 0; i < dimension1; i++)
		{
			ptr[i] = ptr1d + i * dimension2;
		}

		return ptr;
	}

	template <class Real = float>
	void delete2D(Real**& ptr)
	{
		if (ptr == nullptr) return;

		free(ptr);
		ptr = nullptr;
	}

	// 3D 数组的创建与释放。
	template <class Real = float>
	Real*** new3D(int dimension1, int dimension2, int dimension3) //array[dimension1][dimension2][dimension3]
	{
		// 算法说明：很多模块都希望使用 `a[z][y][x]` 这种直观访问方式，
		// 因此这里在易用索引和连续内存布局之间做了兼顾。
		Real*** ptr = nullptr;
		Real* ptr1d = (Real*)calloc(dimension1 * dimension2 * dimension3, sizeof(Real));
		Real** ptr2d = (Real**)malloc(dimension1 * dimension2 * sizeof(Real*));
		ptr = (Real***)malloc(dimension1 * sizeof(Real**));

		for (int i = 0; i < dimension1; i++)
		{
			for (int j = 0; j < dimension2; j++)
			{
				ptr2d[i * dimension2 + j] = ptr1d + (i * dimension2 + j) * dimension3;
			}
			ptr[i] = ptr2d + i * dimension2;
		}

		return ptr;
	}

	template <class Real = float>
	void delete3D(Real***& ptr)
	{
		if (ptr == nullptr) return;

		free(ptr[0]);
		free(ptr);
		ptr = nullptr;
	}

	// 4D 数组的创建与释放。
	template <class Real = float>
	Real**** new4D(int dimension1, int dimension2, int dimension3, int dimension4) //array[dimension1][dimension2][dimension3][dimension4]
	{
		Real**** ptr = nullptr;
		Real* ptr1d = (Real*)calloc(dimension1 * dimension2 * dimension3 * dimension4, sizeof(Real));
		Real** ptr2d = (Real**)malloc(dimension1 * dimension2 * dimension3 * sizeof(Real*));
		Real*** ptr3d = (Real***)malloc(dimension1 * dimension2 * sizeof(Real**));
		ptr = (Real****)malloc(dimension1 * sizeof(Real***));

		for (int i = 0; i < dimension1; i++)
		{
			for (int j = 0; j < dimension2; j++)
			{
				for (int k = 0; k < dimension3; k++)
				{
					ptr2d[(i * dimension2 + j) * dimension3 + k] = ptr1d + ((i * dimension2 + j) * dimension3 + k) * dimension4;
				}
				ptr3d[i * dimension2 + j] = ptr2d + (i * dimension2 + j) * dimension3;
			}
			ptr[i] = ptr3d + i * dimension2;
		}

		return ptr;
	}

	template <class Real = float>
	void delete4D(Real****& ptr)
	{
		if (ptr == nullptr) return;

		free(ptr[0][0][0]);
		free(ptr[0][0]);
		free(ptr[0]);
		free(ptr);
		ptr = nullptr;
	}

}//namespace opencorr

#endif //_ARRAY_H_



