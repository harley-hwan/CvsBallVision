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
    // System constants
    namespace Constants
    {
        // Default camera parameters
        constexpr int DEFAULT_WIDTH = 1280;
        constexpr int DEFAULT_HEIGHT = 880;
        constexpr double DEFAULT_FPS = 100.0;
        constexpr double DEFAULT_EXPOSURE_US = 5000.0;
        constexpr double DEFAULT_GAIN_DB = 1.0;

        // Buffer management
        constexpr size_t BUFFER_POOL_SIZE = 3;
        constexpr size_t BUFFER_POOL_MAX_SIZE = 5;
        constexpr double BUFFER_RESERVE_FACTOR = 1.5;

        // Timing constants (milliseconds)
        constexpr int ACQUISITION_STOP_TIMEOUT_MS = 200;
        constexpr int CALLBACK_UNREGISTER_DELAY_MS = 50;
        constexpr int HARDWARE_PREP_TIME_MS = 50;
        constexpr int CAMERA_STOP_WAIT_MS = 100;
        constexpr int BUFFER_WAIT_RETRY_MS = 10;
        constexpr int GRAB_THREAD_SLEEP_MS = 1;
        constexpr int GRAB_ERROR_SLEEP_MS = 10;
        constexpr int RESOLUTION_CHANGE_DELAY_MS = 30;

        // Timeout constants (seconds)
        constexpr int SHUTDOWN_TIMEOUT_SEC = 2;
        constexpr int BUFFER_RETURN_TIMEOUT_SEC = 2;
        constexpr int CALLBACK_COMPLETE_TIMEOUT_SEC = 2;

        // UI update intervals
        constexpr int UI_UPDATE_INTERVAL_MS = 33;  // ~30 FPS
        constexpr int STATISTICS_UPDATE_INTERVAL_MS = 1000;

        // Buffer retry counts
        constexpr int BUFFER_RELEASE_MAX_RETRIES = 10;
        constexpr int BUFFER_WAIT_TIMEOUT_MS = 5;

        // Error tracking
        constexpr size_t MAX_ERROR_HISTORY = 100;
    }

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