
#include "CvsBallVisionCore.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <map>

namespace CvsBallVision {

    // Internal implementation class
    class CameraController::Impl {
    public:
        int32_t hDevice = -1;
        std::atomic<bool> isSystemInit{ false };
        std::atomic<bool> isConnected{ false };
        std::atomic<bool> isAcquiring{ false };
        std::thread grabThread;
        std::atomic<bool> stopGrabbing{ false };

        ImageCallback imageCallback;
        ErrorCallback errorCallback;
        std::map<int32_t, EventCallback> eventCallbacks;

        std::mutex callbackMutex;
        std::mutex deviceMutex;

        CVS_ERROR lastError = MCAM_ERR_OK;
        CameraInfo currentCamera;
        ImageFormat currentFormat;

        uint32_t bufferCount = 10;
        uint32_t grabTimeout = 5000;

        // Static callback handlers
        static void GrabCallbackHandler(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine);
        static void EventCallbackHandler(const CVS_EVENT* pEvent, void* pUserDefine);

        void GrabThreadFunc();
        bool UpdateImageFormat();
        std::string GetErrorDescription(CVS_ERROR error);
    };

    // Static callback implementation
    void CameraController::Impl::GrabCallbackHandler(int32_t eventID, const CVS_BUFFER* pBuffer, void* pUserDefine) {
        auto* impl = static_cast<Impl*>(pUserDefine);
        if (!impl || !impl->imageCallback) return;

        std::lock_guard<std::mutex> lock(impl->callbackMutex);

        CameraStatus status;
        status.isConnected = impl->isConnected;
        status.isAcquiring = impl->isAcquiring;

        uint64_t frameCount = 0, errorCount = 0;
        ST_GetGrabCount(impl->hDevice, &frameCount, &errorCount);
        status.frameCount = frameCount;
        status.errorCount = errorCount;

        double frameRate = 0.0;
        ST_GetFrameRate(impl->hDevice, &frameRate);
        status.frameRate = frameRate;

        double bandwidth = 0.0;
        ST_GetBandwidth(impl->hDevice, &bandwidth);
        status.bandwidth = bandwidth;

        if (eventID == EVENT_NEW_IMAGE && pBuffer) {
            impl->imageCallback(pBuffer, status);
        }
        else if (eventID == EVENT_GRAB_ERROR || eventID == EVENT_GRAB_TIMEOUT) {
            status.lastError = impl->GetErrorDescription(impl->lastError);
            if (impl->errorCallback) {
                impl->errorCallback(status.lastError, impl->lastError);
            }
        }
    }

    void CameraController::Impl::EventCallbackHandler(const CVS_EVENT* pEvent, void* pUserDefine) {
        auto* impl = static_cast<Impl*>(pUserDefine);
        if (!impl || !pEvent) return;

        std::lock_guard<std::mutex> lock(impl->callbackMutex);
        auto it = impl->eventCallbacks.find(pEvent->id);
        if (it != impl->eventCallbacks.end() && it->second) {
            it->second(pEvent->id, pEvent);
        }
    }

    void CameraController::Impl::GrabThreadFunc() {
        CVS_BUFFER buffer;
        ST_InitBuffer(hDevice, &buffer, 3);  // RGB channels

        while (!stopGrabbing) {
            if (isAcquiring) {
                lastError = ST_GrabImage(hDevice, &buffer);
                if (lastError == MCAM_ERR_OK) {
                    CameraStatus status;
                    status.isConnected = isConnected;
                    status.isAcquiring = isAcquiring;

                    uint64_t frameCount = 0, errorCount = 0;
                    ST_GetGrabCount(hDevice, &frameCount, &errorCount);
                    status.frameCount = frameCount;
                    status.errorCount = errorCount;

                    double frameRate = 0.0;
                    ST_GetFrameRate(hDevice, &frameRate);
                    status.frameRate = frameRate;

                    if (imageCallback) {
                        std::lock_guard<std::mutex> lock(callbackMutex);
                        imageCallback(&buffer, status);
                    }
                }
                else if (lastError != MCAM_ERR_TIMEOUT) {
                    if (errorCallback) {
                        std::lock_guard<std::mutex> lock(callbackMutex);
                        errorCallback(GetErrorDescription(lastError), lastError);
                    }
                }
            }
            else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ST_FreeBuffer(&buffer);
    }

    bool CameraController::Impl::UpdateImageFormat() {
        if (!isConnected) return false;

        int64_t width = 0, height = 0;
        lastError = ST_GetIntReg(hDevice, "Width", &width);
        if (lastError != MCAM_ERR_OK) return false;

        lastError = ST_GetIntReg(hDevice, "Height", &height);
        if (lastError != MCAM_ERR_OK) return false;

        char pixelFormat[256] = { 0 };
        uint32_t size = sizeof(pixelFormat);
        lastError = ST_GetEnumReg(hDevice, "PixelFormat", pixelFormat, &size);
        if (lastError != MCAM_ERR_OK) return false;

        currentFormat.width = static_cast<int32_t>(width);
        currentFormat.height = static_cast<int32_t>(height);
        currentFormat.pixelFormat = pixelFormat;

        // Determine channels and bytes per pixel based on pixel format
        if (strstr(pixelFormat, "Mono8")) {
            currentFormat.channels = 1;
            currentFormat.bytesPerPixel = 1;
        }
        else if (strstr(pixelFormat, "Mono10") || strstr(pixelFormat, "Mono12")) {
            currentFormat.channels = 1;
            currentFormat.bytesPerPixel = 2;
        }
        else if (strstr(pixelFormat, "Bayer")) {
            currentFormat.channels = 1;
            currentFormat.bytesPerPixel = (strstr(pixelFormat, "8") ? 1 : 2);
        }
        else if (strstr(pixelFormat, "RGB") || strstr(pixelFormat, "BGR")) {
            currentFormat.channels = 3;
            currentFormat.bytesPerPixel = 3;
        }
        else if (strstr(pixelFormat, "YUV")) {
            currentFormat.channels = 2;
            currentFormat.bytesPerPixel = 2;
        }

        return true;
    }

    std::string CameraController::Impl::GetErrorDescription(CVS_ERROR error) {
        switch (error) {
        case MCAM_ERR_OK: return "Success";
        case MCAM_ERR_GENERIC_ERROR: return "Generic error";
        case MCAM_ERR_NOT_INITIALIZED: return "System not initialized";
        case MCAM_ERR_NOT_CONNECTED: return "Device not connected";
        case MCAM_ERR_TIMEOUT: return "Operation timeout";
        case MCAM_ERR_BUSY: return "Device busy";
        case MCAM_ERR_STATE_ERROR: return "Invalid state";
        case MCAM_ERR_INVALID_PARAMETER: return "Invalid parameter";
        case MCAM_ERR_NOT_SUPPORTED: return "Feature not supported";
        case MCAM_ERR_NO_DEVICE: return "No device found";
        default: return "Unknown error";
        }
    }

    // CameraController implementation
    CameraController::CameraController() : pImpl(std::make_unique<Impl>()) {}

    CameraController::~CameraController() {
        if (pImpl->isAcquiring) {
            StopAcquisition();
        }
        if (pImpl->isConnected) {
            DisconnectCamera();
        }
        if (pImpl->isSystemInit) {
            ReleaseSystem();
        }
    }

    bool CameraController::InitializeSystem() {
        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        if (pImpl->isSystemInit) return true;

        pImpl->lastError = ST_InitSystem();
        if (pImpl->lastError == MCAM_ERR_OK) {
            pImpl->isSystemInit = true;
            return true;
        }
        return false;
    }

    void CameraController::ReleaseSystem() {
        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        if (pImpl->isConnected) {
            DisconnectCamera();
        }

        if (pImpl->isSystemInit) {
            ST_FreeSystem();
            pImpl->isSystemInit = false;
        }
    }

    bool CameraController::IsSystemInitialized() const {
        return pImpl->isSystemInit;
    }

    bool CameraController::UpdateDeviceList(uint32_t timeout) {
        if (!pImpl->isSystemInit) {
            if (!InitializeSystem()) return false;
        }

        pImpl->lastError = ST_UpdateDevice(timeout);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    std::vector<CameraInfo> CameraController::GetAvailableCameras() {
        std::vector<CameraInfo> cameras;

        if (!pImpl->isSystemInit) return cameras;

        uint32_t cameraCount = 0;
        if (ST_GetAvailableCameraNum(&cameraCount) != MCAM_ERR_OK) {
            return cameras;
        }

        for (uint32_t i = 0; i < cameraCount; ++i) {
            CameraInfo info;
            info.enumIndex = i;

            char buffer[256];
            uint32_t size = sizeof(buffer);

            // Get device ID
            if (ST_GetEnumDeviceID(i, buffer, &size) == MCAM_ERR_OK) {
                info.deviceId = buffer;
            }

            // Get model name
            size = sizeof(buffer);
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_MODEL_NAME, buffer, &size) == MCAM_ERR_OK) {
                info.modelName = buffer;
            }

            // Get serial number
            size = sizeof(buffer);
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_SERIAL_NUMBER, buffer, &size) == MCAM_ERR_OK) {
                info.serialNumber = buffer;
            }

            // Get IP address
            size = sizeof(buffer);
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_IP_ADDRESS, buffer, &size) == MCAM_ERR_OK) {
                info.ipAddress = buffer;
            }

            // Get MAC address
            size = sizeof(buffer);
            if (ST_GetEnumDeviceInfo(i, MCAM_DEVICEINFO_MAC_ADDRESS, buffer, &size) == MCAM_ERR_OK) {
                info.macAddress = buffer;
            }

            // Check if connectable
            bool connectable = false;
            if (ST_IsConnectable(i, &connectable) == MCAM_ERR_OK) {
                info.isConnectable = connectable;
            }

            cameras.push_back(info);
        }

        return cameras;
    }

    bool CameraController::ConnectCamera(uint32_t index) {
        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        if (pImpl->isConnected) {
            DisconnectCamera();
        }

        pImpl->lastError = ST_OpenDevice(index, &pImpl->hDevice, false);
        if (pImpl->lastError != MCAM_ERR_OK) {
            return false;
        }

        pImpl->isConnected = true;

        // Get camera info
        GetCameraInfo(index, pImpl->currentCamera);

        // Update image format
        pImpl->UpdateImageFormat();

        // Set default buffer count
        ST_SetBufferCount(pImpl->hDevice, pImpl->bufferCount);

        // Set default timeout
        ST_SetGrabTimeout(pImpl->hDevice, pImpl->grabTimeout);

        // Start grab thread
        pImpl->stopGrabbing = false;
        pImpl->grabThread = std::thread(&Impl::GrabThreadFunc, pImpl.get());

        return true;
    }

    bool CameraController::DisconnectCamera() {
        if (!pImpl->isConnected) return true;

        // Stop acquisition if running
        if (pImpl->isAcquiring) {
            StopAcquisition();
        }

        // Stop grab thread
        pImpl->stopGrabbing = true;
        if (pImpl->grabThread.joinable()) {
            pImpl->grabThread.join();
        }

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        pImpl->lastError = ST_CloseDevice(pImpl->hDevice);
        pImpl->hDevice = -1;
        pImpl->isConnected = false;

        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::StartAcquisition() {
        if (!pImpl->isConnected) return false;
        if (pImpl->isAcquiring) return true;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        pImpl->lastError = ST_AcqStart(pImpl->hDevice);
        if (pImpl->lastError == MCAM_ERR_OK) {
            pImpl->isAcquiring = true;
            return true;
        }
        return false;
    }

    bool CameraController::StopAcquisition() {
        if (!pImpl->isConnected) return false;
        if (!pImpl->isAcquiring) return true;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);

        pImpl->lastError = ST_AcqStop(pImpl->hDevice);
        if (pImpl->lastError == MCAM_ERR_OK) {
            pImpl->isAcquiring = false;
            return true;
        }
        return false;
    }

    bool CameraController::SetExposureTime(double microseconds) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_SetFloatReg(pImpl->hDevice, "ExposureTime", microseconds);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::GetExposureTime(double& microseconds) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_GetFloatReg(pImpl->hDevice, "ExposureTime", &microseconds);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::SetGain(double gain) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_SetFloatReg(pImpl->hDevice, "Gain", gain);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::GetGain(double& gain) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_GetFloatReg(pImpl->hDevice, "Gain", &gain);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::SetFrameRate(double fps) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_SetFloatReg(pImpl->hDevice, "AcquisitionFrameRate", fps);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    bool CameraController::GetFrameRate(double& fps) {
        if (!pImpl->isConnected) return false;

        std::lock_guard<std::mutex> lock(pImpl->deviceMutex);
        pImpl->lastError = ST_GetFloatReg(pImpl->hDevice, "ResultingFrameRate", &fps);
        return pImpl->lastError == MCAM_ERR_OK;
    }

    void CameraController::RegisterImageCallback(ImageCallback callback) {
        std::lock_guard<std::mutex> lock(pImpl->callbackMutex);
        pImpl->imageCallback = callback;
    }

    CameraStatus CameraController::GetStatus() {
        CameraStatus status;
        status.isConnected = pImpl->isConnected;
        status.isAcquiring = pImpl->isAcquiring;

        if (pImpl->isConnected) {
            uint64_t frameCount = 0, errorCount = 0;
            ST_GetGrabCount(pImpl->hDevice, &frameCount, &errorCount);
            status.frameCount = frameCount;
            status.errorCount = errorCount;

            double frameRate = 0.0;
            ST_GetFrameRate(pImpl->hDevice, &frameRate);
            status.frameRate = frameRate;

            double bandwidth = 0.0;
            ST_GetBandwidth(pImpl->hDevice, &bandwidth);
            status.bandwidth = bandwidth;
        }

        status.lastError = pImpl->GetErrorDescription(pImpl->lastError);
        return status;
    }

    bool CameraController::GetCameraInfo(uint32_t index, CameraInfo& info) {
        char buffer[256];
        uint32_t size = sizeof(buffer);

        info.enumIndex = index;

        if (ST_GetEnumDeviceID(index, buffer, &size) == MCAM_ERR_OK) {
            info.deviceId = buffer;
        }

        size = sizeof(buffer);
        if (ST_GetEnumDeviceInfo(index, MCAM_DEVICEINFO_MODEL_NAME, buffer, &size) == MCAM_ERR_OK) {
            info.modelName = buffer;
        }

        size = sizeof(buffer);
        if (ST_GetEnumDeviceInfo(index, MCAM_DEVICEINFO_SERIAL_NUMBER, buffer, &size) == MCAM_ERR_OK) {
            info.serialNumber = buffer;
        }

        size = sizeof(buffer);
        if (ST_GetEnumDeviceInfo(index, MCAM_DEVICEINFO_IP_ADDRESS, buffer, &size) == MCAM_ERR_OK) {
            info.ipAddress = buffer;
        }

        bool connectable = false;
        if (ST_IsConnectable(index, &connectable) == MCAM_ERR_OK) {
            info.isConnectable = connectable;
        }

        return true;
    }

    // Stub implementations for remaining methods
    bool CameraController::IsConnected() const { return pImpl->isConnected; }
    bool CameraController::IsAcquiring() const { return pImpl->isAcquiring; }
    std::string CameraController::GetLastErrorString() const { return pImpl->GetErrorDescription(pImpl->lastError); }
    CVS_ERROR CameraController::GetLastErrorCode() const { return pImpl->lastError; }

    // ImageProcessor implementation
    bool ImageProcessor::ConvertBayerToRGB(const CVS_BUFFER& src, CVS_BUFFER& dst, ConvertColor code) {
        return ST_CvtColor(const_cast<CVS_BUFFER>(src), &dst, code) == MCAM_ERR_OK;
    }

    bool ImageProcessor::AllocateBuffer(CVS_BUFFER& buffer, int32_t width, int32_t height, int32_t channels) {
        buffer.image.width = width;
        buffer.image.height = height;
        buffer.image.channels = channels;
        buffer.image.step = width * channels;
        buffer.size = width * height * channels;
        buffer.image.pImage = malloc(buffer.size);
        return buffer.image.pImage != nullptr;
    }

    void ImageProcessor::FreeBuffer(CVS_BUFFER& buffer) {
        if (buffer.image.pImage) {
            free(buffer.image.pImage);
            buffer.image.pImage = nullptr;
        }
    }

    // Version information
    std::string GetLibraryVersion() { return "1.0.0"; }
    std::string GetAPIVersion() { return "1.0.8"; }
}