#include "app/acquisition/HikCameraDevice.h"
#include <MvCameraControl.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace htmsr::app;

namespace {
struct FakeCamera {
    std::vector<std::pair<unsigned int, std::string>> selectors{{6, "FrameBurstStart"}};
    unsigned int selected = 6;
    std::map<unsigned int, bool> enabled{{6, true}};
    int64_t burstCount = 4;
    int burstReadCode = MV_OK;
    int burstWriteCode = MV_OK;
    int selectorWriteCode = MV_OK;
    bool ignoreBurstWrite = false;
    bool ignoreSelectorWrite = false;
    bool line5Supported = true;
    int burstWrites = 0;
    std::string source;
    std::string activation;
    unsigned int buffers = 0;
    MV_GRAB_STRATEGY strategy = MV_GrabStrategy_LatestImagesOnly;
} fake;
MV_CC_DEVICE_INFO deviceInfo{};

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void reset()
{
    fake = FakeCamera{};
    deviceInfo = {};
    deviceInfo.nTLayerType = MV_USB_DEVICE;
    std::strcpy(reinterpret_cast<char*>(deviceInfo.SpecialInfo.stUsb3VInfo.chSerialNumber), "TEST");
}

bool configure(std::string& error, bool hardware = true)
{
    CameraDeviceInfo info;
    info.id = "USB3:TEST";
    HikCameraDevice camera(info);
    check(camera.connect(), "fake camera connection failed");
    CameraParameterConfig config;
    config.useHardwareTrigger = hardware;
    config.triggerSourceLine = 5;
    const bool result = camera.configure(config);
    error = camera.lastError();
    return result;
}
} // namespace

extern "C" {
int __stdcall MV_CC_EnumDevices(unsigned int, MV_CC_DEVICE_INFO_LIST* list)
{
    list->nDeviceNum = 1;
    list->pDeviceInfo[0] = &deviceInfo;
    return MV_OK;
}
int __stdcall MV_CC_CreateHandle(void** handle, const MV_CC_DEVICE_INFO*) { *handle = &fake; return MV_OK; }
int __stdcall MV_CC_OpenDevice(void*, unsigned int, unsigned short) { return MV_OK; }
int __stdcall MV_CC_CloseDevice(void*) { return MV_OK; }
int __stdcall MV_CC_DestroyHandle(void*) { return MV_OK; }
int __stdcall MV_CC_GetEnumValue(void*, const char* key, MVCC_ENUMVALUE* value)
{
    if (std::string(key) != "TriggerSelector") return MV_E_SUPPORT;
    *value = {};
    value->nCurValue = fake.selected;
    value->nSupportedNum = static_cast<unsigned int>(fake.selectors.size());
    for (std::size_t i = 0; i < fake.selectors.size(); ++i) value->nSupportValue[i] = fake.selectors[i].first;
    return MV_OK;
}
int __stdcall MV_CC_GetEnumEntrySymbolic(void*, const char*, MVCC_ENUMENTRY* entry)
{
    for (const auto& selector : fake.selectors) {
        if (selector.first == entry->nValue) {
            std::strcpy(entry->chSymbolic, selector.second.c_str());
            return MV_OK;
        }
    }
    return MV_E_PARAMETER;
}
int __stdcall MV_CC_SetEnumValue(void*, const char* key, unsigned int value)
{
    if (std::string(key) != "TriggerSelector") return MV_E_PARAMETER;
    if (fake.selectorWriteCode != MV_OK) return fake.selectorWriteCode;
    if (!fake.ignoreSelectorWrite) fake.selected = value;
    return MV_OK;
}
int __stdcall MV_CC_SetEnumValueByString(void*, const char* key, const char* value)
{
    const std::string node(key), name(value);
    if (node == "TriggerSelector") {
        for (const auto& selector : fake.selectors) {
            if (selector.second == name) return MV_CC_SetEnumValue(nullptr, key, selector.first);
        }
        return MV_E_PARAMETER; // Reproduces the reported error on a FrameBurstStart-only camera.
    }
    if (node == "TriggerMode") { fake.enabled[fake.selected] = name == "On"; return MV_OK; }
    if (node == "TriggerSource") {
        if (!fake.enabled[fake.selected]) return MV_E_GC_ACCESS;
        if (name == "Line5" && !fake.line5Supported) return MV_E_PARAMETER;
        fake.source = name;
        return MV_OK;
    }
    if (node == "TriggerActivation") { fake.activation = name; return MV_OK; }
    return node == "AcquisitionMode" && name == "Continuous" ? MV_OK : MV_E_PARAMETER;
}
int __stdcall MV_CC_GetIntValueEx(void*, const char* key, MVCC_INTVALUE_EX* value)
{
    *value = {};
    if (std::string(key) == "PayloadSize") { value->nCurValue = 64; return MV_OK; }
    if (std::string(key) != "AcquisitionBurstFrameCount") return MV_E_SUPPORT;
    value->nCurValue = fake.burstCount;
    return fake.burstReadCode;
}
int __stdcall MV_CC_SetIntValueEx(void*, const char* key, int64_t value)
{
    if (std::string(key) != "AcquisitionBurstFrameCount") return MV_E_PARAMETER;
    ++fake.burstWrites;
    if (fake.burstWriteCode != MV_OK) return fake.burstWriteCode;
    if (!fake.ignoreBurstWrite) fake.burstCount = value;
    return MV_OK;
}
int __stdcall MV_CC_SetFloatValue(void*, const char*, float) { return MV_OK; }
int __stdcall MV_CC_GetFloatValue(void*, const char*, MVCC_FLOATVALUE*) { return MV_E_SUPPORT; }
int __stdcall MV_CC_GetBoolValue(void*, const char*, bool*) { return MV_E_SUPPORT; }
int __stdcall MV_CC_SetBoolValue(void*, const char*, bool) { return MV_OK; }
int __stdcall MV_CC_SetImageNodeNum(void*, unsigned int value) { fake.buffers = value; return MV_OK; }
int __stdcall MV_CC_SetGrabStrategy(void*, MV_GRAB_STRATEGY value) { fake.strategy = value; return MV_OK; }
int __stdcall MV_CC_StartGrabbing(void*) { return MV_OK; }
int __stdcall MV_CC_StopGrabbing(void*) { return MV_OK; }
int __stdcall MV_CC_ClearImageBuffer(void*) { return MV_OK; }
int __stdcall MV_CC_GetOneFrameTimeout(void*, unsigned char*, unsigned int, MV_FRAME_OUT_INFO_EX*, unsigned int) { return MV_E_NODATA; }
int __stdcall MV_CC_GetImageBuffer(void*, MV_FRAME_OUT*, unsigned int) { return MV_E_NODATA; }
int __stdcall MV_CC_FreeImageBuffer(void*, MV_FRAME_OUT*) { return MV_OK; }
int __stdcall MV_CC_ConvertPixelTypeEx(void*, MV_CC_PIXEL_CONVERT_PARAM_EX*) { return MV_E_SUPPORT; }
}

int main()
{
    try {
        std::string error;
        reset();
        check(configure(error), "FrameBurstStart-only camera must configure successfully");
        check(fake.selected == 6 && fake.enabled[6] && fake.burstCount == 1 && fake.burstWrites == 1,
            "burst mode must expose exactly one frame per trigger");
        check(fake.source == "Line5" && fake.activation == "RisingEdge", "Line5 and rising-edge settings must be preserved");
        check(fake.buffers == 16 && fake.strategy == MV_GrabStrategy_OneByOne, "ordered buffering must be preserved");

        reset();
        fake.selectors = {{13, "FrameBurstStart"}, {42, "FrameStart"}, {3, "AcquisitionStart"}};
        fake.selected = 13;
        fake.enabled = {{13, true}, {42, true}, {3, true}};
        fake.burstReadCode = MV_E_SUPPORT;
        check(configure(error), "FrameStart must be preferred when supported, independent of its numeric value");
        check(fake.selected == 42 && fake.enabled[42] && !fake.enabled[13] && !fake.enabled[3],
            "other trigger gates must be disabled");
        check(fake.burstWrites == 0, "FrameStart must not require a burst-count node");

        reset();
        fake.burstCount = 1;
        fake.burstWriteCode = MV_E_GC_ACCESS;
        check(configure(error) && fake.burstWrites == 0, "a read-only burst count already at one must work");

        reset();
        fake.burstReadCode = MV_E_SUPPORT;
        check(!configure(error) && error.find("AcquisitionBurstFrameCount") != std::string::npos,
            "unknown burst count must fail explicitly");

        reset();
        fake.burstWriteCode = MV_E_GC_ACCESS;
        check(!configure(error) && error.find("AcquisitionBurstFrameCount=1") != std::string::npos,
            "unwritable multi-frame burst count must fail");

        reset();
        fake.ignoreBurstWrite = true;
        check(!configure(error) && error.find("verify AcquisitionBurstFrameCount=1") != std::string::npos,
            "burst-count readback must reject a silently ignored write");

        reset();
        fake.selectors = {{3, "AcquisitionStart"}};
        fake.selected = 3;
        check(!configure(error) && error.find("supported TriggerSelector entries=[AcquisitionStart]") != std::string::npos,
            "unsupported trigger types must report the camera's actual entries");

        reset();
        fake.selectorWriteCode = MV_E_GC_ACCESS;
        check(!configure(error), "selector access errors must remain failures");

        reset();
        fake.line5Supported = false;
        check(!configure(error) && error.find("TriggerSource=Line5") != std::string::npos,
            "an unsupported input must remain a failure");

        reset();
        fake.selectors = {{6, "FrameBurstStart"}, {42, "FrameStart"}};
        fake.ignoreSelectorWrite = true;
        check(!configure(error) && error.find("verify TriggerSelector=FrameStart") != std::string::npos,
            "selector readback must reject a silently ignored write");

        reset();
        fake.selectors.clear();
        check(!configure(error), "empty selector lists must fail");

        reset();
        fake.selectors.clear();
        check(configure(error, false) && !fake.enabled[6], "free-run preview must not require selector enumeration");
        std::cout << "All 12 Hik camera configuration scenarios passed.\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
