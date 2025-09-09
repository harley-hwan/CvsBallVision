// CvsBallVisionUIDlg.h : header file for the main dialog
#pragma once

#include "afxdialogex.h"
#include "../CvsBallVisionCore/CvsBallVisionCore.h"
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

// Forward declarations
class CStaticImage;

// CvsBallVisionUIDlg dialog
class CCvsBallVisionUIDlg : public CDialogEx
{
    // Construction
public:
    CCvsBallVisionUIDlg(CWnd* pParent = nullptr);
    virtual ~CCvsBallVisionUIDlg();

    // Dialog Data
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CVSBALLVISIONUI_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

    // Implementation
protected:
    HICON m_hIcon;

    // Generated message map functions
    virtual BOOL OnInitDialog();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnBnClickedButtonRefresh();
    afx_msg void OnBnClickedButtonConnect();
    afx_msg void OnBnClickedButtonDisconnect();
    afx_msg void OnBnClickedButtonStartAcq();
    afx_msg void OnBnClickedButtonStopAcq();
    afx_msg void OnBnClickedButtonSingleGrab();
    afx_msg void OnBnClickedButtonSaveImage();
    afx_msg void OnBnClickedButtonSaveConfig();
    afx_msg void OnBnClickedButtonLoadConfig();
    afx_msg void OnCbnSelchangeComboCameras();
    afx_msg void OnEnChangeEditExposure();
    afx_msg void OnEnChangeEditGain();
    afx_msg void OnEnChangeEditFramerate();
    afx_msg void OnBnClickedCheckAutoExposure();
    afx_msg void OnBnClickedCheckAutoGain();
    afx_msg void OnBnClickedCheckAutoWhitebalance();
    afx_msg void OnBnClickedCheckTriggerMode();
    afx_msg void OnBnClickedButtonSoftTrigger();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDestroy();
    afx_msg LRESULT OnUpdateImage(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnUpdateStatus(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    // Camera controller
    std::unique_ptr<CvsBallVision::CameraController> m_pCameraController;

    // UI Controls
    CComboBox m_comboCameras;
    CStatic m_staticImage;
    CEdit m_editExposure;
    CEdit m_editGain;
    CEdit m_editFrameRate;
    CButton m_checkAutoExposure;
    CButton m_checkAutoGain;
    CButton m_checkAutoWhiteBalance;
    CButton m_checkTriggerMode;
    CButton m_btnConnect;
    CButton m_btnDisconnect;
    CButton m_btnStartAcq;
    CButton m_btnStopAcq;
    CButton m_btnSingleGrab;
    CButton m_btnSoftTrigger;
    CButton m_btnSaveImage;
    CButton m_btnSaveConfig;
    CButton m_btnLoadConfig;
    CStatic m_staticStatus;
    CStatic m_staticFrameCount;
    CStatic m_staticErrorCount;
    CStatic m_staticFPS;
    CStatic m_staticBandwidth;
    CStatic m_staticResolution;
    CStatic m_staticPixelFormat;
    CProgressCtrl m_progressBuffer;

    // Image display
    CDC m_memDC;
    CBitmap m_bitmap;
    CBitmap* m_pOldBitmap;
    BITMAPINFO* m_pBitmapInfo;

    // Image buffer management
    struct ImageData {
        std::vector<BYTE> data;
        int width;
        int height;
        int channels;
        uint64_t frameNumber;
    };

    std::queue<std::shared_ptr<ImageData>> m_imageQueue;
    std::mutex m_imageMutex;
    std::atomic<bool> m_newImageAvailable{ false };

    // Current image for display
    std::shared_ptr<ImageData> m_currentImage;

    // Camera information
    std::vector<CvsBallVision::CameraInfo> m_cameras;
    CvsBallVision::CameraStatus m_currentStatus;

    // Timer ID for status update
    UINT_PTR m_nTimerID;

    // Thread safety
    std::mutex m_statusMutex;

    // Helper functions
    void InitializeControls();
    void UpdateCameraList();
    void UpdateCameraInfo();
    void UpdateControlsState();
    void UpdateStatusDisplay();
    void DisplayImage(const ImageData& imageData);
    void SaveImage(const CString& filePath);
    void LogMessage(const CString& message);
    void EnableCameraControls(BOOL enable);
    void EnableAcquisitionControls(BOOL enable);

    // Callback handlers
    void OnImageReceived(const CVS_BUFFER* buffer, const CvsBallVision::CameraStatus& status);
    void OnErrorOccurred(const std::string& error, CVS_ERROR errorCode);
    void OnDeviceEvent(int32_t eventId, const CVS_EVENT* eventData);

    // Image processing
    bool ConvertToDisplayFormat(const CVS_BUFFER* buffer, ImageData& imageData);
    void CreateBitmapInfo(int width, int height, int channels);

    // Configuration
    void LoadDefaultSettings();
    void SaveCurrentSettings();
};

// Custom static control for image display
class CStaticImage : public CStatic
{
public:
    CStaticImage();
    virtual ~CStaticImage();

    void SetImage(const BYTE* pData, int width, int height, int channels);
    void ClearImage();
    void SetFitToWindow(bool fit) { m_bFitToWindow = fit; }
    void SetMaintainAspectRatio(bool maintain) { m_bMaintainAspectRatio = maintain; }

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    CDC m_memDC;
    CBitmap m_bitmap;
    CBitmap* m_pOldBitmap;
    BITMAPINFO* m_pBitmapInfo;

    std::vector<BYTE> m_imageData;
    int m_imageWidth;
    int m_imageHeight;
    int m_imageChannels;

    bool m_bFitToWindow;
    bool m_bMaintainAspectRatio;

    void DrawImage(CDC* pDC);
    void CreateBitmapInfo();
    CRect CalculateDisplayRect(const CRect& clientRect);
};

// Message definitions
#define WM_UPDATE_IMAGE     (WM_USER + 100)
#define WM_UPDATE_STATUS    (WM_USER + 101)