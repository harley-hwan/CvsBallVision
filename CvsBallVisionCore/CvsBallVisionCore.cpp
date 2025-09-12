#include "CvsBallVisionCore.h"
#include "cvsCamCtrl.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>

#pragma comment(lib, "cvsCamCtrl.lib")

namespace CvsBallVision
{
    // Forward declaration for callback type
    typedef void(*GrabCallbackFunc)(int32_t, const CVS_BUFFER*, void*);

    // RAII helper class for acquisition state management
    class AcquisitionGuard
    {
    private:
        int32_t m_hDevice;
        bool m_wasAcquiring;
        bool m_shouldRestart;

    public:
        AcquisitionGuard(int32_t hDevice, bool isAcquiring)
            : m_hDevice(hDevice)
            , m_wasAcquiring(isAcquiring)
            , m_shouldRestart(isAcquiring)
        {
            if (m_wasAcquiring)
            {
                ST_AcqStop(m_hDevice);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        ~AcquisitionGuard()
        {
            if (m_shouldRestart && m_wasAcquiring)
            {
                ST_AcqStart(m_hDevice);
            }
        }

        void DisableRestart() { m_shouldRestart = false; }
        bool WasAcquiring() const { return m_wasAcquiring; }
    };

    // RAII helper class for callback management
    class CallbackGuard
    {
    private:
        int32_t m_hDevice;
        bool m_wasRegistered;
        bool m_shouldReregister;
        GrabCallbackFunc m_callback;
        void* m_pUserData;

    public:
        CallbackGuard(int32_t hDevice, bool isRegistered, GrabCallbackFunc callback, void* pUserData)
            : m_hDevice(hDevice)
            , m_wasRegistered(isRegistered)
            , m_shouldReregister(isRegistered)
            , m_callback(callback)
            , m_pUserData(pUserData)
        {
            if (m_wasRegistered)
            {
                ST_UnregisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }

        ~CallbackGuard()
        {
            if (m_shouldReregister && m_wasRegistered && m_callback)
            {
                ST_RegisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE, m_callback, m_pUserData);
            }
        }

        void DisableReregister() { m_shouldReregister = false; }
        bool WasRegistered() const { return m_wasRegistered; }
    };

    // Image buffer pool for zero-copy optimization
    class ImageBufferPool
    {
    private:
        struct BufferInfo
        {
            CVS_BUFFER buffer;
            std::atomic<bool> inUse;
            uint64_t lastUsed;

            BufferInfo() : inUse(false), lastUsed(0)
            {
                memset(&buffer, 0, sizeof(CVS_BUFFER));
            }
        };

        std::vector<std::unique_ptr<BufferInfo>> m_buffers;
        std::mutex m_poolMutex;
        int32_t m_hDevice;
        size_t m_maxBuffers;

    public:
        ImageBufferPool(int32_t hDevice, size_t maxBuffers = 3)
            : m_hDevice(hDevice)
            , m_maxBuffers(maxBuffers)
        {
        }

        ~ImageBufferPool()
        {
            Clear();
        }

        CVS_BUFFER* GetBuffer()
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);

            // Find available buffer
            for (auto& bufInfo : m_buffers)
            {
                bool expected = false;
                if (bufInfo->inUse.compare_exchange_strong(expected, true))
                {
                    return &bufInfo->buffer;
                }
            }

            // Create new buffer if under limit
            if (m_buffers.size() < m_maxBuffers)
            {
                auto bufInfo = std::make_unique<BufferInfo>();

                CVS_ERROR status = ST_InitBuffer(m_hDevice, &bufInfo->buffer);
                if (status == MCAM_ERR_OK)
                {
                    bufInfo->inUse = true;
                    CVS_BUFFER* pBuffer = &bufInfo->buffer;
                    m_buffers.push_back(std::move(bufInfo));
                    return pBuffer;
                }
            }

            return nullptr;
        }

        void ReleaseBuffer(CVS_BUFFER* pBuffer)
        {
            if (!pBuffer) return;

            std::lock_guard<std::mutex> lock(m_poolMutex);
            for (auto& bufInfo : m_buffers)
            {
                if (&bufInfo->buffer == pBuffer)
                {
                    bufInfo->inUse = false;
                    bufInfo->lastUsed = std::chrono::steady_clock::now().time_since_epoch().count();
                    break;
                }
            }
        }

        // 중요: 모든 버퍼를 사용 가능 상태로 리셋
        void ResetBuffers()
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);
            for (auto& bufInfo : m_buffers)
            {
                bufInfo->inUse = false;
                bufInfo->lastUsed = 0;
            }
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);
            for (auto& bufInfo : m_buffers)
            {
                if (bufInfo->buffer.image.pImage)
                {
                    ST_FreeBuffer(&bufInfo->buffer);
                }
            }
            m_buffers.clear();
        }

        void Reinitialize()
        {
            Clear();
        }
    };

    // Implementation class
    class CameraController::Impl
    {
    public:
        Impl();
        ~Impl();

        // Static callback function must be public for external access
        static void StaticGrabCallback(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine);

        // Member variables
        int32_t m_hDevice;
        std::atomic<bool> m_bSystemInitialized;
        std::atomic<bool> m_bConnected;
        std::atomic<bool> m_bAcquiring;
        std::atomic<bool> m_bCallbackRegistered;

        // Optimized buffer management
        std::unique_ptr<ImageBufferPool> m_bufferPool;
        CVS_BUFFER* m_pCurrentBuffer;
        CVS_BUFFER m_rgbBuffer;
        std::mutex m_imageMutex;

        ImageCallback m_imageCallback;
        ErrorCallback m_errorCallback;
        StatusCallback m_statusCallback;

        std::thread m_grabThread;
        std::atomic<bool> m_bStopGrabThread;

        uint64_t m_frameCount;
        uint64_t m_errorCount;
        std::chrono::steady_clock::time_point m_lastFpsTime;
        uint64_t m_lastFrameCount;
        double m_currentFps;

        int m_lastError;

        // Current resolution cache
        int m_currentWidth;
        int m_currentHeight;

        // Feature availability flags
        bool m_bHasGain;
        bool m_bHasExposure;
        bool m_bHasFrameRate;
        std::string m_gainNodeName;

        // Zero-copy image data for callback
        ImageData m_lastImageData;

        // Methods
        void GrabThreadFunc();
        void OnImageReceived(const CVS_BUFFER* pBuffer);
        void ReportError(int error, const std::string& context);
        void ReportStatus(const std::string& status);
        bool IsColorCamera();
        void DetectAvailableFeatures();
        bool CheckFeatureAvailable(const char* nodeName);
        std::string FindGainNodeName();
        bool ReinitializeBuffers();
        bool SetResolutionOptimized(int width, int height);
    };

    CameraController::Impl::Impl()
        : m_hDevice(-1)
        , m_bSystemInitialized(false)
        , m_bConnected(false)
        , m_bAcquiring(false)
        , m_bCallbackRegistered(false)
        , m_bStopGrabThread(false)
        , m_frameCount(0)
        , m_errorCount(0)
        , m_lastFrameCount(0)
        , m_currentFps(0.0)
        , m_lastError(MCAM_ERR_OK)
        , m_currentWidth(0)
        , m_currentHeight(0)
        , m_bHasGain(false)
        , m_bHasExposure(false)
        , m_bHasFrameRate(false)
        , m_pCurrentBuffer(nullptr)
    {
        memset(&m_rgbBuffer, 0, sizeof(m_rgbBuffer));
        memset(&m_lastImageData, 0, sizeof(m_lastImageData));
        m_lastFpsTime = std::chrono::steady_clock::now();
    }

    CameraController::Impl::~Impl()
    {
        if (m_bAcquiring)
        {
            ST_AcqStop(m_hDevice);
        }

        if (m_bCallbackRegistered)
        {
            ST_UnregisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE);
        }

        if (m_bufferPool)
        {
            m_bufferPool->Clear();
        }

        if (m_rgbBuffer.image.pImage)
        {
            ST_FreeBuffer(&m_rgbBuffer);
        }

        if (m_bConnected)
        {
            ST_CloseDevice(m_hDevice);
        }

        if (m_bSystemInitialized)
        {
            ST_FreeSystem();
        }
    }

    bool CameraController::Impl::ReinitializeBuffers()
    {
        if (!m_bConnected)
            return false;

        // Reinitialize buffer pool
        if (m_bufferPool)
        {
            m_bufferPool->Reinitialize();
        }
        else
        {
            m_bufferPool = std::make_unique<ImageBufferPool>(m_hDevice, 3);
        }

        // Free and reinitialize RGB buffer if needed
        if (m_rgbBuffer.image.pImage)
        {
            ST_FreeBuffer(&m_rgbBuffer);
            memset(&m_rgbBuffer, 0, sizeof(m_rgbBuffer));
        }

        if (IsColorCamera())
        {
            CVS_ERROR status = ST_InitBuffer(m_hDevice, &m_rgbBuffer, 3);
            if (status != MCAM_ERR_OK)
            {
                ReportError(status, "Failed to reinitialize RGB buffer");
                // Not critical, continue
            }
        }

        return true;
    }

    bool CameraController::Impl::SetResolutionOptimized(int width, int height)
    {
        if (!m_bConnected)
            return false;

        // Check if resolution actually changed
        if (m_currentWidth == width && m_currentHeight == height)
        {
            ReportStatus("Resolution unchanged");
            return true;
        }

        // Use RAII guards for safe state management
        AcquisitionGuard acqGuard(m_hDevice, m_bAcquiring);
        CallbackGuard callbackGuard(m_hDevice, m_bCallbackRegistered, StaticGrabCallback, this);

        // Store original values for rollback
        int originalWidth = m_currentWidth;
        int originalHeight = m_currentHeight;

        // Set new resolution
        CVS_ERROR status = ST_SetIntReg(m_hDevice, "Width", width);
        if (status != MCAM_ERR_OK)
        {
            ReportError(status, "Failed to set width");
            return false;
        }

        status = ST_SetIntReg(m_hDevice, "Height", height);
        if (status != MCAM_ERR_OK)
        {
            // Rollback width change
            ST_SetIntReg(m_hDevice, "Width", originalWidth);
            ReportError(status, "Failed to set height");
            return false;
        }

        // Update cached resolution
        m_currentWidth = width;
        m_currentHeight = height;

        // Reinitialize buffers
        if (!ReinitializeBuffers())
        {
            // Rollback resolution changes
            ST_SetIntReg(m_hDevice, "Width", originalWidth);
            ST_SetIntReg(m_hDevice, "Height", originalHeight);
            m_currentWidth = originalWidth;
            m_currentHeight = originalHeight;
            ReinitializeBuffers();

            ReportError(-1, "Failed to reinitialize buffers");
            return false;
        }

        // Update acquisition state flags for guards
        m_bAcquiring = acqGuard.WasAcquiring();
        m_bCallbackRegistered = callbackGuard.WasRegistered();

        ReportStatus("Resolution changed successfully");
        return true;
    }

    bool CameraController::Impl::CheckFeatureAvailable(const char* nodeName)
    {
        if (!m_bConnected)
            return false;

        // Try to read the feature to check if it exists
        int64_t intVal;
        double floatVal;
        char strVal[256];
        uint32_t size = 256;

        // Try as integer
        if (ST_GetIntReg(m_hDevice, nodeName, &intVal) == MCAM_ERR_OK)
            return true;

        // Try as float
        if (ST_GetFloatReg(m_hDevice, nodeName, &floatVal) == MCAM_ERR_OK)
            return true;

        // Try as enumeration
        if (ST_GetEnumReg(m_hDevice, nodeName, strVal, &size) == MCAM_ERR_OK)
            return true;

        return false;
    }

    std::string CameraController::Impl::FindGainNodeName()
    {
        // Common gain node names used by different camera manufacturers
        const char* gainNames[] = {
            "Gain",
            "GainRaw",
            "AnalogGain",
            "DigitalGain",
            "GainAbs",
            "AllGain",
            "MasterGain"
        };

        for (const char* name : gainNames)
        {
            if (CheckFeatureAvailable(name))
            {
                ReportStatus(std::string("Found gain control: ") + name);
                return name;
            }
        }

        return "";
    }

    void CameraController::Impl::DetectAvailableFeatures()
    {
        if (!m_bConnected)
            return;

        ReportStatus("Detecting available camera features...");

        // Check for Exposure
        m_bHasExposure = CheckFeatureAvailable("ExposureTime");
        if (!m_bHasExposure)
        {
            m_bHasExposure = CheckFeatureAvailable("ExposureTimeAbs");
        }

        // Check for Gain (try multiple possible names)
        m_gainNodeName = FindGainNodeName();
        m_bHasGain = !m_gainNodeName.empty();

        // Check for Frame Rate
        m_bHasFrameRate = CheckFeatureAvailable("AcquisitionFrameRate");
        if (!m_bHasFrameRate)
        {
            m_bHasFrameRate = CheckFeatureAvailable("FrameRate");
        }

        // Report detected features
        std::stringstream ss;
        ss << "Features detected - ";
        ss << "Exposure: " << (m_bHasExposure ? "Yes" : "No") << ", ";
        ss << "Gain: " << (m_bHasGain ? "Yes" : "No");
        if (m_bHasGain)
        {
            ss << " (" << m_gainNodeName << ")";
        }
        ss << ", Frame Rate: " << (m_bHasFrameRate ? "Yes" : "No");

        ReportStatus(ss.str());
    }

    void CameraController::Impl::GrabThreadFunc()
    {
        while (!m_bStopGrabThread)
        {
            CVS_BUFFER* pBuffer = m_bufferPool ? m_bufferPool->GetBuffer() : nullptr;
            if (!pBuffer)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            CVS_ERROR status = ST_GrabImage(m_hDevice, pBuffer);

            if (status == MCAM_ERR_OK)
            {
                OnImageReceived(pBuffer);
            }
            else if (status == MCAM_ERR_TIMEOUT)
            {
                // Timeout is normal in trigger mode
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            else
            {
                m_errorCount++;
                ReportError(status, "Image grab failed");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (m_bufferPool)
            {
                m_bufferPool->ReleaseBuffer(pBuffer);
            }
        }
    }

    void CameraController::Impl::StaticGrabCallback(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine)
    {
        if (eventID == EVENT_NEW_IMAGE && pUserDefine)
        {
            Impl* pImpl = static_cast<Impl*>(pUserDefine);
            pImpl->OnImageReceived(pBuffer);
        }
    }

    void CameraController::Impl::OnImageReceived(const CVS_BUFFER* pBuffer)
    {
        if (!pBuffer || !m_imageCallback)
            return;

        // 획득 중이 아니면 무시
        if (!m_bAcquiring)
            return;

        std::lock_guard<std::mutex> lock(m_imageMutex);

        m_frameCount++;

        // Calculate FPS
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFpsTime).count();
        if (elapsed >= 1000)
        {
            m_currentFps = (m_frameCount - m_lastFrameCount) * 1000.0 / elapsed;
            m_lastFrameCount = m_frameCount;
            m_lastFpsTime = now;
        }

        // 버퍼 유효성 검사
        if (!pBuffer->image.pImage || pBuffer->image.width == 0 || pBuffer->image.height == 0)
        {
            ReportError(-1, "Invalid image buffer received");
            return;
        }

        // Zero-copy approach: directly use buffer data
        m_lastImageData.width = pBuffer->image.width;
        m_lastImageData.height = pBuffer->image.height;
        m_lastImageData.step = pBuffer->image.step;
        m_lastImageData.blockID = pBuffer->blockID;
        m_lastImageData.timestamp = pBuffer->timestamp;

        // Check if color conversion is needed
        if (IsColorCamera() && m_rgbBuffer.image.pImage)
        {
            // Convert Bayer to RGB
            CVS_ERROR status = ST_CvtColor(*pBuffer, &m_rgbBuffer, CVP_BayerRG2RGB);
            if (status == MCAM_ERR_OK)
            {
                m_lastImageData.pData = (uint8_t*)m_rgbBuffer.image.pImage;
                m_lastImageData.channels = m_rgbBuffer.image.channels;
                m_lastImageData.width = m_rgbBuffer.image.width;
                m_lastImageData.height = m_rgbBuffer.image.height;
                m_lastImageData.step = m_rgbBuffer.image.step;
            }
            else
            {
                // Fall back to raw data
                m_lastImageData.pData = (uint8_t*)pBuffer->image.pImage;
                m_lastImageData.channels = pBuffer->image.channels;
            }
        }
        else
        {
            // Use raw data directly (zero-copy)
            m_lastImageData.pData = (uint8_t*)pBuffer->image.pImage;
            m_lastImageData.channels = pBuffer->image.channels;
        }

        // Call the callback with zero-copy data
        if (m_imageCallback)
        {
            m_imageCallback(m_lastImageData);
        }
    }

    void CameraController::Impl::ReportError(int error, const std::string& context)
    {
        m_lastError = error;
        if (m_errorCallback)
        {
            std::stringstream ss;
            ss << context << " (Error: " << error << ")";
            m_errorCallback(error, ss.str());
        }
    }

    void CameraController::Impl::ReportStatus(const std::string& status)
    {
        if (m_statusCallback)
        {
            m_statusCallback(status);
        }
    }

    bool CameraController::Impl::IsColorCamera()
    {
        char pixelFormat[256] = { 0 };
        uint32_t size = 256;
        CVS_ERROR status = ST_GetEnumReg(m_hDevice, "PixelFormat", pixelFormat, &size);

        if (status == MCAM_ERR_OK)
        {
            std::string format(pixelFormat);
            return (format.find("Bayer") != std::string::npos);
        }

        return false;
    }

    // CameraController implementation
    CameraController::CameraController()
        : m_pImpl(std::make_unique<Impl>())
    {
    }

    CameraController::~CameraController() = default;

    bool CameraController::InitializeSystem()
    {
        if (m_pImpl->m_bSystemInitialized)
            return true;

        CVS_ERROR status = ST_InitSystem();
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to initialize system");
            return false;
        }

        m_pImpl->m_bSystemInitialized = true;
        m_pImpl->ReportStatus("System initialized successfully");
        return true;
    }

    void CameraController::FreeSystem()
    {
        if (m_pImpl->m_bSystemInitialized)
        {
            ST_FreeSystem();
            m_pImpl->m_bSystemInitialized = false;
            m_pImpl->ReportStatus("System freed");
        }
    }

    bool CameraController::IsSystemInitialized() const
    {
        return m_pImpl->m_bSystemInitialized;
    }

    bool CameraController::UpdateDeviceList(uint32_t timeout)
    {
        if (!m_pImpl->m_bSystemInitialized)
        {
            m_pImpl->ReportError(-1, "System not initialized");
            return false;
        }

        CVS_ERROR status = ST_UpdateDevice(timeout);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to update device list");
            return false;
        }

        m_pImpl->ReportStatus("Device list updated");
        return true;
    }

    std::vector<CameraInfo> CameraController::GetAvailableCameras()
    {
        std::vector<CameraInfo> cameras;

        if (!m_pImpl->m_bSystemInitialized)
            return cameras;

        uint32_t camNum = 0;
        CVS_ERROR status = ST_GetAvailableCameraNum(&camNum);

        if (status != MCAM_ERR_OK || camNum == 0)
            return cameras;

        cameras.reserve(camNum);

        for (uint32_t i = 0; i < camNum; i++)
        {
            CameraInfo info;
            info.enumIndex = i;
            info.isConnected = false;

            char buffer[256];
            uint32_t size;

            // Get User ID
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_USER_ID, buffer, &size) == MCAM_ERR_OK)
                info.userID = buffer;

            // Get Model Name
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_MODEL_NAME, buffer, &size) == MCAM_ERR_OK)
                info.modelName = buffer;

            // Get Serial Number
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_SERIAL_NUMBER, buffer, &size) == MCAM_ERR_OK)
                info.serialNumber = buffer;

            // Get Device Version
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_DEVICE_VERSION, buffer, &size) == MCAM_ERR_OK)
                info.deviceVersion = buffer;

            // Get IP Address (GigE only)
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_IP_ADDRESS, buffer, &size) == MCAM_ERR_OK)
                info.ipAddress = buffer;

            // Get MAC Address (GigE only)
            size = 256;
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_MAC_ADDRESS, buffer, &size) == MCAM_ERR_OK)
                info.macAddress = buffer;

            cameras.push_back(info);
        }

        return cameras;
    }

    bool CameraController::ConnectCamera(uint32_t enumIndex)
    {
        if (!m_pImpl->m_bSystemInitialized)
        {
            m_pImpl->ReportError(-1, "System not initialized");
            return false;
        }

        if (m_pImpl->m_bConnected)
        {
            DisconnectCamera();
        }

        CVS_ERROR status = ST_OpenDevice(enumIndex, &m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to open device");
            return false;
        }

        m_pImpl->m_bConnected = true;

        // Initialize buffer pool
        m_pImpl->m_bufferPool = std::make_unique<ImageBufferPool>(m_pImpl->m_hDevice, 3);

        // Detect available features
        m_pImpl->DetectAvailableFeatures();

        // Initialize RGB buffer if color camera
        if (m_pImpl->IsColorCamera())
        {
            status = ST_InitBuffer(m_pImpl->m_hDevice, &m_pImpl->m_rgbBuffer, 3);
            if (status != MCAM_ERR_OK)
            {
                m_pImpl->ReportError(status, "Failed to initialize RGB buffer");
                // Not critical, continue
            }
        }

        // Get current resolution
        int64_t width, height;
        ST_GetIntReg(m_pImpl->m_hDevice, "Width", &width);
        ST_GetIntReg(m_pImpl->m_hDevice, "Height", &height);
        m_pImpl->m_currentWidth = static_cast<int>(width);
        m_pImpl->m_currentHeight = static_cast<int>(height);

        // Set default parameters for 1280x880 @ 100 FPS
        SetResolution(1280, 880);

        // Only set frame rate if supported
        if (m_pImpl->m_bHasFrameRate)
        {
            SetFrameRate(100.0);
        }

        // Register callback
        status = ST_RegisterGrabCallback(m_pImpl->m_hDevice, EVENT_NEW_IMAGE,
            Impl::StaticGrabCallback, m_pImpl.get());
        if (status == MCAM_ERR_OK)
        {
            m_pImpl->m_bCallbackRegistered = true;
        }

        m_pImpl->ReportStatus("Camera connected successfully");
        return true;
    }

    bool CameraController::DisconnectCamera()
    {
        if (!m_pImpl->m_bConnected)
            return true;

        if (m_pImpl->m_bAcquiring)
        {
            StopAcquisition();
        }

        if (m_pImpl->m_bCallbackRegistered)
        {
            ST_UnregisterGrabCallback(m_pImpl->m_hDevice, EVENT_NEW_IMAGE);
            m_pImpl->m_bCallbackRegistered = false;
        }

        // Clear buffer pool
        if (m_pImpl->m_bufferPool)
        {
            m_pImpl->m_bufferPool->Clear();
            m_pImpl->m_bufferPool.reset();
        }

        if (m_pImpl->m_rgbBuffer.image.pImage)
        {
            ST_FreeBuffer(&m_pImpl->m_rgbBuffer);
            memset(&m_pImpl->m_rgbBuffer, 0, sizeof(m_pImpl->m_rgbBuffer));
        }

        CVS_ERROR status = ST_CloseDevice(m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to close device");
            return false;
        }

        m_pImpl->m_hDevice = -1;
        m_pImpl->m_bConnected = false;
        m_pImpl->m_bHasGain = false;
        m_pImpl->m_bHasExposure = false;
        m_pImpl->m_bHasFrameRate = false;
        m_pImpl->m_gainNodeName.clear();

        m_pImpl->ReportStatus("Camera disconnected");
        return true;
    }

    bool CameraController::IsConnected() const
    {
        return m_pImpl->m_bConnected;
    }

    bool CameraController::StartAcquisition()
    {
        if (!m_pImpl->m_bConnected)
        {
            m_pImpl->ReportError(-1, "Camera not connected");
            return false;
        }

        if (m_pImpl->m_bAcquiring)
            return true;

        // 버퍼 풀 상태 리셋
        if (m_pImpl->m_bufferPool)
        {
            m_pImpl->m_bufferPool->ResetBuffers();
        }

        // 이미지 데이터 초기화
        {
            std::lock_guard<std::mutex> lock(m_pImpl->m_imageMutex);
            memset(&m_pImpl->m_lastImageData, 0, sizeof(m_pImpl->m_lastImageData));
            m_pImpl->m_pCurrentBuffer = nullptr;
        }

        // 콜백이 등록되어 있지 않으면 재등록
        if (!m_pImpl->m_bCallbackRegistered)
        {
            CVS_ERROR status = ST_RegisterGrabCallback(m_pImpl->m_hDevice, EVENT_NEW_IMAGE,
                Impl::StaticGrabCallback, m_pImpl.get());
            if (status == MCAM_ERR_OK)
            {
                m_pImpl->m_bCallbackRegistered = true;
            }
            else
            {
                m_pImpl->ReportError(status, "Failed to register callback");
                return false;
            }
        }

        // 약간의 지연 추가 (하드웨어 준비 시간)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        CVS_ERROR status = ST_AcqStart(m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to start acquisition");
            return false;
        }

        m_pImpl->m_bAcquiring = true;
        m_pImpl->m_frameCount = 0;
        m_pImpl->m_errorCount = 0;
        m_pImpl->m_lastFrameCount = 0;
        m_pImpl->m_lastFpsTime = std::chrono::steady_clock::now();

        m_pImpl->ReportStatus("Acquisition started");
        return true;
    }


    bool CameraController::StopAcquisition()
    {
        if (!m_pImpl->m_bAcquiring)
            return true;

        // Stop grab thread if running
        if (m_pImpl->m_grabThread.joinable())
        {
            m_pImpl->m_bStopGrabThread = true;
            m_pImpl->m_grabThread.join();
            m_pImpl->m_bStopGrabThread = false;
        }

        CVS_ERROR status = ST_AcqStop(m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to stop acquisition");
            return false;
        }

        m_pImpl->m_bAcquiring = false;

        // 충분한 대기 시간 (카메라가 완전히 정지할 때까지)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 버퍼 풀 상태 리셋
        if (m_pImpl->m_bufferPool)
        {
            m_pImpl->m_bufferPool->ResetBuffers();
        }

        // 이미지 데이터 초기화
        {
            std::lock_guard<std::mutex> lock(m_pImpl->m_imageMutex);
            memset(&m_pImpl->m_lastImageData, 0, sizeof(m_pImpl->m_lastImageData));
            m_pImpl->m_pCurrentBuffer = nullptr;
        }

        // 콜백 재등록을 위해 플래그 리셋 (다음 Start 시 재등록)
        if (m_pImpl->m_bCallbackRegistered)
        {
            ST_UnregisterGrabCallback(m_pImpl->m_hDevice, EVENT_NEW_IMAGE);
            m_pImpl->m_bCallbackRegistered = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        m_pImpl->ReportStatus("Acquisition stopped");
        return true;
    }

    bool CameraController::IsAcquiring() const
    {
        return m_pImpl->m_bAcquiring;
    }

    bool CameraController::SetResolution(int width, int height)
    {
        return m_pImpl->SetResolutionOptimized(width, height);
    }

    bool CameraController::GetResolution(int& width, int& height)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        int64_t w, h;
        CVS_ERROR status = ST_GetIntReg(m_pImpl->m_hDevice, "Width", &w);
        if (status != MCAM_ERR_OK)
            return false;

        status = ST_GetIntReg(m_pImpl->m_hDevice, "Height", &h);
        if (status != MCAM_ERR_OK)
            return false;

        width = static_cast<int>(w);
        height = static_cast<int>(h);
        return true;
    }

    bool CameraController::SetExposureTime(double exposureTimeUs)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        if (!m_pImpl->m_bHasExposure)
        {
            m_pImpl->ReportStatus("Exposure control not available on this camera");
            return false;
        }

        CVS_ERROR status = ST_SetFloatReg(m_pImpl->m_hDevice, "ExposureTime", exposureTimeUs);
        if (status != MCAM_ERR_OK)
        {
            status = ST_SetFloatReg(m_pImpl->m_hDevice, "ExposureTimeAbs", exposureTimeUs);
        }

        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set exposure time");
            return false;
        }

        return true;
    }

    bool CameraController::GetExposureTime(double& exposureTimeUs)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasExposure)
            return false;

        CVS_ERROR status = ST_GetFloatReg(m_pImpl->m_hDevice, "ExposureTime", &exposureTimeUs);
        if (status != MCAM_ERR_OK)
        {
            status = ST_GetFloatReg(m_pImpl->m_hDevice, "ExposureTimeAbs", &exposureTimeUs);
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::GetExposureTimeRange(double& min, double& max)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasExposure)
            return false;

        CVS_ERROR status = ST_GetFloatRegRange(m_pImpl->m_hDevice, "ExposureTime", &min, &max);
        if (status != MCAM_ERR_OK)
        {
            status = ST_GetFloatRegRange(m_pImpl->m_hDevice, "ExposureTimeAbs", &min, &max);
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::SetGain(double gain)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        if (!m_pImpl->m_bHasGain)
        {
            m_pImpl->ReportStatus("Gain control not available on this camera");
            return false;
        }

        CVS_ERROR status = ST_SetFloatReg(m_pImpl->m_hDevice,
            m_pImpl->m_gainNodeName.c_str(), gain);

        if (status != MCAM_ERR_OK)
        {
            status = ST_SetIntReg(m_pImpl->m_hDevice,
                m_pImpl->m_gainNodeName.c_str(),
                static_cast<int64_t>(gain));
        }

        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set gain");
            return false;
        }

        return true;
    }

    bool CameraController::GetGain(double& gain)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasGain)
            return false;

        CVS_ERROR status = ST_GetFloatReg(m_pImpl->m_hDevice,
            m_pImpl->m_gainNodeName.c_str(), &gain);

        if (status != MCAM_ERR_OK)
        {
            int64_t intGain;
            status = ST_GetIntReg(m_pImpl->m_hDevice,
                m_pImpl->m_gainNodeName.c_str(), &intGain);
            if (status == MCAM_ERR_OK)
            {
                gain = static_cast<double>(intGain);
            }
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::GetGainRange(double& min, double& max)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasGain)
            return false;

        CVS_ERROR status = ST_GetFloatRegRange(m_pImpl->m_hDevice,
            m_pImpl->m_gainNodeName.c_str(), &min, &max);

        if (status != MCAM_ERR_OK)
        {
            int64_t intMin, intMax, intInc;
            status = ST_GetIntRegRange(m_pImpl->m_hDevice,
                m_pImpl->m_gainNodeName.c_str(),
                &intMin, &intMax, &intInc);
            if (status == MCAM_ERR_OK)
            {
                min = static_cast<double>(intMin);
                max = static_cast<double>(intMax);
            }
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::SetFrameRate(double fps)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        if (!m_pImpl->m_bHasFrameRate)
        {
            m_pImpl->ReportStatus("Frame rate control not available on this camera");
            return false;
        }

        CVS_ERROR status = ST_SetFloatReg(m_pImpl->m_hDevice, "AcquisitionFrameRate", fps);
        if (status != MCAM_ERR_OK)
        {
            status = ST_SetFloatReg(m_pImpl->m_hDevice, "FrameRate", fps);
        }

        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set frame rate");
            return false;
        }

        return true;
    }

    bool CameraController::GetFrameRate(double& fps)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasFrameRate)
            return false;

        CVS_ERROR status = ST_GetFloatReg(m_pImpl->m_hDevice, "AcquisitionFrameRate", &fps);
        if (status != MCAM_ERR_OK)
        {
            status = ST_GetFloatReg(m_pImpl->m_hDevice, "FrameRate", &fps);
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::GetFrameRateRange(double& min, double& max)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bHasFrameRate)
            return false;

        CVS_ERROR status = ST_GetFloatRegRange(m_pImpl->m_hDevice, "AcquisitionFrameRate", &min, &max);
        if (status != MCAM_ERR_OK)
        {
            status = ST_GetFloatRegRange(m_pImpl->m_hDevice, "FrameRate", &min, &max);
        }

        return (status == MCAM_ERR_OK);
    }

    bool CameraController::SetPixelFormat(const std::string& format)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_SetEnumReg(m_pImpl->m_hDevice, "PixelFormat", const_cast<char*>(format.c_str()));
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set pixel format");
            return false;
        }

        return true;
    }

    std::string CameraController::GetPixelFormat()
    {
        if (!m_pImpl->m_bConnected)
            return "";

        char buffer[256];
        uint32_t size = 256;
        CVS_ERROR status = ST_GetEnumReg(m_pImpl->m_hDevice, "PixelFormat", buffer, &size);

        if (status == MCAM_ERR_OK)
            return std::string(buffer);

        return "";
    }

    std::vector<std::string> CameraController::GetAvailablePixelFormats()
    {
        std::vector<std::string> formats;

        if (!m_pImpl->m_bConnected)
            return formats;

        int32_t entrySize = 0;
        CVS_ERROR status = ST_GetEnumEntrySize(m_pImpl->m_hDevice, "PixelFormat", &entrySize);

        if (status != MCAM_ERR_OK)
            return formats;

        formats.reserve(entrySize);

        for (int32_t i = 0; i < entrySize; i++)
        {
            char buffer[256];
            uint32_t size = 256;
            status = ST_GetEnumEntryValue(m_pImpl->m_hDevice, "PixelFormat", i, buffer, &size);

            if (status == MCAM_ERR_OK)
                formats.push_back(std::string(buffer));
        }

        return formats;
    }

    bool CameraController::SetTriggerMode(bool enable)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_SetEnumReg(m_pImpl->m_hDevice, "TriggerMode",
            const_cast<char*>(enable ? "On" : "Off"));
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set trigger mode");
            return false;
        }

        return true;
    }

    bool CameraController::SetTriggerSource(const std::string& source)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_SetEnumReg(m_pImpl->m_hDevice, "TriggerSource",
            const_cast<char*>(source.c_str()));
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to set trigger source");
            return false;
        }

        return true;
    }

    bool CameraController::ExecuteSoftwareTrigger()
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_SetCmdReg(m_pImpl->m_hDevice, "TriggerSoftware");
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to execute software trigger");
            return false;
        }

        return true;
    }

    bool CameraController::GetLatestImage(ImageData& imageData)
    {
        if (!m_pImpl->m_bConnected || !m_pImpl->m_bAcquiring)
            return false;

        std::lock_guard<std::mutex> lock(m_pImpl->m_imageMutex);

        if (!m_pImpl->m_lastImageData.pData)
            return false;

        // Return reference to last image (zero-copy)
        imageData = m_pImpl->m_lastImageData;
        return true;
    }

    void CameraController::RegisterImageCallback(ImageCallback callback)
    {
        m_pImpl->m_imageCallback = callback;
    }

    void CameraController::RegisterErrorCallback(ErrorCallback callback)
    {
        m_pImpl->m_errorCallback = callback;
    }

    void CameraController::RegisterStatusCallback(StatusCallback callback)
    {
        m_pImpl->m_statusCallback = callback;
    }

    void CameraController::GetStatistics(uint64_t& frameCount, uint64_t& errorCount, double& currentFps)
    {
        frameCount = m_pImpl->m_frameCount;
        errorCount = m_pImpl->m_errorCount;
        currentFps = m_pImpl->m_currentFps;
    }

    int CameraController::GetLastError() const
    {
        return m_pImpl->m_lastError;
    }

    std::string CameraController::GetLastErrorDescription() const
    {
        if (!m_pImpl->m_bConnected)
            return "Not connected";

        const char* desc = ST_GetLastErrorDescription(m_pImpl->m_hDevice);
        if (desc)
            return std::string(desc);

        return "Unknown error";
    }

    bool CameraController::SaveParameters(const std::string& filePath)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_ExportJson(m_pImpl->m_hDevice, filePath.c_str());
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to save parameters");
            return false;
        }

        return true;
    }

    bool CameraController::LoadParameters(const std::string& filePath)
    {
        if (!m_pImpl->m_bConnected)
            return false;

        CVS_ERROR status = ST_ImportJson(m_pImpl->m_hDevice, filePath.c_str());
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to load parameters");
            return false;
        }

        return true;
    }

    // Utility functions
    std::string GetSDKVersion()
    {
        return "1.0.8";
    }

    bool ConvertBayerToRGB(const uint8_t* pSrc, uint8_t* pDst,
        int width, int height,
        const std::string& bayerPattern)
    {
        if (!pSrc || !pDst)
            return false;

        CVS_BUFFER srcBuffer = { 0 };
        srcBuffer.image.pImage = const_cast<uint8_t*>(pSrc);
        srcBuffer.image.width = width;
        srcBuffer.image.height = height;
        srcBuffer.image.channels = 1;
        srcBuffer.image.step = width;

        CVS_BUFFER dstBuffer = { 0 };
        dstBuffer.image.pImage = pDst;
        dstBuffer.image.width = width;
        dstBuffer.image.height = height;
        dstBuffer.image.channels = 3;
        dstBuffer.image.step = width * 3;

        int convCode = CVP_BayerRG2RGB;
        if (bayerPattern == "BayerBG")
            convCode = CVP_BayerBG2RGB;
        else if (bayerPattern == "BayerGB")
            convCode = CVP_BayerGB2RGB;
        else if (bayerPattern == "BayerGR")
            convCode = CVP_BayerGR2RGB;

        CVS_ERROR status = ST_CvtColor(srcBuffer, &dstBuffer, convCode);
        return (status == MCAM_ERR_OK);
    }
}