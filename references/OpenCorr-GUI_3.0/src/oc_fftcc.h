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

#ifndef _FFTCC_H_
#define _FFTCC_H_

#include <vector>
#include "fftw3.h"

// 模块概览：基于 FFT 的互相关初始化，以及面向多线程的 FFTW 实例池。
#include "oc_dic.h"

namespace opencorr
{
	// 持有 FFTCC 模块复用的 FFTW 缓冲区与执行计划。
	class FFTW
	{
	public:
		float* ref_subset;
		float* tar_subset;
		float* zncc;
		fftwf_complex* ref_freq;
		fftwf_complex* tar_freq;
		fftwf_complex* zncc_freq;
		fftwf_plan ref_plan;
		fftwf_plan tar_plan;
		fftwf_plan zncc_plan;

		static std::unique_ptr<FFTW> allocate(int subset_radius_x, int subset_radius_y);
		static std::unique_ptr<FFTW> allocate(int subset_radius_x, int subset_radius_y, int subset_radius_z);

		static void release(std::unique_ptr<FFTW>& instance);

		static void update(std::unique_ptr<FFTW>& instance, int subset_radius_x, int subset_radius_y);
		static void update(std::unique_ptr<FFTW>&, int subset_radius_x, int subset_radius_y, int subset_radius_z);
	};


	// 本模块的 2D 部分实现了下述文献中的方法。
	//Z. Jiang et al, Optics and Lasers in Engineering (2015) 65: 93-102.
	//https://doi.org/10.1016/j.optlaseng.2014.06.011

	// 算法说明：FFTCC 主要用作粗初始化，而不是最终的高精度求解器。
	// 算法说明：它的任务是在迭代细化前找出最能对齐两个局部子区的整数像素位移。
	class FFTCC2D : public DIC
	{
	private:
		std::vector<std::unique_ptr<FFTW>> instance_pool; //pool of FFTW instances for multi-thread processing
		std::unique_ptr<FFTW>& getInstance(int tid); //get an instance according to the number of current thread id

	public:
		FFTCC2D(int subset_radius_x, int subset_radius_y, int thread_number);
		~FFTCC2D();

		void prepare();

		void compute(POI2D* poi);
		void compute(std::vector<POI2D>& poi_queue);
	};


	// 本模块的 3D 部分实现了下述文献中的方法。
	//T. Wang et al, Experimental Mechanics (2016) 56(2): 297-309.
	//https://doi.org/10.1007/s11340-015-0091-4

	// 算法说明：3D 版本沿用了 2D FFTCC 的思路，但在体数据中搜索的是完整位移立方体。
	// 算法说明：它为后续 3D ICGN 细化提供了一个实用的初始值。
	class FFTCC3D : public DVC
	{
	private:
		std::vector<std::unique_ptr<FFTW>> instance_pool; //pool of FFTW instances for multi-thread processing
		std::unique_ptr<FFTW>& getInstance(int tid); //get an instance according to the number of current thread id

	public:
		FFTCC3D(int subset_radius_x, int subset_radius_y, int subset_radius_z, int thread_number);
		~FFTCC3D();

		void prepare();

		void compute(POI3D* poi);
		void compute(std::vector<POI3D>& poi_queue);
	};

}//namespace opencorr

#endif //_FFTCC_H_



