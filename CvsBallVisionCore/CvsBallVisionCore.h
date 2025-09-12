#pragma once

#ifdef CVSBALLVISIONCORE_EXPORTS
#define CVSBALLVISION_API __declspec(dllexport)
#else
#define CVSBALLVISION_API __declspec(dllimport)
#endif

#include <Windows.h>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
struct _cvsBuffer;
typedef struct _cvsBuffer CVS_BUFFER;

namespace CvsBallVision
{
    // Camera information structure
    struct CameraInfo
    {
        std::string userID;
        std::string modelName;
        std::string serialNumber;
        std::string deviceVersion;
        std::string ipAddress;
        std::string macAddress;
        uint32_t enumIndex;
        bool isConnected;
    };

    // Camera parameters structure
    struct CameraParameters
    {
        int width;
        int height;
        double exposureTime;
        double gain;
        double fps;
        std::string pixelFormat;
    };

    // Image data structure
    struct ImageData
    {
        uint8_t* pData;
        int width;
        int height;
        int channels;
        int step;
        uint64_t blockID;
        uint64_t timestamp;
    };

    // Callback types
    using ImageCallback = std::function<void(const ImageData&)>;
    using ErrorCallback = std::function<void(int errorCode, const std::string& errorMsg)>;
    using StatusCallback = std::function<void(const std::string& status)>;

    class CVSBALLVISION_API CameraController
    {
    public:
        CameraController();
        ~CameraController();

        // System initialization
        bool InitializeSystem();
        void FreeSystem();
        bool IsSystemInitialized() const;

        // Camera enumeration and connection
        bool UpdateDeviceList(uint32_t timeout = 500);
        std::vector<CameraInfo> GetAvailableCameras();
        bool ConnectCamera(uint32_t enumIndex);
        bool DisconnectCamera();
        bool IsConnected() const;

        // Acquisition control
        bool StartAcquisition();
        bool StopAcquisition();
        bool IsAcquiring() const;

        // Parameter control
        bool SetResolution(int width, int height);
        bool GetResolution(int& width, int& height);

        bool SetExposureTime(double exposureTimeUs);
        bool GetExposureTime(double& exposureTimeUs);
        bool GetExposureTimeRange(double& min, double& max);

        bool SetGain(double gain);
        bool GetGain(double& gain);
        bool GetGainRange(double& min, double& max);

        bool SetFrameRate(double fps);
        bool GetFrameRate(double& fps);
        bool GetFrameRateRange(double& min, double& max);

        bool SetPixelFormat(const std::string& format);
        std::string GetPixelFormat();
        std::vector<std::string> GetAvailablePixelFormats();

        // Advanced parameter control
        bool SetTriggerMode(bool enable);
        bool SetTriggerSource(const std::string& source);
        bool ExecuteSoftwareTrigger();

        // Image retrieval
        bool GetLatestImage(ImageData& imageData);

        // Callbacks
        void RegisterImageCallback(ImageCallback callback);
        void RegisterErrorCallback(ErrorCallback callback);
        void RegisterStatusCallback(StatusCallback callback);

        // Statistics
        void GetStatistics(uint64_t& frameCount, uint64_t& errorCount, double& currentFps);

        // Error handling
        int GetLastError() const;
        std::string GetLastErrorDescription() const;

        // Parameter persistence
        bool SaveParameters(const std::string& filePath);
        bool LoadParameters(const std::string& filePath);

    private:
        class Impl;
        std::unique_ptr<Impl> m_pImpl;

        // Disable copy
        CameraController(const CameraController&) = delete;
        CameraController& operator=(const CameraController&) = delete;
    };

    // Utility functions
    CVSBALLVISION_API std::string GetSDKVersion();
    CVSBALLVISION_API bool ConvertBayerToRGB(const uint8_t* pSrc, uint8_t* pDst,
        int width, int height,
        const std::string& bayerPattern);
}