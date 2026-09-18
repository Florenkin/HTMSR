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

#ifndef _ICGN_GPU_H_
#define _ICGN_GPU_H_

#ifdef OPENCORRGPU_EXPORTS
#define OC_API __declspec(dllexport)
#else
#define OC_API __declspec(dllimport)
#endif

#include <vector>
#include "oc_poi.h"

// 模块概览：面向 DLL 调用的 GPU ICGN 接口，用于外部高性能执行。
namespace opencorr
{
	// GPU ICGN 动态库期望使用的扁平 2D 图像缓冲区布局。
	struct Img2D
	{
		int width, height;
		float* data;
	};

	// GPU ICGN 动态库期望使用的扁平 3D 体数据缓冲区布局。
	struct Img3D
	{
		int dim_x, dim_y, dim_z;
		float* data;
	};

	// 本模块实现参考以下文献。
	//L. Zhang et al, Optics and Lasers in Engineering (2015) 69: 7-12.
	//https://doi.org/10.1016/j.optlaseng.2015.01.012

	class OC_API ICGN2D1GPU
	{
	private:
		// 算法说明：调用者面对的是稳定的包装接口，而真正的 GPU 实现则隐藏在这个不透明句柄之后。
		void* _self;

	public:
		ICGN2D1GPU(int subset_radius_x, int subset_radius_y, float conv_criterion, int stop_condition);

		void setImages(Img2D ref_img, Img2D tar_img);
		void setSubset(int radius_x, int radius_y);
		void setIteration(float convergence_criterion, int stop_condition);

		void prepare();
		void compute(std::vector<POI2D>& poi_queue);
	};

	// 本模块实现参考以下文献。
	//A. Lin et al, Optics and Lasers in Engineering (2022) 149: 106812.
	//https://doi.org/10.1016/j.optlaseng.2021.106812

	class OC_API ICGN2D2GPU
	{
	private:
		// 算法说明：公开 API 尽量贴近 CPU 求解器，以降低切换执行后端时的集成成本。
		void* _self;

	public:
		ICGN2D2GPU(int subset_radius_x, int subset_radius_y, float conv_criterion, int stop_condition);

		void setImages(Img2D ref_img, Img2D tar_img);
		void setSubset(int radius_x, int radius_y);
		void setIteration(float convergence_criterion, int stop_condition);

		void prepare();
		void compute(std::vector<POI2D>& poi_queue);
	};

	// 本模块实现参考以下文献。
	//J. Yang et al, Optics and Lasers in Engineering (2021) 136: 106323.
	//https://doi.org/10.1016/j.optlaseng.2020.106323
	
	class OC_API ICGN3D1GPU
	{
	private:
		// 算法说明：扁平缓冲区构成了向预编译 GPU 后端传递大型体数据的最小 ABI。
		void* _self;

	public:
		ICGN3D1GPU(int subset_radius_x, int subset_radius_y, int subset_radius_z, float conv_criterion, int stop_condition);

		void setImages(Img3D ref_img, Img3D tar_img);
		void setSubset(int radius_x, int radius_y, int radius_z);
		void setIteration(float convergence_criterion, int stop_condition);

		void prepare();
		void compute(std::vector<POI3D>& poi_queue);
	};

}//namespace opencorr

#endif //_ICGN_GPU_H_



