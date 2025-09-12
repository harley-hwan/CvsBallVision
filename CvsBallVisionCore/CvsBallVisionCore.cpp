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
        std::atomic<bool>* m_pAcquiringFlag;

    public:
        AcquisitionGuard(int32_t hDevice, std::atomic<bool>* pAcquiringFlag)
            : m_hDevice(hDevice)
            , m_pAcquiringFlag(pAcquiringFlag)
            , m_wasAcquiring(pAcquiringFlag->load())
            , m_shouldRestart(m_wasAcquiring)
        {
            if (m_wasAcquiring)
            {
                *m_pAcquiringFlag = false;
                ST_AcqStop(m_hDevice);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        ~AcquisitionGuard()
        {
            if (m_shouldRestart && m_wasAcquiring)
            {
                ST_AcqStart(m_hDevice);
                *m_pAcquiringFlag = true;
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
        std::atomic<bool>* m_pCallbackFlag;

    public:
        CallbackGuard(int32_t hDevice, std::atomic<bool>* pCallbackFlag, GrabCallbackFunc callback, void* pUserData)
            : m_hDevice(hDevice)
            , m_pCallbackFlag(pCallbackFlag)
            , m_wasRegistered(pCallbackFlag->load())
            , m_shouldReregister(m_wasRegistered)
            , m_callback(callback)
            , m_pUserData(pUserData)
        {
            if (m_wasRegistered)
            {
                *m_pCallbackFlag = false;
                ST_UnregisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE);
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
        }

        ~CallbackGuard()
        {
            if (m_shouldReregister && m_wasRegistered && m_callback)
            {
                ST_RegisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE, m_callback, m_pUserData);
                *m_pCallbackFlag = true;
            }
        }

        void DisableReregister() { m_shouldReregister = false; }
        bool WasRegistered() const { return m_wasRegistered; }
    };

    // Resolution change transaction for safe rollback
    class ResolutionTransaction
    {
    private:
        int32_t m_hDevice;
        int m_oldWidth;
        int m_oldHeight;
        bool m_committed;
        bool m_needsRollback;

    public:
        ResolutionTransaction(int32_t hDevice, int oldWidth, int oldHeight)
            : m_hDevice(hDevice)
            , m_oldWidth(oldWidth)
            , m_oldHeight(oldHeight)
            , m_committed(false)
            , m_needsRollback(false)
        {
        }

        ~ResolutionTransaction()
        {
            if (!m_committed && m_needsRollback)
            {
                // Rollback resolution changes
                ST_SetIntReg(m_hDevice, "Width", m_oldWidth);
                ST_SetIntReg(m_hDevice, "Height", m_oldHeight);
            }
        }

        void EnableRollback() { m_needsRollback = true; }
        void Commit() { m_committed = true; }
        int GetOldWidth() const { return m_oldWidth; }
        int GetOldHeight() const { return m_oldHeight; }
    };

    // Optimized image buffer pool for zero-copy and real-time performance
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
        std::atomic<bool> m_shuttingDown;

    public:
        ImageBufferPool(int32_t hDevice, size_t maxBuffers = 3)
            : m_hDevice(hDevice)
            , m_maxBuffers(maxBuffers)
            , m_shuttingDown(false)
        {
            // Pre-allocate buffers for better real-time performance
            PreallocateBuffers();
        }

        ~ImageBufferPool()
        {
            m_shuttingDown = true;
            WaitForAllBuffersReturned();
            Clear();
        }

        void PreallocateBuffers()
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);
            m_buffers.reserve(m_maxBuffers);

            // Pre-create initial buffers
            for (size_t i = 0; i < 2 && i < m_maxBuffers; ++i)
            {
                auto bufInfo = std::make_unique<BufferInfo>();
                CVS_ERROR status = ST_InitBuffer(m_hDevice, &bufInfo->buffer);
                if (status == MCAM_ERR_OK)
                {
                    m_buffers.push_back(std::move(bufInfo));
                }
            }
        }

        CVS_BUFFER* GetBuffer()
        {
            if (m_shuttingDown)
                return nullptr;

            // Try to get buffer without locking first (lock-free fast path)
            for (auto& bufInfo : m_buffers)
            {
                bool expected = false;
                if (bufInfo->inUse.compare_exchange_strong(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return &bufInfo->buffer;
                }
            }

            // Need to create new buffer
            std::lock_guard<std::mutex> lock(m_poolMutex);

            // Double-check after acquiring lock
            for (auto& bufInfo : m_buffers)
            {
                bool expected = false;
                if (bufInfo->inUse.compare_exchange_strong(expected, true,
                    std::memory_order_acquire, std::memory_order_relaxed))
                {
                    return &bufInfo->buffer;
                }
            }

            // Create new buffer if under limit
            if (m_buffers.size() < m_maxBuffers && !m_shuttingDown)
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

            for (auto& bufInfo : m_buffers)
            {
                if (&bufInfo->buffer == pBuffer)
                {
                    bufInfo->inUse.store(false, std::memory_order_release);
                    bufInfo->lastUsed = std::chrono::steady_clock::now().time_since_epoch().count();
                    break;
                }
            }
        }

        void ResetBuffers()
        {
            for (auto& bufInfo : m_buffers)
            {
                bufInfo->inUse.store(false, std::memory_order_release);
                bufInfo->lastUsed = 0;
            }
        }

        void WaitForAllBuffersReturned()
        {
            auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < timeout)
            {
                bool allReturned = true;
                for (auto& bufInfo : m_buffers)
                {
                    if (bufInfo->inUse.load(std::memory_order_acquire))
                    {
                        allReturned = false;
                        break;
                    }
                }
                if (allReturned) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);
            for (auto& bufInfo : m_buffers)
            {
                // Wait for buffer to be released
                int retries = 10;
                while (bufInfo->inUse.load(std::memory_order_acquire) && retries-- > 0)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                if (bufInfo->buffer.image.pImage)
                {
                    ST_FreeBuffer(&bufInfo->buffer);
                }
            }
            m_buffers.clear();
        }

        void Reinitialize()
        {
            m_shuttingDown = false;
            WaitForAllBuffersReturned();
            Clear();
            PreallocateBuffers();
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
        std::atomic<bool> m_bShuttingDown;

        // Optimized buffer management
        std::unique_ptr<ImageBufferPool> m_bufferPool;
        CVS_BUFFER* m_pCurrentBuffer;
        CVS_BUFFER m_rgbBuffer;
        std::mutex m_imageMutex;
        std::mutex m_callbackMutex;

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
        bool ValidateBufferSize(const CVS_BUFFER* pSrc, const CVS_BUFFER* pDst);
    };

    CameraController::Impl::Impl()
        : m_hDevice(-1)
        , m_bSystemInitialized(false)
        , m_bConnected(false)
        , m_bAcquiring(false)
        , m_bCallbackRegistered(false)
        , m_bShuttingDown(false)
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
        m_bShuttingDown = true;

        // Stop acquisition first
        if (m_bAcquiring)
        {
            m_bAcquiring = false;
            ST_AcqStop(m_hDevice);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Clear callbacks
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_imageCallback = nullptr;
            m_errorCallback = nullptr;
            m_statusCallback = nullptr;
        }

        if (m_bCallbackRegistered)
        {
            ST_UnregisterGrabCallback(m_hDevice, EVENT_NEW_IMAGE);
            m_bCallbackRegistered = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (m_bufferPool)
        {
            m_bufferPool.reset();
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

    bool CameraController::Impl::ValidateBufferSize(const CVS_BUFFER* pSrc, const CVS_BUFFER* pDst)
    {
        if (!pSrc || !pDst || !pSrc->image.pImage || !pDst->image.pImage)
            return false;

        size_t srcSize = pSrc->image.width * pSrc->image.height * pSrc->image.channels;
        size_t dstSize = pDst->image.width * pDst->image.height * pDst->image.channels;

        // For Bayer to RGB conversion, destination needs 3x the source size
        if (pSrc->image.channels == 1 && pDst->image.channels == 3)
        {
            return (dstSize >= srcSize * 3);
        }

        return (dstSize >= srcSize);
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
            // RGB buffer initialization with size validation
            CVS_ERROR status = ST_InitBuffer(m_hDevice, &m_rgbBuffer, 3);
            if (status != MCAM_ERR_OK)
            {
                ReportError(status, "Failed to reinitialize RGB buffer");
                return false;
            }

            // Verify buffer size matches current resolution
            if (m_rgbBuffer.image.width != m_currentWidth ||
                m_rgbBuffer.image.height != m_currentHeight)
            {
                // Resize buffer to match current resolution
                ST_FreeBuffer(&m_rgbBuffer);
                memset(&m_rgbBuffer, 0, sizeof(m_rgbBuffer));

                // Try to initialize with specific size
                m_rgbBuffer.image.width = m_currentWidth;
                m_rgbBuffer.image.height = m_currentHeight;
                m_rgbBuffer.image.channels = 3;
                status = ST_InitBuffer(m_hDevice, &m_rgbBuffer, 3);

                if (status != MCAM_ERR_OK)
                {
                    ReportError(status, "Failed to initialize RGB buffer with specific size");
                    return false;
                }
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

        // Create transaction for safe rollback
        ResolutionTransaction transaction(m_hDevice, m_currentWidth, m_currentHeight);

        // Use RAII guards for safe state management
        AcquisitionGuard acqGuard(m_hDevice, &m_bAcquiring);
        CallbackGuard callbackGuard(m_hDevice, &m_bCallbackRegistered,
            StaticGrabCallback, this);

        // Set new resolution
        CVS_ERROR status = ST_SetIntReg(m_hDevice, "Width", width);
        if (status != MCAM_ERR_OK)
        {
            ReportError(status, "Failed to set width");
            return false;
        }

        transaction.EnableRollback();

        status = ST_SetIntReg(m_hDevice, "Height", height);
        if (status != MCAM_ERR_OK)
        {
            ReportError(status, "Failed to set height");
            // Transaction destructor will handle rollback
            return false;
        }

        // Update cached resolution
        m_currentWidth = width;
        m_currentHeight = height;

        // Reinitialize buffers
        if (!ReinitializeBuffers())
        {
            // Restore original resolution
            m_currentWidth = transaction.GetOldWidth();
            m_currentHeight = transaction.GetOldHeight();

            ReportError(-1, "Failed to reinitialize buffers after resolution change");
            // Transaction destructor will handle rollback
            return false;
        }

        // Commit transaction - no rollback needed
        transaction.Commit();

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
        // Early validation for real-time performance
        if (!pBuffer || !pBuffer->image.pImage || m_bShuttingDown)
            return;

        // Use try_lock for real-time performance - skip frame if locked
        std::unique_lock<std::mutex> lock(m_imageMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return;  // Skip this frame to maintain real-time performance

        // Check acquisition state with memory ordering
        if (!m_bAcquiring.load(std::memory_order_acquire))
            return;

        // Get callback under lock to ensure thread safety
        ImageCallback callback;
        {
            std::lock_guard<std::mutex> cbLock(m_callbackMutex);
            callback = m_imageCallback;
        }

        if (!callback)
            return;

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

        // Buffer validation
        if (pBuffer->image.width == 0 || pBuffer->image.height == 0)
        {
            ReportError(-1, "Invalid image buffer dimensions");
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
            // Validate buffer sizes before conversion
            if (ValidateBufferSize(pBuffer, &m_rgbBuffer))
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
                ReportError(-1, "RGB buffer size mismatch - using raw data");
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

        // Release lock before callback for better performance
        lock.unlock();

        // Call the callback
        if (callback && !m_bShuttingDown)
        {
            try
            {
                callback(m_lastImageData);
            }
            catch (...)
            {
                // Prevent callback exceptions from crashing the system
                ReportError(-1, "Exception in image callback");
            }
        }
    }

    void CameraController::Impl::ReportError(int error, const std::string& context)
    {
        m_lastError = error;

        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_errorCallback && !m_bShuttingDown)
        {
            try
            {
                std::stringstream ss;
                ss << context << " (Error: " << error << ")";
                m_errorCallback(error, ss.str());
            }
            catch (...)
            {
                // Ignore callback exceptions
            }
        }
    }

    void CameraController::Impl::ReportStatus(const std::string& status)
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        if (m_statusCallback && !m_bShuttingDown)
        {
            try
            {
                m_statusCallback(status);
            }
            catch (...)
            {
                // Ignore callback exceptions
            }
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

        // Get current resolution
        int64_t width, height;
        ST_GetIntReg(m_pImpl->m_hDevice, "Width", &width);
        ST_GetIntReg(m_pImpl->m_hDevice, "Height", &height);
        m_pImpl->m_currentWidth = static_cast<int>(width);
        m_pImpl->m_currentHeight = static_cast<int>(height);

        // Initialize RGB buffer if color camera
        if (m_pImpl->IsColorCamera())
        {
            // RGB buffer initialization
            status = ST_InitBuffer(m_pImpl->m_hDevice, &m_pImpl->m_rgbBuffer, 3);
            if (status != MCAM_ERR_OK)
            {
                m_pImpl->ReportError(status, "Failed to initialize RGB buffer");
                // Not critical, continue
            }
        }

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

        // Reset buffer pool state
        if (m_pImpl->m_bufferPool)
        {
            m_pImpl->m_bufferPool->ResetBuffers();
        }

        // Initialize image data
        {
            std::lock_guard<std::mutex> lock(m_pImpl->m_imageMutex);
            memset(&m_pImpl->m_lastImageData, 0, sizeof(m_pImpl->m_lastImageData));
            m_pImpl->m_pCurrentBuffer = nullptr;
        }

        // Register callback if not already registered
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

        // Hardware preparation time
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        CVS_ERROR status = ST_AcqStart(m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to start acquisition");
            return false;
        }

        m_pImpl->m_bAcquiring.store(true, std::memory_order_release);
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

        // Set flag first
        m_pImpl->m_bAcquiring.store(false, std::memory_order_release);

        CVS_ERROR status = ST_AcqStop(m_pImpl->m_hDevice);
        if (status != MCAM_ERR_OK)
        {
            m_pImpl->ReportError(status, "Failed to stop acquisition");
            return false;
        }

        // Wait for camera to fully stop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Reset buffer pool
        if (m_pImpl->m_bufferPool)
        {
            m_pImpl->m_bufferPool->ResetBuffers();
        }

        // Clear image data
        {
            std::lock_guard<std::mutex> lock(m_pImpl->m_imageMutex);
            memset(&m_pImpl->m_lastImageData, 0, sizeof(m_pImpl->m_lastImageData));
            m_pImpl->m_pCurrentBuffer = nullptr;
        }

        // Unregister callback for next start
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
        return m_pImpl->m_bAcquiring.load(std::memory_order_acquire);
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
        std::lock_guard<std::mutex> lock(m_pImpl->m_callbackMutex);
        m_pImpl->m_imageCallback = callback;
    }

    void CameraController::RegisterErrorCallback(ErrorCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_pImpl->m_callbackMutex);
        m_pImpl->m_errorCallback = callback;
    }

    void CameraController::RegisterStatusCallback(StatusCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_pImpl->m_callbackMutex);
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