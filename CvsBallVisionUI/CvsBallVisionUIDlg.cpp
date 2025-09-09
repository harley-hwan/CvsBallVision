#include "pch.h"
#include "framework.h"
#include "CvsBallVisionUI.h"
#include "CvsBallVisionUIDlg.h"
#include "afxdialogex.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CAboutDlg dialog used for App About
class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg();
    enum { IDD = IDD_ABOUTBOX };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// CCvsBallVisionUIDlg dialog
IMPLEMENT_DYNAMIC(CCvsBallVisionUIDlg, CDialogEx)

CCvsBallVisionUIDlg::CCvsBallVisionUIDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CVSBALLVISIONUI_DIALOG, pParent)
    , m_bConnected(false)
    , m_bAcquiring(false)
    , m_frameCount(0)
    , m_lastFpsTime(0)
    , m_currentFps(0.0)
    , m_gainValue(0.0)
    , m_exposureValue(1000.0)  // 1ms default
    , m_fpsValue(100.0)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CCvsBallVisionUIDlg::~CCvsBallVisionUIDlg()
{
    if (m_pCamera) {
        m_pCamera->UnregisterFrameCallback();
        if (m_pCamera->IsAcquiring()) {
            m_pCamera->StopAcquisition();
        }
        if (m_pCamera->IsConnected()) {
            m_pCamera->DisconnectCamera();
        }
    }
}

void CCvsBallVisionUIDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_BTN_CONNECT, m_btnConnect);
    DDX_Control(pDX, IDC_BTN_DISCONNECT, m_btnDisconnect);
    DDX_Control(pDX, IDC_BTN_START, m_btnStart);
    DDX_Control(pDX, IDC_BTN_STOP, m_btnStop);
    DDX_Control(pDX, IDC_BTN_APPLY_SETTINGS, m_btnApplySettings);
    DDX_Control(pDX, IDC_STATIC_VIDEO, m_staticVideo);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
    DDX_Control(pDX, IDC_STATIC_FPS_DISPLAY, m_staticFpsDisplay);
    DDX_Control(pDX, IDC_STATIC_RESOLUTION, m_staticResolution);
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure);
    DDX_Control(pDX, IDC_EDIT_FPS, m_editFps);
    DDX_Control(pDX, IDC_SLIDER_GAIN, m_sliderGain);
    DDX_Control(pDX, IDC_SLIDER_EXPOSURE, m_sliderExposure);
    DDX_Control(pDX, IDC_SLIDER_FPS, m_sliderFps);
}

BEGIN_MESSAGE_MAP(CCvsBallVisionUIDlg, CDialogEx)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_DESTROY()
    ON_WM_TIMER()
    ON_WM_HSCROLL()
    ON_BN_CLICKED(IDC_BTN_CONNECT, &CCvsBallVisionUIDlg::OnBnClickedBtnConnect)
    ON_BN_CLICKED(IDC_BTN_DISCONNECT, &CCvsBallVisionUIDlg::OnBnClickedBtnDisconnect)
    ON_BN_CLICKED(IDC_BTN_START, &CCvsBallVisionUIDlg::OnBnClickedBtnStart)
    ON_BN_CLICKED(IDC_BTN_STOP, &CCvsBallVisionUIDlg::OnBnClickedBtnStop)
    ON_BN_CLICKED(IDC_BTN_APPLY_SETTINGS, &CCvsBallVisionUIDlg::OnBnClickedBtnApplySettings)
    ON_EN_CHANGE(IDC_EDIT_GAIN, &CCvsBallVisionUIDlg::OnEnChangeEditGain)
    ON_EN_CHANGE(IDC_EDIT_EXPOSURE, &CCvsBallVisionUIDlg::OnEnChangeEditExposure)
    ON_EN_CHANGE(IDC_EDIT_FPS, &CCvsBallVisionUIDlg::OnEnChangeEditFps)
    ON_MESSAGE(WM_IMAGE_UPDATE, &CCvsBallVisionUIDlg::OnImageUpdate)
END_MESSAGE_MAP()

BOOL CCvsBallVisionUIDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // Add "About..." menu item to system menu
    ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
    ASSERT(IDM_ABOUTBOX < 0xF000);

    CMenu* pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != nullptr) {
        BOOL bNameValid;
        CString strAboutMenu;
        bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
        ASSERT(bNameValid);
        if (!strAboutMenu.IsEmpty()) {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // Create camera instance
    m_pCamera = std::make_unique<CvsBallVisionCore>();

    // Create image display window
    CRect videoRect;
    m_staticVideo.GetWindowRect(&videoRect);
    ScreenToClient(&videoRect);

    m_pImageWnd = std::make_unique<CImageDisplayWnd>();
    m_pImageWnd->Create(this, videoRect);
    m_pImageWnd->ShowWindow(SW_SHOW);

    // Initialize sliders
    m_sliderGain.SetRange(0, 560);  // 0-56 dB in 0.1 dB steps
    m_sliderGain.SetPos(0);
    m_sliderGain.SetTicFreq(100);

    m_sliderExposure.SetRange(1, 30000);  // 1us to 30ms (logarithmic scale needed)
    m_sliderExposure.SetPos(1000);  // 1ms
    m_sliderExposure.SetTicFreq(1000);

    m_sliderFps.SetRange(10, 1000);  // 1-100 fps in 0.1 fps steps
    m_sliderFps.SetPos(1000);  // 100 fps
    m_sliderFps.SetTicFreq(100);

    // Initialize edit controls with default values
    CString str;
    str.Format(_T("%.1f"), m_gainValue);
    m_editGain.SetWindowText(str);

    str.Format(_T("%.1f"), m_exposureValue);
    m_editExposure.SetWindowText(str);

    str.Format(_T("%.1f"), m_fpsValue);
    m_editFps.SetWindowText(str);

    // Initialize bitmap info structure
    memset(&m_bitmapInfo, 0, sizeof(BITMAPINFO));
    m_bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    m_bitmapInfo.bmiHeader.biPlanes = 1;
    m_bitmapInfo.bmiHeader.biBitCount = 8;
    m_bitmapInfo.bmiHeader.biCompression = BI_RGB;
    m_bitmapInfo.bmiHeader.biSizeImage = 0;

    // Initialize controls state
    UpdateControls();
    SetStatusText(_T("Ready"));

    // Register frame callback
    auto callback = [this](const ImageData& imageData) {
        OnFrameReceived(imageData);
        };
    m_pCamera->RegisterFrameCallback(callback);

    // Start FPS update timer
    SetTimer(TIMER_UPDATE_FPS, 1000, nullptr);

    return TRUE;
}

void CCvsBallVisionUIDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX) {
        CAboutDlg dlgAbout;
        dlgAbout.DoModal();
    }
    else {
        CDialogEx::OnSysCommand(nID, lParam);
    }
}

void CCvsBallVisionUIDlg::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this);

        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;

        dc.DrawIcon(x, y, m_hIcon);
    }
    else {
        CDialogEx::OnPaint();
    }
}

HCURSOR CCvsBallVisionUIDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CCvsBallVisionUIDlg::OnDestroy()
{
    KillTimer(TIMER_UPDATE_FPS);

    if (m_pCamera) {
        m_pCamera->UnregisterFrameCallback();
        if (m_pCamera->IsAcquiring()) {
            m_pCamera->StopAcquisition();
        }
        if (m_pCamera->IsConnected()) {
            m_pCamera->DisconnectCamera();
        }
    }

    CDialogEx::OnDestroy();
}

void CCvsBallVisionUIDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_UPDATE_FPS) {
        DWORD currentTime = GetTickCount();
        DWORD deltaTime = currentTime - m_lastFpsTime;

        if (deltaTime > 0) {
            int frames = m_frameCount.exchange(0);
            m_currentFps = (frames * 1000.0) / deltaTime;
            m_lastFpsTime = currentTime;

            CString strFps;
            strFps.Format(_T("FPS: %.1f"), m_currentFps);
            m_staticFpsDisplay.SetWindowText(strFps);
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

void CCvsBallVisionUIDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (pScrollBar == (CScrollBar*)&m_sliderGain) {
        int pos = m_sliderGain.GetPos();
        m_gainValue = pos / 10.0;
        CString str;
        str.Format(_T("%.1f"), m_gainValue);
        m_editGain.SetWindowText(str);
    }
    else if (pScrollBar == (CScrollBar*)&m_sliderExposure) {
        int pos = m_sliderExposure.GetPos();
        m_exposureValue = static_cast<double>(pos);
        CString str;
        str.Format(_T("%.1f"), m_exposureValue);
        m_editExposure.SetWindowText(str);
    }
    else if (pScrollBar == (CScrollBar*)&m_sliderFps) {
        int pos = m_sliderFps.GetPos();
        m_fpsValue = pos / 10.0;
        CString str;
        str.Format(_T("%.1f"), m_fpsValue);
        m_editFps.SetWindowText(str);
    }

    CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CCvsBallVisionUIDlg::OnBnClickedBtnConnect()
{
    if (!m_pCamera) {
        MessageBox(_T("Camera object not initialized"), _T("Error"), MB_ICONERROR);
        return;
    }

    SetStatusText(_T("Connecting to camera..."));

    // Enumerate available cameras
    std::vector<std::string> cameras;
    if (!m_pCamera->EnumerateCameras(cameras)) {
        SetStatusText(_T("No cameras found"));
        MessageBox(_T("No cameras found. Please check connection."), _T("Error"), MB_ICONERROR);
        return;
    }

    // Connect to first available camera
    if (m_pCamera->ConnectCamera()) {
        m_bConnected = true;

        // Get camera info
        CString info;
        info.Format(_T("Connected to %s"), CString(m_pCamera->GetCameraModel().c_str()));
        SetStatusText(info);

        // Set default resolution
        m_pCamera->SetImageSize(1280, 880);
        m_staticResolution.SetWindowText(_T("Resolution: 1280 x 880"));

        // Set default parameters
        m_pCamera->SetFrameRate(100.0);
        m_pCamera->SetExposureTime(1000.0);
        m_pCamera->SetGain(0.0);

        UpdateControls();
    }
    else {
        SetStatusText(_T("Failed to connect"));
        CString error(m_pCamera->GetLastError().c_str());
        MessageBox(_T("Failed to connect: ") + error, _T("Error"), MB_ICONERROR);
    }
}

void CCvsBallVisionUIDlg::OnBnClickedBtnDisconnect()
{
    if (!m_pCamera) return;

    if (m_pCamera->IsAcquiring()) {
        m_pCamera->StopAcquisition();
        m_bAcquiring = false;
    }

    if (m_pCamera->DisconnectCamera()) {
        m_bConnected = false;
        SetStatusText(_T("Disconnected"));
        m_staticResolution.SetWindowText(_T("Resolution: -"));
        m_staticFpsDisplay.SetWindowText(_T("FPS: -"));

        // Clear display
        if (m_pImageWnd) {
            m_pImageWnd->Clear();
        }

        UpdateControls();
    }
}

void CCvsBallVisionUIDlg::OnBnClickedBtnStart()
{
    if (!m_pCamera || !m_pCamera->IsConnected()) {
        MessageBox(_T("Camera not connected"), _T("Error"), MB_ICONERROR);
        return;
    }

    SetStatusText(_T("Starting acquisition..."));

    // Reset frame counter
    m_frameCount = 0;
    m_lastFpsTime = GetTickCount();

    if (m_pCamera->StartAcquisition()) {
        m_bAcquiring = true;
        SetStatusText(_T("Acquiring"));
        UpdateControls();
    }
    else {
        SetStatusText(_T("Failed to start acquisition"));
        CString error(m_pCamera->GetLastError().c_str());
        MessageBox(_T("Failed to start: ") + error, _T("Error"), MB_ICONERROR);
    }
}

void CCvsBallVisionUIDlg::OnBnClickedBtnStop()
{
    if (!m_pCamera) return;

    if (m_pCamera->StopAcquisition()) {
        m_bAcquiring = false;
        SetStatusText(_T("Stopped"));
        UpdateControls();
    }
}

void CCvsBallVisionUIDlg::OnBnClickedBtnApplySettings()
{
    if (!ValidateSettings()) {
        return;
    }

    ApplyCameraSettings();
}

void CCvsBallVisionUIDlg::OnEnChangeEditGain()
{
    CString str;
    m_editGain.GetWindowText(str);
    double value = _tstof(str);

    if (value >= 0.0 && value <= 56.0) {
        m_gainValue = value;
        m_sliderGain.SetPos(static_cast<int>(value * 10));
    }
}

void CCvsBallVisionUIDlg::OnEnChangeEditExposure()
{
    CString str;
    m_editExposure.GetWindowText(str);
    double value = _tstof(str);

    if (value >= 1.0 && value <= 30000.0) {
        m_exposureValue = value;
        m_sliderExposure.SetPos(static_cast<int>(value));
    }
}

void CCvsBallVisionUIDlg::OnEnChangeEditFps()
{
    CString str;
    m_editFps.GetWindowText(str);
    double value = _tstof(str);

    if (value >= 1.0 && value <= 100.0) {
        m_fpsValue = value;
        m_sliderFps.SetPos(static_cast<int>(value * 10));
    }
}

LRESULT CCvsBallVisionUIDlg::OnImageUpdate(WPARAM wParam, LPARAM lParam)
{
    // Image update is handled directly in the callback
    return 0;
}

void CCvsBallVisionUIDlg::OnFrameReceived(const ImageData& imageData)
{
    // Increment frame counter
    m_frameCount++;

    // Update display
    if (m_pImageWnd && imageData.pData) {
        // Convert to display format if needed
        if (imageData.pixelFormat == 0) {  // Mono8
            m_pImageWnd->SetImage(imageData.pData, imageData.width, imageData.height, 8);
        }
        else {
            // Handle other formats as needed
            m_pImageWnd->SetImage(imageData.pData, imageData.width, imageData.height, 8);
        }
    }
}

void CCvsBallVisionUIDlg::UpdateControls()
{
    BOOL bConnected = m_bConnected;
    BOOL bAcquiring = m_bAcquiring;

    m_btnConnect.EnableWindow(!bConnected);
    m_btnDisconnect.EnableWindow(bConnected && !bAcquiring);
    m_btnStart.EnableWindow(bConnected && !bAcquiring);
    m_btnStop.EnableWindow(bConnected && bAcquiring);

    EnableCameraControls(bConnected && !bAcquiring);
}

void CCvsBallVisionUIDlg::EnableCameraControls(BOOL bEnable)
{
    m_editGain.EnableWindow(bEnable);
    m_editExposure.EnableWindow(bEnable);
    m_editFps.EnableWindow(bEnable);
    m_sliderGain.EnableWindow(bEnable);
    m_sliderExposure.EnableWindow(bEnable);
    m_sliderFps.EnableWindow(bEnable);
    m_btnApplySettings.EnableWindow(bEnable);
}

bool CCvsBallVisionUIDlg::ValidateSettings()
{
    // Validate gain
    if (m_gainValue < 0.0 || m_gainValue > 56.0) {
        MessageBox(_T("Gain must be between 0 and 56 dB"), _T("Invalid Setting"), MB_ICONWARNING);
        return false;
    }

    // Validate exposure time
    if (m_exposureValue < 1.0 || m_exposureValue > 3000000.0) {
        MessageBox(_T("Exposure time must be between 1 and 3000000 ¥ìs"), _T("Invalid Setting"), MB_ICONWARNING);
        return false;
    }

    // Validate frame rate
    if (m_fpsValue < 1.0 || m_fpsValue > 100.0) {
        MessageBox(_T("Frame rate must be between 1 and 100 fps"), _T("Invalid Setting"), MB_ICONWARNING);
        return false;
    }

    return true;
}

void CCvsBallVisionUIDlg::ApplyCameraSettings()
{
    if (!m_pCamera || !m_pCamera->IsConnected()) {
        return;
    }

    bool success = true;
    CString errorMsg;

    // Apply gain
    if (!m_pCamera->SetGain(m_gainValue)) {
        success = false;
        errorMsg += _T("Failed to set gain\n");
    }

    // Apply exposure time
    if (!m_pCamera->SetExposureTime(m_exposureValue)) {
        success = false;
        errorMsg += _T("Failed to set exposure time\n");
    }

    // Apply frame rate
    if (!m_pCamera->SetFrameRate(m_fpsValue)) {
        success = false;
        errorMsg += _T("Failed to set frame rate\n");
    }

    if (success) {
        SetStatusText(_T("Settings applied successfully"));
    }
    else {
        SetStatusText(_T("Failed to apply some settings"));
        MessageBox(errorMsg, _T("Settings Error"), MB_ICONWARNING);
    }
}

void CCvsBallVisionUIDlg::SetStatusText(const CString& text)
{
    m_staticStatus.SetWindowText(text);
}

// CImageDisplayWnd implementation
CImageDisplayWnd::CImageDisplayWnd()
    : m_imageWidth(0)
    , m_imageHeight(0)
    , m_imageBpp(8)
    , m_bHasImage(false)
{
}

CImageDisplayWnd::~CImageDisplayWnd()
{
}

BEGIN_MESSAGE_MAP(CImageDisplayWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CImageDisplayWnd::Create(CWnd* pParent, const CRect& rect)
{
    return CreateEx(0, AfxRegisterWndClass(0), _T(""),
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        rect, pParent, 0);
}

void CImageDisplayWnd::SetImage(const BYTE* pData, int width, int height, int bpp)
{
    if (!pData || width <= 0 || height <= 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_imageMutex);

    m_imageWidth = width;
    m_imageHeight = height;
    m_imageBpp = bpp;

    size_t imageSize = width * height * (bpp / 8);
    m_imageBuffer.resize(imageSize);
    memcpy(m_imageBuffer.data(), pData, imageSize);

    m_bHasImage = true;

    // Trigger repaint
    InvalidateRect(nullptr, FALSE);
}

void CImageDisplayWnd::Clear()
{
    std::lock_guard<std::mutex> lock(m_imageMutex);
    m_bHasImage = false;
    m_imageBuffer.clear();
    InvalidateRect(nullptr, TRUE);
}

void CImageDisplayWnd::OnPaint()
{
    CPaintDC dc(this);
    CRect rect;
    GetClientRect(&rect);

    std::lock_guard<std::mutex> lock(m_imageMutex);

    if (m_bHasImage && !m_imageBuffer.empty()) {
        // Create bitmap info for 8-bit grayscale
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(BITMAPINFO));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_imageWidth;
        bmi.bmiHeader.biHeight = -m_imageHeight; // Top-down bitmap
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 8;
        bmi.bmiHeader.biCompression = BI_RGB;

        // Set grayscale palette
        for (int i = 0; i < 256; i++) {
            bmi.bmiColors[i].rgbRed = i;
            bmi.bmiColors[i].rgbGreen = i;
            bmi.bmiColors[i].rgbBlue = i;
            bmi.bmiColors[i].rgbReserved = 0;
        }

        // Calculate aspect ratio preserving dimensions
        double scaleX = (double)rect.Width() / m_imageWidth;
        double scaleY = (double)rect.Height() / m_imageHeight;
        double scale = min(scaleX, scaleY);

        int drawWidth = (int)(m_imageWidth * scale);
        int drawHeight = (int)(m_imageHeight * scale);
        int drawX = (rect.Width() - drawWidth) / 2;
        int drawY = (rect.Height() - drawHeight) / 2;

        // Draw the image
        SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
        StretchDIBits(dc.GetSafeHdc(),
            drawX, drawY, drawWidth, drawHeight,
            0, 0, m_imageWidth, m_imageHeight,
            m_imageBuffer.data(), &bmi,
            DIB_RGB_COLORS, SRCCOPY);
    }
    else {
        // Draw placeholder
        dc.FillSolidRect(&rect, RGB(64, 64, 64));

        dc.SetBkMode(TRANSPARENT);
        dc.SetTextColor(RGB(192, 192, 192));

        CString str = _T("No Video Signal");
        CFont font;
        font.CreatePointFont(200, _T("Arial"));
        CFont* pOldFont = dc.SelectObject(&font);

        dc.DrawText(str, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(pOldFont);
    }
}

BOOL CImageDisplayWnd::OnEraseBkgnd(CDC* pDC)
{
    return TRUE;  // Prevent flicker
}