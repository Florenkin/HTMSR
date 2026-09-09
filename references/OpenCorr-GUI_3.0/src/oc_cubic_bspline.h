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

#ifndef  _CUBIC_BSPLINE_H_
#define  _CUBIC_BSPLINE_H_

#include "oc_interpolation.h"

// 模块概览：用于亚像素和亚体素采样的 2D/3D B 样条插值。
namespace opencorr
{
	// 本模块的 2D 部分实现参考以下文献。
	//Z. Pan et al, Theoretical and Applied Mechanics Letters (2016) 6(3): 126-130.
	//https://doi.org/10.1016/j.taml.2016.04.003

	class BicubicBspline : public Interpolation2D
	{
	private:
		// 算法说明：预计算得到的系数块把大部分插值开销前移到 `prepare()`，
		// 这样后续重复采样时只需执行很轻量的局部多项式求值。
		float**** coefficient = nullptr;

		//B
		const float FUNCTION_MATRIX[4][4] =
		{
			{ -1.0f / 6.0f, 3.0f / 6.0f, -3.0f / 6.0f, 1.0f / 6.0f },
			{ 3.0f / 6.0f, -6.0f / 6.0f, 3.0f / 6.0f, 0.0f },
			{ -3.0f / 6.0f, 0.0f, 3.0f / 6.0f, 0.0f },
			{ 1.0f / 6.0f, 4.0f / 6.0f, 1.0f / 6.0f, 0.0f }
		};

		//C
		const float CONTROL_MATRIX[4][4] =
		{
			{ 71.0f / 56.0f, -19.0f / 56.0f, 5.0f / 56.0f, -1.0f / 56.0f },
			{ -19.0f / 56.0f, 95.0f / 56.0f, -25.0f / 56.0f, 5.0f / 56.0f },
			{ 5.0f / 56.0f, -25.0f / 56.0f, 95.0f / 56.0f, -19.0f / 56.0f },
			{ -1.0f / 56.0f, 5.0f / 56.0f, -19.0f / 56.0f, 71.0f / 56.0f }
		};

		//BC = B * C
		const float BC_MATRIX[4][4] =
		{
			{ -144.0f / 336.0f, 384.0f / 336.0f, -384.0f / 336.0f, 144.0f / 336.0f },
			{ 342.0f / 336.0f, -702.0f / 336.0f, 450.0f / 336.0f, -90.0f / 336.0f },
			{ -198.0f / 336.0f, -18.0f / 336.0f, 270.0f / 336.0f, -54.0f / 336.0f },
			{ 0.0f, 1.0f, 0.0f, 0.0f}
		};

	public:
		BicubicBspline(Image2D& image);
		~BicubicBspline();

		void setImage(Image2D& image); //set image to process

		void prepare();
		float compute(Point2D& location);
	};

	// 本模块的 3D 部分实现参考以下文献。
	//J. Yang et al, Optics and Lasers in Engineering (2021) 136: 106323.
	//https://doi.org/10.1016/j.optlaseng.2020.106323

	class TricubicBspline : public Interpolation3D
	{
	private:
		// 算法说明：三次体样条预滤波会把原始体素值转换成样条系数，
		// 从而支持平滑的亚体素插值。
		float*** coefficient = nullptr;

		// B 样条预滤波器系数。
		const float BSPLINE_PREFILTER[8] =
		{
			1.732176555412860f,  //b0
			-0.464135309171000f, //b1
			0.124364681271139f,  //b2
			-0.033323415913556f, //b3
			0.008928982383084f,  //b4
			-0.002392513618779f, //b5
			0.000641072092032f,  //b6
			-0.000171774749350f, //b7
		};

	public:
		TricubicBspline(Image3D& image);
		~TricubicBspline();

		void setImage(Image3D& image); //set image to process

		void prepare();
		float compute(Point3D& location);
	};

}//namespace opencorr

#endif //_CUBIC_BSPLINE_H_



