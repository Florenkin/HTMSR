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

#ifndef  _IO_H_
#define  _IO_H_

#include "oc_calibration.h"
#include "oc_poi.h"

// 模块概览：读写 POI、标定文件、结果表、结果图和二进制输出。
namespace opencorr
{
	// 可导出为结果图的变量枚举。
	enum OutputVariable
	{
		// 算法说明：统一的变量枚举让所有导出器在 2D、双目和体相关流程中
		// 都使用同一套字段语义。
		u = 1, // 位移
		v = 2,
		w = 3,
		e_xx = 4, // 应变
		e_yy = 5,
		e_zz = 6,
		e_xy = 7,
		e_yz = 8,
		e_zx = 9,
		zncc = 10, // r1t1 匹配质量
		zncc_r1r2 = 11,
		zncc_r1t2 = 12,
		deformation_increment = 13, // 收敛增量
		iteration_step = 14, // 迭代步数
		feature_nearby = 15, // 附近 SIFT 特征数
		u_x = 16, // 位移梯度
		u_y = 17,
		u_z = 18,
		v_x = 19,
		v_y = 20,
		v_z = 21,
		w_x = 22,
		w_y = 23,
		w_z = 24,
	};

	// 负责 CSV 表输入输出的 2D / 双目结果 IO 模块。
	class IO2D
	{
	private:
		// 算法说明：IO 与求解器分离后，数值核心就能专注于计算本身，
		// 而不必掺杂具体文件格式细节。
		std::string file_path;
		std::string delimiter = ",";
		int width, height;

	public:
		IO2D();
		~IO2D();

		OutputVariable out_var;

		std::string getPath() const;
		std::string getDelimiter() const;
		int getWidth() const;
		int getHeight() const;
		void setPath(std::string file_path);
		void setDelimiter(std::string delimiter);
		void setWidth(int width);
		void setHeight(int height);

		// 从 CSV 表读取 POI 坐标。
		std::vector<Point2D> loadPoint2D(std::string file_path);

		// 将 POI 坐标保存到 CSV 表。
		void savePoint2D(std::vector<Point2D> point_queue, std::string file_path);

		// 读取双目视觉所需的相机标定参数。
		void loadCalibration(Calibration& calibration_cam1, Calibration& calibration_cam2, std::string file_path);

		// 从 CSV 表读取 2D POI 结果。
		std::vector<POI2D> loadTable2D();

		// 将 2D DIC 结果保存到 CSV 表。
		void saveTable2D(std::vector<POI2D>& poi_queue);
		void saveDeformationTable2D(std::vector<POI2D>& poi_queue);

		// `variable` 取值见 `OutputVariable` 枚举。
		void saveMap2D(std::vector<POI2D>& poi_queue, OutputVariable variable);

		// 从 CSV 表读取双目 POI 结果。
		std::vector<POI2DS> loadTable2DS();

		// 将双目 DIC 结果保存到 CSV 表。
		void saveTable2DS(std::vector<POI2DS>& poi_queue);

		// `variable` 取值见 `OutputVariable` 枚举。
		void saveMap2DS(std::vector<POI2DS>& poi_queue, OutputVariable variable);
	};

	// 面向 3D 点、DVC 结果表和二进制矩阵的输入输出辅助类。
	class IO3D
	{
	private:
		// 算法说明：大型 DVC 输出既需要便于分析的表格导出，
		// 也需要面向性能和存储效率的紧凑二进制导出。
		std::string file_path;
		std::string delimiter;
		int dim_x, dim_y, dim_z;

	public:
		IO3D();
		~IO3D();

		std::string getPath() const;
		std::string getDelimiter() const;
		void setPath(std::string file_path);
		void setDelimiter(std::string delimiter);

		int getDimX();
		int getDimY();
		int getDimZ();
		void setDimX(int dim_x);
		void setDimY(int dim_y);
		void setDimZ(int dim_z);

		// 从 CSV 表读取 3D 点坐标。
		std::vector<Point3D> loadPoint3D(std::string file_path);

		// 将 3D 点坐标保存到 CSV 表。
		void savePoint3D(std::vector<Point3D> poi_queue, std::string file_path);

		// 从已保存的数据表中读取 3D POI 结果。
		std::vector<POI3D> loadTable3D();

		// 将 DVC 结果保存到 CSV 表。
		void saveTable3D(std::vector<POI3D>& poi_queue);

		// `variable` 取值见 `OutputVariable` 枚举。
		void saveMap3D(std::vector<POI3D>& poi_queue, OutputVariable variable);

		// 以二进制矩阵形式保存和加载 POI 结果。
		void saveMatrixBin(std::vector<POI3D>& poi_queue);
		std::vector<POI3D> loadMatrixBin();
	};

}//namespace opencorr

#endif //_IO_H_



