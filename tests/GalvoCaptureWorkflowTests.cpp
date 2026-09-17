#include "app/acquisition/GalvoCaptureSupport.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace htmsr::app;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FakeGalvo final : public IGalvoController {
public:
    std::vector<std::string> events;
    bool connected = true;
    bool queriesAlwaysTimeout = false;
    int transientStepTimeouts = 0;
    int stepQueries = 0;
    int totalQueries = 0;
    int syncQueries = 0;
    int laserOnCalls = 0;
    int laserOffCalls = 0;
    int laserOffFailures = 0;
    bool laserOnFails = false;
    bool scanFails = false;
    std::string failedWrite;
    double step = 0.05;
    int total = 40;

    GalvoCommandResult sent(const char* event, bool fail = false)
    {
        events.push_back(event);
        GalvoCommandResult result;
        result.success = connected && !fail && failedWrite != event;
        result.message = result.success ? "Command sent." : "Simulated serial failure.";
        return result;
    }
    bool connect() override { events.push_back("connect"); connected = true; return true; }
    void disconnect() override { events.push_back("disconnect"); connected = false; }
    bool isConnected() const override { return connected; }
    GalvoCommandResult setSyncMode(GalvoSyncMode) override { return sent("set sync"); }
    GalvoCommandResult getSyncMode(GalvoSyncMode&) override { ++syncQueries; return sent("get sync", true); }
    GalvoCommandResult setCaptureIntervalMs(int) override { return sent("set interval"); }
    GalvoCommandResult getCaptureIntervalMs(int&) override { return sent("get interval"); }
    GalvoCommandResult setContinuousCaptureWaitMs(int) override { return sent("set wait"); }
    GalvoCommandResult getContinuousCaptureWaitMs(int&) override { return sent("get wait"); }
    GalvoCommandResult setStepAngle(double value) override { step = value; return sent("set step"); }
    GalvoCommandResult getStepAngle(double& value) override
    {
        ++stepQueries;
        value = step;
        const bool fail = queriesAlwaysTimeout || transientStepTimeouts > 0;
        if (transientStepTimeouts > 0) { --transientStepTimeouts; }
        return sent("get step", fail);
    }
    GalvoCommandResult setAutoRotationAngle(int value) override { total = value; return sent("set total"); }
    GalvoCommandResult getAutoRotationAngle(int& value) override
    {
        ++totalQueries;
        value = total;
        return sent("get total", queriesAlwaysTimeout);
    }
    GalvoCommandResult setForwardSpeedMs(int) override { return sent("set forward speed"); }
    GalvoCommandResult getForwardSpeedMs(int&) override { return sent("get forward speed"); }
    GalvoCommandResult setReverseSpeedMs(int) override { return sent("set reverse speed"); }
    GalvoCommandResult getReverseSpeedMs(int&) override { return sent("get reverse speed"); }
    GalvoCommandResult setLaserDuty(int) override { return sent("set duty"); }
    GalvoCommandResult setVoltageRange(double) override { return sent("set voltage"); }
    GalvoCommandResult getVoltageRange(double&) override { return sent("get voltage"); }
    GalvoCommandResult setScanDirection(GalvoScanDirection) override { return sent("direction motion"); }
    GalvoCommandResult laserOn() override { ++laserOnCalls; return sent("laser on", laserOnFails); }
    GalvoCommandResult laserOff() override
    {
        require(connected, "Laser cleanup happened after releasing serial connection");
        ++laserOffCalls;
        const bool fail = laserOffFailures > 0;
        if (fail) { --laserOffFailures; }
        return sent("laser off", fail);
    }
    GalvoCommandResult startContinuousCapture() override { return sent("continuous scan", scanFails); }
    GalvoCommandResult sendRawCommand(const std::vector<unsigned char>&, bool) override { return sent("raw"); }
};

IntegratedScanConfig verifiedConfig()
{
    IntegratedScanConfig config;
    config.galvo.stepAngleDeg = 0.05;
    config.galvo.autoRotationAngleDeg = 40;
    config.totalRotationAngleDeg = 40;
    config.stereoCamera.frameCount = 800;
    config.verifiedGalvoMotion = VerifiedGalvoMotionParameters{config.galvo.portName, 0.05, 40};
    return config;
}

} // namespace

void runGalvoCaptureWorkflowTests()
{
    const GalvoWait noWait = [](int) {};
    auto config = verifiedConfig();
    FakeGalvo controller;
    controller.queriesAlwaysTimeout = true;
    std::vector<int> waits;
    const auto motion = prepareGalvoForCapture(controller, config, [&](int ms) { waits.push_back(ms); });
    require(motion.stepAngleDeg == 0.05 && motion.totalRotationAngleDeg == 40, "Verified angle snapshot was not reused");
    require(controller.stepQueries == 0 && controller.totalQueries == 0 && controller.syncQueries == 0,
        "Verified capture sent duplicate or mandatory sync queries");
    require(std::find(controller.events.begin(), controller.events.end(), "set step") == controller.events.end(), "Step angle was written twice");
    require(std::find(controller.events.begin(), controller.events.end(), "direction motion") == controller.events.end(), "Premature direction motion was sent");
    require(waits.size() == 7 && std::all_of(waits.begin(), waits.end(), [](int ms) { return ms >= 150; }), "Firmware settling intervals missing");
    std::cout << "PASS: verified UI snapshot bypasses duplicate timeouts, preserves 800 pairs, and paces parameter writes\n";

    FakeGalvo unverified;
    unverified.transientStepTimeouts = 1;
    auto freshConfig = config;
    freshConfig.verifiedGalvoMotion.reset();
    prepareGalvoForCapture(unverified, freshConfig, noWait);
    require(unverified.stepQueries == 2 && unverified.totalQueries == 1 && unverified.syncQueries == 0,
        "Fresh capture did not perform angle verification with recovery");
    std::cout << "PASS: entry without a snapshot sets/verifies angles once with reconnect recovery\n";

    FakeGalvo wrongPort;
    auto mismatched = config;
    mismatched.verifiedGalvoMotion->portName = "COM99";
    bool rejected = false;
    try { prepareGalvoForCapture(wrongPort, mismatched, noWait); } catch (const std::exception&) { rejected = true; }
    require(rejected && wrongPort.events.empty(), "Mismatched verification snapshot was accepted");
    FakeGalvo failedAngles;
    failedAngles.queriesAlwaysTimeout = true;
    rejected = false;
    try { prepareGalvoForCapture(failedAngles, freshConfig, noWait); } catch (const std::exception&) { rejected = true; }
    require(rejected && failedAngles.laserOnCalls == 0, "Real angular preflight failure was silently ignored");
    std::cout << "PASS: mismatched snapshots and real preflight failure remain blocking errors\n";

    {
        GalvoLaserCaptureGuard laser(controller, noWait);
        controller.events.push_back("cameras ready");
        laser.turnOn();
        require(controller.startContinuousCapture().success, "Scan start failed unexpectedly");
        controller.events.push_back("all pairs saved");
        require(laser.close().success, "Laser did not close on successful capture");
    }
    require(controller.laserOnCalls == 1 && controller.laserOffCalls == 1, "Successful flow must open and close laser once");
    const auto ready = std::find(controller.events.begin(), controller.events.end(), "cameras ready");
    const auto on = std::find(controller.events.begin(), controller.events.end(), "laser on");
    const auto scan = std::find(controller.events.begin(), controller.events.end(), "continuous scan");
    const auto saved = std::find(controller.events.begin(), controller.events.end(), "all pairs saved");
    const auto off = std::find(controller.events.begin(), controller.events.end(), "laser off");
    require(ready < on && on < scan && scan < saved && saved < off, "Laser/acquisition flow ordering is wrong");
    std::cout << "PASS: camera ready -> laser on -> scan/save -> laser off exactly once\n";

    for (int failure = 0; failure < 3; ++failure) {
        FakeGalvo failed;
        failed.scanFails = failure == 0;
        failed.laserOnFails = failure == 2;
        try {
            GalvoLaserCaptureGuard laser(failed, noWait);
            laser.turnOn();
            require(failed.startContinuousCapture().success, "Simulated scan start failure");
            throw std::runtime_error("Simulated image-save exception");
        } catch (const std::exception&) {
        }
        require(failed.laserOffCalls == 1, "Exceptional flow failed to close laser");
        failed.disconnect();
    }
    std::cout << "PASS: scan failure, save exception and partial laser-on failure all close before disconnect\n";

    FakeGalvo retryOff;
    retryOff.laserOffFailures = 2;
    {
        GalvoLaserCaptureGuard laser(retryOff, noWait);
        laser.turnOn();
        require(laser.close().success && retryOff.laserOffCalls == 3, "Laser off retry did not recover");
    }
    FakeGalvo permanentOffFailure;
    permanentOffFailure.laserOffFailures = 100;
    {
        GalvoLaserCaptureGuard laser(permanentOffFailure, noWait);
        laser.turnOn();
        require(!laser.close().success, "Permanent laser off failure was marked successful");
    }
    require(permanentOffFailure.laserOffCalls >= 3, "Laser off cleanup did not retry");
    std::cout << "PASS: laser-off retry works and permanent close failure is reported\n";
}
