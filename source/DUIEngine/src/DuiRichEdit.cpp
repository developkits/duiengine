#include "duistd.h"
#include "DuiRichEdit.h"
#include "DuiSystem.h"
#include "DuiMenu.h"

#pragma comment(lib,"imm32.lib")

namespace DuiEngine
{

	template<> CDuiTextServiceHelper * Singleton<CDuiTextServiceHelper>::ms_Singleton=0;

	class CDuiRicheditDropTarget : public IDropTarget
	{
	public:
		CDuiRicheditDropTarget(ITextServices *pTxtSvr)
			:nRef(1)
			,pserv(pTxtSvr)
		{
			DUIASSERT(pserv);
			pserv->AddRef();
		}

		~CDuiRicheditDropTarget()
		{
			DUIASSERT(pserv);
			pserv->Release();
		}

		//IUnkown
		virtual HRESULT STDMETHODCALLTYPE QueryInterface( 
			/* [in] */ REFIID riid,
			/* [iid_is][out] */ __RPC__deref_out void __RPC_FAR *__RPC_FAR *ppvObject)
		{
			HRESULT hr=S_FALSE;
			if(riid==__uuidof(IUnknown))
				*ppvObject=(IUnknown*) this,hr=S_OK;
			else if(riid==__uuidof(IDropTarget))
				*ppvObject=(IDropTarget*)this,hr=S_OK;
			if(SUCCEEDED(hr)) AddRef();
			return hr;
		}

		virtual ULONG STDMETHODCALLTYPE AddRef( void){return ++nRef;}

		virtual ULONG STDMETHODCALLTYPE Release( void) { 
			ULONG uRet= -- nRef;
			if(uRet==0) delete this;
			return uRet;
		}

		//IDropTarget
		virtual HRESULT STDMETHODCALLTYPE DragEnter( 
			/* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ __RPC__inout DWORD *pdwEffect)
		{
			HRESULT hr=S_FALSE;
			IDropTarget *pDropTarget=NULL;
			hr=pserv->TxGetDropTarget(&pDropTarget);
			if(SUCCEEDED(hr))
			{
				hr=pDropTarget->DragEnter(pDataObj,grfKeyState,pt,pdwEffect);
				*pdwEffect = DROPEFFECT_COPY;
				pDropTarget->Release();
			}
			return hr;
		}

		virtual HRESULT STDMETHODCALLTYPE DragOver( 
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ __RPC__inout DWORD *pdwEffect)	
		{
			HRESULT hr=S_FALSE;
			IDropTarget *pDropTarget=NULL;
			hr=pserv->TxGetDropTarget(&pDropTarget);
			if(SUCCEEDED(hr))
			{
				hr=pDropTarget->DragOver(grfKeyState,pt,pdwEffect);
				*pdwEffect = DROPEFFECT_COPY;
				pDropTarget->Release();
			}
			return hr;
		}


		virtual HRESULT STDMETHODCALLTYPE DragLeave( void) 
		{
			HRESULT hr=S_FALSE;
			IDropTarget *pDropTarget=NULL;
			hr=pserv->TxGetDropTarget(&pDropTarget);
			if(SUCCEEDED(hr))
			{
				hr=pDropTarget->DragLeave();
				pDropTarget->Release();
			}
			return hr;
		}


		virtual HRESULT STDMETHODCALLTYPE Drop( 
			/* [unique][in] */ __RPC__in_opt IDataObject *pDataObj,
			/* [in] */ DWORD grfKeyState,
			/* [in] */ POINTL pt,
			/* [out][in] */ __RPC__inout DWORD *pdwEffect)
		{
			if(*pdwEffect == DROPEFFECT_NONE) return S_FALSE;
			HRESULT hr=S_FALSE;
			IDropTarget *pDropTarget=NULL;
			hr=pserv->TxGetDropTarget(&pDropTarget);
			if(SUCCEEDED(hr))
			{
				hr=pDropTarget->Drop(pDataObj,grfKeyState,pt,pdwEffect);
				pDropTarget->Release();
			}
			return hr;
		}



	protected:
		ITextServices	*pserv;		    // pointer to Text Services object
		LONG			nRef;
	};



const LONG cInitTextMax = (32 * 1024) - 1;
#define FValidCF(_pcf) ((_pcf)->cbSize == sizeof(CHARFORMAT2W))
#define FValidPF(_ppf) ((_ppf)->cbSize == sizeof(PARAFORMAT2))
#define TIMER_INVALIDATE	6

EXTERN_C const IID IID_ITextServices =   // 8d33f740-cf58-11ce-a89d-00aa006cadc5
{
    0x8d33f740,
    0xcf58,
    0x11ce,
    {0xa8, 0x9d, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}
};

EXTERN_C const IID IID_ITextHost =   /* c5bdd8d0-d26e-11ce-a89e-00aa006cadc5 */
{
    0xc5bdd8d0,
    0xd26e,
    0x11ce,
    {0xa8, 0x9e, 0x00, 0xaa, 0x00, 0x6c, 0xad, 0xc5}
};

// Convert Pixels on the X axis to Himetric
LONG DXtoHimetricX(LONG dx, LONG xPerInch)
{
    return (LONG) MulDiv(dx, HIMETRIC_PER_INCH, xPerInch);
}

// Convert Pixels on the Y axis to Himetric
LONG DYtoHimetricY(LONG dy, LONG yPerInch)
{
    return (LONG) MulDiv(dy, HIMETRIC_PER_INCH, yPerInch);
}

// Convert Himetric along the X axis to X pixels
LONG HimetricXtoDX(LONG xHimetric, LONG xPerInch)
{
    return (LONG) MulDiv(xHimetric, xPerInch, HIMETRIC_PER_INCH);
}

// Convert Himetric along the Y axis to Y pixels
LONG HimetricYtoDY(LONG yHimetric, LONG yPerInch)
{
    return (LONG) MulDiv(yHimetric, yPerInch, HIMETRIC_PER_INCH);
}

CDuiTextHost::CDuiTextHost(void)
    :m_pDuiRichEdit(NULL)
    ,cRefs(0)
	,m_fUiActive(FALSE)
{
}

CDuiTextHost::~CDuiTextHost(void)
{
    pserv->Release();
}

//////////////////////////////////////////////////////////////////////////
// IUnknown
HRESULT _stdcall CDuiTextHost::QueryInterface( REFIID riid, void **ppvObject )
{
    HRESULT hr = E_NOINTERFACE;
    *ppvObject = NULL;

    if (IsEqualIID(riid, IID_IUnknown)
            || IsEqualIID(riid, IID_ITextHost))
    {
        AddRef();
        *ppvObject = (ITextHost *) this;
        hr = S_OK;
    }

    return hr;
}

ULONG _stdcall CDuiTextHost::AddRef( void )
{
    return ++cRefs;
}

ULONG _stdcall CDuiTextHost::Release( void )
{
    ULONG c_Refs = --cRefs;

    if (c_Refs == 0)
    {
        delete this;
    }

    return c_Refs;
}


//////////////////////////////////////////////////////////////////////////
// ITextHost
HRESULT CDuiTextHost::TxGetViewInset( LPRECT prc )
{
    *prc=m_pDuiRichEdit->m_rcInset;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetCharFormat( const CHARFORMATW **ppCF )
{
    *ppCF=&m_pDuiRichEdit->m_cfDef;
    return S_OK;
}


HRESULT CDuiTextHost::TxGetParaFormat( const PARAFORMAT **ppPF )
{
    *ppPF=&m_pDuiRichEdit->m_pfDef;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetClientRect( LPRECT prc )
{
    m_pDuiRichEdit->GetClient(prc);
    return S_OK;
}

HRESULT CDuiTextHost::TxDeactivate( LONG lNewState )
{
	m_fUiActive=FALSE;
    return S_OK;
}

HRESULT CDuiTextHost::TxActivate( LONG * plOldState )
{
	*plOldState = m_fUiActive;
	m_fUiActive=TRUE;
    return S_OK;
}

BOOL CDuiTextHost::TxClientToScreen( LPPOINT lppt )
{
    return ::ClientToScreen(m_pDuiRichEdit->GetContainer()->GetHostHwnd(),lppt);
}

BOOL CDuiTextHost::TxScreenToClient( LPPOINT lppt )
{
    return ::ScreenToClient(m_pDuiRichEdit->GetContainer()->GetHostHwnd(),lppt);
}

void CDuiTextHost::TxSetCursor( HCURSOR hcur, BOOL fText )
{
    ::SetCursor(hcur);
}

void CDuiTextHost::TxSetFocus()
{
    m_pDuiRichEdit->SetDuiFocus();
}

void CDuiTextHost::TxSetCapture( BOOL fCapture )
{
    if(fCapture)
        m_pDuiRichEdit->SetDuiCapture();
    else
        m_pDuiRichEdit->ReleaseDuiCapture();
}

void CDuiTextHost::TxScrollWindowEx( INT dx, INT dy, LPCRECT lprcScroll, LPCRECT lprcClip, HRGN hrgnUpdate, LPRECT lprcUpdate, UINT fuScroll )
{
	m_pDuiRichEdit->NotifyInvalidate();
}

void CDuiTextHost::TxKillTimer( UINT idTimer )
{
    m_pDuiRichEdit->KillDuiTimerEx(idTimer);
}

BOOL CDuiTextHost::TxSetTimer( UINT idTimer, UINT uTimeout )
{
    return m_pDuiRichEdit->SetDuiTimerEx(idTimer,uTimeout);
}

BOOL CDuiTextHost::TxSetCaretPos( INT x, INT y )
{
	m_ptCaret.x=x,m_ptCaret.y=y;
    return m_pDuiRichEdit->GetContainer()->DuiSetCaretPos(x,y);
}

BOOL CDuiTextHost::TxShowCaret( BOOL fShow )
{
	if(fShow && !m_fUiActive) return FALSE;
    return m_pDuiRichEdit->GetContainer()->DuiShowCaret(fShow);
}

BOOL CDuiTextHost::TxCreateCaret( HBITMAP hbmp, INT xWidth, INT yHeight )
{
    return m_pDuiRichEdit->GetContainer()->DuiCreateCaret(hbmp,xWidth,yHeight);
}

HDC CDuiTextHost::TxGetDC()
{
	return ::GetDC(NULL);
}

INT CDuiTextHost::TxReleaseDC( HDC hdc )
{
	return ::ReleaseDC(NULL,hdc);
}

BOOL CDuiTextHost::TxShowScrollBar( INT fnBar, BOOL fShow )
{
    int wBar=0;
    switch(fnBar)
    {
    case SB_BOTH:
        wBar=DUISB_BOTH;
        break;
    case SB_VERT:
        wBar=DUISB_VERT;
        break;
    case SB_HORZ:
        wBar=DUISB_HORZ;
        break;
    }
    m_pDuiRichEdit->SetDuiTimer(TIMER_INVALIDATE,40);//todo:
    return m_pDuiRichEdit->ShowScrollBar(wBar,fShow);
}

BOOL CDuiTextHost::TxEnableScrollBar( INT fuSBFlags, INT fuArrowflags )
{
    int wBar=0;
    switch(fuSBFlags)
    {
    case SB_BOTH:
        wBar=DUISB_BOTH;
        break;
    case SB_VERT:
        wBar=DUISB_VERT;
        break;
    case SB_HORZ:
        wBar=DUISB_HORZ;
        break;
    }
    return m_pDuiRichEdit->EnableScrollBar(wBar,fuArrowflags==ESB_ENABLE_BOTH);
}

BOOL CDuiTextHost::TxSetScrollRange( INT fnBar, LONG nMinPos, INT nMaxPos, BOOL fRedraw )
{
    if(fnBar==SB_HORZ)
        return m_pDuiRichEdit->SetScrollRange(FALSE,nMinPos,nMaxPos,fRedraw);
    else
        return m_pDuiRichEdit->SetScrollRange(TRUE,nMinPos,nMaxPos,fRedraw);
}

BOOL CDuiTextHost::TxSetScrollPos( INT fnBar, INT nPos, BOOL fRedraw )
{
    BOOL bRet=FALSE;
    if(m_pDuiRichEdit->m_fScrollPending) return TRUE;
    m_pDuiRichEdit->m_fScrollPending=TRUE;
    if(fnBar==SB_HORZ)
        bRet= m_pDuiRichEdit->SetScrollPos(FALSE,nPos,fRedraw);
    else
        bRet= m_pDuiRichEdit->SetScrollPos(TRUE,nPos,fRedraw);
    m_pDuiRichEdit->m_fScrollPending=FALSE;
    return bRet;
}

void CDuiTextHost::TxInvalidateRect( LPCRECT prc, BOOL fMode )
{
    if(prc)
    {
        m_pDuiRichEdit->NotifyInvalidateRect(prc);
    }
    else
    {
        m_pDuiRichEdit->NotifyInvalidate();
    }
}

void CDuiTextHost::TxViewChange( BOOL fUpdate )
{
    if(fUpdate)
    {
		m_pDuiRichEdit->GetContainer()->DuiUpdateWindow();
    }
}

COLORREF CDuiTextHost::TxGetSysColor( int nIndex )
{
    return ::GetSysColor(nIndex);
}

HRESULT CDuiTextHost::TxGetBackStyle( TXTBACKSTYLE *pstyle )
{
	*pstyle=TXTBACK_TRANSPARENT;
    //*pstyle = m_pDuiRichEdit->m_fTransparent ?TXTBACK_TRANSPARENT: TXTBACK_OPAQUE;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetMaxLength( DWORD *plength )
{
    *plength = m_pDuiRichEdit->m_cchTextMost;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetScrollBars( DWORD *pdwScrollBar )
{
    *pdwScrollBar =  m_pDuiRichEdit->m_dwStyle & (WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL |
                     ES_AUTOHSCROLL | ES_DISABLENOSCROLL);

    return S_OK;
}

HRESULT CDuiTextHost::TxGetPasswordChar( TCHAR *pch )
{
    *pch=m_pDuiRichEdit->m_chPasswordChar;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetAcceleratorPos( LONG *pcp )
{
    *pcp=m_pDuiRichEdit->m_lAccelPos;
    return S_OK;
}

HRESULT CDuiTextHost::TxGetExtent( LPSIZEL lpExtent )
{
    *lpExtent=m_pDuiRichEdit->m_sizelExtent;
    return S_OK;
}

HRESULT CDuiTextHost::OnTxCharFormatChange( const CHARFORMATW * pcf )
{
    return S_OK;
}

HRESULT CDuiTextHost::OnTxParaFormatChange( const PARAFORMAT * ppf )
{
    return S_OK;
}

HRESULT CDuiTextHost::TxGetPropertyBits( DWORD dwMask, DWORD *pdwBits )
{
    DWORD dwProperties = 0;

    if (m_pDuiRichEdit->m_fRich)
    {
        dwProperties = TXTBIT_RICHTEXT;
    }

    if (m_pDuiRichEdit->m_dwStyle & ES_MULTILINE)
    {
        dwProperties |= TXTBIT_MULTILINE;
    }

    if (m_pDuiRichEdit->m_dwStyle & ES_READONLY)
    {
        dwProperties |= TXTBIT_READONLY;
    }


    if (m_pDuiRichEdit->m_dwStyle & ES_PASSWORD)
    {
        dwProperties |= TXTBIT_USEPASSWORD;
    }

    if (!(m_pDuiRichEdit->m_dwStyle & ES_NOHIDESEL))
    {
        dwProperties |= TXTBIT_HIDESELECTION;
    }

    if (m_pDuiRichEdit->m_fEnableAutoWordSel)
    {
        dwProperties |= TXTBIT_AUTOWORDSEL;
    }

    if (m_pDuiRichEdit->m_fVertical)
    {
        dwProperties |= TXTBIT_VERTICAL;
    }

    if (m_pDuiRichEdit->m_fWordWrap)
    {
        dwProperties |= TXTBIT_WORDWRAP;
    }

    if (m_pDuiRichEdit->m_fAllowBeep)
    {
        dwProperties |= TXTBIT_ALLOWBEEP;
    }

    if (m_pDuiRichEdit->m_fSaveSelection)
    {
        dwProperties |= TXTBIT_SAVESELECTION;
    }

    *pdwBits = dwProperties & dwMask;
    return NOERROR;
}

HRESULT CDuiTextHost::TxNotify( DWORD iNotify, void *pv )
{
    if(iNotify==EN_REQUESTRESIZE)
    {
        return S_OK;
    }
    return m_pDuiRichEdit->OnTxNotify(iNotify,pv);
}

HIMC CDuiTextHost::TxImmGetContext()
{
    return ImmGetContext(m_pDuiRichEdit->GetContainer()->GetHostHwnd());
}

void CDuiTextHost::TxImmReleaseContext( HIMC himc )
{
    ImmReleaseContext(m_pDuiRichEdit->GetContainer()->GetHostHwnd(),himc);
}

HRESULT CDuiTextHost::TxGetSelectionBarWidth( LONG *plSelBarWidth )
{
    *plSelBarWidth=0;
    return S_OK;
}

BOOL CDuiTextHost::Init(CDuiRichEdit* pDuiRichEdit)
{
    IUnknown *pUnk;
    HRESULT hr;

    m_pDuiRichEdit=pDuiRichEdit;

    // Create Text Services component
	if(FAILED(CDuiTextServiceHelper::getSingleton().CreateTextServices(NULL, this, &pUnk))) return FALSE;

    hr = pUnk->QueryInterface(IID_ITextServices,(void **)&pserv);

    pUnk->Release();

	return SUCCEEDED(hr);
}


//////////////////////////////////////////////////////////////////////////
// dui interface

CDuiRichEdit::CDuiRichEdit()
    :m_pTxtHost(NULL)
    ,m_fTransparent(0)
    ,m_fRich(1)
    ,m_fSaveSelection(TRUE)
    ,m_fVertical(FALSE)
    ,m_fWordWrap(FALSE)
    ,m_fAllowBeep(FALSE)
    ,m_fEnableAutoWordSel(TRUE)
    ,m_fWantTab(FALSE)
    ,m_fSingleLineVCenter(TRUE)
    ,m_fScrollPending(FALSE)
	,m_fEnableDragDrop(FALSE)
    ,m_cchTextMost(cInitTextMax)
    ,m_chPasswordChar(_T('*'))
    ,m_lAccelPos(-1)
    ,m_dwStyle(ES_LEFT|ES_AUTOHSCROLL)
    ,m_rcInsetPixel(2,2,2,2)
	,m_dwDbcsReadByte(0)
{
	m_bTabStop=TRUE;
    m_sizelExtent.cx=m_sizelExtent.cy=0;
	addEvent(DUINM_RICHEDIT_NOTIFY);
}


LRESULT CDuiRichEdit::OnCreate( LPVOID )
{
    if(0 != __super::OnCreate(NULL)) return 1;

    InitDefaultCharFormat(&m_cfDef);
    InitDefaultParaFormat(&m_pfDef);

    m_pTxtHost=new CDuiTextHost;
    m_pTxtHost->AddRef();
    if(!m_pTxtHost->Init(this))
    {
        m_pTxtHost->Release();
        m_pTxtHost=NULL;
        return 1;
    }

	if(!m_fTransparent && m_style.m_crBg==CLR_INVALID && !m_pBgSkin) 
		m_style.m_crBg=0xFFFFFF; 
	//inplace activate
	m_pTxtHost->GetTextService()->OnTxInPlaceActivate(NULL);
	//默认没有焦点
	m_pTxtHost->m_fUiActive=FALSE;
	m_pTxtHost->GetTextService()->OnTxUIDeactivate();
	m_pTxtHost->GetTextService()->TxSendMessage(WM_KILLFOCUS, 0, 0, 0);

	// set IME
	DWORD dw = DuiSendMessage(EM_GETLANGOPTIONS);
	dw |= IMF_AUTOKEYBOARD | IMF_DUALFONT;
	dw &= ~IMF_AUTOFONT;
	DuiSendMessage(EM_SETLANGOPTIONS, 0, dw);

    SetWindowText(DUI_CT2W(GetInnerText()));

	//register droptarget
	OnEnableDragDrop( !(m_dwStyle&ES_READONLY) & m_fEnableDragDrop);
    return 0;
}

void CDuiRichEdit::OnDestroy()
{
	OnEnableDragDrop(FALSE);
    __super::OnDestroy();

    if(m_pTxtHost)
    {
		m_pTxtHost->GetTextService()->OnTxInPlaceDeactivate();
        m_pTxtHost->Release();
        m_pTxtHost=NULL;
    }
}


void CDuiRichEdit::OnPaint( CDCHandle dc )
{
    int nSaveDC=dc.SaveDC();
    CRect rcClient,rcClip;
    GetClient(&rcClient);
    dc.GetClipBox(rcClip);
    rcClip.IntersectRect(rcClient,rcClip);
    ALPHAINFO ai;
    if(GetContainer()->IsTranslucent())
    {
        CGdiAlpha::AlphaBackup(dc,&rcClient,ai);
    }

    RECTL rcL= {rcClient.left,rcClient.top,rcClient.right,rcClient.bottom};
    m_pTxtHost->GetTextService()->TxDraw(
        DVASPECT_CONTENT,  		// Draw Aspect
        /*-1*/0,						// Lindex
        NULL,					// Info for drawing optimazation
        NULL,					// target device information
        dc,			// Draw device HDC
        NULL, 				   	// Target device HDC
        &rcL,			// Bounding client rectangle
        NULL, 			// Clipping rectangle for metafiles
        &rcClient,		// Update rectangle
        NULL, 	   				// Call back function
        NULL,					// Call back parameter
		TXTVIEW_ACTIVE);

    if(GetContainer()->IsTranslucent())
    {
        CGdiAlpha::AlphaRestore(dc,ai);
    }
    dc.RestoreDC(nSaveDC);
}

void CDuiRichEdit::OnSetDuiFocus()
{
    __super::OnSetDuiFocus();

	CRect rcClient;
	GetClient(&rcClient);
	if(GetParent()) GetParent()->OnSetCaretValidateRect(&rcClient);

    if(m_pTxtHost)
    {
		m_pTxtHost->m_fUiActive=TRUE;
		m_pTxtHost->GetTextService()->OnTxUIActivate();
        m_pTxtHost->GetTextService()->TxSendMessage(WM_SETFOCUS, 0, 0, 0);
    }
}

void CDuiRichEdit::OnKillDuiFocus()
{
    __super::OnKillDuiFocus();
    if(m_pTxtHost)
    {
		m_pTxtHost->m_fUiActive=FALSE;
		m_pTxtHost->GetTextService()->OnTxUIDeactivate();
		m_pTxtHost->GetTextService()->TxSendMessage(WM_KILLFOCUS, 0, 0, 0);
        m_pTxtHost->TxShowCaret(FALSE);
    }
}

void CDuiRichEdit::OnDuiTimer( char idEvent )
{
    if(idEvent==TIMER_INVALIDATE)
    {
        NotifyInvalidate();
        KillDuiTimer(idEvent);
    }
    else
    {
        __super::OnDuiTimer(idEvent);
    }
}

void CDuiRichEdit::OnDuiTimerEx( UINT_PTR idEvent )
{
    m_pTxtHost->GetTextService()->TxSendMessage(WM_TIMER,idEvent,0,NULL);
}


BOOL CDuiRichEdit::OnScroll( BOOL bVertical,UINT uCode,int nPos )
{
    if(m_fScrollPending) return FALSE;
    LRESULT lresult=-1;
    m_fScrollPending=TRUE;
    m_pTxtHost->GetTextService()->TxSendMessage(bVertical?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(uCode,nPos),0,&lresult);
    m_fScrollPending=FALSE;
    __super::OnScroll(bVertical,uCode,nPos);
    return lresult==0;
}

BOOL CDuiRichEdit::OnDuiSetCursor(const CPoint &pt)
{
    CRect rcClient;
    GetClient(&rcClient);
    if(!rcClient.PtInRect(pt))
        return FALSE;

    HDC hdc=GetDC(GetContainer()->GetHostHwnd());
    m_pTxtHost->GetTextService()->OnTxSetCursor(
        DVASPECT_CONTENT,
        -1,
        NULL,
        NULL,
        hdc,
        NULL,
        &rcClient,
        pt.x,
        pt.y);
    ReleaseDC(GetContainer()->GetHostHwnd(),hdc);
    return TRUE;
}

BOOL CDuiRichEdit::DuiWndProc( UINT uMsg,WPARAM wParam,LPARAM lParam,LRESULT & lResult )
{
    if(m_pTxtHost && m_pTxtHost->GetTextService())
    {//WM_IME_CHAR消息不处理。
       if(m_pTxtHost->GetTextService()->TxSendMessage(uMsg,wParam,lParam,&lResult)==S_OK)
		{
			SetMsgHandled(TRUE);
			return TRUE;
		}
    }
    return __super::DuiWndProc(uMsg,wParam,lParam,lResult);
}

HRESULT CDuiRichEdit::InitDefaultCharFormat( CHARFORMAT2W* pcf ,HFONT hFont)
{
    CDCHandle dc=GetDuiDC(NULL,OLEDC_NODRAW);
    DUIASSERT(dc);
    BeforePaintEx(dc);

    LOGFONT lf;
    if(hFont==NULL) hFont=(HFONT)GetCurrentObject(dc,OBJ_FONT);
    ::GetObject(hFont, sizeof(LOGFONT), &lf);
    GetTextMetrics(dc,&m_tmFont);

    memset(pcf, 0, sizeof(CHARFORMAT2W));
    pcf->cbSize = sizeof(CHARFORMAT2W);
	pcf->dwMask = CFM_SIZE | CFM_OFFSET | CFM_FACE | CFM_CHARSET | CFM_COLOR | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE;

    pcf->crTextColor = GetTextColor(dc);
    LONG yPixPerInch = GetDeviceCaps(dc, LOGPIXELSY);
    pcf->yHeight = -abs(lf.lfHeight * LY_PER_INCH / yPixPerInch);
    pcf->yOffset = 0;
    pcf->dwEffects = 0;
    if(lf.lfWeight >= FW_BOLD)
        pcf->dwEffects |= CFE_BOLD;
    if(lf.lfItalic)
        pcf->dwEffects |= CFE_ITALIC;
    if(lf.lfUnderline)
        pcf->dwEffects |= CFE_UNDERLINE;
	pcf->bCharSet = lf.lfCharSet;
    pcf->bPitchAndFamily = lf.lfPitchAndFamily;
#ifdef _UNICODE
    _tcscpy(pcf->szFaceName, lf.lfFaceName);
#else
    //need to thunk pcf->szFaceName to a standard char string.in this case it's easy because our thunk is also our copy
    MultiByteToWideChar(CP_ACP, 0, lf.lfFaceName, LF_FACESIZE, pcf->szFaceName, LF_FACESIZE) ;
#endif

    ReleaseDuiDC(dc);
    return S_OK;
}

HRESULT CDuiRichEdit::InitDefaultParaFormat( PARAFORMAT2* ppf )
{
    memset(ppf, 0, sizeof(PARAFORMAT2));
    ppf->cbSize = sizeof(PARAFORMAT2);
    ppf->dwMask = PFM_ALL;
    ppf->cTabCount = 1;
    ppf->rgxTabs[0] = lDefaultTab;

    if(m_dwStyle&ES_CENTER)
        ppf->wAlignment=PFA_CENTER;
    else if(m_dwStyle&ES_RIGHT)
        ppf->wAlignment=PFA_RIGHT;
    else
        ppf->wAlignment = PFA_LEFT;

    return S_OK;
}



HRESULT CDuiRichEdit::OnTxNotify( DWORD iNotify,LPVOID pv )
{
    DUIRICHEDITNOTIFY nms;
    nms.hdr.code=DUINM_RICHEDIT_NOTIFY;
    nms.hdr.hDuiWnd=m_hDuiWnd;
    nms.hdr.idFrom=GetCmdID();
	nms.hdr.pszNameFrom=GetName();
    nms.iNotify=iNotify;
    nms.pv=pv;
    return DuiNotify((LPDUINMHDR)&nms);
}
//////////////////////////////////////////////////////////////////////////
//	richedit interfaces
BOOL CDuiRichEdit::GetWordWrap( void )
{
    return m_fWordWrap;
}

void CDuiRichEdit::SetWordWrap( BOOL fWordWrap )
{
    m_fWordWrap = fWordWrap;
    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_WORDWRAP, fWordWrap ? TXTBIT_WORDWRAP : 0);
}

BOOL CDuiRichEdit::GetReadOnly()
{
    return (m_dwStyle & ES_READONLY) != 0;
}

BOOL CDuiRichEdit::SetReadOnly(BOOL bReadOnly)
{
    return 0 != DuiSendMessage(EM_SETREADONLY, bReadOnly);
}

LONG CDuiRichEdit::GetLimitText()
{
    return m_cchTextMost;
}

BOOL CDuiRichEdit::SetLimitText(int nLength)
{
    return 0 != DuiSendMessage(EM_EXLIMITTEXT, nLength);
}

WORD CDuiRichEdit::GetDefaultAlign()
{
    return m_pfDef.wAlignment;
}

void CDuiRichEdit::SetDefaultAlign( WORD wNewAlign )
{
    m_pfDef.wAlignment = wNewAlign;

    // Notify control of property change
    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE, 0);
}

BOOL CDuiRichEdit::GetRichTextFlag()
{
    return m_fRich;
}

void CDuiRichEdit::SetRichTextFlag( BOOL fRich )
{
    m_fRich = fRich;

    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_RICHTEXT,
            fRich ? TXTBIT_RICHTEXT : 0);
}

LONG CDuiRichEdit::GetDefaultLeftIndent()
{
    return m_pfDef.dxOffset;
}

void CDuiRichEdit::SetDefaultLeftIndent( LONG lNewIndent )
{
    m_pfDef.dxOffset = lNewIndent;

    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE, 0);
}

BOOL CDuiRichEdit::SetSaveSelection( BOOL fSaveSelection )
{
    BOOL fResult = fSaveSelection;

    m_fSaveSelection = fSaveSelection;

    // notify text services of property change
    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_SAVESELECTION,
            m_fSaveSelection ? TXTBIT_SAVESELECTION : 0);

    return fResult;
}

HRESULT CDuiRichEdit::DefAttributeProc(const CDuiStringA & strAttribName,const CDuiStringA & strValue, BOOL bLoading)
{
    HRESULT hRet=S_FALSE;
    DWORD dwBit=0,dwMask=0;
    //hscrollbar
    if(strAttribName=="hscrollbar")
    {
        if(strValue=="0")
            m_dwStyle&=~WS_HSCROLL;
        else
            m_dwStyle|=WS_HSCROLL;
        dwBit|=TXTBIT_SCROLLBARCHANGE;
        dwMask|=TXTBIT_SCROLLBARCHANGE;
    }
    //vscrollbar
    else if(strAttribName=="vscrollbar")
    {
        if(strValue=="0")
            m_dwStyle&=~WS_VSCROLL;
        else
            m_dwStyle|=WS_VSCROLL;
        dwBit|=TXTBIT_SCROLLBARCHANGE;
        dwMask|=TXTBIT_SCROLLBARCHANGE;
    }
    //auto hscroll
    else if(strAttribName=="autohscroll")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_AUTOHSCROLL;
        else
            m_dwStyle|=ES_AUTOHSCROLL;
        dwBit|=TXTBIT_SCROLLBARCHANGE;
        dwMask|=TXTBIT_SCROLLBARCHANGE;
    }
    //auto hscroll
    else if(strAttribName=="autovscroll")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_AUTOVSCROLL;
        else
            m_dwStyle|=ES_AUTOVSCROLL;
        dwBit|=TXTBIT_SCROLLBARCHANGE;
        dwMask|=TXTBIT_SCROLLBARCHANGE;
    }
    //multilines
    else if(strAttribName=="multilines" && strValue!="0")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_MULTILINE;
        else
            m_dwStyle|=ES_MULTILINE,dwBit|=TXTBIT_MULTILINE;
        dwMask|=TXTBIT_MULTILINE;
    }
    //readonly
    else if(strAttribName=="readonly")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_READONLY;
        else
            m_dwStyle|=ES_READONLY,dwBit|=TXTBIT_READONLY;
        dwMask|=TXTBIT_READONLY;
		if(!bLoading)
		{//update dragdrop
			OnEnableDragDrop(!(m_dwStyle&ES_READONLY) && m_fEnableDragDrop);
		}
    }
    //want return
    else if(strAttribName=="wantreturn")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_WANTRETURN;
        else
            m_dwStyle|=ES_WANTRETURN;
    }
    //password
    else if(strAttribName=="password")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_PASSWORD;
        else
            m_dwStyle|=ES_PASSWORD,dwBit|=TXTBIT_USEPASSWORD;
        dwMask|=TXTBIT_USEPASSWORD;
    }
    //number
    else if(strAttribName=="number")
    {
        if(strValue=="0")
            m_dwStyle&=~ES_NUMBER;
        else
            m_dwStyle|=ES_NUMBER;
    }
    //password char
    else if(strAttribName=="passwordchar")
    {
        CDuiStringT strValueT=DUI_CA2T(strValue,CP_UTF8);
        m_chPasswordChar=strValueT[0];
    }
    //align
    else if(strAttribName=="align")
    {
        if(strValue=="center") m_dwStyle|=ES_CENTER;
        else if(strValue=="right") m_dwStyle|=ES_RIGHT;
        else m_dwStyle|=ES_LEFT;
    }
	//enabledragdrop
	else if(strAttribName=="enabledragdrop")
	{
		if(strValue=="0")
		{
			m_fEnableDragDrop=FALSE;
		}else
		{
			m_fEnableDragDrop=TRUE;
		}
		if(!bLoading)
		{
			OnEnableDragDrop( !(m_dwStyle&ES_READONLY) & m_fEnableDragDrop);
		}
	}
    else
    {
        hRet=__super::DefAttributeProc(strAttribName,strValue,bLoading);
    }
    if(!bLoading)
    {
        m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(dwMask,dwBit);
        hRet=TRUE;
    }
    return hRet;
}

void CDuiRichEdit::OnLButtonDown( UINT nFlags, CPoint point )
{
    if(m_hDuiWnd!=GetContainer()->GetDuiFocus())
    {
        SetDuiFocus();
    }
    m_pTxtHost->GetTextService()->TxSendMessage(GetCurDuiMsg()->uMsg,GetCurDuiMsg()->wParam,GetCurDuiMsg()->lParam,NULL);
}

void CDuiRichEdit::OnLButtonUp( UINT nFlags, CPoint point )
{
    m_pTxtHost->GetTextService()->TxSendMessage(GetCurDuiMsg()->uMsg,GetCurDuiMsg()->wParam,GetCurDuiMsg()->lParam,NULL);
}

enum{
	MENU_CUT =	1,
	MENU_COPY,
	MENU_PASTE,
	MENU_DEL,
	MENU_SELALL,
};

void CDuiRichEdit::OnRButtonDown( UINT nFlags, CPoint point )
{
	if(NotifyContextMenu(point)) return;//用户自己响应右键
	SetDuiFocus();
	//弹出默认编辑窗菜单
	pugi::xml_node xmlMenu=DuiSystem::getSingleton().GetEditMenuTemplate().first_child();
	if(xmlMenu)
	{
		CDuiMenu menu;
		if(menu.LoadMenu(xmlMenu))
		{
			CRect rcCantainer=GetContainer()->GetContainerRect();
			point.Offset(rcCantainer.TopLeft());
			HWND hHost=GetContainer()->GetHostHwnd();
			::ClientToScreen(hHost,&point);
			BOOL canPaste=DuiSendMessage(EM_CANPASTE,0);
			DWORD dwStart=0,dwEnd=0;
			DuiSendMessage(EM_GETSEL,(WPARAM)&dwStart,(LPARAM)&dwEnd);
			BOOL hasSel=dwStart<dwEnd;
			UINT uLen=DuiSendMessage(WM_GETTEXTLENGTH ,0,0);
			BOOL bReadOnly=m_dwStyle&ES_READONLY;
			EnableMenuItem(menu.m_hMenu,MENU_CUT,MF_BYCOMMAND|((hasSel&&(!bReadOnly))?0:MF_GRAYED));
			EnableMenuItem(menu.m_hMenu,MENU_COPY,MF_BYCOMMAND|(hasSel?0:MF_GRAYED));
			EnableMenuItem(menu.m_hMenu,MENU_PASTE,MF_BYCOMMAND|((canPaste&&(!bReadOnly))?0:MF_GRAYED));
			EnableMenuItem(menu.m_hMenu,MENU_DEL,MF_BYCOMMAND|((hasSel&&(!bReadOnly))?0:MF_GRAYED));
			EnableMenuItem(menu.m_hMenu,MENU_SELALL,MF_BYCOMMAND|((uLen>0)?0:MF_GRAYED));

			UINT uCmd=menu.TrackPopupMenu(TPM_RETURNCMD|TPM_LEFTALIGN,point.x,point.y,hHost);
			switch(uCmd)
			{
			case MENU_CUT:
				DuiSendMessage(WM_CUT);
				break;
			case MENU_COPY:
				DuiSendMessage(WM_COPY);
				break;
			case MENU_PASTE:
				DuiSendMessage(WM_PASTE);
				break;
			case MENU_DEL:
				DuiSendMessage(EM_REPLACESEL,0,(LPARAM)_T(""));
				break;
			case MENU_SELALL:
				DuiSendMessage(EM_SETSEL,0,-1);
				break;
			default:
				break;
			}

		}

	}
}

void CDuiRichEdit::OnMouseMove( UINT nFlags, CPoint point )
{
    m_pTxtHost->GetTextService()->TxSendMessage(GetCurDuiMsg()->uMsg,GetCurDuiMsg()->wParam,GetCurDuiMsg()->lParam,NULL);
}

void CDuiRichEdit::OnKeyDown( UINT nChar, UINT nRepCnt, UINT nFlags )
{
    if(nChar==VK_RETURN && !(m_dwStyle&ES_WANTRETURN) && !(GetKeyState(VK_CONTROL)&0x8000))
    {
		SetMsgHandled(FALSE);
        return;
    }
    m_pTxtHost->GetTextService()->TxSendMessage(GetCurDuiMsg()->uMsg,GetCurDuiMsg()->wParam,GetCurDuiMsg()->lParam,NULL);
}

#define CTRL(_ch) (_ch - 'A' + 1)

void CDuiRichEdit::OnChar( UINT nChar, UINT nRepCnt, UINT nFlags )
{
    switch(nChar)
    {
        // Ctrl-Return generates Ctrl-J (LF), treat it as an ordinary return
    case CTRL('J'):
    case VK_RETURN:
        if(!(GetKeyState(VK_CONTROL) & 0x8000)
                && !(m_dwStyle & ES_WANTRETURN))
            return;
        break;

    case VK_TAB:
        if(!m_fWantTab && !(GetKeyState(VK_CONTROL) & 0x8000))
            return;
        break;
    default:
        if(m_dwStyle&ES_NUMBER && !isdigit(nChar) && nChar!='-' && nChar!='.' && nChar!=',')
            return;
#ifndef _UNICODE
		if(m_dwDbcsReadByte==0)
		{
			if(IsDBCSLeadByte(nChar))
			{
				m_dwDbcsReadByte=nChar;
				return;
			}
		}else
		{
			nChar=MAKEWORD(nChar,m_dwDbcsReadByte);
			m_pTxtHost->GetTextService()->TxSendMessage(WM_IME_CHAR,nChar,0,NULL);
			m_dwDbcsReadByte=0;
			return;
		}
#endif//_UNICODE
        break;
    }
    m_pTxtHost->GetTextService()->TxSendMessage(GetCurDuiMsg()->uMsg,GetCurDuiMsg()->wParam,GetCurDuiMsg()->lParam,NULL);
}

LRESULT CDuiRichEdit::OnNcCalcSize( BOOL bCalcValidRects, LPARAM lParam )
{
    __super::OnNcCalcSize(bCalcValidRects,lParam);

    m_siHoz.nPage=m_rcClient.Width()-m_rcInsetPixel.left-m_rcInsetPixel.right;
    m_siVer.nPage=m_rcClient.Height()-m_rcInsetPixel.top-m_rcInsetPixel.bottom;

    if(m_pTxtHost)
    {
        HDC hdc=GetDC(GetContainer()->GetHostHwnd());
        LONG xPerInch = ::GetDeviceCaps(hdc, LOGPIXELSX);
        LONG yPerInch =	::GetDeviceCaps(hdc, LOGPIXELSY);
        m_sizelExtent.cx = DXtoHimetricX(m_siHoz.nPage, xPerInch);
        m_sizelExtent.cy = DYtoHimetricY(m_siVer.nPage, yPerInch);

        m_rcInset.left=DXtoHimetricX(m_rcInsetPixel.left,xPerInch);
        m_rcInset.right=DXtoHimetricX(m_rcInsetPixel.right,xPerInch);
        if(!m_fRich && m_fSingleLineVCenter && !(m_dwStyle&ES_MULTILINE))
        {
            m_rcInset.top=
                m_rcInset.bottom=DYtoHimetricY(m_siVer.nPage-m_tmFont.tmHeight,yPerInch)/2;
        }
        else
        {
            m_rcInset.top=DYtoHimetricY(m_rcInsetPixel.top,yPerInch);
            m_rcInset.bottom=DYtoHimetricY(m_rcInsetPixel.bottom,yPerInch);
        }
        ReleaseDC(GetContainer()->GetHostHwnd(),hdc);
		BOOL bFocus = GetContainer()->GetDuiFocus()==m_hDuiWnd;
		if(bFocus) KillDuiFocus();
        m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_EXTENTCHANGE|TXTBIT_CLIENTRECTCHANGE, TXTBIT_EXTENTCHANGE|TXTBIT_CLIENTRECTCHANGE);
		if(bFocus) SetDuiFocus();
    }
    return 0;
}

LRESULT CDuiRichEdit::OnSetReadOnly( UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	return SUCCEEDED(SetAttribute("readonly",wParam?"1":"0"));
}

LRESULT CDuiRichEdit::OnSetLimitText( UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    if(wParam==0) m_cchTextMost=cInitTextMax;
    else m_cchTextMost=(DWORD)wParam;
    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_MAXLENGTHCHANGE, TXTBIT_MAXLENGTHCHANGE);
    return 1;
}

LRESULT CDuiRichEdit::OnSetCharFormat( UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    if(!FValidCF((CHARFORMAT2W *) lParam))
    {
        return 0;
    }

    if(wParam & SCF_SELECTION)
        m_pTxtHost->GetTextService()->TxSendMessage(uMsg,wParam,lParam,NULL);
    else
    {
        m_cfDef=*(CHARFORMAT2W *)lParam;
        m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE,TXTBIT_CHARFORMATCHANGE);
    }
    return 1;
}

LRESULT CDuiRichEdit::OnSetParaFormat( UINT uMsg, WPARAM wparam, LPARAM lparam )
{
    if(!FValidPF((PARAFORMAT *) lparam))
    {
        return 0;
    }

    // check to see if we're setting the default.
    // either SCF_DEFAULT will be specified *or* there is no
    // no text in the document (richedit1.0 behaviour).
    if (!(wparam & SCF_DEFAULT))
    {
        HRESULT hr = m_pTxtHost->GetTextService()->TxSendMessage(WM_GETTEXTLENGTH, 0, 0, 0);

        if (hr == 0)
        {
            wparam |= SCF_DEFAULT;
        }
    }

    if(wparam & SCF_DEFAULT)
    {
        m_pfDef=*(PARAFORMAT2 *)lparam;
        m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_PARAFORMATCHANGE,TXTBIT_PARAFORMATCHANGE);
    }
    else
    {
        m_pTxtHost->GetTextService()->TxSendMessage(uMsg,wparam,lparam,NULL);	// Change selection format
    }
    return 1;
}

LRESULT CDuiRichEdit::OnSetText(UINT uMsg,WPARAM wparam,LPARAM lparam)
{
    // For RichEdit 1.0, the max text length would be reset by a settext so
    // we follow pattern here as well.

    HRESULT hr = m_pTxtHost->GetTextService()->TxSendMessage(uMsg, wparam, lparam, 0);

    if (FAILED(hr)) return 0;
    // Update succeeded.
    LONG cNewText = lparam?_tcslen((LPCTSTR) lparam):0;

    // If the new text is greater than the max set the max to the new
    // text length.
    if (cNewText > m_cchTextMost)
    {
        m_cchTextMost = cNewText;
    }
    return 1;
}

void CDuiRichEdit::OnSetFont( HFONT font, BOOL bRedraw )
{
    if(SUCCEEDED(InitDefaultCharFormat(&m_cfDef, font)))
    {
        m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE,
                TXTBIT_CHARFORMATCHANGE);
    }
}

BOOL CDuiRichEdit::SetWindowText( LPCWSTR lpszText )
{
    return (BOOL)DuiSendMessage(WM_SETTEXT,0,(LPARAM)lpszText);
}

int CDuiRichEdit::GetWindowText(LPWSTR lpString,
                                int nMaxCount
                               )
{
    return (int)DuiSendMessage(WM_GETTEXT,(WPARAM)nMaxCount,(LPARAM)lpString);
}

int CDuiRichEdit::GetWindowTextLength()
{
    return (int)DuiSendMessage(WM_GETTEXTLENGTH);
}

void CDuiRichEdit::ReplaceSel(LPWSTR pszText,BOOL bCanUndo)
{
    DuiSendMessage(EM_REPLACESEL,(WPARAM)bCanUndo,(LPARAM)pszText);
}

void CDuiRichEdit::SetSel(DWORD dwSelection, BOOL bNoScroll)
{
    DuiSendMessage(EM_SETSEL, LOWORD(dwSelection), HIWORD(dwSelection));
    if(!bNoScroll)
        DuiSendMessage(EM_SCROLLCARET, 0, 0L);
}

LRESULT CDuiRichEdit::OnSetTextColor( const CDuiStringA &  strValue,BOOL bLoading )
{
    m_style.m_crText=CDuiObject::HexStringToColor(strValue);
    if(!bLoading)
    {
        SetDefaultTextColor(m_style.m_crText);
    }
    return S_OK;
}

COLORREF CDuiRichEdit::SetDefaultTextColor( COLORREF cr )
{
    COLORREF crOld=m_cfDef.crTextColor;
    m_cfDef.crTextColor=cr;
    m_pTxtHost->GetTextService()->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE);
    return crOld;
}

void CDuiRichEdit::OnEnableDragDrop( BOOL bEnable )
{
	if(bEnable)
	{
		CDuiRicheditDropTarget *pDropTarget=new CDuiRicheditDropTarget(m_pTxtHost->GetTextService());
		GetContainer()->RegisterDragDrop(m_hDuiWnd,pDropTarget);
		pDropTarget->Release();
	}else
	{
		GetContainer()->RevokeDragDrop(m_hDuiWnd);
	}
}

}//namespace DuiEngine