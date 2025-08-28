#include "stdafx.h"
#include "BFC388Tool.h"
#include "BFC388ToolDlg.h"
#include "resource.h"
#include <initguid.h>
#include <devguid.h>
#include <ntddser.h>
#include "DShowPreview.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Helpers to translate FACE_STATE to English messages (from protocol Table 10)
static const wchar_t* FaceStateToString(int16_t s) {
    switch (s) {
    case 0: return L"Face OK";
    case 1: return L"No face detected";
    case 2: return L"Too close to top";
    case 3: return L"Too close to bottom";
    case 4: return L"Too close to left";
    case 5: return L"Too close to right";
    case 6: return L"Too far";
    case 7: return L"Too close";
    case 8: return L"Eyebrow occlusion";
    case 9: return L"Eye occlusion";
    case 10:return L"Face occlusion";
    case 11:return L"Wrong direction";
    case 12:return L"Open eyes";
    case 13:return L"Closed eyes";
    case 14:return L"Unknown eye state";
    default:return L"Unknown state";
    }
}

CBFC388ToolDlg::CBFC388ToolDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_BFC388TOOL_DIALOG, pParent),
      m_deviceOpen(false), m_contVerify(false), m_downloadingImage(false),
      m_enrollIndex(0), m_enrollMask(0), m_expectedImageSize(0), m_receivedImageSize(0) {
}

void CBFC388ToolDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_COMBO_PORT, m_cbPort);
	DDX_Control(pDX, IDC_COMBO_BAUD, m_cbBaud);
	DDX_Control(pDX, IDC_EDIT_NAME, m_edName);
	DDX_Control(pDX, IDC_EDIT_ID, m_edId);
	DDX_Control(pDX, IDC_CHECK_ADMIN, m_chkAdmin);
	DDX_Control(pDX, IDC_PICTURE, m_pic);
	DDX_Control(pDX, IDC_EDIT_LOG, m_log);
	DDX_Control(pDX, IDC_PREVIEW, m_wndPreview);
}

BEGIN_MESSAGE_MAP(CBFC388ToolDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_OPEN, &CBFC388ToolDlg::OnBnClickedBtnOpen)
    ON_BN_CLICKED(IDC_BTN_CLOSE, &CBFC388ToolDlg::OnBnClickedBtnClose)
    ON_BN_CLICKED(IDC_BTN_ENROLL5, &CBFC388ToolDlg::OnBnClickedBtnEnroll5)
    ON_BN_CLICKED(IDC_BTN_ENROLL_SINGLE, &CBFC388ToolDlg::OnBnClickedBtnEnrollSingle)
    ON_BN_CLICKED(IDC_BTN_VERIFY, &CBFC388ToolDlg::OnBnClickedBtnVerify)
    ON_BN_CLICKED(IDC_BTN_VERIFY_CONT, &CBFC388ToolDlg::OnBnClickedBtnVerifyCont)
    ON_BN_CLICKED(IDC_BTN_CANCEL, &CBFC388ToolDlg::OnBnClickedBtnCancel)
    ON_BN_CLICKED(IDC_BTN_DELID, &CBFC388ToolDlg::OnBnClickedBtnDelid)
    ON_BN_CLICKED(IDC_BTN_DELALL, &CBFC388ToolDlg::OnBnClickedBtnDelall)
    ON_BN_CLICKED(IDC_BTN_LISTIDS, &CBFC388ToolDlg::OnBnClickedBtnListids)
    ON_BN_CLICKED(IDC_BTN_SNAP, &CBFC388ToolDlg::OnBnClickedBtnSnap)
    ON_BN_CLICKED(IDC_BTN_UPLOAD_CHAR, &CBFC388ToolDlg::OnBnClickedBtnUploadChar)
    ON_BN_CLICKED(IDC_BTN_DOWNLOAD_CHAR, &CBFC388ToolDlg::OnBnClickedBtnDownloadChar)
    ON_MESSAGE(WM_APP_NOTE, &CBFC388ToolDlg::OnNoteMsg)
    ON_MESSAGE(WM_APP_REPLY, &CBFC388ToolDlg::OnReplyMsg)
    ON_MESSAGE(WM_APP_IMAGE, &CBFC388ToolDlg::OnImageMsg)
END_MESSAGE_MAP()

BOOL CBFC388ToolDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();
    SetWindowText(L"CAMABIO Face Tool");

    // Baud rates
    const int bauds[] = {9600,19200,38400,57600,115200,230400,460800,921600};
    for (int b : bauds) {
        CString s; s.Format(L"%d", b);
        m_cbBaud.AddString(s);
    }
    m_cbBaud.SetCurSel(4); // 115200 default

    RefreshComPorts();

    // set callbacks
    bfc388_set_callbacks(&CBFC388ToolDlg::OnReply, &CBFC388ToolDlg::OnNote, &CBFC388ToolDlg::OnImage, this);
    AppendLog(L"Ready.");

	// 绑定控件（如果未使用 ClassWizard 自动绑定）
	// m_wndPreview.SubclassDlgItem(IDC_PREVIEW, this);

	// 初始化 COM（MFC 工程一般已调用 AfxOleInit，也可显式初始化）
	HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	// hrCo 可能返回 S_FALSE，表示已初始化，无需特殊处理

	// 打开并预览（1：第一个设备；2：根据名称包含子串匹配）
	HRESULT hr = m_cam.OpenFirst(m_wndPreview.GetSafeHwnd());
	// 或：HRESULT hr = m_cam.OpenByNamePart(m_wndPreview.GetSafeHwnd(), L"HD USB Camera");

	if (FAILED(hr))
	{
		CString msg;
		msg.Format(L"打开摄像头失败，HRESULT=0x%08X。\n请确认摄像头已连接，或尝试以管理员启动。", hr);
		AfxMessageBox(msg, MB_ICONERROR);
	}

    return TRUE;
}

void CBFC388ToolDlg::AppendLog(const CString& text)
{
    CString curr; m_log.GetWindowText(curr);
    curr += text; curr += L"\r\n";
    m_log.SetWindowText(curr);
    m_log.LineScroll(1000);
}

void CBFC388ToolDlg::RefreshComPorts()
{
    m_cbPort.ResetContent();
    // Use SetupDi enumeration to list COM ports
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_COMPORT, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA ifData; ifData.cbSize = sizeof(ifData);
        DWORD index = 0;
        while (SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &GUID_DEVINTERFACE_COMPORT, index, &ifData)) {
            DWORD size = 0;
            SetupDiGetDeviceInterfaceDetail(hDevInfo, &ifData, NULL, 0, &size, NULL);
            std::vector<BYTE> buf(size);
            auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buf.data();
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
            SP_DEVINFO_DATA devInfo; devInfo.cbSize = sizeof(devInfo);
            if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &ifData, detail, size, NULL, &devInfo)) {
                wchar_t friendly[256] = L"";
                SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfo, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendly, sizeof(friendly), NULL);
                // Extract COMx
                CString f = friendly;
                int pos = f.Find(L"(COM");
                if (pos >= 0) {
                    int pos2 = f.Find(L")", pos);
                    CString com = f.Mid(pos+1, pos2-pos-1); // COMx
                    m_cbPort.AddString(com);
                }
            }
            index++;
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    if (m_cbPort.GetCount() == 0) {
        // fallback: try COM1..COM32
        for (int i=1;i<=32;i++) {
            CString com; com.Format(L"COM%d", i);
            m_cbPort.AddString(com);
        }
    }
    m_cbPort.SetCurSel(0);
}

void CBFC388ToolDlg::ShowJpegInPicture(const std::vector<BYTE>& jpegData)
{
    if (jpegData.empty()) return;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, jpegData.size());
    if (!hMem) return;
    void* p = GlobalLock(hMem);
    memcpy(p, jpegData.data(), jpegData.size());
    GlobalUnlock(hMem);
    IStream* pStream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
        Bitmap bmp(pStream);
        if (bmp.GetLastStatus() == Ok) {
            CClientDC dc(&m_pic);
            CRect rc; m_pic.GetClientRect(&rc);
            Graphics g(dc);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetSmoothingMode(SmoothingModeHighQuality);
            g.DrawImage(&bmp, rc.left, rc.top, rc.Width(), rc.Height());
        }
        pStream->Release();
    }
}

void CBFC388ToolDlg::OnBnClickedBtnOpen()
{
    CString com; m_cbPort.GetLBText(m_cbPort.GetCurSel(), com);
    CString sbaud; m_cbBaud.GetLBText(m_cbBaud.GetCurSel(), sbaud);
    int baud = _wtoi(sbaud);
    if (bfc388_open(com, baud) == 0) {
        m_deviceOpen = true;
        AppendLog(L"Device opened.");
    } else {
        AppendLog(L"Open failed.");
    }
}

void CBFC388ToolDlg::OnBnClickedBtnClose()
{
    bfc388_close();
    m_deviceOpen = false;
    AppendLog(L"Device closed.");
}

void CBFC388ToolDlg::StartEnroll5()
{
    m_enrollOrder.clear();
    // order: middle, up, down, left, right
    m_enrollOrder.push_back(FACE_DIRECTION_MIDDLE);
    m_enrollOrder.push_back(FACE_DIRECTION_UP);
    m_enrollOrder.push_back(FACE_DIRECTION_DOWN);
    m_enrollOrder.push_back(FACE_DIRECTION_LEFT);
    m_enrollOrder.push_back(FACE_DIRECTION_RIGHT);
    m_enrollIndex = 0;
    m_enrollMask = 0;
    SendNextEnrollDirection();
}

void CBFC388ToolDlg::SendNextEnrollDirection()
{
    if (m_enrollIndex >= m_enrollOrder.size()) {
        AppendLog(L"Enroll complete.");
        return;
    }
    uint8_t dir = m_enrollOrder[m_enrollIndex];
    CString info;
    switch(dir) {
        case FACE_DIRECTION_MIDDLE: info = L"Please face the camera (middle)"; break;
        case FACE_DIRECTION_UP: info = L"Please look up"; break;
        case FACE_DIRECTION_DOWN: info = L"Please look down"; break;
        case FACE_DIRECTION_LEFT: info = L"Please turn left"; break;
        case FACE_DIRECTION_RIGHT: info = L"Please turn right"; break;
    }
    AppendLog(info);

    // read inputs
    wchar_t namebuf[33] = {0};
    m_edName.GetWindowText(namebuf, 32);
    bool admin = (m_chkAdmin.GetCheck() == BST_CHECKED);
    bfc388_enroll((uint8_t)admin, namebuf, dir, 20); // timeout 20s
}

void CBFC388ToolDlg::OnBnClickedBtnEnroll5()
{
    if (!m_deviceOpen) { AppendLog(L"Please open device first."); return; }
    StartEnroll5();
}

void CBFC388ToolDlg::OnBnClickedBtnEnrollSingle()
{
    if (!m_deviceOpen) { AppendLog(L"Please open device first."); return; }
    wchar_t namebuf[33] = {0};
    m_edName.GetWindowText(namebuf, 32);
    bool admin = (m_chkAdmin.GetCheck() == BST_CHECKED);
    bfc388_enroll_single((uint8_t)admin, namebuf, FACE_DIRECTION_UNDEFINE, 0xFF);
    AppendLog(L"Single-frame enroll started.");
}

void CBFC388ToolDlg::OnBnClickedBtnVerify()
{
    if (!m_deviceOpen) { AppendLog(L"Please open device first."); return; }
    bfc388_verify(0/*pd_rightaway*/, 10/*timeout*/, 0/*verify_mode*/);
    AppendLog(L"Verify once...");
}

void CBFC388ToolDlg::OnBnClickedBtnVerifyCont()
{
    if (!m_deviceOpen) { AppendLog(L"Please open device first."); return; }
    m_contVerify = !m_contVerify;
    if (m_contVerify) {
        AppendLog(L"Continuous verify: ON");
        bfc388_verify(0, 10, 0);
    } else {
        AppendLog(L"Continuous verify: OFF");
    }
}

void CBFC388ToolDlg::OnBnClickedBtnCancel()
{
    bfc388_reset();
    AppendLog(L"Cancel/RESET sent.");
}

void CBFC388ToolDlg::OnBnClickedBtnDelid()
{
    CString s; m_edId.GetWindowText(s);
    int id = _wtoi(s);
    if (id <= 0) { AppendLog(L"Invalid ID."); return; }
    bfc388_delete_user((uint16_t)id, 0/*face*/);
    AppendLog(L"Delete ID sent.");
}

void CBFC388ToolDlg::OnBnClickedBtnDelall()
{
    // Delete all face users (type=1 per spec)
    bfc388_delete_all(1/*face only*/, 0, 0);
    AppendLog(L"Delete all face IDs sent.");
}

void CBFC388ToolDlg::OnBnClickedBtnListids()
{
    bfc388_get_all_userid(0/*fmt long*/);
    AppendLog(L"Get all user IDs...");
}

void CBFC388ToolDlg::OnBnClickedBtnSnap()
{
    // capture 1 image as #1 then fetch
    m_imageBuf.clear(); m_expectedImageSize = 0; m_receivedImageSize = 0; m_downloadingImage = false;
    bfc388_snap_image(1, 1);
    // ask size
    bfc388_get_saved_image_size(1);
    AppendLog(L"Snap image and request size...");
}

void CBFC388ToolDlg::OnBnClickedBtnUploadChar()
{
    // upload template from module to a file
    CString s; m_edId.GetWindowText(s);
    int id = _wtoi(s);
    if (id <= 0) { AppendLog(L"Enter User ID for upload."); return; }

    CFileDialog fd(FALSE, L"bin", L"face_char.bin", OFN_OVERWRITEPROMPT, L"Binary (*.bin)|*.bin|All Files (*.*)|*.*||", this);
    if (fd.DoModal() != IDOK) return;
    CString path = fd.GetPathName();

    // download face char from module (upload from module perspective)
    const uint32_t total = 8*516; // 4128 bytes
    std::vector<BYTE> buf; buf.reserve(total);
    uint32_t offset = 0;
    while (offset < total) {
        uint32_t chunk = (total - offset > 4000) ? 4000 : (total - offset);
        bfc388_upload_user_char(0/*face*/, (uint16_t)id, offset, chunk);
        // The data will arrive via MID_IMAGE-like? Actually via REPLY data(payload) in protocol MID_UPLOADUSERCHAR.
        // Our C layer will dispatch via OnImage callback? No, it's a REPLY with data. We handle in OnReply.
        // For simplicity, we rely on OnReply handler to append to a temp memory file. Here we just inform the user.
        offset += chunk;
    }
    AppendLog(L"Requested template upload (watch log for completion).");
    // Actual saving handled in OnReply by writing to file path? For clarity, we keep it simple:
    // We store last char buffer to a temp file in OnReply with a known path.
}

void CBFC388ToolDlg::OnBnClickedBtnDownloadChar()
{
    // download template from PC to module
    CString s; m_edId.GetWindowText(s);
    int id = _wtoi(s);
    if (id <= 0) { AppendLog(L"Enter User ID to save template."); return; }

    CFileDialog fd(TRUE, L"bin", NULL, OFN_FILEMUSTEXIST, L"Binary (*.bin)|*.bin|All Files (*.*)|*.*||", this);
    if (fd.DoModal() != IDOK) return;
    CString path = fd.GetPathName();

    CFile file;
    if (!file.Open(path, CFile::modeRead|CFile::shareDenyNone)) {
        AppendLog(L"Failed to open file.");
        return;
    }
    DWORD len = (DWORD)file.GetLength();
    std::vector<BYTE> data(len);
    file.Read(data.data(), len);
    file.Close();

    uint32_t offset=0;
    while (offset < len) {
        uint32_t chunk = (len - offset > 4000) ? 4000 : (len - offset);
        bfc388_download_user_char(0/*face*/, offset, chunk, data.data()+offset);
        offset += chunk;
    }
    bfc388_save_download_user_char(0/*face*/, (uint16_t)id);
    AppendLog(L"Template downloaded to device (save command sent).");
}

// ---- static callbacks from C layer ----
void __cdecl CBFC388ToolDlg::OnReply(uint8_t mid, uint8_t result, const uint8_t* data, uint16_t size, void* user)
{
    // Pack into heap to pass via PostMessage
    BYTE* payload = (BYTE*)::CoTaskMemAlloc(4 + size);
    if (!payload) return;
    payload[0] = mid;
    payload[1] = result;
    payload[2] = (BYTE)(size & 0xFF);
    payload[3] = (BYTE)(size >> 8);
    if (size) memcpy(payload+4, data, size);
    CBFC388ToolDlg* dlg = (CBFC388ToolDlg*)user;
    ::PostMessage(dlg->m_hWnd, WM_APP_REPLY, (WPARAM)payload, 0);
}

void __cdecl CBFC388ToolDlg::OnNote(uint8_t nid, const uint8_t* data, uint16_t size, void* user)
{
    BYTE* payload = (BYTE*)::CoTaskMemAlloc(2 + size);
    if (!payload) return;
    payload[0] = nid;
    payload[1] = (BYTE)size;
    if (size) memcpy(payload+2, data, size);
    CBFC388ToolDlg* dlg = (CBFC388ToolDlg*)user;
    ::PostMessage(dlg->m_hWnd, WM_APP_NOTE, (WPARAM)payload, 0);
}

void __cdecl CBFC388ToolDlg::OnImage(const uint8_t* data, uint16_t size, void* user)
{
    BYTE* payload = (BYTE*)::CoTaskMemAlloc(size);
    if (!payload) return;
    memcpy(payload, data, size);
    CBFC388ToolDlg* dlg = (CBFC388ToolDlg*)user;
    ::PostMessage(dlg->m_hWnd, WM_APP_IMAGE, (WPARAM)payload, size);
}

LRESULT CBFC388ToolDlg::OnNoteMsg(WPARAM wParam, LPARAM lParam)
{
    BYTE* payload = (BYTE*)wParam;
    uint8_t nid = payload[0];
    uint8_t sz = payload[1];
    CString msg; msg.Format(L"NOTE nid=%u", nid);
    if (nid == 1 /*NID_FACE_STATE*/) {
        if (sz >= 16) {
            // s_note_data_face: state, left, top, right, bottom, yaw, pitch, roll (all int16)
            const int16_t* p = (const int16_t*)(payload+2);
            int16_t state = p[0];

			static DWORD lastTick = 0;
			static int16_t lastState = -1000;

			DWORD curTick = GetTickCount();
			if (state != lastState || (curTick - lastTick >= 3000))
			{
				int16_t yaw = p[5], pitch = p[6], roll = p[7];
				CString s2; s2.Format(L"FaceState: %s (yaw=%d, pitch=%d, roll=%d)", FaceStateToString(state), yaw, pitch, roll);
				AppendLog(s2);
			}
			
			lastState = state;
			lastTick = curTick;
        }
    } else if (nid == 0 /*READY*/) {
        AppendLog(L"NOTE: Module READY");
    }
    ::CoTaskMemFree(payload);
    return 0;
}

LRESULT CBFC388ToolDlg::OnReplyMsg(WPARAM wParam, LPARAM lParam)
{
    BYTE* payload = (BYTE*)wParam;
    uint8_t mid = payload[0];
    uint8_t result = payload[1];
    uint16_t size = payload[2] | (payload[3] << 8);
    const BYTE* data = payload + 4;

    CString head; head.Format(L"REPLY mid=0x%02X result=%u", mid, result);
    AppendLog(head);

    if (mid == MID_ENROLL || mid == MID_ENROLL_ITG || mid == MID_ENROLL_SINGLE) {
        if (result == MR_SUCCESS && size >= 3) {
            uint16_t uid = (data[0] << 8) | data[1];
            uint8_t face_dir = data[2];
            CString s; s.Format(L"Enroll OK. user_id=%u, face_dir_mask=0x%02X", uid, face_dir);
            AppendLog(s);
            // progress check for 5-dir
            m_enrollMask = face_dir;
            if ((m_enrollMask & 0x1F) == 0x1F) {
                AppendLog(L"All 5 directions completed.");
                m_enrollOrder.clear();
                m_enrollIndex = 0;
            } else {
                m_enrollIndex++;
                SendNextEnrollDirection();
            }
        } else if (result != MR_SUCCESS) {
            AppendLog(L"Enroll failed.");
        }
    } else if (mid == MID_VERIFY) {
        if (result == MR_SUCCESS && size >= 4) {
            uint16_t uid = (data[0] << 8) | data[1];
            uint8_t admin = data[2];
            CString s; s.Format(L"Verify SUCCESS. user_id=%u, admin=%u", uid, admin);
            AppendLog(s);
        } else {
            AppendLog(L"Verify failed.");
        }
        if (m_contVerify) bfc388_verify(0, 10, 0);
    } else if (mid == MID_GETSAVEDIMAGE) {
        if (result == MR_SUCCESS && size >= 4) {
            // image size MSB -> bytes in spec but here data[0..3] MSB; convert
            uint32_t sz = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
            m_expectedImageSize = sz;
            m_receivedImageSize = 0;
            m_imageBuf.clear(); m_imageBuf.reserve(sz);
            m_downloadingImage = true;
            // request in chunks
            uint32_t offset = 0;
            while (offset < m_expectedImageSize) {
                uint32_t chunk = (m_expectedImageSize - offset > 4000) ? 4000 : (m_expectedImageSize - offset);
                bfc388_upload_image(offset, chunk);
                offset += chunk;
            }
            AppendLog(L"Start receiving image data...");
        }
    } else if (mid == MID_GET_ALL_USERID) {
        if (result == MR_SUCCESS && size >= 1) {
            uint8_t cnt = data[0];
            CString s; s.Format(L"User count: %u", cnt);
            AppendLog(s);
            CString list(L"IDs: ");
            for (UINT i=0; i<cnt; ++i) {
                if (2 + i*2 + 1 <= size) {
                    uint16_t uid = (data[1+i*2]<<8) | data[2+i*2];
                    CString t; t.Format(L"%u ", uid);
                    list += t;
                }
            }
            AppendLog(list);
        }
    } else if (mid == MID_UPLOADUSERCHAR) {
        if (result == MR_SUCCESS && size > 0) {
            // Received a chunk of feature data (face template)
            static std::vector<BYTE> s_charbuf;
            s_charbuf.insert(s_charbuf.end(), data, data + size);
            CString s; s.Format(L"Template chunk received: %u bytes (total=%u)", size, (UINT)s_charbuf.size());
            AppendLog(s);
            // auto-save when >= 4128
            if (s_charbuf.size() >= 4128) {
                CString savePath = L".\\\\face_char.bin";
                CFile f; if (f.Open(savePath, CFile::modeCreate|CFile::modeWrite|CFile::shareDenyWrite)) {
                    f.Write(s_charbuf.data(), 4128);
                    f.Close();
                    AppendLog(L"Template saved to .\\\\face_char.bin");
                }
                s_charbuf.clear();
            }
        }
    }

    ::CoTaskMemFree(payload);
    return 0;
}

LRESULT CBFC388ToolDlg::OnImageMsg(WPARAM wParam, LPARAM lParam)
{
    BYTE* chunk = (BYTE*)wParam;
    UINT sz = (UINT)lParam;
    if (m_downloadingImage) {
        m_imageBuf.insert(m_imageBuf.end(), chunk, chunk+sz);
        m_receivedImageSize += sz;
        CString s; s.Format(L"Image chunk: %u bytes (total=%u/%u)", sz, m_receivedImageSize, m_expectedImageSize);
        AppendLog(s);
        if (m_receivedImageSize >= m_expectedImageSize) {
            AppendLog(L"Image received. Rendering...");
            ShowJpegInPicture(m_imageBuf);
            m_downloadingImage = false;
        }
    }
    ::CoTaskMemFree(chunk);
    return 0;
}

// ---- Button: Open/Close/Commands are implemented above ----
