//#include "pch.h"
#include "CvsBallVisionCore.h"
#include "cvsCamCtrl.h"  // CREVIS API header
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

#pragma comment(lib, "cvsCamCtrl.lib")

// Implementation class
class CvsBallVisionCore::Impl
{
public:
    Impl();
    ~Impl();

    bool EnumerateCameras(std::vector<std::string>& cameraList);
    bool ConnectCamera(const std::string& cameraId);
    bool DisconnectCamera();
    bool IsConnected() const { return m_bConnected; }

    bool StartAcquisition();
    bool StopAcquisition();
    bool IsAcquiring() const { return m_bAcquiring; }

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

    bool GetCameraParams(CameraParams& params);
    bool SetCameraParams(const CameraParams& params);

    void RegisterFrameCallback(FrameCallback callback);
    void UnregisterFrameCallback();

    std::string GetLastError() const { return m_lastError; }
    std::string GetCameraModel() const { return m_cameraModel; }
    std::string GetCameraSerialNumber() const { return m_serialNumber; }
    std::string GetCameraVendor() const { return "CREVIS"; }

private:
    // Static callback function for CREVIS API
    static void OnFrameGrabbed(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine);

    void ProcessFrame(int32_t eventID, const CVS_BUFFER* pBuffer);
    bool SetError(const std::string& error);
    void AcquisitionThread();

private:
    int32_t m_hDevice;
    std::atomic<bool> m_bConnected;
    std::atomic<bool> m_bAcquiring;
    std::atomic<bool> m_bStopThread;
    uint32_t m_currentEnumNum;

    FrameCallback m_frameCallback;
    std::mutex m_callbackMutex;

    CameraParams m_params;
    std::mutex m_paramMutex;

    std::string m_lastError;
    std::string m_cameraModel;
    std::string m_serialNumber;

    std::thread m_acquisitionThread;
    CVS_BUFFER m_cvsBuffer;
    std::mutex m_bufferMutex;

    // Frame statistics
    std::atomic<uint64_t> m_frameCount;
    std::atomic<uint64_t> m_errorCount;
};

// Static callback implementation
void CvsBallVisionCore::Impl::OnFrameGrabbed(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine)
{
    if (pUserDefine) {
        Impl* pImpl = static_cast<Impl*>(pUserDefine);
        pImpl->ProcessFrame(eventID, pBuffer);
    }
}

// Process frame from callback
void CvsBallVisionCore::Impl::ProcessFrame(int32_t eventID, const CVS_BUFFER* pBuffer)
{
    if (eventID == EVENT_NEW_IMAGE && pBuffer && pBuffer->image.pImage) {
        ImageData imageData;
        imageData.pData = static_cast<unsigned char*>(pBuffer->image.pImage);
        imageData.dataSize = pBuffer->size;
        imageData.width = pBuffer->image.width;
        imageData.height = pBuffer->image.height;
        imageData.stride = pBuffer->image.step;
        imageData.timestamp = pBuffer->timestamp;
        imageData.pixelFormat = pBuffer->image.channels == 1 ? 0 : 2; // Mono8 or RGB8

        m_frameCount++;

        // Call registered callback
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_frameCallback) {
            m_frameCallback(imageData);
        }
    }
    else if (eventID == EVENT_GRAB_ERROR || eventID == EVENT_GRAB_TIMEOUT) {
        m_errorCount++;
    }
}

// Implementation of Impl class
CvsBallVisionCore::Impl::Impl()
    : m_hDevice(-1)
    , m_bConnected(false)
    , m_bAcquiring(false)
    , m_bStopThread(false)
    , m_currentEnumNum(0)
    , m_cameraModel("MG-A160K-72")
    , m_serialNumber("")
    , m_frameCount(0)
    , m_errorCount(0)
{
    memset(&m_cvsBuffer, 0, sizeof(CVS_BUFFER));

    // Initialize CREVIS system
    CVS_ERROR err = ST_InitSystem();
    if (err != MCAM_ERR_OK) {
        SetError("Failed to initialize CREVIS system");
    }
}

CvsBallVisionCore::Impl::~Impl()
{
    if (m_bAcquiring) {
        StopAcquisition();
    }
    if (m_bConnected) {
        DisconnectCamera();
    }

    // Free CREVIS system
    ST_FreeSystem();
}

bool CvsBallVisionCore::Impl::EnumerateCameras(std::vector<std::string>& cameraList)
{
    cameraList.clear();

    // Update device list
    CVS_ERROR err = ST_UpdateDevice(500);
    if (err != MCAM_ERR_OK) {
        return SetError("Failed to update device list");
    }

    // Get number of available cameras
    uint32_t numCameras = 0;
    err = ST_GetAvailableCameraNum(&numCameras);
    if (err != MCAM_ERR_OK || numCameras == 0) {
        return SetError("No cameras found");
    }

    // Enumerate all cameras
    for (uint32_t i = 0; i < numCameras; i++) {
        char deviceID[256] = { 0 };
        uint32_t size = sizeof(deviceID);

        if (ST_GetEnumDeviceID(i, deviceID, &size) == MCAM_ERR_OK) {
            // Get additional info
            char modelName[256] = { 0 };
            size = sizeof(modelName);
            ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_MODEL_NAME, modelName, &size);

            std::stringstream ss;
            ss << modelName << "_" << deviceID;
            cameraList.push_back(ss.str());
        }
    }

    return !cameraList.empty();
}

bool CvsBallVisionCore::Impl::ConnectCamera(const std::string& cameraId)
{
    if (m_bConnected) {
        return SetError("Camera already connected");
    }

    try {
        // Update device list
        CVS_ERROR err = ST_UpdateDevice(500);
        if (err != MCAM_ERR_OK) {
            return SetError("Failed to update device list");
        }

        uint32_t numCameras = 0;
        err = ST_GetAvailableCameraNum(&numCameras);
        if (err != MCAM_ERR_OK || numCameras == 0) {
            return SetError("No cameras found");
        }

        // Find camera to connect
        uint32_t targetEnum = 0;
        if (!cameraId.empty()) {
            // Find specific camera
            for (uint32_t i = 0; i < numCameras; i++) {
                char deviceID[256] = { 0 };
                uint32_t size = sizeof(deviceID);
                if (ST_GetEnumDeviceID(i, deviceID, &size) == MCAM_ERR_OK) {
                    if (cameraId.find(deviceID) != std::string::npos) {
                        targetEnum = i;
                        break;
                    }
                }
            }
        }

        // Check if camera is connectable
        bool isConnectable = false;
        err = ST_IsConnectable(targetEnum, &isConnectable);
        if (err != MCAM_ERR_OK || !isConnectable) {
            return SetError("Camera is not connectable");
        }

        // Open device
        err = ST_OpenDevice(targetEnum, &m_hDevice, false);
        if (err != MCAM_ERR_OK) {
            return SetError("Failed to open camera");
        }

        m_currentEnumNum = targetEnum;

        // Get camera information
        char info[256] = { 0 };
        uint32_t size = sizeof(info);

        if (ST_GetEnumDeviceInfo(targetEnum, MCAM_DEVICEINFO_MODEL_NAME, info, &size) == MCAM_ERR_OK) {
            m_cameraModel = std::string(info);
        }

        size = sizeof(info);
        if (ST_GetEnumDeviceInfo(targetEnum, MCAM_DEVICEINFO_SERIAL_NUMBER, info, &size) == MCAM_ERR_OK) {
            m_serialNumber = std::string(info);
        }

        // Initialize buffer
        err = ST_InitBuffer(m_hDevice, &m_cvsBuffer, 1);
        if (err != MCAM_ERR_OK) {
            ST_CloseDevice(m_hDevice);
            return SetError("Failed to initialize buffer");
        }

        // Set buffer count
        ST_SetBufferCount(m_hDevice, 10);

        // Initialize default parameters
        m_params.width = 1280;
        m_params.height = 880;
        m_params.fps = 100.0;
        m_params.exposureTime = 1000.0;  // 1ms
        m_params.gain = 0.0;

        // Apply initial parameters
        SetCameraParams(m_params);

        // Register callback
        err = ST_RegisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE, OnFrameGrabbed, this);
        if (err != MCAM_ERR_OK) {
            ST_FreeBuffer(&m_cvsBuffer);
            ST_CloseDevice(m_hDevice);
            return SetError("Failed to register callback");
        }

        m_bConnected = true;
        return true;
    }
    catch (const std::exception& e) {
        return SetError(std::string("Connection failed: ") + e.what());
    }
}

bool CvsBallVisionCore::Impl::DisconnectCamera()
{
    if (!m_bConnected) {
        return true;
    }

    if (m_bAcquiring) {
        StopAcquisition();
    }

    // Unregister callback
    ST_UnregisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE);

    // Free buffer
    ST_FreeBuffer(&m_cvsBuffer);

    // Close device
    CVS_ERROR err = ST_CloseDevice(m_hDevice);
    if (err != MCAM_ERR_OK) {
        SetError("Failed to close device properly");
    }

    m_hDevice = -1;
    m_bConnected = false;

    return true;
}

bool CvsBallVisionCore::Impl::StartAcquisition()
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    if (m_bAcquiring) {
        return true;
    }

    // Start acquisition
    CVS_ERROR err = ST_AcqStart(m_hDevice);
    if (err != MCAM_ERR_OK) {
        return SetError("Failed to start acquisition");
    }

    m_frameCount = 0;
    m_errorCount = 0;
    m_bStopThread = false;
    m_bAcquiring = true;

    // Start acquisition thread
    m_acquisitionThread = std::thread(&Impl::AcquisitionThread, this);

    return true;
}

bool CvsBallVisionCore::Impl::StopAcquisition()
{
    if (!m_bAcquiring) {
        return true;
    }

    m_bStopThread = true;

    // Stop acquisition
    CVS_ERROR err = ST_AcqStop(m_hDevice);
    if (err != MCAM_ERR_OK) {
        SetError("Failed to stop acquisition properly");
    }

    // Abort any pending grab
    ST_DoAbortGrab(m_hDevice);

    if (m_acquisitionThread.joinable()) {
        m_acquisitionThread.join();
    }

    m_bAcquiring = false;

    return true;
}

void CvsBallVisionCore::Impl::AcquisitionThread()
{
    while (!m_bStopThread) {
        // The actual grabbing is handled by the callback
        // This thread just keeps the acquisition alive
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool CvsBallVisionCore::Impl::SetImageSize(int width, int height)
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    if (m_bAcquiring) {
        return SetError("Cannot change size during acquisition");
    }

    CVS_ERROR err = ST_SetIntReg(m_hDevice, "Width", width);
    if (err != MCAM_ERR_OK) {
        return SetError("Failed to set width");
    }

    err = ST_SetIntReg(m_hDevice, "Height", height);
    if (err != MCAM_ERR_OK) {
        return SetError("Failed to set height");
    }

    std::lock_guard<std::mutex> lock(m_paramMutex);
    m_params.width = width;
    m_params.height = height;

    return true;
}

bool CvsBallVisionCore::Impl::GetImageSize(int& width, int& height)
{
    if (!m_bConnected) {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        width = m_params.width;
        height = m_params.height;
        return true;
    }

    int64_t w = 0, h = 0;
    CVS_ERROR err = ST_GetIntReg(m_hDevice, "Width", &w);
    if (err == MCAM_ERR_OK) {
        err = ST_GetIntReg(m_hDevice, "Height", &h);
        if (err == MCAM_ERR_OK) {
            width = static_cast<int>(w);
            height = static_cast<int>(h);
            return true;
        }
    }

    return false;
}

bool CvsBallVisionCore::Impl::SetFrameRate(double fps)
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    // Validate range
    if (fps < 1.0 || fps > 72.0) {
        return SetError("Frame rate out of range (1-72)");
    }

    CVS_ERROR err = ST_SetFloatReg(m_hDevice, "AcquisitionFrameRate", fps);
    if (err != MCAM_ERR_OK) {
        // Try alternative node name
        err = ST_SetFloatReg(m_hDevice, "AcquisitionFrameRateAbs", fps);
        if (err != MCAM_ERR_OK) {
            return SetError("Failed to set frame rate");
        }
    }

    std::lock_guard<std::mutex> lock(m_paramMutex);
    m_params.fps = fps;

    return true;
}

bool CvsBallVisionCore::Impl::GetFrameRate(double& fps)
{
    if (!m_bConnected) {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        fps = m_params.fps;
        return true;
    }

    double rate = 0.0;
    CVS_ERROR err = ST_GetFloatReg(m_hDevice, "AcquisitionFrameRate", &rate);
    if (err != MCAM_ERR_OK) {
        err = ST_GetFloatReg(m_hDevice, "AcquisitionFrameRateAbs", &rate);
    }

    if (err == MCAM_ERR_OK) {
        fps = rate;
        return true;
    }

    return false;
}

bool CvsBallVisionCore::Impl::GetFrameRateRange(double& minFps, double& maxFps)
{
    minFps = 1.0;
    maxFps = 72.0;

    if (m_bConnected) {
        double min = 0.0, max = 0.0;
        CVS_ERROR err = ST_GetFloatRegRange(m_hDevice, "AcquisitionFrameRate", &min, &max);
        if (err == MCAM_ERR_OK) {
            minFps = min;
            maxFps = max;
        }
    }

    return true;
}

bool CvsBallVisionCore::Impl::SetExposureTime(double exposureTimeUs)
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    // Validate range (1us to 3sec = 3000000us)
    if (exposureTimeUs < 1.0 || exposureTimeUs > 3000000.0) {
        return SetError("Exposure time out of range (1-3000000 us)");
    }

    CVS_ERROR err = ST_SetFloatReg(m_hDevice, "ExposureTime", exposureTimeUs);
    if (err != MCAM_ERR_OK) {
        // Try alternative node name
        err = ST_SetFloatReg(m_hDevice, "ExposureTimeAbs", exposureTimeUs);
        if (err != MCAM_ERR_OK) {
            return SetError("Failed to set exposure time");
        }
    }

    std::lock_guard<std::mutex> lock(m_paramMutex);
    m_params.exposureTime = exposureTimeUs;

    return true;
}

bool CvsBallVisionCore::Impl::GetExposureTime(double& exposureTimeUs)
{
    if (!m_bConnected) {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        exposureTimeUs = m_params.exposureTime;
        return true;
    }

    double exposure = 0.0;
    CVS_ERROR err = ST_GetFloatReg(m_hDevice, "ExposureTime", &exposure);
    if (err != MCAM_ERR_OK) {
        err = ST_GetFloatReg(m_hDevice, "ExposureTimeAbs", &exposure);
    }

    if (err == MCAM_ERR_OK) {
        exposureTimeUs = exposure;
        return true;
    }

    return false;
}

bool CvsBallVisionCore::Impl::GetExposureTimeRange(double& minExpUs, double& maxExpUs)
{
    minExpUs = 1.0;
    maxExpUs = 3000000.0;

    if (m_bConnected) {
        double min = 0.0, max = 0.0;
        CVS_ERROR err = ST_GetFloatRegRange(m_hDevice, "ExposureTime", &min, &max);
        if (err == MCAM_ERR_OK) {
            minExpUs = min;
            maxExpUs = max;
        }
    }

    return true;
}

bool CvsBallVisionCore::Impl::SetGain(double gainDb)
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    // Validate range (0-32 dB analog + 0-24 dB digital = 0-56 dB total)
    if (gainDb < 0.0 || gainDb > 56.0) {
        return SetError("Gain out of range (0-56 dB)");
    }

    // Set analog gain (0-32 dB)
    double analogGain = std::min(gainDb, 32.0);
    CVS_ERROR err = ST_SetFloatReg(m_hDevice, "Gain", analogGain);
    if (err != MCAM_ERR_OK) {
        return SetError("Failed to set analog gain");
    }

    // Set digital gain if needed (0-24 dB)
    double digitalGain = std::max(0.0, gainDb - 32.0);
    if (digitalGain > 0.0) {
        err = ST_SetFloatReg(m_hDevice, "DigitalGain", digitalGain);
        if (err != MCAM_ERR_OK) {
            // Digital gain might not be supported, not critical
        }
    }

    std::lock_guard<std::mutex> lock(m_paramMutex);
    m_params.gain = gainDb;

    return true;
}

bool CvsBallVisionCore::Impl::GetGain(double& gainDb)
{
    if (!m_bConnected) {
        std::lock_guard<std::mutex> lock(m_paramMutex);
        gainDb = m_params.gain;
        return true;
    }

    double analogGain = 0.0, digitalGain = 0.0;
    CVS_ERROR err = ST_GetFloatReg(m_hDevice, "Gain", &analogGain);
    if (err == MCAM_ERR_OK) {
        ST_GetFloatReg(m_hDevice, "DigitalGain", &digitalGain);  // Optional
        gainDb = analogGain + digitalGain;
        return true;
    }

    return false;
}

bool CvsBallVisionCore::Impl::GetGainRange(double& minGain, double& maxGain)
{
    minGain = 0.0;
    maxGain = 56.0;

    if (m_bConnected) {
        double min = 0.0, max = 0.0;
        CVS_ERROR err = ST_GetFloatRegRange(m_hDevice, "Gain", &min, &max);
        if (err == MCAM_ERR_OK) {
            minGain = min;
            maxGain = max;

            // Check for digital gain
            double dMin = 0.0, dMax = 0.0;
            if (ST_GetFloatRegRange(m_hDevice, "DigitalGain", &dMin, &dMax) == MCAM_ERR_OK) {
                maxGain += dMax;
            }
        }
    }

    return true;
}

bool CvsBallVisionCore::Impl::GetCameraParams(CameraParams& params)
{
    std::lock_guard<std::mutex> lock(m_paramMutex);
    params = m_params;
    return true;
}

bool CvsBallVisionCore::Impl::SetCameraParams(const CameraParams& params)
{
    if (!m_bConnected) {
        return SetError("Camera not connected");
    }

    bool success = true;
    success &= SetImageSize(params.width, params.height);
    success &= SetFrameRate(params.fps);
    success &= SetExposureTime(params.exposureTime);
    success &= SetGain(params.gain);

    return success;
}

void CvsBallVisionCore::Impl::RegisterFrameCallback(FrameCallback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_frameCallback = callback;
}

void CvsBallVisionCore::Impl::UnregisterFrameCallback()
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_frameCallback = nullptr;
}

bool CvsBallVisionCore::Impl::SetError(const std::string& error)
{
    m_lastError = error;
    return false;
}

// CvsBallVisionCore public interface implementation
CvsBallVisionCore::CvsBallVisionCore()
    : m_pImpl(new Impl())
{
}

CvsBallVisionCore::~CvsBallVisionCore()
{
    delete m_pImpl;
    m_pImpl = nullptr;
}

bool CvsBallVisionCore::EnumerateCameras(std::vector<std::string>& cameraList)
{
    return m_pImpl->EnumerateCameras(cameraList);
}

bool CvsBallVisionCore::ConnectCamera(const std::string& cameraId)
{
    return m_pImpl->ConnectCamera(cameraId);
}

bool CvsBallVisionCore::DisconnectCamera()
{
    return m_pImpl->DisconnectCamera();
}

bool CvsBallVisionCore::IsConnected() const
{
    return m_pImpl->IsConnected();
}

bool CvsBallVisionCore::StartAcquisition()
{
    return m_pImpl->StartAcquisition();
}

bool CvsBallVisionCore::StopAcquisition()
{
    return m_pImpl->StopAcquisition();
}

bool CvsBallVisionCore::IsAcquiring() const
{
    return m_pImpl->IsAcquiring();
}

bool CvsBallVisionCore::SetImageSize(int width, int height)
{
    return m_pImpl->SetImageSize(width, height);
}

bool CvsBallVisionCore::GetImageSize(int& width, int& height)
{
    return m_pImpl->GetImageSize(width, height);
}

bool CvsBallVisionCore::SetFrameRate(double fps)
{
    return m_pImpl->SetFrameRate(fps);
}

bool CvsBallVisionCore::GetFrameRate(double& fps)
{
    return m_pImpl->GetFrameRate(fps);
}

bool CvsBallVisionCore::GetFrameRateRange(double& minFps, double& maxFps)
{
    return m_pImpl->GetFrameRateRange(minFps, maxFps);
}

bool CvsBallVisionCore::SetExposureTime(double exposureTimeUs)
{
    return m_pImpl->SetExposureTime(exposureTimeUs);
}

bool CvsBallVisionCore::GetExposureTime(double& exposureTimeUs)
{
    return m_pImpl->GetExposureTime(exposureTimeUs);
}

bool CvsBallVisionCore::GetExposureTimeRange(double& minExpUs, double& maxExpUs)
{
    return m_pImpl->GetExposureTimeRange(minExpUs, maxExpUs);
}

bool CvsBallVisionCore::SetGain(double gainDb)
{
    return m_pImpl->SetGain(gainDb);
}

bool CvsBallVisionCore::GetGain(double& gainDb)
{
    return m_pImpl->GetGain(gainDb);
}

bool CvsBallVisionCore::GetGainRange(double& minGain, double& maxGain)
{
    return m_pImpl->GetGainRange(minGain, maxGain);
}

bool CvsBallVisionCore::GetCameraParams(CameraParams& params)
{
    return m_pImpl->GetCameraParams(params);
}

bool CvsBallVisionCore::SetCameraParams(const CameraParams& params)
{
    return m_pImpl->SetCameraParams(params);
}

void CvsBallVisionCore::RegisterFrameCallback(FrameCallback callback)
{
    m_pImpl->RegisterFrameCallback(callback);
}

void CvsBallVisionCore::UnregisterFrameCallback()
{
    m_pImpl->UnregisterFrameCallback();
}

std::string CvsBallVisionCore::GetLastError() const
{
    return m_pImpl->GetLastError();
}

std::string CvsBallVisionCore::GetCameraModel() const
{
    return m_pImpl->GetCameraModel();
}

std::string CvsBallVisionCore::GetCameraSerialNumber() const
{
    return m_pImpl->GetCameraSerialNumber();
}

std::string CvsBallVisionCore::GetCameraVendor() const
{
    return m_pImpl->GetCameraVendor();
}

// Export functions
extern "C" {
    CVSCORE_API CvsBallVisionCore* CreateCameraInstance()
    {
        return new CvsBallVisionCore();
    }

    CVSCORE_API void DestroyCameraInstance(CvsBallVisionCore* pCamera)
    {
        delete pCamera;
    }

    CVSCORE_API const char* GetVersionString()
    {
        return "CvsBallVisionCore v1.0.0 - CREVIS API";
    }
}