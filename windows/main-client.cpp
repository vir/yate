/**
 * main-client.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004-2014 Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "yatengine.h"

#include "resource.h"
#include <commctrl.h>
#include <stdio.h>

using namespace TelEngine;

class DialogWrapper
{
public:
    inline DialogWrapper()
	: m_wnd(0)
	{ }
    virtual ~DialogWrapper();
    virtual int wndFunc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp);
    virtual int command(WORD id, LPARAM lp);
    inline HWND hwnd() const
	{ return m_wnd; }
    static bool insert(DialogWrapper* dlg, int id);
private:
    HWND m_wnd;
};

static HMODULE s_handle = 0;
static HICON s_icon = 0;
static HWND s_main = 0;

// set the status text on bottom of the window
static void mainStatus(const char* stat)
{
    if (s_main)
	::SetDlgItemText(s_main,IDC_STATUS,stat);
}

// recompute tabs visibility and show/hide child dialogs
static void tabsVisibility()
{
    if (!s_main)
	return;
    HWND tabs = ::GetDlgItem(s_main,IDC_MAINTABS);
    int current = TabCtrl_GetCurSel(tabs);
    int numtabs = TabCtrl_GetItemCount(tabs);
    for (int i=0; i<numtabs; i++) {
	TCITEM item;
	item.mask = TCIF_PARAM;
	if (TabCtrl_GetItem(tabs,i,&item))
	    ::ShowWindow((HWND)item.lParam,(i == current) ? SW_SHOW : SW_HIDE);
    }
}

static int CALLBACK innerDialog(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DialogWrapper* dlg = reinterpret_cast<DialogWrapper*>(::GetWindowLong(wnd,DWL_USER));
    if (dlg)
	return dlg->wndFunc(wnd,msg,wp,lp);
    switch (msg) {
	case WM_INITDIALOG:
	    dlg = reinterpret_cast<DialogWrapper*>(lp);
	    ::SetWindowLong(wnd,DWL_USER,(long)dlg);
	    if (dlg)
		return dlg->wndFunc(wnd,msg,wp,lp);
	    break;
	case WM_CLOSE:
	    DestroyWindow(wnd);
	    break;
	default:
	    return 0;
    }
    return 1;
}

DialogWrapper::~DialogWrapper()
{
    if (m_wnd) {
	::SetWindowLong(m_wnd,DWL_USER,0);
	DestroyWindow(m_wnd);
	if (s_main) {
	    HWND tabs = ::GetDlgItem(s_main,IDC_MAINTABS);
	    int numtabs = tabs ? TabCtrl_GetItemCount(tabs) : 0;
	    for (int i=0; i<numtabs; i++) {
		TCITEM item;
		item.mask = TCIF_PARAM;
		if (TabCtrl_GetItem(tabs,i,&item) && ((HWND)item.lParam == m_wnd)) {
		    BOOL current = (TabCtrl_GetCurSel(tabs) == i);
		    TabCtrl_DeleteItem(tabs,i);
		    if (current) {
			TabCtrl_SetCurSel(tabs,0);
			tabsVisibility();
		    }
		}
	    }
	}
    }
}

int DialogWrapper::wndFunc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
	case WM_INITDIALOG:
	    m_wnd = wnd;
	    break;
	case WM_CLOSE:
	    DestroyWindow(wnd);
	    break;
	case WM_NCDESTROY:
	    ::SetWindowLong(wnd,DWL_USER,0);
	    m_wnd = 0;
	    delete this;
	    return 0;
	case WM_COMMAND:
	    return command(LOWORD(wp),lp);
	default:
	    return 0;
    }
    return 1;
}

int DialogWrapper::command(WORD id, LPARAM lp)
{
#if 0
    char buf[128];
    sprintf(buf,"id=%u lparam=0x%08X",id,lp);
    ::MessageBox(hwnd(),buf,"WM_COMMAND",MB_OK);
#endif
    return 0;
}


bool DialogWrapper::insert(DialogWrapper* dlg, int id)
{
    if (!(s_main && dlg && id))
	return false;
    HWND tabs = ::GetDlgItem(s_main,IDC_MAINTABS);
    if (!tabs)
	return false;
    HWND wnd = ::CreateDialogParam(s_handle,MAKEINTRESOURCE(id),tabs,innerDialog,(LPARAM)dlg);
    if (!wnd)
	return false;

    char buf[128];
    ::LoadString(s_handle,id,buf,sizeof(buf));

    TCITEM item;
    item.mask = TCIF_TEXT | TCIF_PARAM;
    item.lParam = (LPARAM)wnd;
    item.pszText = buf;
    TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&item);

    RECT rect;
    ::GetClientRect(tabs,&rect);
    TabCtrl_AdjustRect(tabs,FALSE,&rect);
    ::MoveWindow(wnd,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,TRUE);
    ::ShowWindow(wnd,SW_SHOW);
    return true;
}

static void initMainDlg(HWND wnd)
{
    DWORD ver = ::GetVersion();
    if (((ver & 0xff) >= 4) || ((ver & 0xff00) >= 0x5f00)) {
	/* set icons in the new shell */
	::SendMessage(wnd,WM_SETICON,ICON_BIG,(LPARAM)s_icon);
	s_icon = (HICON)::LoadImage(s_handle,MAKEINTRESOURCE(IDI_NULLTEAM),IMAGE_ICON,16,16,0);
	if (s_icon)
	    ::SendMessage(wnd,WM_SETICON,ICON_SMALL,(LPARAM)s_icon);
    }
    HMENU smenu = ::GetSystemMenu(wnd,FALSE);
    ::DeleteMenu(smenu,SC_MAXIMIZE,MF_BYCOMMAND);
    ::DeleteMenu(smenu,SC_SIZE,MF_BYCOMMAND);
    s_main = wnd;
    DialogWrapper::insert(new DialogWrapper,IDD_CALLS);
    tabsVisibility();
}

static int CALLBACK mainDialog(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
	case WM_INITDIALOG:
	    initMainDlg(wnd);
	    break;
	case WM_CLOSE:
	    ::EndDialog(wnd,1);
	    break;
	case WM_QUERYDRAGICON:
	    /* in some cases this is used as application icon */
	    return (int)s_icon;
	case WM_PAINT:
	    if (s_icon && IsIconic(wnd)) {
		/* handle iconic draw in the old shell */
		PAINTSTRUCT ps;
		::BeginPaint(wnd,&ps);
		::DefWindowProc(wnd,WM_ICONERASEBKGND,(WPARAM)ps.hdc,0);
		::DrawIcon(ps.hdc,2,2,s_icon);
		::EndPaint(wnd,&ps);
		break;
	    }
	    return 0;
	case WM_NOTIFY:
	    if ((wp == IDC_MAINTABS) && (((LPNMHDR)lp)->code == TCN_SELCHANGE))
		tabsVisibility();
	    break;
	default:
	    return 0;
    }
    return 1;
}

class WinClientThread : public Thread
{
public:
    void run();
};

class WinClientPlugin : public Plugin
{
public:
    WinClientPlugin()
	: m_thread(0)
	{ }
    virtual void initialize(void);
    virtual bool isBusy() const
	{ return true; }
private:
    WinClientThread* m_thread;
};

void WinClientThread::run()
{
    s_handle = ::GetModuleHandle(0);
    s_icon = ::LoadIcon(s_handle,MAKEINTRESOURCE(IDI_NULLTEAM));
    ::InitCommonControls();
    int ret = ::DialogBox(s_handle,MAKEINTRESOURCE(IDD_TCLIENT),0,mainDialog);
    s_main = 0;
    if (s_icon)
	::DestroyIcon(s_icon);
    if (ret < 0)
	ret = 127;
    Engine::halt(ret);
}

void WinClientPlugin::initialize()
{
    if (!m_thread) {
	m_thread = new WinClientThread;
	if (m_thread->error())
	    Engine::halt(1);
	else
	    m_thread->startup();
    }
}

INIT_PLUGIN(WinClientPlugin);

// We force mainCRTStartup as entry point (from linker settings) so we get
//  the parser called even for a GUI application
extern "C" int main(int argc, const char** argv, const char** envp)
{
    return Engine::main(argc,argv,envp,TelEngine::Engine::Client);
}
