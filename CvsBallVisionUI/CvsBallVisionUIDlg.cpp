#include "pch.h"
#include "framework.h"
#include "CvsBallVisionUI.h"
#include "CvsBallVisionUIDlg.h"
#include "afxdialogex.h"
#include "../CvsBallVisionCore/CvsBallVisionCore.h"
#include <sstream>
#include <iomanip>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CvsBallVisionUIDlg, CDialogEx)

CvsBallVisionUIDlg::CvsBallVisionUIDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_CVSBALLVISIONUI_DIALOG, pParent)
    , m_frameCount(0)
    , m_errorCount(0)
    , m_currentFps(0.0)
    , m_imageWidth(1280)
    , m_imageHeight(880)
    , m_bImageUpdated(false)
{
    m_pCamera = std::make_unique<CvsBallVision::CameraController>();
}

CvsBallVisionUIDlg::~CvsBallVisionUIDlg()
{
    if (m_pCamera)
    {
        if (m_pCamera->IsAcquiring())
            m_pCamera->StopAcquisition();

        if (m_pCamera->IsConnected())
            m_pCamera->DisconnectCamera();

        if (m_pCamera->IsSystemInitialized())
            m_pCamera->FreeSystem();
    }
}

void CvsBallVisionUIDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);

    DDX_Control(pDX, IDC_COMBO_CAMERA_LIST, m_comboCameraList);
    DDX_Control(pDX, IDC_BUTTON_CONNECT, m_btnConnect);
    DDX_Control(pDX, IDC_BUTTON_DISCONNECT, m_btnDisconnect);
    DDX_Control(pDX, IDC_BUTTON_START, m_btnStart);
    DDX_Control(pDX, IDC_BUTTON_STOP, m_btnStop);
    DDX_Control(pDX, IDC_BUTTON_REFRESH, m_btnRefresh);
    DDX_Control(pDX, IDC_BUTTON_APPLY_SETTINGS, m_btnApplySettings);
    DDX_Control(pDX, IDC_BUTTON_SAVE_SETTINGS, m_btnSaveSettings);
    DDX_Control(pDX, IDC_BUTTON_LOAD_SETTINGS, m_btnLoadSettings);

    DDX_Control(pDX, IDC_STATIC_VIDEO, m_staticVideo);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
    DDX_Control(pDX, IDC_STATIC_FPS, m_staticFps);
    DDX_Control(pDX, IDC_STATIC_FRAME_COUNT, m_staticFrameCount);
    DDX_Control(pDX, IDC_STATIC_ERROR_COUNT, m_staticErrorCount);

    DDX_Control(pDX, IDC_EDIT_WIDTH, m_editWidth);
    DDX_Control(pDX, IDC_EDIT_HEIGHT, m_editHeight);
    DDX_Control(pDX, IDC_EDIT_EXPOSURE, m_editExposure);
    DDX_Control(pDX, IDC_EDIT_GAIN, m_editGain);
    DDX_Control(pDX, IDC_EDIT_FPS, m_editFps);

    DDX_Control(pDX, IDC_SLIDER_EXPOSURE, m_sliderExposure);
    DDX_Control(pDX, IDC_SLIDER_GAIN, m_sliderGain);
    DDX_Control(pDX, IDC_SLIDER_FPS, m_sliderFps);

    DDX_Control(pDX, IDC_STATIC_EXPOSURE_VALUE, m_staticExposureValue);
    DDX_Control(pDX, IDC_STATIC_GAIN_VALUE, m_staticGainValue);
    DDX_Control(pDX, IDC_STATIC_FPS_VALUE, m_staticFpsValue);
}

BEGIN_MESSAGE_MAP(CvsBallVisionUIDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BUTTON_CONNECT, &CvsBallVisionUIDlg::OnBnClickedButtonConnect)
    ON_BN_CLICKED(IDC_BUTTON_DISCONNECT, &CvsBallVisionUIDlg::OnBnClickedButtonDisconnect)
    ON_BN_CLICKED(IDC_BUTTON_START, &CvsBallVisionUIDlg::OnBnClickedButtonStart)
    ON_BN_CLICKED(IDC_BUTTON_STOP, &CvsBallVisionUIDlg::OnBnClickedButtonStop)
    ON_BN_CLICKED(IDC_BUTTON_REFRESH, &CvsBallVisionUIDlg::OnBnClickedButtonRefresh)
    ON_BN_CLICKED(IDC_BUTTON_APPLY_SETTINGS, &CvsBallVisionUIDlg::OnBnClickedButtonApplySettings)
    ON_BN_CLICKED(IDC_BUTTON_SAVE_SETTINGS, &CvsBallVisionUIDlg::OnBnClickedButtonSaveSettings)
    ON_BN_CLICKED(IDC_BUTTON_LOAD_SETTINGS, &CvsBallVisionUIDlg::OnBnClickedButtonLoadSettings)
    ON_CBN_SELCHANGE(IDC_COMBO_CAMERA_LIST, &CvsBallVisionUIDlg::OnCbnSelchangeComboCameraList)
    ON_WM_HSCROLL()
    ON_WM_TIMER()
    ON_WM_PAINT()
    ON_WM_CTLCOLOR()
    ON_WM_DESTROY()
    ON_MESSAGE(WM_IMAGE_RECEIVED, &CvsBallVisionUIDlg::OnImageReceived)
END_MESSAGE_MAP()

BOOL CvsBallVisionUIDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // Set window title
    SetWindowText(_T("CvsBallVision - CREVIS Camera Control"));

    // Initialize camera system
    InitializeCamera();

    // Set default values
    m_editWidth.SetWindowText(_T("1280"));
    m_editHeight.SetWindowText(_T("880"));
    m_editExposure.SetWindowText(_T("5000"));
    m_editGain.SetWindowText(_T("1.0"));
    m_editFps.SetWindowText(_T("100"));

    // Initialize sliders
    m_sliderExposure.SetRange(100, 100000);
    m_sliderExposure.SetPos(5000);
    m_sliderGain.SetRange(0, 100);
    m_sliderGain.SetPos(10);
    m_sliderFps.SetRange(1, 200);
    m_sliderFps.SetPos(100);

    // Update slider value displays
    m_staticExposureValue.SetWindowText(_T("5000 μs"));
    m_staticGainValue.SetWindowText(_T("1.0 dB"));
    m_staticFpsValue.SetWindowText(_T("100 fps"));

    // Create memory DC for image display
    CreateMemoryDC();

    // Update UI state
    UpdateUIState();

    // Start UI update timer (30Hz)
    SetTimer(TIMER_UPDATE_UI, 33, nullptr);

    // Refresh camera list
    UpdateCameraList();

    return TRUE;
}

void CvsBallVisionUIDlg::OnCancel()
{
    // Clean up before closing
    KillTimer(TIMER_UPDATE_UI);

    if (m_pCamera)
    {
        if (m_pCamera->IsAcquiring())
            m_pCamera->StopAcquisition();

        if (m_pCamera->IsConnected())
            m_pCamera->DisconnectCamera();
    }

    CDialogEx::OnCancel();
}

void CvsBallVisionUIDlg::OnOK()
{
    // Prevent closing on Enter key
    // Do nothing
}

void CvsBallVisionUIDlg::OnDestroy()
{
    KillTimer(TIMER_UPDATE_UI);

    if (m_memDC.GetSafeHdc())
    {
        m_memDC.DeleteDC();
    }

    if (m_memBitmap.GetSafeHandle())
    {
        m_memBitmap.DeleteObject();
    }

    CDialogEx::OnDestroy();
}

void CvsBallVisionUIDlg::InitializeCamera()
{
    if (!m_pCamera)
        return;

    // Register callbacks
    m_pCamera->RegisterImageCallback(
        [this](const CvsBallVision::ImageData& imageData) {
            OnImageCallback(imageData);
        });

    m_pCamera->RegisterErrorCallback(
        [this](int errorCode, const std::string& errorMsg) {
            OnErrorCallback(errorCode, errorMsg);
        });

    m_pCamera->RegisterStatusCallback(
        [this](const std::string& status) {
            OnStatusCallback(status);
        });

    // Initialize system
    if (!m_pCamera->InitializeSystem())
    {
        AfxMessageBox(_T("Failed to initialize camera system!"), MB_ICONERROR);
    }
}

void CvsBallVisionUIDlg::UpdateCameraList()
{
    if (!m_pCamera)
        return;

    // Clear current list
    m_comboCameraList.ResetContent();
    m_cameraList.clear();

    // Update device list
    m_pCamera->UpdateDeviceList();

    // Get available cameras
    m_cameraList = m_pCamera->GetAvailableCameras();

    // Add to combo box
    for (const auto& camera : m_cameraList)
    {
        CString itemText;
        itemText.Format(_T("%s - %s [%s]"),
            CString(camera.modelName.c_str()),
            CString(camera.serialNumber.c_str()),
            CString(camera.ipAddress.c_str()));
        m_comboCameraList.AddString(itemText);
    }

    // Select first item if available
    if (m_comboCameraList.GetCount() > 0)
    {
        m_comboCameraList.SetCurSel(0);
    }

    UpdateUIState();
}

void CvsBallVisionUIDlg::UpdateUIState()
{
    bool bConnected = m_pCamera && m_pCamera->IsConnected();
    bool bAcquiring = m_pCamera && m_pCamera->IsAcquiring();
    bool bHasCameras = m_comboCameraList.GetCount() > 0;

    // Enable/disable buttons
    m_btnConnect.EnableWindow(!bConnected && bHasCameras);
    m_btnDisconnect.EnableWindow(bConnected);
    m_btnStart.EnableWindow(bConnected && !bAcquiring);
    m_btnStop.EnableWindow(bConnected && bAcquiring);
    m_btnRefresh.EnableWindow(!bConnected);
    m_btnApplySettings.EnableWindow(bConnected);
    m_btnSaveSettings.EnableWindow(bConnected);
    m_btnLoadSettings.EnableWindow(bConnected);

    // Enable/disable parameter controls
    m_editWidth.EnableWindow(bConnected && !bAcquiring);
    m_editHeight.EnableWindow(bConnected && !bAcquiring);
    m_editExposure.EnableWindow(bConnected);
    m_editGain.EnableWindow(bConnected);
    m_editFps.EnableWindow(bConnected);

    m_sliderExposure.EnableWindow(bConnected);
    m_sliderGain.EnableWindow(bConnected);
    m_sliderFps.EnableWindow(bConnected);

    m_comboCameraList.EnableWindow(!bConnected);
}

void CvsBallVisionUIDlg::UpdateStatistics()
{
    if (!m_pCamera || !m_pCamera->IsConnected())
        return;

    m_pCamera->GetStatistics(m_frameCount, m_errorCount, m_currentFps);

    CString str;
    str.Format(_T("FPS: %.1f"), m_currentFps);
    m_staticFps.SetWindowText(str);

    str.Format(_T("Frames: %llu"), m_frameCount);
    m_staticFrameCount.SetWindowText(str);

    str.Format(_T("Errors: %llu"), m_errorCount);
    m_staticErrorCount.SetWindowText(str);
}

void CvsBallVisionUIDlg::UpdateParameterRanges()
{
    if (!m_pCamera || !m_pCamera->IsConnected())
        return;

    double min, max;

    // Update exposure range
    if (m_pCamera->GetExposureTimeRange(min, max))
    {
        m_sliderExposure.SetRange(static_cast<int>(min), static_cast<int>(max));
    }

    // Update gain range
    if (m_pCamera->GetGainRange(min, max))
    {
        m_sliderGain.SetRange(static_cast<int>(min * 10), static_cast<int>(max * 10));
    }

    // Update FPS range
    if (m_pCamera->GetFrameRateRange(min, max))
    {
        m_sliderFps.SetRange(static_cast<int>(min), static_cast<int>(max));
    }
}

void CvsBallVisionUIDlg::UpdateParameterValues()
{
    if (!m_pCamera || !m_pCamera->IsConnected())
        return;

    double value;
    CString str;

    // Update exposure
    if (m_pCamera->GetExposureTime(value))
    {
        str.Format(_T("%.0f"), value);
        m_editExposure.SetWindowText(str);
        m_sliderExposure.SetPos(static_cast<int>(value));

        str.Format(_T("%.0f μs"), value);
        m_staticExposureValue.SetWindowText(str);
    }

    // Update gain
    if (m_pCamera->GetGain(value))
    {
        str.Format(_T("%.1f"), value);
        m_editGain.SetWindowText(str);
        m_sliderGain.SetPos(static_cast<int>(value * 10));

        str.Format(_T("%.1f dB"), value);
        m_staticGainValue.SetWindowText(str);
    }

    // Update FPS
    if (m_pCamera->GetFrameRate(value))
    {
        str.Format(_T("%.0f"), value);
        m_editFps.SetWindowText(str);
        m_sliderFps.SetPos(static_cast<int>(value));

        str.Format(_T("%.0f fps"), value);
        m_staticFpsValue.SetWindowText(str);
    }

    // Update resolution
    int width, height;
    if (m_pCamera->GetResolution(width, height))
    {
        str.Format(_T("%d"), width);
        m_editWidth.SetWindowText(str);

        str.Format(_T("%d"), height);
        m_editHeight.SetWindowText(str);
    }
}

void CvsBallVisionUIDlg::ApplySettings()
{
    if (!m_pCamera || !m_pCamera->IsConnected())
        return;

    CString str;

    // Apply resolution
    m_editWidth.GetWindowText(str);
    int width = _ttoi(str);

    m_editHeight.GetWindowText(str);
    int height = _ttoi(str);

    if (width > 0 && height > 0)
    {
        m_pCamera->SetResolution(width, height);
    }

    // Apply exposure
    m_editExposure.GetWindowText(str);
    double exposure = _ttof(str);
    if (exposure > 0)
    {
        m_pCamera->SetExposureTime(exposure);
    }

    // Apply gain
    m_editGain.GetWindowText(str);
    double gain = _ttof(str);
    if (gain >= 0)
    {
        m_pCamera->SetGain(gain);
    }

    // Apply FPS
    m_editFps.GetWindowText(str);
    double fps = _ttof(str);
    if (fps > 0)
    {
        m_pCamera->SetFrameRate(fps);
    }

    UpdateParameterValues();
}

void CvsBallVisionUIDlg::CreateMemoryDC()
{
    CClientDC dc(&m_staticVideo);

    if (m_memDC.GetSafeHdc())
    {
        m_memDC.DeleteDC();
    }

    if (m_memBitmap.GetSafeHandle())
    {
        m_memBitmap.DeleteObject();
    }

    m_memDC.CreateCompatibleDC(&dc);

    CRect rect;
    m_staticVideo.GetClientRect(&rect);

    m_memBitmap.CreateCompatibleBitmap(&dc, rect.Width(), rect.Height());
    m_memDC.SelectObject(&m_memBitmap);

    // Fill with black
    m_memDC.FillSolidRect(&rect, RGB(0, 0, 0));
}

void CvsBallVisionUIDlg::DrawImage()
{
    if (!m_memDC.GetSafeHdc())
        return;

    CRect rect;
    m_staticVideo.GetClientRect(&rect);

    std::lock_guard<std::mutex> lock(m_imageMutex);

    if (m_displayBuffer.empty() || m_imageWidth <= 0 || m_imageHeight <= 0)
    {
        // Clear with black
        m_memDC.FillSolidRect(&rect, RGB(0, 0, 0));
    }
    else
    {
        // Calculate scaling to fit
        double scaleX = static_cast<double>(rect.Width()) / m_imageWidth;
        double scaleY = static_cast<double>(rect.Height()) / m_imageHeight;
        double scale = min(scaleX, scaleY);

        int destWidth = static_cast<int>(m_imageWidth * scale);
        int destHeight = static_cast<int>(m_imageHeight * scale);
        int destX = (rect.Width() - destWidth) / 2;
        int destY = (rect.Height() - destHeight) / 2;

        // Clear background
        m_memDC.FillSolidRect(&rect, RGB(0, 0, 0));

        // Create bitmap from image data
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_imageWidth;
        bmi.bmiHeader.biHeight = -m_imageHeight; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        // Draw image
        SetStretchBltMode(m_memDC.GetSafeHdc(), HALFTONE);
        StretchDIBits(m_memDC.GetSafeHdc(),
            destX, destY, destWidth, destHeight,
            0, 0, m_imageWidth, m_imageHeight,
            m_displayBuffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }

    // Copy to screen
    CClientDC dc(&m_staticVideo);
    dc.BitBlt(0, 0, rect.Width(), rect.Height(), &m_memDC, 0, 0, SRCCOPY);
}

void CvsBallVisionUIDlg::OnImageCallback(const CvsBallVision::ImageData& imageData)
{
    // 데이터 유효성 검사 추가
    if (!imageData.pData || imageData.width <= 0 || imageData.height <= 0)
        return;

    std::lock_guard<std::mutex> lock(m_imageMutex);

    // Update dimensions
    m_imageWidth = imageData.width;
    m_imageHeight = imageData.height;

    // Copy image data
    int dataSize = imageData.width * imageData.height * 3;
    m_displayBuffer.resize(dataSize);

    if (imageData.channels == 3)
    {
        // RGB data - copy directly
        memcpy(m_displayBuffer.data(), imageData.pData, dataSize);
    }
    else if (imageData.channels == 1)
    {
        // Grayscale - convert to RGB
        for (int i = 0; i < imageData.width * imageData.height; i++)
        {
            uint8_t gray = imageData.pData[i];
            m_displayBuffer[i * 3] = gray;
            m_displayBuffer[i * 3 + 1] = gray;
            m_displayBuffer[i * 3 + 2] = gray;
        }
    }

    m_bImageUpdated = true;

    // Post message to update UI
    PostMessage(WM_IMAGE_RECEIVED);
}

void CvsBallVisionUIDlg::OnErrorCallback(int errorCode, const std::string& errorMsg)
{
    CString msg;
    msg.Format(_T("Error %d: %s"), errorCode, CString(errorMsg.c_str()));
    m_staticStatus.SetWindowText(msg);
}

void CvsBallVisionUIDlg::OnStatusCallback(const std::string& status)
{
    m_statusText = CString(status.c_str());
    m_staticStatus.SetWindowText(m_statusText);
}

void CvsBallVisionUIDlg::OnBnClickedButtonConnect()
{
    int sel = m_comboCameraList.GetCurSel();
    if (sel < 0 || sel >= static_cast<int>(m_cameraList.size()))
    {
        AfxMessageBox(_T("Please select a camera"), MB_ICONWARNING);
        return;
    }

    if (m_pCamera->ConnectCamera(m_cameraList[sel].enumIndex))
    {
        UpdateParameterRanges();
        UpdateParameterValues();
        UpdateUIState();

        // Apply default settings
        ApplySettings();
    }
    else
    {
        AfxMessageBox(_T("Failed to connect to camera"), MB_ICONERROR);
    }
}

void CvsBallVisionUIDlg::OnBnClickedButtonDisconnect()
{
    if (m_pCamera->IsAcquiring())
    {
        m_pCamera->StopAcquisition();
    }

    m_pCamera->DisconnectCamera();
    UpdateUIState();

    // Clear display
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        m_displayBuffer.clear();
    }
    DrawImage();
}

void CvsBallVisionUIDlg::OnBnClickedButtonStart()
{
    // UI 업데이트 전 이미지 버퍼 초기화
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        m_displayBuffer.clear();
        m_bImageUpdated = false;
    }

    if (m_pCamera->StartAcquisition())
    {
        UpdateUIState();
    }
    else
    {
        AfxMessageBox(_T("Failed to start acquisition"), MB_ICONERROR);
    }
}

void CvsBallVisionUIDlg::OnBnClickedButtonStop()
{
    m_pCamera->StopAcquisition();

    // Stop 후 디스플레이 버퍼 유지 (마지막 프레임 표시)
    // 필요시 아래 주석 해제하여 화면 클리어
    /*
    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        m_displayBuffer.clear();
        m_bImageUpdated = true;
    }
    DrawImage();
    */

    UpdateUIState();
}

void CvsBallVisionUIDlg::OnBnClickedButtonRefresh()
{
    UpdateCameraList();
}

void CvsBallVisionUIDlg::OnBnClickedButtonApplySettings()
{
    ApplySettings();
}

void CvsBallVisionUIDlg::OnBnClickedButtonSaveSettings()
{
    CFileDialog dlg(FALSE, _T("json"), _T("camera_settings.json"),
        OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
        _T("JSON Files (*.json)|*.json|All Files (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK)
    {
        CString filePath = dlg.GetPathName();
        if (m_pCamera->SaveParameters(CStringA(filePath).GetString()))
        {
            AfxMessageBox(_T("Settings saved successfully"), MB_ICONINFORMATION);
        }
        else
        {
            AfxMessageBox(_T("Failed to save settings"), MB_ICONERROR);
        }
    }
}

void CvsBallVisionUIDlg::OnBnClickedButtonLoadSettings()
{
    CFileDialog dlg(TRUE, _T("json"), nullptr,
        OFN_HIDEREADONLY | OFN_FILEMUSTEXIST,
        _T("JSON Files (*.json)|*.json|All Files (*.*)|*.*||"));

    if (dlg.DoModal() == IDOK)
    {
        CString filePath = dlg.GetPathName();
        if (m_pCamera->LoadParameters(CStringA(filePath).GetString()))
        {
            UpdateParameterValues();
            AfxMessageBox(_T("Settings loaded successfully"), MB_ICONINFORMATION);
        }
        else
        {
            AfxMessageBox(_T("Failed to load settings"), MB_ICONERROR);
        }
    }
}

void CvsBallVisionUIDlg::OnCbnSelchangeComboCameraList()
{
    // Camera selection changed
}

void CvsBallVisionUIDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    if (!m_pCamera || !m_pCamera->IsConnected())
    {
        CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
        return;
    }

    CSliderCtrl* pSlider = (CSliderCtrl*)pScrollBar;
    int pos = pSlider->GetPos();
    CString str;

    if (pSlider == &m_sliderExposure)
    {
        double exposure = static_cast<double>(pos);
        m_pCamera->SetExposureTime(exposure);

        str.Format(_T("%.0f"), exposure);
        m_editExposure.SetWindowText(str);

        str.Format(_T("%.0f μs"), exposure);
        m_staticExposureValue.SetWindowText(str);
    }
    else if (pSlider == &m_sliderGain)
    {
        double gain = pos / 10.0;
        m_pCamera->SetGain(gain);

        str.Format(_T("%.1f"), gain);
        m_editGain.SetWindowText(str);

        str.Format(_T("%.1f dB"), gain);
        m_staticGainValue.SetWindowText(str);
    }
    else if (pSlider == &m_sliderFps)
    {
        double fps = static_cast<double>(pos);
        m_pCamera->SetFrameRate(fps);

        str.Format(_T("%.0f"), fps);
        m_editFps.SetWindowText(str);

        str.Format(_T("%.0f fps"), fps);
        m_staticFpsValue.SetWindowText(str);
    }

    CDialogEx::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CvsBallVisionUIDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_UPDATE_UI)
    {
        UpdateStatistics();

        if (m_bImageUpdated)
        {
            m_bImageUpdated = false;
            DrawImage();
        }
    }

    CDialogEx::OnTimer(nIDEvent);
}

void CvsBallVisionUIDlg::OnPaint()
{
    CPaintDC dc(this);

    // Draw image if available
    DrawImage();
}

HBRUSH CvsBallVisionUIDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    HBRUSH hbr = CDialogEx::OnCtlColor(pDC, pWnd, nCtlColor);

    // Customize colors if needed

    return hbr;
}

LRESULT CvsBallVisionUIDlg::OnImageReceived(WPARAM wParam, LPARAM lParam)
{
    // Image received notification
    // Drawing is handled in OnTimer for better performance

    return 0;
}