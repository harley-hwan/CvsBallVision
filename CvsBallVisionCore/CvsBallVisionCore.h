#pragma once

#ifdef CVSBALLVISIONCORE_EXPORTS
#define CVSCORE_API __declspec(dllexport)
#else
#define CVSCORE_API __declspec(dllimport)
#endif
#define NOMINMAX

#include <Windows.h>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <mutex>
#include <stdint.h>


// Camera parameters structure
struct CVSCORE_API CameraParams {
    int width;
    int height;
    double fps;
    double exposureTime;  // in microseconds
    double gain;         // in dB

    CameraParams() :
        width(1280),
        height(880),
        fps(100.0),
        exposureTime(1000.0),  // 1ms = 1000us
        gain(0.0) {
    }
};

// Image data structure
struct CVSCORE_API ImageData {
    unsigned char* pData;
    size_t dataSize;
    int width;
    int height;
    int stride;
    uint64_t timestamp;
    int pixelFormat;  // 0: Mono8, 1: Bayer8, 2: RGB8

    ImageData() : pData(nullptr), dataSize(0), width(0), height(0),
        stride(0), timestamp(0), pixelFormat(0) {
    }
};

// Callback function type for new frame events
typedef std::function<void(const ImageData&)> FrameCallback;

class CVSCORE_API CvsBallVisionCore
{
public:
    CvsBallVisionCore();
    ~CvsBallVisionCore();

    // Camera enumeration and connection
    bool EnumerateCameras(std::vector<std::string>& cameraList);
    bool ConnectCamera(const std::string& cameraId = "");
    bool DisconnectCamera();
    bool IsConnected() const;

    // Camera control
    bool StartAcquisition();
    bool StopAcquisition();
    bool IsAcquiring() const;

    // Parameter control
    bool SetImageSize(int width, int height);
    bool GetImageSize(int& width, int& height);

    bool SetFrameRate(double fps);
    bool GetFrameRate(double& fps);
    bool GetFrameRateRange(double& minFps, double& maxFps);

    bool SetExposureTime(double exposureTimeUs);
    bool GetExposureTime(double& exposureTimeUs);
    bool GetExposureTimeRange(double& minExpUs, double& maxExpUs);

    bool SetGain(double gainDb);
    bool GetGain(double& gainDb);
    bool GetGainRange(double& minGain, double& maxGain);

    // Get current parameters
    bool GetCameraParams(CameraParams& params);
    bool SetCameraParams(const CameraParams& params);

    // Frame callback registration
    void RegisterFrameCallback(FrameCallback callback);
    void UnregisterFrameCallback();

    // Get last error message
    std::string GetLastError() const;

    // Camera information
    std::string GetCameraModel() const;
    std::string GetCameraSerialNumber() const;
    std::string GetCameraVendor() const;

private:
    // Private implementation to avoid DLL boundary issues
    class Impl;
    Impl* m_pImpl;  // Use raw pointer instead of unique_ptr for DLL compatibility

    // Disable copy operations
    CvsBallVisionCore(const CvsBallVisionCore&) = delete;
    CvsBallVisionCore& operator=(const CvsBallVisionCore&) = delete;
};

// Helper functions
extern "C" {
    CVSCORE_API CvsBallVisionCore* CreateCameraInstance();
    CVSCORE_API void DestroyCameraInstance(CvsBallVisionCore* pCamera);
    CVSCORE_API const char* GetVersionString();
}