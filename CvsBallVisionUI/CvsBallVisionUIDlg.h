#pragma once

#include "afxwin.h"
#include "afxcmn.h"
#include "../CvsBallVisionCore/CvsBallVisionCore.h"
#include <memory>
#include <atomic>
#include <mutex>

// Forward declarations
class CImageDisplayWnd;

// CCvsBallVisionUIDlg dialog
class CCvsBallVisionUIDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CCvsBallVisionUIDlg)

public:
    CCvsBallVisionUIDlg(CWnd* pParent = nullptr);
    virtual ~CCvsBallVisionUIDlg();

    // Dialog Data
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_CVSBALLVISIONUI_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    DECLARE_MESSAGE_MAP()

    // Message handlers
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnBnClickedBtnConnect();
    afx_msg void OnBnClickedBtnDisconnect();
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnBnClickedBtnStop();
    afx_msg void OnBnClickedBtnApplySettings();
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnEnChangeEditGain();
    afx_msg void OnEnChangeEditExposure();
    afx_msg void OnEnChangeEditFps();
    afx_msg void OnDestroy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg LRESULT OnImageUpdate(WPARAM wParam, LPARAM lParam);

private:
    // Camera core instance
    std::unique_ptr<CvsBallVisionCore> m_pCamera;

    // Frame callback handler
    void OnFrameReceived(const ImageData& imageData);

    // Update UI controls
    void UpdateControls();
    void UpdateParameterDisplay();
    void EnableCameraControls(BOOL bEnable);

    // Image display
    void DisplayImage(const ImageData& imageData);
    void ClearDisplay();

    // Conversion helper
    void ConvertBayerToRGB(const unsigned char* pSrc, unsigned char* pDst, int width, int height);

    // Settings validation
    bool ValidateSettings();
    void ApplyCameraSettings();

    // Status update
    void SetStatusText(const CString& text);

private:
    // Controls
    CButton m_btnConnect;
    CButton m_btnDisconnect;
    CButton m_btnStart;
    CButton m_btnStop;
    CButton m_btnApplySettings;

    CStatic m_staticVideo;
    CStatic m_staticStatus;
    CStatic m_staticFpsDisplay;
    CStatic m_staticResolution;

    CEdit m_editGain;
    CEdit m_editExposure;
    CEdit m_editFps;

    CSliderCtrl m_sliderGain;
    CSliderCtrl m_sliderExposure;
    CSliderCtrl m_sliderFps;

    // Image display window
    std::unique_ptr<CImageDisplayWnd> m_pImageWnd;

    // State variables
    std::atomic<bool> m_bConnected;
    std::atomic<bool> m_bAcquiring;

    // Frame statistics
    std::atomic<int> m_frameCount;
    std::atomic<DWORD> m_lastFpsTime;
    double m_currentFps;

    // Image buffer for display
    std::vector<BYTE> m_displayBuffer;
    std::mutex m_bufferMutex;
    BITMAPINFO m_bitmapInfo;

    // Camera parameters
    double m_gainValue;
    double m_exposureValue;
    double m_fpsValue;

    // Window icon
    HICON m_hIcon;

    // Timer ID
    static const UINT_PTR TIMER_UPDATE_FPS = 1001;
};

// Custom image display window class
class CImageDisplayWnd : public CWnd
{
public:
    CImageDisplayWnd();
    virtual ~CImageDisplayWnd();

    BOOL Create(CWnd* pParent, const CRect& rect);
    void SetImage(const BYTE* pData, int width, int height, int bpp = 8);
    void Clear();

protected:
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    DECLARE_MESSAGE_MAP()

private:
    std::vector<BYTE> m_imageBuffer;
    int m_imageWidth;
    int m_imageHeight;
    int m_imageBpp;
    std::mutex m_imageMutex;
    CBitmap m_bitmap;
    bool m_bHasImage;
};

// Custom message for image updates
#define WM_IMAGE_UPDATE (WM_USER + 100)