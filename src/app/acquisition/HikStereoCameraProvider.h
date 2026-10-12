#pragma once

#include "app/acquisition/AcquisitionTypes.h"

namespace htmsr::app {

class HikStereoCameraProvider final : public IAcquisitionProvider {
public:
    /*
        函数功能：构造海康双相机在线采集源
        输入：
            leftDevice：左相机设备对象
            rightDevice：右相机设备对象
            config：双相机采集任务参数
        输出：
            无（构造后接管左右相机对象生命周期）
    */
    HikStereoCameraProvider(CameraDevicePtr leftDevice, CameraDevicePtr rightDevice, StereoCameraConfig config);
    ~HikStereoCameraProvider() override;

    std::string name() const override;
    bool hasNext() const override;
    FramePair next() override;
    void reset() override;

private:
    CameraDevicePtr leftDevice_;
    CameraDevicePtr rightDevice_;
    StereoCameraConfig config_;
    int index_ = 0;
    bool prepared_ = false;

    void prepare();
};

} // namespace htmsr::app
