// CvsBallVisionUIDlg.cpp : implementation file
#include "pch.h"
#include "framework.h"
#include "CvsBallVisionUI.h"
#include "CvsBallVisionUIDlg.h"
#include "afxdialogex.h"
#include <sstream>
#include <iomanip>
#include <chrono>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CAboutDlg dialog for App About
class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg() : CDialogEx(IDD_ABOUTBOX) {}
};

// CCvsBallVisionUIDlg dialog
CCvsBallVisionUIDlg::CCvsBallVisionUIDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CVSBALLVISIONUI_DIALOG, pParent)
    , m_pBitmapInfo(nullptr)
    , m_pOldBitmap(nullptr)
    , m_nTimerID(0)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CCvsBallVisionUIDlg::~CCvsBallVisionUIDlg()
{
    if (m_pBitmapInfo) {
        delete m_pBitmapInfo;
        m_pBitmapInfo = nullptr;
    }
}

void CCvsBallVisionUIDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_COMBO_CAMERAS, m_comboCameras);
    DDX_Control(pDX, IDC_STATIC_IMAGE, m_staticImage);
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure);
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);
    DDX_Control(pDX, IDC_EDIT_FRAMERATE, m_editFrameRate);
    DDX_Control(pDX, IDC_CHECK_AUTO_EXPOSURE, m_checkAutoExposure);
    DDX_Control(pDX, IDC_CHECK_AUTO_GAIN, m_checkAutoGain);
    DDX_Control(pDX, IDC_CHECK_AUTO_WHITEBALANCE, m_checkAutoWhiteBalance);
    DDX_Control(pDX, IDC_CHECK_TRIGGER_MODE, m_checkTriggerMode);
    DDX_Control(pDX, IDC_BUTTON_CONNECT, m_btnConnect);
    DDX_Control(pDX, IDC_BUTTON_DISCONNECT, m_btnDisconnect);
    DDX_Control(pDX, IDC_BUTTON_START_ACQ, m_btnStartAcq);
    DDX_Control(pDX, IDC_BUTTON_STOP_ACQ, m_btnStopAcq);
    DDX_Control(pDX, IDC_BUTTON_SINGLE_GRAB, m_btnSingleGrab);
    DDX_Control(pDX, IDC_BUTTON_SOFT_TRIGGER, m_btnSoftTrigger);
    DDX_Control(pDX, IDC_BUTTON_SAVE_IMAGE, m_btnSaveImage);
    DDX_Control(pDX, IDC_BUTTON_SAVE_CONFIG, m_btnSaveConfig);
    DDX_Control(pDX, IDC_BUTTON_LOAD_CONFIG, m_btnLoadConfig);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
    DDX_Control(pDX, IDC_STATIC_FRAME_COUNT, m_staticFrameCount);
    DDX_Control(pDX, IDC_STATIC_ERROR_COUNT, m_staticErrorCount);
    DDX_Control(pDX, IDC_STATIC_FPS, m_staticFPS);
    DDX_Control(pDX, IDC_STATIC_BANDWIDTH, m_staticBandwidth);
    DDX_Control(pDX, IDC_STATIC_RESOLUTION, m_staticResolution);
    DDX_Control(pDX, IDC_STATIC_PIXEL_FORMAT, m_staticPixelFormat);
    DDX_Control(pDX, IDC_PROGRESS_BUFFER, m_progressBuffer);
}

BEGIN_MESSAGE_MAP(CCvsBallVisionUIDlg, CDialogEx)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_TIMER()
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_BUTTON_REFRESH, &CCvsBallVisionUIDlg::OnBnClickedButtonRefresh)
    ON_BN_CLICKED(IDC_BUTTON_CONNECT, &CCvsBallVisionUIDlg::OnBnClickedButtonConnect)
    ON_BN_CLICKED(IDC_BUTTON_DISCONNECT, &CCvsBallVisionUIDlg::OnBnClickedButtonDisconnect)
    ON_BN_CLICKED(IDC_BUTTON_START_ACQ, &CCvsBallVisionUIDlg::OnBnClickedButtonStartAcq)
    ON_BN_CLICKED(IDC_BUTTON_STOP_ACQ, &CCvsBallVisionUIDlg::OnBnClickedButtonStopAcq)
    ON_BN_CLICKED(IDC_BUTTON_SINGLE_GRAB, &CCvsBallVisionUIDlg::OnBnClickedButtonSingleGrab)
    ON_BN_CLICKED(IDC_BUTTON_SAVE_IMAGE, &CCvsBallVisionUIDlg::OnBnClickedButtonSaveImage)
    ON_BN_CLICKED(IDC_BUTTON_SAVE_CONFIG, &CCvsBallVisionUIDlg::OnBnClickedButtonSaveConfig)
    ON_BN_CLICKED(IDC_BUTTON_LOAD_CONFIG, &CCvsBallVisionUIDlg::OnBnClickedButtonLoadConfig)
    ON_BN_CLICKED(IDC_BUTTON_SOFT_TRIGGER, &CCvsBallVisionUIDlg::OnBnClickedButtonSoftTrigger)
    ON_CBN_SELCHANGE(IDC_COMBO_CAMERAS, &CCvsBallVisionUIDlg::OnCbnSelchangeComboCameras)
    ON_EN_CHANGE(IDC_EDIT_EXPOSURE, &CCvsBallVisionUIDlg::OnEnChangeEditExposure)
    ON_EN_CHANGE(IDC_EDIT_GAIN, &CCvsBallVisionUIDlg::OnEnChangeEditGain)
    ON_EN_CHANGE(IDC_EDIT_FRAMERATE, &CCvsBallVisionUIDlg::OnEnChangeEditFramerate)
    ON_BN_CLICKED(IDC_CHECK_AUTO_EXPOSURE, &CCvsBallVisionUIDlg::OnBnClickedCheckAutoExposure)
    ON_BN_CLICKED(IDC_CHECK_AUTO_GAIN, &CCvsBallVisionUIDlg::OnBnClickedCheckAutoGain)
    ON_BN_CLICKED(IDC_CHECK_AUTO_WHITEBALANCE, &CCvsBallVisionUIDlg::OnBnClickedCheckAutoWhitebalance)
    ON_BN_CLICKED(IDC_CHECK_TRIGGER_MODE, &CCvsBallVisionUIDlg::OnBnClickedCheckTriggerMode)
    ON_MESSAGE(WM_UPDATE_IMAGE, &CCvsBallVisionUIDlg::OnUpdateImage)
    ON_MESSAGE(WM_UPDATE_STATUS, &CCvsBallVisionUIDlg::OnUpdateStatus)
END_MESSAGE_MAP()

// CCvsBallVisionUIDlg message handlers
BOOL CCvsBallVisionUIDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // Add "About..." menu item to system menu
    CMenu* pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != nullptr) {
        CString strAboutMenu;
        strAboutMenu.LoadString(IDS_ABOUTBOX);
        if (!strAboutMenu.IsEmpty()) {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }

    SetIcon(m_hIcon, TRUE);  // Set big icon
    SetIcon(m_hIcon, FALSE); // Set small icon

    // Initialize camera controller
    m_pCameraController = std::make_unique<CvsBallVision::CameraController>();

    // Initialize system
    if (!m_pCameraController->InitializeSystem()) {
        AfxMessageBox(_T("Failed to initialize camera system!"));
    }

    // Set callbacks
    m_pCameraController->RegisterImageCallback(
        [this](const CVS_BUFFER* buffer, const CvsBallVision::CameraStatus& status) {
            OnImageReceived(buffer, status);
        });

    m_pCameraController->RegisterErrorCallback(
        [this](const std::string& error, CVS_ERROR errorCode) {
            OnErrorOccurred(error, errorCode);
        });

    // Initialize controls
    InitializeControls();

    // Update camera list
    UpdateCameraList();

    // Start status update timer (100ms interval)
    m_nTimerID = SetTimer(1, 100, nullptr);

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
    // Kill timer
    if (m_nTimerID != 0) {
        KillTimer(m_nTimerID);
        m_nTimerID = 0;
    }

    // Stop acquisition if running
    if (m_pCameraController && m_pCameraController->IsAcquiring()) {
        m_pCameraController->StopAcquisition();
    }

    // Disconnect camera
    if (m_pCameraController && m_pCameraController->IsConnected()) {
        m_pCameraController->DisconnectCamera();
    }

    CDialogEx::OnDestroy();
}

void CCvsBallVisionUIDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1) {
        UpdateStatusDisplay();

        // Process image queue
        if (m_newImageAvailable) {
            PostMessage(WM_UPDATE_IMAGE);
            m_newImageAvailable = false;
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

void CCvsBallVisionUIDlg::InitializeControls()
{
    // Set default values
    m_editExposure.SetWindowText(_T("10000"));
    m_editGain.SetWindowText(_T("1.0"));
    m_editFrameRate.SetWindowText(_T("30.0"));

    // Set progress bar range
    m_progressBuffer.SetRange(0, 100);

    // Update control states
    UpdateControlsState();
}

void CCvsBallVisionUIDlg::UpdateCameraList()
{
    m_comboCameras.ResetContent();
    m_cameras.clear();

    if (!m_pCameraController->UpdateDeviceList()) {
        LogMessage(_T("Failed to update device list"));
        return;
    }

    m_cameras = m_pCameraController->GetAvailableCameras();

    for (const auto& camera : m_cameras) {
        CString itemText;
        itemText.Format(_T("%s - %s [%s]"),
            CString(camera.modelName.c_str()),
            CString(camera.serialNumber.c_str()),
            CString(camera.ipAddress.c_str()));
        m_comboCameras.AddString(itemText);
    }

    if (!m_cameras.empty()) {
        m_comboCameras.SetCurSel(0);
        UpdateCameraInfo();
    }

    UpdateControlsState();
}

void CCvsBallVisionUIDlg::UpdateCameraInfo()
{
    int sel = m_comboCameras.GetCurSel();
    if (sel < 0 || sel >= static_cast<int>(m_cameras.size())) {
        return;
    }

    const auto& camera = m_cameras[sel];
    CString info;
    info.Format(_T("Model: %s\nSerial: %s\nIP: %s\nMAC: %s"),
        CString(camera.modelName.c_str()),
        CString(camera.serialNumber.c_str()),
        CString(camera.ipAddress.c_str()),
        CString(camera.macAddress.c_str()));

    // Update status display with camera info
    m_staticStatus.SetWindowText(info);
}

void CCvsBallVisionUIDlg::UpdateControlsState()
{
    BOOL isConnected = m_pCameraController && m_pCameraController->IsConnected();
    BOOL isAcquiring = m_pCameraController && m_pCameraController->IsAcquiring();
    BOOL hasCamera = m_comboCameras.GetCount() > 0;

    m_comboCameras.EnableWindow(!isConnected);
    m_btnConnect.EnableWindow(hasCamera && !isConnected);
    m_btnDisconnect.EnableWindow(isConnected);
    m_btnStartAcq.EnableWindow(isConnected && !isAcquiring);
    m_btnStopAcq.EnableWindow(isConnected && isAcquiring);
    m_btnSingleGrab.EnableWindow(isConnected && !isAcquiring);

    EnableCameraControls(isConnected);

    // Trigger controls
    BOOL triggerMode = m_checkTriggerMode.GetCheck() == BST_CHECKED;
    m_btnSoftTrigger.EnableWindow(isConnected && triggerMode && isAcquiring);
}

void CCvsBallVisionUIDlg::UpdateStatusDisplay()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) {
        return;
    }

    auto status = m_pCameraController->GetStatus();

    CString str;
    str.Format(_T("Frames: %llu"), status.frameCount);
    m_staticFrameCount.SetWindowText(str);

    str.Format(_T("Errors: %llu"), status.errorCount);
    m_staticErrorCount.SetWindowText(str);

    str.Format(_T("FPS: %.2f"), status.frameRate);
    m_staticFPS.SetWindowText(str);

    str.Format(_T("Bandwidth: %.2f MB/s"), status.bandwidth / 1048576.0);
    m_staticBandwidth.SetWindowText(str);

    // Update buffer usage
    if (status.frameCount > 0) {
        int bufferUsage = static_cast<int>((m_imageQueue.size() * 100) / 10);
        m_progressBuffer.SetPos(min(bufferUsage, 100));
    }
}

void CCvsBallVisionUIDlg::EnableCameraControls(BOOL enable)
{
    m_editExposure.EnableWindow(enable);
    m_editGain.EnableWindow(enable);
    m_editFrameRate.EnableWindow(enable);
    m_checkAutoExposure.EnableWindow(enable);
    m_checkAutoGain.EnableWindow(enable);
    m_checkAutoWhiteBalance.EnableWindow(enable);
    m_checkTriggerMode.EnableWindow(enable);
    m_btnSaveImage.EnableWindow(enable);
    m_btnSaveConfig.EnableWindow(enable);
    m_btnLoadConfig.EnableWindow(enable);
}

void CCvsBallVisionUIDlg::OnBnClickedButtonRefresh()
{
    UpdateCameraList();
}

void CCvsBallVisionUIDlg::OnBnClickedButtonConnect()
{
    int sel = m_comboCameras.GetCurSel();
    if (sel < 0 || sel >= static_cast<int>(m_cameras.size())) {
        AfxMessageBox(_T("Please select a camera"));
        return;
    }

    if (m_pCameraController->ConnectCamera(sel)) {
        LogMessage(_T("Camera connected successfully"));

        // Get current camera settings
        double exposure = 0.0;
        if (m_pCameraController->GetExposureTime(exposure)) {
            CString str;
            str.Format(_T("%.2f"), exposure);
            m_editExposure.SetWindowText(str);
        }

        double gain = 0.0;
        if (m_pCameraController->GetGain(gain)) {
            CString str;
            str.Format(_T("%.2f"), gain);
            m_editGain.SetWindowText(str);
        }

        double fps = 0.0;
        if (m_pCameraController->GetFrameRate(fps)) {
            CString str;
            str.Format(_T("%.2f"), fps);
            m_editFrameRate.SetWindowText(str);
        }
    }
    else {
        AfxMessageBox(_T("Failed to connect to camera"));
    }

    UpdateControlsState();
}

void CCvsBallVisionUIDlg::OnBnClickedButtonDisconnect()
{
    if (m_pCameraController->DisconnectCamera()) {
        LogMessage(_T("Camera disconnected"));
    }
    UpdateControlsState();
}

void CCvsBallVisionUIDlg::OnBnClickedButtonStartAcq()
{
    if (m_pCameraController->StartAcquisition()) {
        LogMessage(_T("Acquisition started"));
    }
    else {
        AfxMessageBox(_T("Failed to start acquisition"));
    }
    UpdateControlsState();
}

void CCvsBallVisionUIDlg::OnBnClickedButtonStopAcq()
{
    if (m_pCameraController->StopAcquisition()) {
        LogMessage(_T("Acquisition stopped"));
    }
    UpdateControlsState();
}

void CCvsBallVisionUIDlg::OnBnClickedButtonSingleGrab()
{
    CVS_BUFFER buffer;
    if (m_pCameraController->SingleGrab(&buffer)) {
        auto imageData = std::make_shared<ImageData>();
        if (ConvertToDisplayFormat(&buffer, *imageData)) {
            m_currentImage = imageData;
            DisplayImage(*imageData);
            LogMessage(_T("Single image grabbed"));
        }
    }
    else {
        AfxMessageBox(_T("Failed to grab single image"));
    }
}

void CCvsBallVisionUIDlg::OnBnClickedButtonSoftTrigger()
{
    if (m_pCameraController->ExecuteSoftwareTrigger()) {
        LogMessage(_T("Software trigger executed"));
    }
}

void CCvsBallVisionUIDlg::OnBnClickedButtonSaveImage()
{
    if (!m_currentImage) {
        AfxMessageBox(_T("No image to save"));
        return;
    }

    CFileDialog dlg(FALSE, _T("bmp"), _T("image.bmp"),
        OFN_OVERWRITEPROMPT, _T("Bitmap Files (*.bmp)|*.bmp|All Files (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK) {
        SaveImage(dlg.GetPathName());
    }
}

void CCvsBallVisionUIDlg::OnBnClickedButtonSaveConfig()
{
    CFileDialog dlg(FALSE, _T("xml"), _T("config.xml"),
        OFN_OVERWRITEPROMPT, _T("XML Files (*.xml)|*.xml|All Files (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK) {
        CStringA path(dlg.GetPathName());
        if (m_pCameraController->ExportSettingsToXML(path.GetString())) {
            LogMessage(_T("Configuration saved"));
        }
        else {
            AfxMessageBox(_T("Failed to save configuration"));
        }
    }
}

void CCvsBallVisionUIDlg::OnBnClickedButtonLoadConfig()
{
    CFileDialog dlg(TRUE, _T("xml"), nullptr,
        OFN_FILEMUSTEXIST, _T("XML Files (*.xml)|*.xml|All Files (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK) {
        CStringA path(dlg.GetPathName());
        if (m_pCameraController->ImportSettingsFromXML(path.GetString())) {
            LogMessage(_T("Configuration loaded"));
        }
        else {
            AfxMessageBox(_T("Failed to load configuration"));
        }
    }
}

void CCvsBallVisionUIDlg::OnEnChangeEditExposure()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    CString str;
    m_editExposure.GetWindowText(str);
    double exposure = _ttof(str);

    if (exposure > 0) {
        m_pCameraController->SetExposureTime(exposure);
    }
}

void CCvsBallVisionUIDlg::OnEnChangeEditGain()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    CString str;
    m_editGain.GetWindowText(str);
    double gain = _ttof(str);

    if (gain >= 0) {
        m_pCameraController->SetGain(gain);
    }
}

void CCvsBallVisionUIDlg::OnEnChangeEditFramerate()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    CString str;
    m_editFrameRate.GetWindowText(str);
    double fps = _ttof(str);

    if (fps > 0) {
        m_pCameraController->SetFrameRate(fps);
    }
}

void CCvsBallVisionUIDlg::OnBnClickedCheckAutoExposure()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    bool enable = m_checkAutoExposure.GetCheck() == BST_CHECKED;
    m_pCameraController->SetAutoExposure(enable);
    m_editExposure.EnableWindow(!enable);
}

void CCvsBallVisionUIDlg::OnBnClickedCheckAutoGain()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    bool enable = m_checkAutoGain.GetCheck() == BST_CHECKED;
    m_pCameraController->SetAutoGain(enable);
    m_editGain.EnableWindow(!enable);
}

void CCvsBallVisionUIDlg::OnBnClickedCheckAutoWhitebalance()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    bool enable = m_checkAutoWhiteBalance.GetCheck() == BST_CHECKED;
    m_pCameraController->SetWhiteBalanceAuto(enable);
}

void CCvsBallVisionUIDlg::OnBnClickedCheckTriggerMode()
{
    if (!m_pCameraController || !m_pCameraController->IsConnected()) return;

    bool enable = m_checkTriggerMode.GetCheck() == BST_CHECKED;
    m_pCameraController->SetTriggerMode(enable);
    UpdateControlsState();
}

void CCvsBallVisionUIDlg::OnCbnSelchangeComboCameras()
{
    UpdateCameraInfo();
}

void CCvsBallVisionUIDlg::OnImageReceived(const CVS_BUFFER* buffer, const CvsBallVision::CameraStatus& status)
{
    auto imageData = std::make_shared<ImageData>();
    if (ConvertToDisplayFormat(buffer, *imageData)) {
        imageData->frameNumber = status.frameCount;

        std::lock_guard<std::mutex> lock(m_imageMutex);

        // Keep queue size limited
        while (m_imageQueue.size() > 5) {
            m_imageQueue.pop();
        }

        m_imageQueue.push(imageData);
        m_currentImage = imageData;
        m_newImageAvailable = true;
    }

    // Update status
    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_currentStatus = status;
}

void CCvsBallVisionUIDlg::OnErrorOccurred(const std::string& error, CVS_ERROR errorCode)
{
    CString msg;
    msg.Format(_T("Error: %s (Code: %d)"), CString(error.c_str()), errorCode);
    LogMessage(msg);
}

LRESULT CCvsBallVisionUIDlg::OnUpdateImage(WPARAM wParam, LPARAM lParam)
{
    std::shared_ptr<ImageData> imageToDisplay;

    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        if (!m_imageQueue.empty()) {
            imageToDisplay = m_imageQueue.front();
            m_imageQueue.pop();
        }
    }

    if (imageToDisplay) {
        DisplayImage(*imageToDisplay);
    }

    return 0;
}

LRESULT CCvsBallVisionUIDlg::OnUpdateStatus(WPARAM wParam, LPARAM lParam)
{
    UpdateStatusDisplay();
    return 0;
}

bool CCvsBallVisionUIDlg::ConvertToDisplayFormat(const CVS_BUFFER* buffer, ImageData& imageData)
{
    if (!buffer || !buffer->image.pImage) return false;

    imageData.width = buffer->image.width;
    imageData.height = buffer->image.height;
    imageData.channels = buffer->image.channels;

    size_t dataSize = imageData.width * imageData.height * imageData.channels;
    imageData.data.resize(dataSize);

    memcpy(imageData.data.data(), buffer->image.pImage, dataSize);

    return true;
}

void CCvsBallVisionUIDlg::DisplayImage(const ImageData& imageData)
{
    CClientDC dc(&m_staticImage);
    CRect rect;
    m_staticImage.GetClientRect(&rect);

    if (!m_pBitmapInfo ||
        m_pBitmapInfo->bmiHeader.biWidth != imageData.width ||
        m_pBitmapInfo->bmiHeader.biHeight != -imageData.height) {
        CreateBitmapInfo(imageData.width, imageData.height, imageData.channels);
    }

    // Calculate display rectangle maintaining aspect ratio
    double imageAspect = static_cast<double>(imageData.width) / imageData.height;
    double windowAspect = static_cast<double>(rect.Width()) / rect.Height();

    CRect displayRect = rect;
    if (imageAspect > windowAspect) {
        int newHeight = static_cast<int>(rect.Width() / imageAspect);
        displayRect.top = (rect.Height() - newHeight) / 2;
        displayRect.bottom = displayRect.top + newHeight;
    }
    else {
        int newWidth = static_cast<int>(rect.Height() * imageAspect);
        displayRect.left = (rect.Width() - newWidth) / 2;
        displayRect.right = displayRect.left + newWidth;
    }

    // Clear background
    dc.FillSolidRect(&rect, RGB(64, 64, 64));

    // Draw image
    SetStretchBltMode(dc.GetSafeHdc(), HALFTONE);
    StretchDIBits(dc.GetSafeHdc(),
        displayRect.left, displayRect.top,
        displayRect.Width(), displayRect.Height(),
        0, 0, imageData.width, imageData.height,
        imageData.data.data(), m_pBitmapInfo,
        DIB_RGB_COLORS, SRCCOPY);
}

void CCvsBallVisionUIDlg::CreateBitmapInfo(int width, int height, int channels)
{
    if (m_pBitmapInfo) {
        delete m_pBitmapInfo;
    }

    size_t infoSize = sizeof(BITMAPINFOHEADER);
    if (channels == 1) {
        infoSize += 256 * sizeof(RGBQUAD);
    }

    m_pBitmapInfo = reinterpret_cast<BITMAPINFO*>(new BYTE[infoSize]);
    memset(m_pBitmapInfo, 0, infoSize);

    m_pBitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    m_pBitmapInfo->bmiHeader.biWidth = width;
    m_pBitmapInfo->bmiHeader.biHeight = -height; // Top-down DIB
    m_pBitmapInfo->bmiHeader.biPlanes = 1;
    m_pBitmapInfo->bmiHeader.biBitCount = channels * 8;
    m_pBitmapInfo->bmiHeader.biCompression = BI_RGB;

    // Set grayscale palette for mono images
    if (channels == 1) {
        for (int i = 0; i < 256; i++) {
            m_pBitmapInfo->bmiColors[i].rgbBlue = i;
            m_pBitmapInfo->bmiColors[i].rgbGreen = i;
            m_pBitmapInfo->bmiColors[i].rgbRed = i;
            m_pBitmapInfo->bmiColors[i].rgbReserved = 0;
        }
    }
}

void CCvsBallVisionUIDlg::SaveImage(const CString& filePath)
{
    if (!m_currentImage) return;

    // Create file header
    BITMAPFILEHEADER fileHeader;
    fileHeader.bfType = 0x4D42; // "BM"
    fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) +
        m_currentImage->data.size();
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    if (m_currentImage->channels == 1) {
        fileHeader.bfOffBits += 256 * sizeof(RGBQUAD);
    }

    // Save to file
    CFile file;
    if (file.Open(filePath, CFile::modeCreate | CFile::modeWrite)) {
        file.Write(&fileHeader, sizeof(BITMAPFILEHEADER));
        file.Write(m_pBitmapInfo, sizeof(BITMAPINFOHEADER));

        if (m_currentImage->channels == 1) {
            file.Write(m_pBitmapInfo->bmiColors, 256 * sizeof(RGBQUAD));
        }

        file.Write(m_currentImage->data.data(), m_currentImage->data.size());
        file.Close();

        LogMessage(_T("Image saved: ") + filePath);
    }
    else {
        AfxMessageBox(_T("Failed to save image"));
    }
}

void CCvsBallVisionUIDlg::LogMessage(const CString& message)
{
    CString timestamp;
    SYSTEMTIME st;
    GetLocalTime(&st);
    timestamp.Format(_T("[%02d:%02d:%02d.%03d] "),
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    // Update status display with log message
    m_staticStatus.SetWindowText(timestamp + message);
}