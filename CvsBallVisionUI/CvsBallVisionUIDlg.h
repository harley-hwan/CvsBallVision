#pragma once
#include "afxdialogex.h"
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>

// Forward declarations
namespace CvsBallVision
{
    class CameraController;
    struct CameraInfo;
    struct ImageData;
}

class CvsBallVisionUIDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CvsBallVisionUIDlg)

public:
    CvsBallVisionUIDlg(CWnd* pParent = nullptr);
    virtual ~CvsBallVisionUIDlg();

    enum { IDD = IDD_CVSBALLVISIONUI_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    virtual void OnCancel();
    virtual void OnOK();

    DECLARE_MESSAGE_MAP()

    // Control event handlers
    afx_msg void OnBnClickedButtonConnect();
    afx_msg void OnBnClickedButtonDisconnect();
    afx_msg void OnBnClickedButtonStart();
    afx_msg void OnBnClickedButtonStop();
    afx_msg void OnBnClickedButtonRefresh();
    afx_msg void OnBnClickedButtonApplySettings();
    afx_msg void OnBnClickedButtonSaveSettings();
    afx_msg void OnBnClickedButtonLoadSettings();
    afx_msg void OnCbnSelchangeComboCameraList();
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnPaint();
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedCheckSoftwareGamma();
    afx_msg LRESULT OnImageReceived(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnConnectionComplete(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAsyncOperationComplete(WPARAM wParam, LPARAM lParam);

private:
    // Camera controller
    std::unique_ptr<CvsBallVision::CameraController> m_pCamera;

    // Camera list
    std::vector<CvsBallVision::CameraInfo> m_cameraList;

    // Async operation management
    std::atomic<bool> m_bAsyncOperationInProgress;
    std::future<bool> m_asyncFuture;
    std::thread m_asyncThread;

    // Image display
    CDC m_memDC;
    CBitmap m_memBitmap;
    std::mutex m_imageMutex;
    std::vector<uint8_t> m_displayBuffer;
    int m_imageWidth;
    int m_imageHeight;
    size_t m_displayBufferSize;
    std::atomic<bool> m_bImageUpdated;
    std::atomic<bool> m_bShuttingDown;

    // Statistics
    uint64_t m_frameCount;
    uint64_t m_errorCount;
    double m_currentFps;
    CString m_statusText;

    // UI Controls
    CComboBox m_comboCameraList;
    CButton m_btnConnect;
    CButton m_btnDisconnect;
    CButton m_btnStart;
    CButton m_btnStop;
    CButton m_btnRefresh;
    CButton m_btnApplySettings;
    CButton m_btnSaveSettings;
    CButton m_btnLoadSettings;

    CStatic m_staticVideo;
    CStatic m_staticStatus;
    CStatic m_staticFps;
    CStatic m_staticFrameCount;
    CStatic m_staticErrorCount;

    CEdit m_editWidth;
    CEdit m_editHeight;
    CEdit m_editExposure;
    CEdit m_editGain;
    CEdit m_editFps;
    CEdit m_editGamma;

    CSliderCtrl m_sliderExposure;
    CSliderCtrl m_sliderGain;
    CSliderCtrl m_sliderFps;
    CSliderCtrl m_sliderGamma;

    CStatic m_staticExposureValue;
    CStatic m_staticGainValue;
    CStatic m_staticFpsValue;
    CStatic m_staticGammaValue;

    CButton m_checkSoftwareGamma;

    // Timer ID
    static const UINT_PTR TIMER_UPDATE_UI = 1;

    // Private methods
    void InitializeCamera();
    void ShutdownCamera();
    void UpdateCameraList();
    void UpdateUIState();
    void UpdateStatistics();
    void UpdateParameterRanges();
    void UpdateParameterValues();
    void ApplySettings();
    void DrawImage();
    void CreateMemoryDC();

    // Async operations
    void ConnectCameraAsync(uint32_t enumIndex);
    void DisconnectCameraAsync();
    void ApplySettingsAsync();

    // Callbacks from camera
    void OnImageCallback(const CvsBallVision::ImageData& imageData);
    void OnErrorCallback(int errorCode, const std::string& errorMsg);
    void OnStatusCallback(const std::string& status);
};

// Custom messages
#define WM_IMAGE_RECEIVED (WM_USER + 100)
#define WM_CONNECTION_COMPLETE (WM_USER + 101)
#define WM_ASYNC_OPERATION_COMPLETE (WM_USER + 102)