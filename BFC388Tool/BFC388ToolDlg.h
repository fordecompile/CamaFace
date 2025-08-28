#pragma once
#include "stdafx.h"
#include "proto/bfc388_proto.h"
#include "DShowPreview.h"

class CBFC388ToolDlg : public CDialogEx
{
public:
    CBFC388ToolDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_BFC388TOOL_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();

    DECLARE_MESSAGE_MAP()

private:
    // UI helpers
    void AppendLog(const CString& text);
    void RefreshComPorts();

    // Image display
    void ShowJpegInPicture(const std::vector<BYTE>& jpegData);

    // Enrollment helpers
    void StartEnroll5();
    void SendNextEnrollDirection();

    // State
    CComboBox m_cbPort;
    CComboBox m_cbBaud;
    CEdit     m_edName;
    CEdit     m_edId;
    CButton   m_chkAdmin;
    CStatic   m_pic;
    CEdit     m_log;

    bool      m_deviceOpen;
    bool      m_contVerify;
    bool      m_downloadingImage;

    // enroll
    std::vector<uint8_t> m_enrollOrder;
    size_t    m_enrollIndex;
    uint8_t   m_enrollMask;
    CString   m_enrollName;
    bool      m_enrollAdmin;

    // image buffer for MID_UPLOADIMAGE
    std::vector<BYTE> m_imageBuf;
    uint32_t m_expectedImageSize;
    uint32_t m_receivedImageSize;

    // callbacks from C module
    static void __cdecl OnReply(uint8_t mid, uint8_t result, const uint8_t* data, uint16_t size, void* user);
    static void __cdecl OnNote(uint8_t nid, const uint8_t* data, uint16_t size, void* user);
    static void __cdecl OnImage(const uint8_t* data, uint16_t size, void* user);

    afx_msg LRESULT OnNoteMsg(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnReplyMsg(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnImageMsg(WPARAM wParam, LPARAM lParam);

public:
    afx_msg void OnBnClickedBtnOpen();
    afx_msg void OnBnClickedBtnClose();
    afx_msg void OnBnClickedBtnEnroll5();
    afx_msg void OnBnClickedBtnEnrollSingle();
    afx_msg void OnBnClickedBtnVerify();
    afx_msg void OnBnClickedBtnVerifyCont();
    afx_msg void OnBnClickedBtnCancel();
    afx_msg void OnBnClickedBtnDelid();
    afx_msg void OnBnClickedBtnDelall();
    afx_msg void OnBnClickedBtnListids();
    afx_msg void OnBnClickedBtnSnap();
    afx_msg void OnBnClickedBtnUploadChar();
    afx_msg void OnBnClickedBtnDownloadChar();
	CStatic m_wndPreview;
	DShowPreview m_cam;
};
