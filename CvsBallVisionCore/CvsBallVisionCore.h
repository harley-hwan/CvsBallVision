// CvsBallVisionCore.h : DLL interface for CREVIS camera control
#pragma once

#ifdef CVSBALLVISIONCORE_EXPORTS
#define CVSBALLVISIONCORE_API __declspec(dllexport)
#else
#define CVSBALLVISIONCORE_API __declspec(dllimport)
#endif

#include <windows.h>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include "C:\Program Files\CREVIS\cvsCam\Includes\cvsCamCtrl.h"

#pragma comment(lib, "C:\Program Files\CREVIS\cvsCam\Libraries\cvsCamCtrl.lib")

namespace CvsBallVision {

    // Forward declarations
    class CameraController;

    // Camera information structure
    struct CameraInfo {
        std::string deviceId;
        std::string modelName;
        std::string serialNumber;
        std::string ipAddress;
        std::string macAddress;
        bool isConnectable;
        uint32_t enumIndex;
    };

    // Image format information
    struct ImageFormat {
        int32_t width;
        int32_t height;
        int32_t channels;
        int32_t bytesPerPixel;
        std::string pixelFormat;
    };

    // Camera status structure
    struct CameraStatus {
        bool isConnected;
        bool isAcquiring;
        uint64_t frameCount;
        uint64_t errorCount;
        double frameRate;
        double bandwidth;
        std::string lastError;
    };

    // Callback types
    using ImageCallback = std::function<void(const CVS_BUFFER*, const CameraStatus&)>;
    using ErrorCallback = std::function<void(const std::string&, CVS_ERROR)>;
    using EventCallback = std::function<void(int32_t, const CVS_EVENT*)>;

    // Camera controller class
    class CVSBALLVISIONCORE_API CameraController {
    public:
        CameraController();
        ~CameraController();

        // System initialization
        bool InitializeSystem();
        void ReleaseSystem();
        bool IsSystemInitialized() const;

        // Device discovery
        bool UpdateDeviceList(uint32_t timeout = 500);
        std::vector<CameraInfo> GetAvailableCameras();
        bool GetCameraInfo(uint32_t index, CameraInfo& info);

        // Device connection
        bool ConnectCamera(uint32_t index);
        bool ConnectCameraById(const std::string& deviceId);
        bool DisconnectCamera();
        bool IsConnected() const;

        // Acquisition control
        bool StartAcquisition();
        bool StopAcquisition();
        bool IsAcquiring() const;
        bool SingleGrab(CVS_BUFFER* buffer);

        // Camera configuration
        bool SetExposureTime(double microseconds);
        bool GetExposureTime(double& microseconds);
        bool SetGain(double gain);
        bool GetGain(double& gain);
        bool SetFrameRate(double fps);
        bool GetFrameRate(double& fps);
        bool SetTriggerMode(bool enabled);
        bool GetTriggerMode(bool& enabled);
        bool ExecuteSoftwareTrigger();

        // Image format control
        bool GetImageFormat(ImageFormat& format);
        bool SetPixelFormat(const std::string& format);
        bool SetROI(int32_t x, int32_t y, int32_t width, int32_t height);
        bool ResetROI();

        // White balance (color models only)
        bool SetWhiteBalanceAuto(bool enabled);
        bool ExecuteWhiteBalanceOnce();
        bool SetWhiteBalanceRatio(double red, double green, double blue);

        // Auto features
        bool SetAutoExposure(bool enabled);
        bool SetAutoGain(bool enabled);
        bool SetAutoExposureTarget(int32_t target);

        // Buffer management
        bool SetBufferCount(uint32_t count);
        uint32_t GetBufferCount() const;
        bool SetGrabTimeout(uint32_t milliseconds);
        uint32_t GetGrabTimeout() const;

        // Status and diagnostics
        CameraStatus GetStatus();
        std::string GetLastErrorString() const;
        CVS_ERROR GetLastErrorCode() const;
        bool GetStatistics(uint64_t& frameCount, uint64_t& errorCount);

        // Callback registration
        void RegisterImageCallback(ImageCallback callback);
        void RegisterErrorCallback(ErrorCallback callback);
        void RegisterEventCallback(int32_t eventId, EventCallback callback);
        void UnregisterImageCallback();
        void UnregisterErrorCallback();
        void UnregisterEventCallback(int32_t eventId);

        // Configuration save/load
        bool SaveConfiguration(const std::string& filename);
        bool LoadConfiguration(const std::string& filename);
        bool ExportSettingsToXML(const std::string& filename);
        bool ImportSettingsFromXML(const std::string& filename);

        // Advanced features
        bool SetGamma(double gamma);
        bool SetBlackLevel(int32_t level);
        bool EnableNoiseReduction(bool enabled);
        bool SetDebounceTime(uint32_t microseconds);
        bool SetPacketSize(int32_t size);
        bool SetPacketDelay(int32_t delay);

        // Multicast support
        bool ConnectCameraMulticast(uint32_t index, const std::string& multicastIP,
            uint32_t dataPort, CvsAccessType accessType = AccessControl);

    private:
        class Impl;
        std::unique_ptr<Impl> pImpl;
    };

    // Utility functions
    class CVSBALLVISIONCORE_API ImageProcessor {
    public:
        // Color conversion
        static bool ConvertBayerToRGB(const CVS_BUFFER& src, CVS_BUFFER& dst, ConvertColor code);
        static bool ConvertYUVToRGB(const CVS_BUFFER& src, CVS_BUFFER& dst, ConvertColor code);
        static bool ConvertMono10ToMono8(const CVS_BUFFER& src, CVS_BUFFER& dst);
        static bool ConvertMono12ToMono8(const CVS_BUFFER& src, CVS_BUFFER& dst);
        static bool ConvertMono16ToMono8(const CVS_BUFFER& src, CVS_BUFFER& dst);

        // Buffer management
        static bool AllocateBuffer(CVS_BUFFER& buffer, int32_t width, int32_t height, int32_t channels);
        static void FreeBuffer(CVS_BUFFER& buffer);
        static bool CopyBuffer(const CVS_BUFFER& src, CVS_BUFFER& dst);

        // Image analysis
        static bool CalculateHistogram(const CVS_BUFFER& buffer, std::vector<int>& histogram);
        static bool CalculateMeanStdDev(const CVS_BUFFER& buffer, double& mean, double& stddev);
        static bool DetectSaturation(const CVS_BUFFER& buffer, double& saturationRatio);
    };

    // Error code to string conversion
    CVSBALLVISIONCORE_API std::string ErrorCodeToString(CVS_ERROR error);

    // Version information
    CVSBALLVISIONCORE_API std::string GetLibraryVersion();
    CVSBALLVISIONCORE_API std::string GetAPIVersion();
}