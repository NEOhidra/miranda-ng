/*

Jabber Protocol Plugin for Miranda NG

Copyright (c) 2002-04  Santithorn Bunchua
Copyright (c) 2005-12  George Hazan
Copyright (c) 2007     Maxim Mluhov
Copyright (c) 2012-14  Miranda NG project

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "jabber.h"

#include <windns.h>   // requires Windows Platform SDK

#include "jabber_list.h"
#include "jabber_iq.h"
#include "jabber_secur.h"
#include "jabber_caps.h"
#include "jabber_privacy.h"
#include "jabber_rc.h"
#include "jabber_proto.h"

#ifndef DNS_TYPE_SRV
#define DNS_TYPE_SRV 0x0021
#endif

// <iq/> identification number for various actions
// for JABBER_REGISTER thread
int iqIdRegGetReg;
int iqIdRegSetReg;

// XML Console
#define JCPF_IN      0x01UL
#define JCPF_OUT     0x02UL
#define JCPF_ERROR   0x04UL

static VOID CALLBACK JabberDummyApcFunc(DWORD_PTR)
{
	return;
}

struct JabberPasswordDlgParam
{
	CJabberProto* pro;

	BOOL   saveOnlinePassword;
	WORD   dlgResult;
	TCHAR  onlinePassword[128];
	HANDLE hEventPasswdDlg;
	TCHAR *ptszJid;
};

static INT_PTR CALLBACK JabberPasswordDlgProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	JabberPasswordDlgParam* param = (JabberPasswordDlgParam*)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG:
		TranslateDialogDefault(hwndDlg);
		{
			param = (JabberPasswordDlgParam*)lParam;
			SetWindowLongPtr(hwndDlg, GWLP_USERDATA, lParam);

			TCHAR text[512];
			mir_sntprintf(text, SIZEOF(text), TranslateT("Enter password for %s"), param->ptszJid);
			SetDlgItemText(hwndDlg, IDC_JID, text);

			int bSavePassword = param->pro->getByte("SaveSessionPassword", 0);
			CheckDlgButton(hwndDlg, IDC_SAVEPASSWORD, (bSavePassword) ? BST_CHECKED : BST_UNCHECKED);
		}
		return TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_SAVE_PERM:
			EnableWindow(GetDlgItem(hwndDlg,IDC_SAVEPASSWORD),!IsDlgButtonChecked(hwndDlg, IDC_SAVE_PERM));
			break;
		case IDOK:
			param->saveOnlinePassword = IsDlgButtonChecked(hwndDlg, IDC_SAVEPASSWORD);
			param->pro->setByte("SaveSessionPassword", param->saveOnlinePassword);

			GetDlgItemText(hwndDlg, IDC_PASSWORD, param->onlinePassword, SIZEOF(param->onlinePassword));
			{
				BOOL savePassword = IsDlgButtonChecked(hwndDlg, IDC_SAVE_PERM);
				param->pro->setByte("SavePassword", savePassword);
				if (savePassword) {
					param->pro->setTString("Password", param->onlinePassword);
					param->saveOnlinePassword = TRUE;
				}
			}
			// Fall through
		case IDCANCEL:
			param->dlgResult = LOWORD(wParam);
			SetEvent(param->hEventPasswdDlg);
			DestroyWindow(hwndDlg);
			return TRUE;
		}
		break;
	}

	return FALSE;
}

static VOID CALLBACK JabberPasswordCreateDialogApcProc(void* param)
{
	CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_PASSWORD), NULL, JabberPasswordDlgProc, (LPARAM)param);
}

/////////////////////////////////////////////////////////////////////////////////////////
// Jabber keep-alive thread

void CJabberProto::OnPingReply(HXML, CJabberIqInfo *pInfo)
{
	if (!pInfo)
		return;
	if (pInfo->GetIqType() == JABBER_IQ_TYPE_FAIL) {
		// disconnect because of timeout
		m_ThreadInfo->send("</stream:stream>");
		m_ThreadInfo->shutdown();
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static int CompareDNS(const DNS_SRV_DATAA* dns1, const DNS_SRV_DATAA* dns2)
{
	return (int)dns1->wPriority - (int)dns2->wPriority;
}

void ThreadData::xmpp_client_query(void)
{
	if (inet_addr(server) != INADDR_NONE)
		return;

	char temp[256];
	mir_snprintf(temp, SIZEOF(temp), "_xmpp-client._tcp.%s", server);

	DNS_RECORDA *results = NULL;
	DNS_STATUS status = DnsQuery_A(temp, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, (PDNS_RECORD *)&results, NULL);
	if (SUCCEEDED(status) && results) {
		LIST<DNS_SRV_DATAA> dnsList(5, CompareDNS);

		for (DNS_RECORDA *rec = results; rec; rec = rec->pNext) {
			if (rec->Data.Srv.pNameTarget && rec->wType == DNS_TYPE_SRV)
				dnsList.insert(&rec->Data.Srv);
		}

		for (int i=0; i < dnsList.getCount(); i++) {
			WORD dnsPort = port == 0 || port == 5222 ? dnsList[i]->wPort : port;
			char* dnsHost = dnsList[i]->pNameTarget;

			proto->debugLogA("%s%s resolved to %s:%d", "_xmpp-client._tcp.", server, dnsHost, dnsPort);
			s = proto->WsConnect(dnsHost, dnsPort);
			if (s) {
				mir_snprintf(manualHost, SIZEOF(manualHost), "%s", dnsHost);
				port = dnsPort;
				break;
		}	}
		DnsRecordListFree(results, DnsFreeRecordList);
	}
	else proto->debugLogA("%s not resolved", temp);
}

void CJabberProto::xmlStreamInitialize(char *szWhich)
{
	debugLogA("Stream will be initialized %s", szWhich);
	if (m_szXmlStreamToBeInitialized)
		free(m_szXmlStreamToBeInitialized);
	m_szXmlStreamToBeInitialized = _strdup(szWhich);
}

void CJabberProto::xmlStreamInitializeNow(ThreadData* info)
{
	debugLogA("Stream is initializing %s",
		m_szXmlStreamToBeInitialized ? m_szXmlStreamToBeInitialized : "after connect");
	if (m_szXmlStreamToBeInitialized) {
		free(m_szXmlStreamToBeInitialized);
		m_szXmlStreamToBeInitialized = NULL;
	}

	HXML n = xi.createNode(_T("xml"), NULL, 1) << XATTR(_T("version"), _T("1.0")) << XATTR(_T("encoding"), _T("UTF-8"));

	HXML stream = n << XCHILDNS(_T("stream:stream"), _T("jabber:client")) << XATTR(_T("to"), _A2T(info->server))
		<< XATTR(_T("xmlns:stream"), _T("http://etherx.jabber.org/streams"));

	if (m_tszSelectedLang)
		xmlAddAttr(stream, _T("xml:lang"), m_tszSelectedLang);

	if (!m_options.Disable3920auth)
		xmlAddAttr(stream, _T("version"), _T("1.0"));

	LPTSTR xmlQuery = xi.toString(n, NULL);
	char* buf = mir_utf8encodeT(xmlQuery);
	int bufLen = (int)strlen(buf);
	if (bufLen > 2) {
		strdel(buf + bufLen - 2, 1);
		bufLen--;
	}

	info->send(buf, bufLen);
	mir_free(buf);
	xi.freeMem(xmlQuery);
	xi.destroyNode(n);
}

void CJabberProto::ServerThread(ThreadData* info)
{
	char* buffer;
	int  datalen;
	int  oldStatus;
	ptrA szValue;
	ptrT tszValue;

	debugLogA("Thread started: type=%d", info->type);

	info->resolveID = -1;
	info->auth = NULL;

	if (m_options.ManualConnect == TRUE) {
		ptrA szManualHost( getStringA("ManualHost"));
		if (szManualHost != NULL)
			strncpy_s(info->manualHost, SIZEOF(info->manualHost), szManualHost, _TRUNCATE);

		info->port = getWord("ManualPort", JABBER_DEFAULT_PORT);
	}
	else info->port = getWord("Port", JABBER_DEFAULT_PORT);

	info->useSSL = m_options.UseSSL;

	if (info->type == JABBER_SESSION_NORMAL) {
		// Normal server connection, we will fetch all connection parameters
		// e.g. username, password, etc. from the database.
		if (m_ThreadInfo != NULL) {
			// Will not start another connection thread if a thread is already running.
			// Make APC call to the main thread. This will immediately wake the thread up
			// in case it is asleep in the reconnect loop so that it will immediately
			// reconnect.
			QueueUserAPC(JabberDummyApcFunc, m_ThreadInfo->hThread, 0);
			debugLogA("Thread ended, another normal thread is running");
LBL_Exit:
			delete info;
			return;
		}

		m_ThreadInfo = info;
		replaceStr(m_szStreamId, NULL);

		if ((tszValue = getTStringA("LoginName")) != NULL)
			_tcsncpy_s(info->username, SIZEOF(info->username), tszValue, _TRUNCATE);

		if (*rtrimt(info->username) == '\0') {
			DWORD dwSize = SIZEOF(info->username);
			if (GetUserName(info->username, &dwSize))
				setTString("LoginName", info->username);
			else
				info->username[0] = 0;
		}

		if (*rtrimt(info->username) == '\0') {
			debugLogA("Thread ended, login name is not configured");
			JLoginFailed(LOGINERR_BADUSERID);
LBL_FatalError:
			m_ThreadInfo = NULL;
			oldStatus = m_iStatus;
			m_iDesiredStatus = m_iStatus = ID_STATUS_OFFLINE;
			ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)oldStatus, m_iStatus);
			goto LBL_Exit;
		}

		if ((szValue = getStringA("LoginServer")) != NULL)
			strncpy_s(info->server, SIZEOF(info->server), szValue, _TRUNCATE);
		else {
			ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK);
			debugLogA("Thread ended, login server is not configured");
			goto LBL_FatalError;
		}

		if (m_options.HostNameAsResource) {
			DWORD dwCompNameLen = SIZEOF(info->resource)-1;
			if (!GetComputerName(info->resource, &dwCompNameLen))
				_tcscpy(info->resource, _T("Miranda"));
		}
		else {
			if ((tszValue = getTStringA("Resource")) != NULL)
				_tcsncpy_s(info->resource, SIZEOF(info->resource), tszValue, _TRUNCATE);
			else
				_tcscpy(info->resource, _T("Miranda"));
		}

		TCHAR jidStr[512];
		mir_sntprintf(jidStr, SIZEOF(jidStr), _T("%s@%S/%s"), info->username, info->server, info->resource);
		_tcsncpy(info->fullJID, jidStr, SIZEOF(info->fullJID)-1);

		if (m_options.SavePassword == FALSE) {
			if (*m_savedPassword) {
				_tcsncpy(info->password, m_savedPassword, SIZEOF(info->password));
				info->password[ SIZEOF(info->password)-1] = '\0';
			}
			else {
				mir_sntprintf(jidStr, SIZEOF(jidStr), _T("%s@%S"), info->username, info->server);

				JabberPasswordDlgParam param;
				param.pro = this;
				param.ptszJid = jidStr;
				param.hEventPasswdDlg = CreateEvent(NULL, FALSE, FALSE, NULL);
				CallFunctionAsync(JabberPasswordCreateDialogApcProc, &param);
				WaitForSingleObject(param.hEventPasswdDlg, INFINITE);
				CloseHandle(param.hEventPasswdDlg);

				if (param.dlgResult == IDCANCEL) {
					JLoginFailed(LOGINERR_BADUSERID);
					debugLogA("Thread ended, password request dialog was canceled");
					goto LBL_FatalError;
				}

				if (param.saveOnlinePassword) lstrcpy(m_savedPassword, param.onlinePassword);
				else *m_savedPassword = 0;

				_tcsncpy(info->password, param.onlinePassword, SIZEOF(info->password));
				info->password[ SIZEOF(info->password)-1] = '\0';
			}
		}
		else {
			TCHAR *passw = getTStringA(NULL, "Password");
			if (passw == NULL) {
				JLoginFailed(LOGINERR_BADUSERID);
				debugLogA("Thread ended, password is not configured");
				goto LBL_FatalError;
			}
			_tcsncpy(info->password, passw, SIZEOF(info->password));
			info->password[SIZEOF(info->password)-1] = '\0';
			mir_free(passw);
		}
	}

	else if (info->type == JABBER_SESSION_REGISTER) {
		// Register new user connection, all connection parameters are already filled-in.
		// Multiple thread allowed, although not possible :)
		// thinking again.. multiple thread should not be allowed
		info->reg_done = FALSE;
		SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 25, (LPARAM)TranslateT("Connecting..."));
		iqIdRegGetReg = -1;
		iqIdRegSetReg = -1;
	}
	else {
		debugLogA("Thread ended, invalid session type");
		goto LBL_FatalError;
	}

	int jabberNetworkBufferSize = 2048;
	if ((buffer=(char*)mir_alloc(jabberNetworkBufferSize + 1)) == NULL) {	// +1 is for '\0' when debug logging this buffer
		debugLogA("Cannot allocate network buffer, thread ended");
		if (info->type == JABBER_SESSION_NORMAL) {
			ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK);
		}
		else if (info->type == JABBER_SESSION_REGISTER) {
			SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)TranslateT("Error: Not enough memory"));
		}
		debugLogA("Thread ended, network buffer cannot be allocated");
		goto LBL_FatalError;
	}

	if (info->manualHost[0] == 0) {
		info->xmpp_client_query();
		if (info->s == NULL) {
			strncpy(info->manualHost, info->server, SIZEOF(info->manualHost));
			info->s = WsConnect(info->manualHost, info->port);
		}
	}
	else
		info->s = WsConnect(info->manualHost, info->port);

	debugLogA("Thread type=%d server='%s' port='%d'", info->type, info->manualHost, info->port);
	if (info->s == NULL) {
		debugLogA("Connection failed (%d)", WSAGetLastError());
		if (info->type == JABBER_SESSION_NORMAL) {
			if (m_ThreadInfo == info) {
				ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK);
		}	}
		else if (info->type == JABBER_SESSION_REGISTER)
			SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)TranslateT("Error: Cannot connect to the server"));

		debugLogA("Thread ended, connection failed");
		mir_free(buffer);
		goto LBL_FatalError;
	}

	// Determine local IP
	if (info->useSSL) {
		debugLogA("Intializing SSL connection");
		if (!CallService(MS_NETLIB_STARTSSL, (WPARAM)info->s, 0)) {
			debugLogA("SSL intialization failed");
			if (info->type == JABBER_SESSION_NORMAL) {
				ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_NONETWORK);
			}
			else if (info->type == JABBER_SESSION_REGISTER) {
				SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)TranslateT("Error: Cannot connect to the server"));
			}
			mir_free(buffer);
			info->close();
			debugLogA("Thread ended, SSL connection failed");
			goto LBL_FatalError;
	}	}

	// User may change status to OFFLINE while we are connecting above
	if (m_iDesiredStatus != ID_STATUS_OFFLINE || info->type == JABBER_SESSION_REGISTER) {

		if (info->type == JABBER_SESSION_NORMAL) {
			size_t len = _tcslen(info->username) + strlen(info->server)+1;
			m_szJabberJID = (TCHAR*)mir_alloc(sizeof(TCHAR)*(len+1));
			mir_sntprintf(m_szJabberJID, len+1, _T("%s@%S"), info->username, info->server);
			m_bSendKeepAlive = m_options.KeepAlive != 0;
			setTString("jid", m_szJabberJID); // store jid in database
		}

		xmlStreamInitializeNow(info);
		const TCHAR *tag = _T("stream:stream");

		debugLogA("Entering main recv loop");
		datalen = 0;

		// cache values
		DWORD dwConnectionKeepAliveInterval = m_options.ConnectionKeepAliveInterval;
		for (;;) {
			if (!info->useZlib || info->zRecvReady) {
				DWORD dwIdle = GetTickCount() - m_lastTicks;
				if (dwIdle >= dwConnectionKeepAliveInterval)
					dwIdle = dwConnectionKeepAliveInterval - 10; // now!

				NETLIBSELECT nls = {0};
				nls.cbSize = sizeof(NETLIBSELECT);
				nls.dwTimeout = dwConnectionKeepAliveInterval - dwIdle;
				nls.hReadConns[0] = info->s;
				int nSelRes = CallService(MS_NETLIB_SELECT, 0, (LPARAM)&nls);
				if (nSelRes == -1) // error
					break;
				else if (nSelRes == 0 && m_bSendKeepAlive) {
					if (m_ThreadInfo->jabberServerCaps & JABBER_CAPS_PING) {
						CJabberIqInfo *pInfo = AddIQ(&CJabberProto::OnPingReply, JABBER_IQ_TYPE_GET, NULL, 0, -1, this);
						pInfo->SetTimeout(m_options.ConnectionKeepAliveTimeout);
						info->send( XmlNodeIq(pInfo) << XATTR(_T("from"), m_ThreadInfo->fullJID) << XCHILDNS(_T("ping"), JABBER_FEAT_PING));
					}
					else info->send(" \t ");
					continue;
			}	}

			int recvResult = info->recv(buffer + datalen, jabberNetworkBufferSize - datalen);
			debugLogA("recvResult = %d", recvResult);
			if (recvResult <= 0)
				break;
			datalen += recvResult;

recvRest:
			buffer[datalen] = '\0';

			TCHAR *str;
			str = mir_utf8decodeW(buffer);

			int bytesParsed = 0;
			XmlNode root(str, &bytesParsed, tag);
			if (root && tag)
			{
				char *p = strstr(buffer, "stream:stream");
				if (p) p = strchr(p, '>');
				if (p)
					bytesParsed = p - buffer + 1;
				else {
					root = XmlNode();
					bytesParsed = 0;
				}

				mir_free(str);

			}
			else {
				if (root) str[ bytesParsed ] = 0;
				bytesParsed = (root) ? mir_utf8lenW(str) : 0;
				mir_free(str);
			}

			debugLogA("bytesParsed = %d", bytesParsed);
			if (root) tag = NULL;

			if (xmlGetName(root) == NULL) {
				for (int i=0; ; i++) {
					HXML n = xmlGetChild(root , i);
					if (!n)
						break;
					OnProcessProtocol(n, info);
				}
			}
			else OnProcessProtocol(root, info);

			if (bytesParsed > 0) {
				if (bytesParsed < datalen)
					memmove(buffer, buffer+bytesParsed, datalen-bytesParsed);
				datalen -= bytesParsed;
			}
			else if (datalen >= jabberNetworkBufferSize) {
				//jabberNetworkBufferSize += 65536;
				jabberNetworkBufferSize *= 2;
				debugLogA("Increasing network buffer size to %d", jabberNetworkBufferSize);
				if ((buffer=(char*)mir_realloc(buffer, jabberNetworkBufferSize + 1)) == NULL) {
					debugLogA("Cannot reallocate more network buffer, go offline now");
					break;
			}	}
			else debugLogA("Unknown state: bytesParsed=%d, datalen=%d, jabberNetworkBufferSize=%d", bytesParsed, datalen, jabberNetworkBufferSize);

			if (m_szXmlStreamToBeInitialized) {
				xmlStreamInitializeNow(info);
				tag = _T("stream:stream");
			}
			if (root && datalen)
				goto recvRest;
		}

		if (info->type == JABBER_SESSION_NORMAL) {
			m_iqManager.ExpireAll(info);
			m_bJabberOnline = FALSE;
			info->zlibUninit();
			EnableMenuItems(FALSE);
			RebuildInfoFrame();
			if (m_hwndJabberChangePassword) {
				//DestroyWindow(hwndJabberChangePassword);
				// Since this is a different thread, simulate the click on the cancel button instead
				SendMessage(m_hwndJabberChangePassword, WM_COMMAND, MAKEWORD(IDCANCEL, 0), 0);
			}

			// Quit all chatrooms (will send quit message)
			LISTFOREACH(i, this, LIST_CHATROOM)
				if (JABBER_LIST_ITEM *item = ListGetItemPtrFromIndex(i))
					GcQuit(item, 0, NULL);

			ListRemoveList(LIST_CHATROOM);
			ListRemoveList(LIST_BOOKMARK);
			//UI_SAFE_NOTIFY(m_pDlgJabberJoinGroupchat, WM_JABBER_CHECK_ONLINE);
			UI_SAFE_NOTIFY_HWND(m_hwndJabberAddBookmark, WM_JABBER_CHECK_ONLINE);
			//UI_SAFE_NOTIFY(m_pDlgBookmarks, WM_JABBER_CHECK_ONLINE);
			WindowNotify(WM_JABBER_CHECK_ONLINE);

			// Set status to offline
			oldStatus = m_iStatus;
			m_iDesiredStatus = m_iStatus = ID_STATUS_OFFLINE;
			ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)oldStatus, m_iStatus);

			// Set all contacts to offline
			for (HANDLE hContact = db_find_first(m_szModuleName); hContact; hContact = db_find_next(hContact, m_szModuleName))
				SetContactOfflineStatus(hContact);

			mir_free(m_szJabberJID);
			m_szJabberJID = NULL;
			m_tmJabberLoggedInTime = 0;
			ListWipe();

			WindowNotify(WM_JABBER_REFRESH_VCARD);
		}
		else if (info->type==JABBER_SESSION_REGISTER && !info->reg_done)
			SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)TranslateT("Error: Connection lost"));
	}
	else if (info->type == JABBER_SESSION_NORMAL) {
		oldStatus = m_iStatus;
		m_iDesiredStatus = m_iStatus = ID_STATUS_OFFLINE;
		ProtoBroadcastAck(NULL, ACKTYPE_STATUS, ACKRESULT_SUCCESS, (HANDLE)oldStatus, m_iStatus);
	}

	debugLogA("Thread ended: type=%d server='%s'", info->type, info->server);

	if (info->type == JABBER_SESSION_NORMAL && m_ThreadInfo == info) {
		mir_free(m_szStreamId);
		m_szStreamId = NULL;
		m_ThreadInfo = NULL;
	}

	info->close();
	mir_free(buffer);
	debugLogA("Exiting ServerThread");
	goto LBL_Exit;
}

void CJabberProto::PerformRegistration(ThreadData* info)
{
	iqIdRegGetReg = SerialNext();
	info->send( XmlNodeIq(_T("get"), iqIdRegGetReg, NULL) << XQUERY(JABBER_FEAT_REGISTER));

	SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 50, (LPARAM)TranslateT("Requesting registration instruction..."));
}

void CJabberProto::PerformIqAuth(ThreadData* info)
{
	if (info->type == JABBER_SESSION_NORMAL) {
		info->send( XmlNodeIq( AddIQ(&CJabberProto::OnIqResultGetAuth, JABBER_IQ_TYPE_GET)) 
			<< XQUERY(_T("jabber:iq:auth")) << XCHILD(_T("username"), info->username));
	}
	else if (info->type == JABBER_SESSION_REGISTER)
		PerformRegistration(info);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CJabberProto::OnProcessStreamOpening(HXML node, ThreadData *info)
{
	if (lstrcmp(xmlGetName(node), _T("stream:stream")))
		return;

	if (info->type == JABBER_SESSION_NORMAL) {
		const TCHAR *sid = xmlGetAttrValue(node, _T("id"));
		if (sid != NULL) {
			char* pszSid = mir_t2a(sid);
			replaceStr(info->proto->m_szStreamId, pszSid);
			mir_free(pszSid);
	}	}

	// old server - disable SASL then
	if (xmlGetAttrValue(node, _T("version")) == NULL)
		info->proto->m_options.Disable3920auth = TRUE;

	if (info->proto->m_options.Disable3920auth)
		info->proto->PerformIqAuth(info);
}

void CJabberProto::PerformAuthentication(ThreadData* info)
{
	TJabberAuth* auth = NULL;
	char* request = NULL;

	if (info->auth) {
		delete info->auth;
		info->auth = NULL;
	}

	if (m_AuthMechs.isSpnegoAvailable) {
		m_AuthMechs.isSpnegoAvailable = false;
		auth = new TNtlmAuth(info, "GSS-SPNEGO");
		if (!auth->isValid()) {
			delete auth;
			auth = NULL;
	}	}

	if (auth == NULL && m_AuthMechs.isNtlmAvailable) {
		m_AuthMechs.isNtlmAvailable = false;
		auth = new TNtlmAuth(info, "NTLM");
		if (!auth->isValid()) {
			delete auth;
			auth = NULL;
	}	}

	if (auth == NULL && m_AuthMechs.isKerberosAvailable) {
		m_AuthMechs.isKerberosAvailable = false;
		auth = new TNtlmAuth(info, "GSSAPI", m_AuthMechs.m_gssapiHostName);
		if (!auth->isValid()) {
			delete auth;
			auth = NULL;
		}
		else {
			request = auth->getInitialRequest();
			if (!request) {
				delete auth;
				auth = NULL;
	}	}	}

	if (auth == NULL && m_AuthMechs.isScramAvailable) {
		m_AuthMechs.isScramAvailable = false;
		auth = new TScramAuth(info);
	}

	if (auth == NULL && m_AuthMechs.isMd5Available) {
		m_AuthMechs.isMd5Available = false;
		auth = new TMD5Auth(info);
	}

	if (auth == NULL && m_AuthMechs.isPlainAvailable) {
		m_AuthMechs.isPlainAvailable = false;
		auth = new TPlainAuth(info, false);
	}

	if (auth == NULL && m_AuthMechs.isPlainOldAvailable) {
		m_AuthMechs.isPlainOldAvailable = false;
		auth = new TPlainAuth(info, true);
	}

	if (auth == NULL) {
		if (m_AuthMechs.isAuthAvailable) { // no known mechanisms but iq_auth is available
			m_AuthMechs.isAuthAvailable = false;
			PerformIqAuth(info);
			return;
		}

		TCHAR text[1024];
		mir_sntprintf(text, SIZEOF(text), TranslateT("Authentication failed for %s@%S."), info->username, info->server);
		MsgPopup(NULL, text, TranslateT("Jabber Authentication"));
		JLoginFailed(LOGINERR_WRONGPASSWORD);
		info->send("</stream:stream>");
		m_ThreadInfo = NULL;
		return;
	}

	info->auth = auth;

	if (!request) request = auth->getInitialRequest();
	info->send(XmlNode(_T("auth"), _A2T(request)) << XATTR(_T("xmlns"), _T("urn:ietf:params:xml:ns:xmpp-sasl"))
		<< XATTR(_T("mechanism"), _A2T(auth->getName())));
	mir_free(request);
}

/////////////////////////////////////////////////////////////////////////////////////////

void CJabberProto::OnProcessFeatures(HXML node, ThreadData* info)
{
	bool isRegisterAvailable = false;
	bool areMechanismsDefined = false;

	for (int i=0; ;i++) {
		HXML n = xmlGetChild(node ,i);
		if (!n)
			break;

		if (!_tcscmp(xmlGetName(n), _T("starttls"))) {
			if (!info->useSSL && m_options.UseTLS) {
				debugLogA("Requesting TLS");
				info->send(XmlNode(xmlGetName(n)) << XATTR(_T("xmlns"), _T("urn:ietf:params:xml:ns:xmpp-tls")));
				return;
		}	}

		if (!_tcscmp(xmlGetName(n), _T("compression")) && m_options.EnableZlib == TRUE) {
			debugLogA("Server compression available");
			for (int k=0; ; k++) {
				HXML c = xmlGetChild(n,k);
				if (!c)
					break;

				if (!_tcscmp(xmlGetName(c), _T("method"))) {
					if (!_tcscmp(xmlGetText(c), _T("zlib")) && info->zlibInit() == TRUE) {
						debugLogA("Requesting Zlib compression");
						info->send(XmlNode(_T("compress")) << XATTR(_T("xmlns"), _T("http://jabber.org/protocol/compress"))
							<< XCHILD(_T("method"), _T("zlib")));
						return;
		}	}	}	}

		if (!_tcscmp(xmlGetName(n), _T("mechanisms"))) {
			m_AuthMechs.isPlainAvailable = false;
			m_AuthMechs.isPlainOldAvailable = false;
			m_AuthMechs.isMd5Available = false;
			m_AuthMechs.isScramAvailable = false;
			m_AuthMechs.isNtlmAvailable = false;
			m_AuthMechs.isSpnegoAvailable = false;
			m_AuthMechs.isKerberosAvailable = false;
			mir_free(m_AuthMechs.m_gssapiHostName); m_AuthMechs.m_gssapiHostName = NULL;

			areMechanismsDefined = true;
			//JabberLog("%d mechanisms\n",n->numChild);
			for (int k=0; ; k++) {
				HXML c = xmlGetChild(n,k);
				if (!c)
					break;

				if (!_tcscmp(xmlGetName(c), _T("mechanism"))) {
					LPCTSTR ptszMechanism = xmlGetText(c);
					     if (!_tcscmp(ptszMechanism, _T("PLAIN")))        m_AuthMechs.isPlainOldAvailable = m_AuthMechs.isPlainAvailable = true;
					else if (!_tcscmp(ptszMechanism, _T("DIGEST-MD5")))   m_AuthMechs.isMd5Available = true;
					else if (!_tcscmp(ptszMechanism, _T("SCRAM-SHA-1")))  m_AuthMechs.isScramAvailable = true;
					else if (!_tcscmp(ptszMechanism, _T("NTLM")))         m_AuthMechs.isNtlmAvailable = true;
					else if (!_tcscmp(ptszMechanism, _T("GSS-SPNEGO")))   m_AuthMechs.isSpnegoAvailable = true;
					else if (!_tcscmp(ptszMechanism, _T("GSSAPI")))       m_AuthMechs.isKerberosAvailable = true;
				}
				else if (!_tcscmp(xmlGetName(c), _T("hostname"))) {
					const TCHAR *mech = xmlGetAttrValue(c, _T("mechanism"));
					if (mech && _tcsicmp(mech, _T("GSSAPI")) == 0) {
						m_AuthMechs.m_gssapiHostName = mir_tstrdup(xmlGetText(c));
					}
				}
			}
		}
		else if (!_tcscmp(xmlGetName(n), _T("register"))) isRegisterAvailable = true;
		else if (!_tcscmp(xmlGetName(n), _T("auth"    ))) m_AuthMechs.isAuthAvailable = true;
		else if (!_tcscmp(xmlGetName(n), _T("session" ))) m_AuthMechs.isSessionAvailable = true;
	}

	if (areMechanismsDefined) {
		if (info->type == JABBER_SESSION_NORMAL)
			PerformAuthentication(info);
		else if (info->type == JABBER_SESSION_REGISTER)
			PerformRegistration(info);
		else
			info->send("</stream:stream>");
		return;
	}

	// mechanisms are not defined.
	if (info->auth) { //We are already logged-in
		info->send(
			XmlNodeIq( AddIQ(&CJabberProto::OnIqResultBind, JABBER_IQ_TYPE_SET))
				<< XCHILDNS(_T("bind"), _T("urn:ietf:params:xml:ns:xmpp-bind"))
				<< XCHILD(_T("resource"), info->resource));

		if (m_AuthMechs.isSessionAvailable)
			info->bIsSessionAvailable = TRUE;

		return;
	}

	//mechanisms not available and we are not logged in
	PerformIqAuth(info);
}

void CJabberProto::OnProcessFailure(HXML node, ThreadData* info)
{
	const TCHAR *type;
//failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"
	if ((type = xmlGetAttrValue(node, _T("xmlns"))) == NULL) return;
	if (!_tcscmp(type, _T("urn:ietf:params:xml:ns:xmpp-sasl"))) {
		PerformAuthentication(info);
}	}

void CJabberProto::OnProcessError(HXML node, ThreadData* info)
{
	TCHAR *buff;
	int i;
	int pos;
	bool skipMsg = false;

	//failure xmlns=\"urn:ietf:params:xml:ns:xmpp-sasl\"
	if (!xmlGetChild(node ,0))
		return;

	buff = (TCHAR *)mir_alloc(1024*sizeof(TCHAR));
	pos=0;
	for (i=0; ; i++) {
		HXML n = xmlGetChild(node , i);
		if (!n)
			break;

		const TCHAR *name = xmlGetName(n);
		const TCHAR *desc = xmlGetText(n);
		if (desc)
			pos += mir_sntprintf(buff+pos, 1024-pos, _T("%s: %s\r\n"), name, desc);
		else
			pos += mir_sntprintf(buff+pos, 1024-pos, _T("%s\r\n"), name);

		if (!_tcscmp(name, _T("conflict")))
			ProtoBroadcastAck(NULL, ACKTYPE_LOGIN, ACKRESULT_FAILED, NULL, LOGINERR_OTHERLOCATION);
		else if (!_tcscmp(name, _T("see-other-host"))) {
			skipMsg = true;
		}
	}
	if (!skipMsg) MsgPopup(NULL, buff, TranslateT("Jabber Error"));
	mir_free(buff);
	info->send("</stream:stream>");
}

void CJabberProto::OnProcessSuccess(HXML node, ThreadData* info)
{
	const TCHAR *type;
//	int iqId;
	// RECVED: <success ...
	// ACTION: if successfully logged in, continue by requesting roster list and set my initial status
	if ((type = xmlGetAttrValue(node, _T("xmlns"))) == NULL)
		return;

	if (!_tcscmp(type, _T("urn:ietf:params:xml:ns:xmpp-sasl"))) {
		if (!info->auth->validateLogin(xmlGetText(node))) {
			info->send("</stream:stream>");
			return;
		}

		debugLogA("Success: Logged-in.");
		ptrT tszNick( getTStringA("Nick"));
		if (tszNick == NULL)
			setTString("Nick", info->username);

		xmlStreamInitialize("after successful sasl");
	}
	else debugLogA("Success: unknown action %S.",type);
}

void CJabberProto::OnProcessChallenge(HXML node, ThreadData* info)
{
	if (info->auth == NULL) {
		debugLogA("No previous auth have been made, exiting...");
		return;
	}

	if (lstrcmp(xmlGetAttrValue(node, _T("xmlns")), _T("urn:ietf:params:xml:ns:xmpp-sasl")))
		return;

	char* challenge = info->auth->getChallenge(xmlGetText(node));
	info->send(XmlNode(_T("response"), _A2T(challenge)) << XATTR(_T("xmlns"), _T("urn:ietf:params:xml:ns:xmpp-sasl")));
	mir_free(challenge);
}

void CJabberProto::OnProcessProtocol(HXML node, ThreadData* info)
{
	OnConsoleProcessXml(node, JCPF_IN);

	if (!lstrcmp(xmlGetName(node), _T("proceed")))
		OnProcessProceed(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("compressed")))
		OnProcessCompressed(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("stream:features")))
		OnProcessFeatures(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("stream:stream")))
		OnProcessStreamOpening(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("success")))
		OnProcessSuccess(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("failure")))
		OnProcessFailure(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("stream:error")))
		OnProcessError(node, info);
	else if (!lstrcmp(xmlGetName(node), _T("challenge")))
		OnProcessChallenge(node, info);
	else if (info->type == JABBER_SESSION_NORMAL) {
		if (!lstrcmp(xmlGetName(node), _T("message")))
			OnProcessMessage(node, info);
		else if (!lstrcmp(xmlGetName(node), _T("presence")))
			OnProcessPresence(node, info);
		else if (!lstrcmp(xmlGetName(node), _T("iq")))
			OnProcessIq(node);
		else
			debugLogA("Invalid top-level tag (only <message/> <presence/> and <iq/> allowed)");
	}
	else if (info->type == JABBER_SESSION_REGISTER) {
		if (!lstrcmp(xmlGetName(node), _T("iq")))
			OnProcessRegIq(node, info);
		else
			debugLogA("Invalid top-level tag (only <iq/> allowed)");
}	}

void CJabberProto::OnProcessProceed(HXML node, ThreadData* info)
{
	const TCHAR *type;
	if ((type = xmlGetAttrValue(node, _T("xmlns"))) != NULL && !lstrcmp(type, _T("error")))
		return;

	if (!lstrcmp(type, _T("urn:ietf:params:xml:ns:xmpp-tls"))) {
		debugLogA("Starting TLS...");

		char* gtlk = strstr(info->manualHost, "google.com");
		bool isHosted = gtlk && !gtlk[10] && stricmp(info->server, "gmail.com") &&
			stricmp(info->server, "googlemail.com");

		NETLIBSSL ssl = {0};
		ssl.cbSize = sizeof(ssl);
		ssl.host = isHosted ? info->manualHost : info->server;
		if (!CallService(MS_NETLIB_STARTSSL, (WPARAM)info->s, (LPARAM)&ssl)) {
			debugLogA("SSL initialization failed");
			info->send("</stream:stream>");
			info->shutdown();
		}
		else
			xmlStreamInitialize("after successful StartTLS");
}	}

void CJabberProto::OnProcessCompressed(HXML node, ThreadData* info)
{
	const TCHAR *type;

	debugLogA("Compression confirmed");

	if ((type = xmlGetAttrValue(node, _T("xmlns"))) != NULL && !lstrcmp(type, _T("error")))
		return;
	if (lstrcmp(type, _T("http://jabber.org/protocol/compress")))
		return;

	debugLogA("Starting Zlib stream compression...");

	info->useZlib = TRUE;
	info->zRecvData = (char*)mir_alloc(ZLIB_CHUNK_SIZE);

	xmlStreamInitialize("after successful Zlib init");
}

void CJabberProto::OnProcessPubsubEvent(HXML node)
{
	const TCHAR *from = xmlGetAttrValue(node, _T("from"));
	if (!from)
		return;

	HXML eventNode = xmlGetChildByTag(node, "event", "xmlns", JABBER_FEAT_PUBSUB_EVENT);
	if (!eventNode)
		return;

	m_pepServices.ProcessEvent(from, eventNode);

	HANDLE hContact = HContactFromJID(from);
	if (!hContact)
		return;

	HXML itemsNode;
	if (m_options.EnableUserTune && (itemsNode = xmlGetChildByTag(eventNode, "items", "node", JABBER_FEAT_USER_TUNE))) {
		// node retract?
		if (xmlGetChild(itemsNode , "retract")) {
			SetContactTune(hContact, NULL, NULL, NULL, NULL, NULL);
			return;
		}

		HXML tuneNode = XPath(itemsNode, _T("item/tune[@xmlns='") JABBER_FEAT_USER_TUNE _T("']"));
		if (!tuneNode)
			return;

		const TCHAR *szArtist = XPathT(tuneNode, "artist");
		const TCHAR *szLength = XPathT(tuneNode, "length");
		const TCHAR *szSource = XPathT(tuneNode, "source");
		const TCHAR *szTitle = XPathT(tuneNode, "title");
		const TCHAR *szTrack = XPathT(tuneNode, "track");

		TCHAR szLengthInTime[32];
		szLengthInTime[0] = 0;
		if (szLength) {
			int nLength = _ttoi(szLength);
			mir_sntprintf(szLengthInTime, SIZEOF(szLengthInTime), _T("%02d:%02d:%02d"),
				nLength / 3600, (nLength / 60) % 60, nLength % 60);
		}

		SetContactTune(hContact, szArtist, szLength ? szLengthInTime : NULL, szSource, szTitle, szTrack);
	}
}

// returns 0, if error or no events
DWORD JabberGetLastContactMessageTime(HANDLE hContact)
{
	// TODO: time cache can improve performance
	HANDLE hDbEvent = db_event_last(hContact);
	if (!hDbEvent)
		return 0;

	DWORD dwTime = 0;

	DBEVENTINFO dbei = { sizeof(dbei) };
	dbei.cbBlob = db_event_getBlobSize(hDbEvent);
	if (dbei.cbBlob != -1) {
		dbei.pBlob = (PBYTE)mir_alloc(dbei.cbBlob + 1);
		int nGetTextResult = db_event_get(hDbEvent, &dbei);
		if (!nGetTextResult)
			dwTime = dbei.timestamp;
		mir_free(dbei.pBlob);
	}
	return dwTime;
}

HANDLE CJabberProto::CreateTemporaryContact(const TCHAR *szJid, JABBER_LIST_ITEM* chatItem)
{
	HANDLE hContact = NULL;
	if (chatItem) {
		const TCHAR *p = _tcschr(szJid, '/');
		if (p != NULL && p[1] != '\0')
			p++;
		else
			p = szJid;
		hContact = DBCreateContact(szJid, p, TRUE, FALSE);

		pResourceStatus r( chatItem->findResource(p));
		if (r)
			setWord(hContact, "Status", r->m_iStatus);
	}
	else {
		TCHAR *nick = JabberNickFromJID(szJid);
		hContact = DBCreateContact(szJid, nick, TRUE, TRUE);
		mir_free(nick);
	}
	return hContact;
}

void CJabberProto::OnProcessMessage(HXML node, ThreadData* info)
{
	HXML xNode, n;

	if (!xmlGetName(node) || _tcscmp(xmlGetName(node), _T("message")))
		return;

	LPCTSTR from, type = xmlGetAttrValue(node, _T("type"));
	if ((from = xmlGetAttrValue(node, _T("from"))) == NULL)
		return;

	LPCTSTR idStr = xmlGetAttrValue(node, _T("id"));
	pResourceStatus pFromResource( ResourceInfoFromJID(from));

	// Message receipts delivery request. Reply here, before a call to HandleMessagePermanent() to make sure message receipts are handled for external plugins too.
	if ((!type || _tcsicmp(type, _T("error"))) && xmlGetChildByTag(node, "request", "xmlns", JABBER_FEAT_MESSAGE_RECEIPTS)) {
		info->send(
			XmlNode(_T("message")) << XATTR(_T("to"), from) << XATTR(_T("id"), idStr)
				<< XCHILDNS(_T("received"), JABBER_FEAT_MESSAGE_RECEIPTS) << XATTR(_T("id"), idStr));

		if (pFromResource)
			pFromResource->m_jcbManualDiscoveredCaps |= JABBER_CAPS_MESSAGE_RECEIPTS;
	}

	if (m_messageManager.HandleMessagePermanent(node, info))
		return;

	HANDLE hContact = HContactFromJID(from);
	JABBER_LIST_ITEM *chatItem = ListGetItemPtr(LIST_CHATROOM, from);
	if (chatItem) {
		HXML xCaptcha = xmlGetChild(node, "captcha");
		if (xCaptcha)
			if (ProcessCaptcha(xCaptcha, node, info))
				return;
	}

	const TCHAR *szMessage = NULL;
	HXML bodyNode = xmlGetChildByTag(node , "body", "xml:lang", m_tszSelectedLang);
	if (bodyNode == NULL)
		bodyNode = xmlGetChild(node , "body");
	if (bodyNode != NULL && xmlGetText(bodyNode))
		szMessage = xmlGetText(bodyNode);

	LPCTSTR ptszSubject = xmlGetText( xmlGetChild(node , "subject"));
	if (ptszSubject && *ptszSubject) {
		size_t cbLen = (szMessage ? _tcslen(szMessage) : 0) + _tcslen(ptszSubject) + 128;
		TCHAR *szTmp = (TCHAR *)alloca(sizeof(TCHAR) * cbLen);
		szTmp[0] = 0;
		if (szMessage)
			_tcscat(szTmp, _T("Subject: "));
		_tcscat(szTmp, ptszSubject);
		if (szMessage) {
			_tcscat(szTmp, _T("\r\n"));
			_tcscat(szTmp, szMessage);
		}
		szMessage = szTmp;
	}

	if (szMessage && (n = xmlGetChildByTag(node, "addresses", "xmlns", JABBER_FEAT_EXT_ADDRESSING))) {
		HXML addressNode = xmlGetChildByTag(n, "address", "type", _T("ofrom"));
		if (addressNode) {
			const TCHAR *szJid = xmlGetAttrValue(addressNode, _T("jid"));
			if (szJid) {
				size_t cbLen = _tcslen(szMessage) + 1000;
				TCHAR *p = (TCHAR*)alloca(sizeof(TCHAR) * cbLen);
				mir_sntprintf(p, cbLen, TranslateT("Message redirected from: %s\r\n%s"), from, szMessage);
				szMessage = p;
				from = szJid;
				// rewrite hContact
				hContact = HContactFromJID(from);
			}
		}
	}

	// If message is from a stranger (not in roster), item is NULL
	JABBER_LIST_ITEM *item = ListGetItemPtr(LIST_ROSTER, from);
	if (item == NULL)
		item = ListGetItemPtr(LIST_VCARD_TEMP, from);

	time_t msgTime = 0;
	bool isChatRoomInvitation = false;
	const TCHAR *inviteRoomJid = NULL;
	const TCHAR *inviteFromJid = NULL;
	const TCHAR *inviteReason = NULL;
	const TCHAR *invitePassword = NULL;
	bool isDelivered = false;

	// check chatstates availability
	if (pFromResource && xmlGetChildByTag(node, "active", "xmlns", JABBER_FEAT_CHATSTATES))
		pFromResource->m_jcbManualDiscoveredCaps |= JABBER_CAPS_CHATSTATES;

	// chatstates composing event
	if (hContact && xmlGetChildByTag(node, "composing", "xmlns", JABBER_FEAT_CHATSTATES))
		CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, 60);

	// chatstates paused event
	if (hContact && xmlGetChildByTag(node, "paused", "xmlns", JABBER_FEAT_CHATSTATES))
		CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, PROTOTYPE_CONTACTTYPING_OFF);

	// chatstates inactive event
	if (hContact && xmlGetChildByTag(node, "inactive", "xmlns", JABBER_FEAT_CHATSTATES)) {
		CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, PROTOTYPE_CONTACTTYPING_OFF);
		if (pFromResource)
			pFromResource->m_bMessageSessionActive = false;
	}

	// message receipts delivery notification
	if (n = xmlGetChildByTag(node, "received", "xmlns", JABBER_FEAT_MESSAGE_RECEIPTS)) {
		int nPacketId = JabberGetPacketID(n);
		if (nPacketId == -1)
			nPacketId = JabberGetPacketID(node);
		if (nPacketId != -1)
			ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, (HANDLE)nPacketId, 0);
	}

	JabberReadXep203delay(node, msgTime);

	// XEP-0224 support (Attention/Nudge)
	if (xmlGetChildByTag(node, "attention", "xmlns", JABBER_FEAT_ATTENTION)) {
		if (!hContact)
			hContact = CreateTemporaryContact(from, chatItem);
		if (hContact)
			NotifyEventHooks(m_hEventNudge, (WPARAM)hContact, 0);
	}

	// chatstates gone event
	if (hContact && xmlGetChildByTag(node, "gone", "xmlns", JABBER_FEAT_CHATSTATES) && m_options.LogChatstates) {
		BYTE bEventType = JABBER_DB_EVENT_CHATSTATES_GONE; // gone event
		DBEVENTINFO dbei = { sizeof(dbei) };
		dbei.pBlob = &bEventType;
		dbei.cbBlob = 1;
		dbei.eventType = JABBER_DB_EVENT_TYPE_CHATSTATES;
		dbei.flags = DBEF_READ;
		dbei.timestamp = time(NULL);
		dbei.szModule = m_szModuleName;
		db_event_add(hContact, &dbei);
	}

	if ((n = xmlGetChildByTag(node, "confirm", "xmlns", JABBER_FEAT_HTTP_AUTH)) && m_options.AcceptHttpAuth) {
		const TCHAR *szId = xmlGetAttrValue(n, _T("id"));
		const TCHAR *szMethod = xmlGetAttrValue(n, _T("method"));
		const TCHAR *szUrl = xmlGetAttrValue(n, _T("url"));
		if (!szId || !szMethod || !szUrl)
			return;

		CJabberHttpAuthParams *pParams = (CJabberHttpAuthParams *)mir_alloc(sizeof(CJabberHttpAuthParams));
		if (!pParams)
			return;

		ZeroMemory(pParams, sizeof(CJabberHttpAuthParams));
		pParams->m_nType = CJabberHttpAuthParams::MSG;
		pParams->m_szFrom = mir_tstrdup(from);
		LPCTSTR ptszThread = xmlGetText( xmlGetChild(node, "thread"));
		if (ptszThread && *ptszThread)
			pParams->m_szThreadId = mir_tstrdup(ptszThread);
		pParams->m_szId = mir_tstrdup(szId);
		pParams->m_szMethod = mir_tstrdup(szMethod);
		pParams->m_szUrl = mir_tstrdup(szUrl);

		AddClistHttpAuthEvent(pParams);
		return;
	}

	// parsing extensions
	for (int i=0; (xNode = xmlGetChild(node, i)) != NULL; i++) {
		if ((xNode = xmlGetNthChild(node, _T("x"), i+1)) == NULL)
			continue;

		const TCHAR *ptszXmlns = xmlGetAttrValue(xNode, _T("xmlns"));
		if (ptszXmlns == NULL)
			continue;

		if (!_tcscmp(ptszXmlns, JABBER_FEAT_MIRANDA_NOTES)) {
			if (OnIncomingNote(from, xmlGetChild(xNode, "note")))
				return;
		}
		else if (!_tcscmp(ptszXmlns, _T("jabber:x:encrypted"))) {
			LPCTSTR ptszText = xmlGetText(xNode);
			if (ptszText == NULL)
				return;

			TCHAR *prolog = _T("-----BEGIN PGP MESSAGE-----\r\n\r\n");
			TCHAR *epilog = _T("\r\n-----END PGP MESSAGE-----\r\n");
			size_t len = _tcslen(prolog) + _tcslen(ptszText) + _tcslen(epilog) + 3;
			TCHAR *tempstring = (TCHAR*)_alloca(sizeof(TCHAR)*len);
			mir_sntprintf(tempstring, len, _T("%s%s%s"), prolog, ptszText, epilog);
			szMessage = tempstring;
		}
		else if (!_tcscmp(ptszXmlns, JABBER_FEAT_DELAY) && msgTime == 0) {
			const TCHAR *ptszTimeStamp = xmlGetAttrValue(xNode, _T("stamp"));
			if (ptszTimeStamp != NULL)
				msgTime = JabberIsoToUnixTime(ptszTimeStamp);
		}
		else if (!_tcscmp(ptszXmlns, JABBER_FEAT_MESSAGE_EVENTS)) {

			// set events support only if we discovered caps and if events not already set
			JabberCapsBits jcbCaps = GetResourceCapabilites(from, TRUE);
			if (jcbCaps & JABBER_RESOURCE_CAPS_ERROR)
				jcbCaps = JABBER_RESOURCE_CAPS_NONE;
			// FIXME: disabled due to expired XEP-0022 and problems with bombus delivery checks
//			if (jcbCaps && pFromResource && (!(jcbCaps & JABBER_CAPS_MESSAGE_EVENTS)))
//				pFromResource->m_jcbManualDiscoveredCaps |= (JABBER_CAPS_MESSAGE_EVENTS | JABBER_CAPS_MESSAGE_EVENTS_NO_DELIVERY);

			if (bodyNode == NULL) {
				HXML idNode = xmlGetChild(xNode, "id");
				if (xmlGetChild(xNode, "delivered") != NULL || xmlGetChild(xNode, "offline") != NULL) {
					int id = -1;
					if (idNode != NULL && xmlGetText(idNode) != NULL)
					if (!_tcsncmp(xmlGetText(idNode), _T(JABBER_IQID), strlen(JABBER_IQID)))
						id = _ttoi((xmlGetText(idNode)) + strlen(JABBER_IQID));

					if (id != -1)
						ProtoBroadcastAck(hContact, ACKTYPE_MESSAGE, ACKRESULT_SUCCESS, (HANDLE)id, 0);
				}

				if (hContact && xmlGetChild(xNode, "composing") != NULL)
					CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, 60);

				// Maybe a cancel to the previous composing
				HXML child = xmlGetChild(xNode, 0);
				if (hContact && (!child || (child && idNode != NULL)))
					CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, PROTOTYPE_CONTACTTYPING_OFF);
			}
			else {
				// Check whether any event is requested
				if (!isDelivered && (n = xmlGetChild(xNode, "delivered")) != NULL) {
					isDelivered = true;

					XmlNode m(_T("message")); m << XATTR(_T("to"), from);
					HXML x = m << XCHILDNS(_T("x"), JABBER_FEAT_MESSAGE_EVENTS);
					x << XCHILD(_T("delivered"));
					x << XCHILD(_T("id"), idStr);
					info->send(m);
				}
				if (item != NULL && xmlGetChild(xNode, "composing") != NULL) {
					if (item->messageEventIdStr)
						mir_free(item->messageEventIdStr);
					item->messageEventIdStr = (idStr == NULL) ? NULL : mir_tstrdup(idStr);
				}
			}
		}
		else if (!_tcscmp(ptszXmlns, JABBER_FEAT_OOB2)) {
			LPCTSTR ptszUrl = xmlGetText(xmlGetChild(xNode, "url"));
			if (ptszUrl != NULL && *ptszUrl) {
				size_t cbLen = (szMessage ? _tcslen(szMessage) : 0) + _tcslen(ptszUrl) + 32;
				TCHAR *szTmp = (TCHAR *)alloca(sizeof(TCHAR)* cbLen);
				_tcscpy(szTmp, ptszUrl);
				if (szMessage) {
					_tcscat(szTmp, _T("\r\n"));
					_tcscat(szTmp, szMessage);
				}
				szMessage = szTmp;
			}
		}
		else if (!_tcscmp(ptszXmlns, JABBER_FEAT_MUC_USER)) {
			HXML inviteNode = xmlGetChild(xNode, _T("invite"));
			if (inviteNode != NULL) {
				inviteFromJid = xmlGetAttrValue(inviteNode, _T("from"));
				inviteReason = xmlGetText(xmlGetChild(inviteNode, _T("reason")));
			}
			inviteRoomJid = from;
			if (inviteReason == NULL)
				inviteReason = szMessage;
			isChatRoomInvitation = true;
			invitePassword = xmlGetText(xmlGetChild(xNode, "password"));
		}
		else if (!_tcscmp(ptszXmlns, JABBER_FEAT_ROSTER_EXCHANGE) &&
			item != NULL && (item->subscription == SUB_BOTH || item->subscription == SUB_TO)) {
			TCHAR chkJID[JABBER_MAX_JID_LEN] = _T("@");
			JabberStripJid(from, chkJID + 1, SIZEOF(chkJID) - 1);
			for (int i = 1;; i++) {
				HXML iNode = xmlGetNthChild(xNode, _T("item"), i);
				if (iNode == NULL) break;
				const TCHAR *action = xmlGetAttrValue(iNode, _T("action"));
				const TCHAR *jid = xmlGetAttrValue(iNode, _T("jid"));
				const TCHAR *nick = xmlGetAttrValue(iNode, _T("name"));
				const TCHAR *group = xmlGetText(xmlGetChild(iNode, _T("group")));
				if (action && jid && _tcsstr(jid, chkJID)) {
					if (!_tcscmp(action, _T("add"))) {
						HANDLE hContact = DBCreateContact(jid, nick, FALSE, FALSE);
						if (group)
							db_set_ts(hContact, "CList", "Group", group);
					}
					else if (!_tcscmp(action, _T("delete"))) {
						HANDLE hContact = HContactFromJID(jid);
						if (hContact)
							CallService(MS_DB_CONTACT_DELETE, (WPARAM)hContact, 0);
					}
				}
			}
		}
		else if (!isChatRoomInvitation && !_tcscmp(ptszXmlns, JABBER_FEAT_DIRECT_MUC_INVITE)) {
			inviteRoomJid = xmlGetAttrValue(xNode, _T("jid"));
			inviteFromJid = from;
			if (inviteReason == NULL)
				inviteReason = xmlGetText(xNode);
			if (!inviteReason)
				inviteReason = szMessage;
			isChatRoomInvitation = true;
		}
	}

	if (isChatRoomInvitation) {
		if (inviteRoomJid != NULL) {
			if (m_options.IgnoreMUCInvites) {
				// FIXME: temporary disabled due to MUC inconsistence on server side
				/*
				XmlNode m("message"); xmlAddAttr(m, "to", from);
				XmlNode xNode = xmlAddChild(m, "x");
				xmlAddAttr(xNode, "xmlns", JABBER_FEAT_MUC_USER);
				XmlNode declineNode = xmlAddChild(xNode, "decline");
				xmlAddAttr(declineNode, "from", inviteRoomJid);
				XmlNode reasonNode = xmlAddChild(declineNode, "reason", "The user has chosen to not accept chat invites");
				info->send(m);
				*/
			}
			else GroupchatProcessInvite(inviteRoomJid, inviteFromJid, inviteReason, invitePassword);
		}
		return;
	}

	// all service info was already processed
	if (szMessage == NULL)
		return;

	if ((szMessage = JabberUnixToDosT(szMessage)) == NULL)
		szMessage = mir_tstrdup(_T(""));

	char *buf = mir_utf8encodeW(szMessage);

	if (item != NULL) {
		if (pFromResource) {
			pFromResource->m_bMessageSessionActive = TRUE;

			JABBER_RESOURCE_STATUS *pLast = item->m_pLastSeenResource;
			item->m_pLastSeenResource = pFromResource;
			if (item->resourceMode == RSMODE_LASTSEEN && pLast == pFromResource)
				UpdateMirVer(item);
		}
	}

	// Create a temporary contact, if needed
	if (hContact == NULL)
		hContact = CreateTemporaryContact(from, chatItem);
	CallService(MS_PROTO_CONTACTISTYPING, (WPARAM)hContact, PROTOTYPE_CONTACTTYPING_OFF);

	time_t now = time(NULL);
	if (!msgTime)
		msgTime = now;

	if (m_options.FixIncorrectTimestamps && (msgTime > now || (msgTime < (time_t)JabberGetLastContactMessageTime(hContact))))
		msgTime = now;

	PROTORECVEVENT recv = { 0 };
	recv.flags = PREF_UTF;
	recv.timestamp = (DWORD)msgTime;
	recv.szMessage = buf;
	recv.lParam = (LPARAM)((pFromResource != NULL && m_options.EnableRemoteControl) ? pFromResource->m_tszResourceName : 0);
	ProtoChainRecvMsg(hContact, &recv);

	mir_free((void*)szMessage);
	mir_free(buf);
}

// XEP-0115: Entity Capabilities
void CJabberProto::OnProcessPresenceCapabilites(HXML node)
{
	const TCHAR *from = xmlGetAttrValue(node, _T("from"));
	if (from == NULL)
		return;

	debugLogA("presence: for jid %S", from);

	pResourceStatus r(ResourceInfoFromJID(from));
	if (r == NULL)
		return;

	// check XEP-0115 support, and old style:
	HXML n;
	if ((n = xmlGetChildByTag(node, "c", "xmlns", JABBER_FEAT_ENTITY_CAPS)) != NULL ||
		 (n = xmlGetChild(node, "c")) != NULL) {
		const TCHAR *szNode = xmlGetAttrValue(n, _T("node"));
		const TCHAR *szVer = xmlGetAttrValue(n, _T("ver"));
		const TCHAR *szExt = xmlGetAttrValue(n, _T("ext"));
		if (szNode && szVer) {
			r->m_tszCapsNode = mir_tstrdup(szNode);
			r->m_tszCapsVer = mir_tstrdup(szVer);
			r->m_tszCapsExt = mir_tstrdup(szExt);
			HANDLE hContact = HContactFromJID(from);
			if (hContact)
				UpdateMirVer(hContact, r);
		}
	}

	// update user's caps
	// JabberCapsBits jcbCaps = GetResourceCapabilites(from, TRUE);
}

void CJabberProto::UpdateJidDbSettings(const TCHAR *jid)
{
	JABBER_LIST_ITEM *item = ListGetItemPtr(LIST_ROSTER, jid);
	if (item == NULL)
		return;
	HANDLE hContact = HContactFromJID(jid);
	if (hContact == NULL)
		return;

	int status = ID_STATUS_OFFLINE;
	if (!item->arResources.getCount()) {
		// set offline only if jid has resources
		if (_tcschr(jid, '/') == NULL)
			status = item->getTemp()->m_iStatus;
		if (item->getTemp()->m_tszStatusMessage)
			db_set_ts(hContact, "CList", "StatusMsg", item->getTemp()->m_tszStatusMessage);
		else
			db_unset(hContact, "CList", "StatusMsg");
	}

	// Determine status to show for the contact based on the remaining resources
	int nSelectedResource = -1, i = 0;
	int nMaxPriority = -999; // -128...+127 valid range
	for (i=0; i < item->arResources.getCount(); i++) {
		pResourceStatus r(item->arResources[i]);
		if (r->m_iPriority > nMaxPriority) {
			nMaxPriority = r->m_iPriority;
			status = r->m_iStatus;
			nSelectedResource = i;
		}
		else if (r->m_iPriority == nMaxPriority) {
			if ((status = JabberCombineStatus(status, r->m_iStatus)) == r->m_iStatus)
				nSelectedResource = i;
		}
	}
	item->getTemp()->m_iStatus = status;
	if (nSelectedResource != -1) {
		pResourceStatus r(item->arResources[nSelectedResource]);
		debugLogA("JabberUpdateJidDbSettings: updating jid %S to rc %S", item->jid, r->m_tszResourceName);
		if (r->m_tszStatusMessage)
			db_set_ts(hContact, "CList", "StatusMsg", r->m_tszStatusMessage);
		else
			db_unset(hContact, "CList", "StatusMsg");
		UpdateMirVer(hContact, r);
	}
	else delSetting(hContact, DBSETTING_DISPLAY_UID);

	if (_tcschr(jid, '@') != NULL || m_options.ShowTransport == TRUE)
		if (getWord(hContact, "Status", ID_STATUS_OFFLINE) != status)
			setWord(hContact, "Status", (WORD)status);

	 // remove x-status icon
	if (status == ID_STATUS_OFFLINE) {
		delSetting(hContact, DBSETTING_XSTATUSID);
		delSetting(hContact, DBSETTING_XSTATUSNAME);
		delSetting(hContact, DBSETTING_XSTATUSMSG);
	}

	MenuUpdateSrmmIcon(item);
}

void CJabberProto::OnProcessPresence(HXML node, ThreadData* info)
{
	HANDLE hContact;
	HXML showNode;
	JABBER_LIST_ITEM *item;
	LPCTSTR from, show;
	TCHAR *nick;

	if (!node || !xmlGetName(node) ||_tcscmp(xmlGetName(node), _T("presence"))) return;
	if ((from = xmlGetAttrValue(node, _T("from"))) == NULL) return;

	if (m_presenceManager.HandlePresencePermanent(node, info))
		return;

	if (ListGetItemPtr(LIST_CHATROOM, from)) {
		GroupchatProcessPresence(node);
		return;
	}

	BOOL bSelfPresence = FALSE;
	TCHAR szBareFrom[JABBER_MAX_JID_LEN];
	JabberStripJid(from, szBareFrom, SIZEOF(szBareFrom));
	TCHAR szBareOurJid[JABBER_MAX_JID_LEN];
	JabberStripJid(info->fullJID, szBareOurJid, SIZEOF(szBareOurJid));

	if (!_tcsicmp(szBareFrom, szBareOurJid))
		bSelfPresence = TRUE;

	LPCTSTR type = xmlGetAttrValue(node, _T("type"));
	if (type == NULL || !_tcscmp(type, _T("available"))) {
		if ((nick = JabberNickFromJID(from)) == NULL)
			return;

		if ((hContact = HContactFromJID(from)) == NULL) {
			if (!_tcsicmp(info->fullJID, from) || (!bSelfPresence && !ListGetItemPtr(LIST_ROSTER, from))) {
				debugLogA("SKIP Receive presence online from %S (who is not in my roster and not in list - skiping)", from);
				mir_free(nick);
				return;
			}
			hContact = DBCreateContact(from, nick, TRUE, TRUE);
		}
		if (!ListGetItemPtr(LIST_ROSTER, from)) {
			debugLogA("Receive presence online from %S (who is not in my roster)", from);
			ListAdd(LIST_ROSTER, from);
		}
		DBCheckIsTransportedContact(from, hContact);
		int status = ID_STATUS_ONLINE;
		if ((showNode = xmlGetChild(node , "show")) != NULL) {
			if ((show = xmlGetText(showNode)) != NULL) {
				if (!_tcscmp(show, _T("away"))) status = ID_STATUS_AWAY;
				else if (!_tcscmp(show, _T("xa"))) status = ID_STATUS_NA;
				else if (!_tcscmp(show, _T("dnd"))) status = ID_STATUS_DND;
				else if (!_tcscmp(show, _T("chat"))) status = ID_STATUS_FREECHAT;
			}
		}

		char priority = 0;
		if (LPCTSTR ptszPriority = xmlGetText( xmlGetChild(node , "priority")))
			priority = (char)_ttoi(ptszPriority);

		ListAddResource(LIST_ROSTER, from, status, xmlGetText( xmlGetChild(node , "status")), priority);

		// XEP-0115: Entity Capabilities
		OnProcessPresenceCapabilites(node);

		UpdateJidDbSettings(from);

		if (_tcschr(from, '@') == NULL) {
			UI_SAFE_NOTIFY(m_pDlgServiceDiscovery, WM_JABBER_TRANSPORT_REFRESH);
		}
		debugLogA("%S (%S) online, set contact status to %S", nick, from, pcli->pfnGetStatusModeDescription(status, 0));
		mir_free(nick);

		HXML xNode;
		if (m_options.EnableAvatars) {
			BOOL hasAvatar = false;
			BOOL removedAvatar = false;

			debugLogA("Avatar enabled");
			for (int i = 1; (xNode=xmlGetNthChild(node, _T("x"), i)) != NULL; i++) {
				if (!lstrcmp(xmlGetAttrValue(xNode, _T("xmlns")), _T("jabber:x:avatar"))) {
					LPCTSTR ptszHash = xmlGetText( xmlGetChild(xNode , "hash"));
					if (ptszHash != NULL) {
						delSetting(hContact,"AvatarXVcard");
						debugLogA("AvatarXVcard deleted");
						setTString(hContact, "AvatarHash", ptszHash);
						hasAvatar = true;
						ptrT saved( getTStringA(hContact, "AvatarSaved"));
						if (saved != NULL || lstrcmp(saved, ptszHash)) {
							debugLogA("Avatar was changed");
							ProtoBroadcastAck(hContact, ACKTYPE_AVATAR, ACKRESULT_STATUS, NULL, NULL);
						}
					}
					else removedAvatar = true;
				}
			}
			if (!hasAvatar) { //no jabber:x:avatar. try vcard-temp:x:update
				debugLogA("Not hasXAvatar");
				for (int i = 1; (xNode=xmlGetNthChild(node, _T("x"), i)) != NULL; i++) {
					if (!lstrcmp(xmlGetAttrValue(xNode, _T("xmlns")), _T("vcard-temp:x:update"))) {
						if ((xNode = xmlGetChild(xNode , "photo")) != NULL) {
							LPCTSTR txt = xmlGetText(xNode);
							if (txt != NULL && txt[0] != 0) {
								setByte(hContact, "AvatarXVcard", 1);
								debugLogA("AvatarXVcard set");
								setTString(hContact, "AvatarHash", txt);
								hasAvatar = true;
								ptrT saved( getTStringA(hContact, "AvatarSaved"));
								if (saved || lstrcmp(saved, txt)) {
									debugLogA("Avatar was changed. Using vcard-temp:x:update");
									ProtoBroadcastAck(hContact, ACKTYPE_AVATAR, ACKRESULT_STATUS, NULL, NULL);
								}
							}
							else removedAvatar = true;
			}	}	}	}

			if (!hasAvatar && removedAvatar) {
				debugLogA("Has no avatar");
				delSetting(hContact, "AvatarHash");
				
				if ( ptrT( getTStringA(hContact, "AvatarSaved")) != NULL) {
					delSetting(hContact, "AvatarSaved");
					ProtoBroadcastAck(hContact, ACKTYPE_AVATAR, ACKRESULT_SUCCESS, NULL, NULL);
		}	}	}
		return;
	}

	if (!_tcscmp(type, _T("unavailable"))) {
		hContact = HContactFromJID(from);
		if ((item = ListGetItemPtr(LIST_ROSTER, from)) != NULL) {
			ListRemoveResource(LIST_ROSTER, from);

			hContact = HContactFromJID(from);
			if (hContact && db_get_b(hContact, "CList", "NotOnList", 0) == 1) {
				// remove selfcontact, if where is no more another resources
				if (item->arResources.getCount() == 1 && ResourceInfoFromJID(info->fullJID))
					ListRemoveResource(LIST_ROSTER, info->fullJID);
			}


			// set status only if no more available resources
			if (!item->arResources.getCount()) {
				item->getTemp()->m_iStatus = ID_STATUS_OFFLINE;
				item->getTemp()->m_tszStatusMessage = mir_tstrdup(xmlGetText( xmlGetChild(node , "status")));
			}
		}
		else debugLogA("SKIP Receive presence offline from %S (who is not in my roster)", from);

		UpdateJidDbSettings(from);

		if (_tcschr(from, '@') == NULL)
			UI_SAFE_NOTIFY(m_pDlgServiceDiscovery, WM_JABBER_TRANSPORT_REFRESH);

		DBCheckIsTransportedContact(from, hContact);
		return;
	}

	if (!_tcscmp(type, _T("subscribe"))) {
		if (hContact = HContactFromJID(from))
			AddDbPresenceEvent(hContact, JABBER_DB_EVENT_PRESENCE_SUBSCRIBE);

		ptrT tszNick( JabberNickFromJID(from));
		HXML xNick = xmlGetChildByTag(node, "nick", "xmlns", JABBER_FEAT_NICK);
		if (xNick != NULL) {
			LPCTSTR xszNick = xmlGetText(xNick);
			if (xszNick != NULL && *xszNick) {
				debugLogA("Grabbed nick from presence: %S", xszNick);
				tszNick = mir_tstrdup(xszNick);
			}
		}

		// automatically send authorization allowed to agent/transport
		if (_tcschr(from, '@') == NULL || m_options.AutoAcceptAuthorization) {
			ListAdd(LIST_ROSTER, from);
			info->send(XmlNode(_T("presence")) << XATTR(_T("to"), from) << XATTR(_T("type"), _T("subscribed")));

			if (m_options.AutoAdd == TRUE) {
				if ((item = ListGetItemPtr(LIST_ROSTER, from)) == NULL || (item->subscription != SUB_BOTH && item->subscription != SUB_TO)) {
					debugLogA("Try adding contact automatically jid = %S", from);
					if ((hContact = AddToListByJID(from, 0)) != NULL) {
						setTString(hContact, "Nick", tszNick);
						db_unset(hContact, "CList", "NotOnList");
					}
				}
			}
			RebuildInfoFrame();
		}
		else {
			debugLogA("%S (%S) requests authorization", tszNick, from);
			DBAddAuthRequest(from, tszNick);
		}
		return;
	}

	if (!_tcscmp(type, _T("unsubscribe")))
		if (hContact = HContactFromJID(from))
			AddDbPresenceEvent(hContact, JABBER_DB_EVENT_PRESENCE_UNSUBSCRIBE);

	if (!_tcscmp(type, _T("unsubscribed")))
		if (hContact = HContactFromJID(from))
			AddDbPresenceEvent(hContact, JABBER_DB_EVENT_PRESENCE_UNSUBSCRIBED);

	if (!_tcscmp(type, _T("error")))
		if (hContact = HContactFromJID(from))
			AddDbPresenceEvent(hContact, JABBER_DB_EVENT_PRESENCE_ERROR);

	if (!_tcscmp(type, _T("subscribed"))) {
		if (hContact = HContactFromJID(from))
			AddDbPresenceEvent(hContact, JABBER_DB_EVENT_PRESENCE_SUBSCRIBED);

		if ((item=ListGetItemPtr(LIST_ROSTER, from)) != NULL) {
			if (item->subscription == SUB_FROM) item->subscription = SUB_BOTH;
			else if (item->subscription == SUB_NONE) {
				item->subscription = SUB_TO;
				if (_tcschr(from, '@') == NULL) {
					UI_SAFE_NOTIFY(m_pDlgServiceDiscovery, WM_JABBER_TRANSPORT_REFRESH);
				}
			}
			UpdateSubscriptionInfo(hContact, item);
		}
	}
}

void CJabberProto::OnIqResultVersion(HXML /*node*/, CJabberIqInfo *pInfo)
{
	pResourceStatus r( ResourceInfoFromJID(pInfo->GetFrom()));
	if (r == NULL)
		return;

	r->m_dwVersionRequestTime = -1;

	r->m_tszSoftware = NULL;
	r->m_tszSoftwareVersion = NULL;
	r->m_tszOs = NULL;

	HXML queryNode = pInfo->GetChildNode();

	if (pInfo->GetIqType() == JABBER_IQ_TYPE_RESULT && queryNode) {
		HXML n;
		if ((n = xmlGetChild(queryNode , "name")) != NULL && xmlGetText(n))
			r->m_tszSoftware = mir_tstrdup(xmlGetText(n));
		if ((n = xmlGetChild(queryNode , "version")) != NULL && xmlGetText(n))
			r->m_tszSoftwareVersion = mir_tstrdup(xmlGetText(n));
		if ((n = xmlGetChild(queryNode , "os")) != NULL && xmlGetText(n))
			r->m_tszOs = mir_tstrdup(xmlGetText(n));
	}

	GetResourceCapabilites(pInfo->GetFrom(), TRUE);
	if (pInfo->GetHContact())
		UpdateMirVer(pInfo->GetHContact(), r);

	JabberUserInfoUpdate(pInfo->GetHContact());
}

BOOL CJabberProto::OnProcessJingle(HXML node)
{
	LPCTSTR type;
	HXML child = xmlGetChildByTag(node, _T("jingle"), _T("xmlns"), JABBER_FEAT_JINGLE);

	if (child) {
		if ((type=xmlGetAttrValue(node, _T("type"))) == NULL) return FALSE;
		if ((!_tcscmp(type, _T("get")) || !_tcscmp(type, _T("set")))) {
			LPCTSTR szAction = xmlGetAttrValue(child, _T("action"));
			LPCTSTR idStr = xmlGetAttrValue(node, _T("id"));
			LPCTSTR from = xmlGetAttrValue(node, _T("from"));
			if (szAction && !_tcscmp(szAction, _T("session-initiate"))) {
				// if this is a Jingle 'session-initiate' and noone processed it yet, reply with "unsupported-applications"
				m_ThreadInfo->send( XmlNodeIq(_T("result"), idStr, from));

				XmlNodeIq iq(_T("set"), SerialNext(), from);
				HXML jingleNode = iq << XCHILDNS(_T("jingle"), JABBER_FEAT_JINGLE);

				jingleNode << XATTR(_T("action"), _T("session-terminate"));
				LPCTSTR szInitiator = xmlGetAttrValue(child, _T("initiator"));
				if (szInitiator)
					jingleNode << XATTR(_T("initiator"), szInitiator);
				LPCTSTR szSid = xmlGetAttrValue(child, _T("sid"));
				if (szSid)
					jingleNode << XATTR(_T("sid"), szSid);

				jingleNode << XCHILD(_T("reason"))
					<< XCHILD(_T("unsupported-applications"));
				m_ThreadInfo->send(iq);
				return TRUE;
			}
			else {
			// if it's something else than 'session-initiate' and noone processed it yet, reply with "unknown-session"
				XmlNodeIq iq(_T("error"), idStr, from);
				HXML errNode = iq << XCHILD(_T("error"));
				errNode << XATTR(_T("type"), _T("cancel"));
				errNode << XCHILDNS(_T("item-not-found"), _T("urn:ietf:params:xml:ns:xmpp-stanzas"));
				errNode << XCHILDNS(_T("unknown-session"), _T("urn:xmpp:jingle:errors:1"));
				m_ThreadInfo->send(iq);
				return TRUE;
			}
		}
	}
	return FALSE;
}

void CJabberProto::OnProcessIq(HXML node)
{
	HXML queryNode;
	const TCHAR *type, *xmlns;

	if (!xmlGetName(node) || _tcscmp(xmlGetName(node), _T("iq"))) return;
	if ((type=xmlGetAttrValue(node, _T("type"))) == NULL) return;

	int id = JabberGetPacketID(node);

	queryNode = xmlGetChild(node , "query");
	xmlns = xmlGetAttrValue(queryNode, _T("xmlns"));

	// new match by id
	if (m_iqManager.HandleIq(id, node))
		return;

	// new iq handler engine
	if (m_iqManager.HandleIqPermanent(node))
		return;

	// Jingle support
	if (OnProcessJingle(node))
		return;

	// RECVED: <iq type='error'> ...
	if (!_tcscmp(type, _T("error"))) {
		debugLogA("XXX on entry");
		// Check for file transfer deny by comparing idStr with ft->iqId
		LISTFOREACH(i, this, LIST_FILE)
		{
			JABBER_LIST_ITEM *item = ListGetItemPtrFromIndex(i);
			if (item->ft != NULL && item->ft->state == FT_CONNECTING && id == item->ft->iqId) {
				debugLogA("Denying file sending request");
				item->ft->state = FT_DENIED;
				if (item->ft->hFileEvent != NULL)
					SetEvent(item->ft->hFileEvent);	// Simulate the termination of file server connection
			}
		}
	}
	else if ((!_tcscmp(type, _T("get")) || !_tcscmp(type, _T("set")))) {
		XmlNodeIq iq(_T("error"), id, xmlGetAttrValue(node, _T("from")));

		HXML pFirstChild = xmlGetChild(node , 0);
		if (pFirstChild)
			xmlAddChild(iq, pFirstChild);

		iq << XCHILD(_T("error")) << XATTR(_T("type"), _T("cancel"))
				<< XCHILDNS(_T("service-unavailable"), _T("urn:ietf:params:xml:ns:xmpp-stanzas"));
		m_ThreadInfo->send(iq);
	}
}

void CJabberProto::OnProcessRegIq(HXML node, ThreadData* info)
{
	HXML errorNode;
	const TCHAR *type;

	if (!xmlGetName(node) || _tcscmp(xmlGetName(node), _T("iq"))) return;
	if ((type=xmlGetAttrValue(node, _T("type"))) == NULL) return;

	int id = JabberGetPacketID(node);

	if (!_tcscmp(type, _T("result"))) {

		// RECVED: result of the request for registration mechanism
		// ACTION: send account registration information
		if (id == iqIdRegGetReg) {
			iqIdRegSetReg = SerialNext();

			XmlNodeIq iq(_T("set"), iqIdRegSetReg);
			HXML query = iq << XQUERY(JABBER_FEAT_REGISTER);
			query << XCHILD(_T("password"), info->password);
			query << XCHILD(_T("username"), info->username);
			info->send(iq);

			SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 75, (LPARAM)TranslateT("Sending registration information..."));
		}
		// RECVED: result of the registration process
		// ACTION: account registration successful
		else if (id == iqIdRegSetReg) {
			info->send("</stream:stream>");
			SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)TranslateT("Registration successful"));
			info->reg_done = TRUE;
	}	}

	else if (!_tcscmp(type, _T("error"))) {
		errorNode = xmlGetChild(node , "error");
		TCHAR *str = JabberErrorMsg(errorNode);
		SendMessage(info->reg_hwndDlg, WM_JABBER_REGDLG_UPDATE, 100, (LPARAM)str);
		mir_free(str);
		info->reg_done = TRUE;
		info->send("</stream:stream>");
}	}

/////////////////////////////////////////////////////////////////////////////////////////
// ThreadData constructor & destructor

ThreadData::ThreadData(CJabberProto* aproto, JABBER_SESSION_TYPE parType)
{
	memset(this, 0, sizeof(*this));
	type = parType;
	proto = aproto;
	iomutex = CreateMutex(NULL, FALSE, NULL);
}

ThreadData::~ThreadData()
{
	if (auth) delete auth;
	mir_free(zRecvData);
	CloseHandle(iomutex);
	CloseHandle(hThread);
}

void ThreadData::close(void)
{
	if (s) {
		Netlib_CloseHandle(s);
		s = NULL;
}	}

void ThreadData::shutdown(void)
{
	if (s)
		Netlib_Shutdown(s);
}

int ThreadData::recvws(char* buf, size_t len, int flags)
{
	if (this == NULL)
		return 0;

	return proto->WsRecv(s, buf, (int)len, flags);
}

int ThreadData::recv(char* buf, size_t len)
{
	if (useZlib)
		return zlibRecv(buf, (long)len);

	return recvws(buf, len, MSG_DUMPASTEXT);
}

int ThreadData::sendws(char* buffer, size_t bufsize, int flags)
{
	return proto->WsSend(s, buffer, (int)bufsize, flags);
}

int ThreadData::send(char* buffer, int bufsize)
{
	if (this == NULL)
		return 0;

	if (bufsize == -1)
		bufsize = (int)strlen(buffer);

	WaitForSingleObject(iomutex, 6000);

	int result;
	if (useZlib)
		result = zlibSend(buffer, bufsize);
	else
		result = sendws(buffer, bufsize, MSG_DUMPASTEXT);

	ReleaseMutex(iomutex);
	return result;
}

// Caution: DO NOT use ->send() to send binary (non-string) data
int ThreadData::send(HXML node)
{
	if (this == NULL)
		return 0;

	while (HXML parent = xi.getParent(node))
		node = parent;

	if (proto->m_sendManager.HandleSendPermanent(node, this))
		return 0;

	proto->OnConsoleProcessXml(node, JCPF_OUT);

	TCHAR *str = xi.toString(node, NULL);

	// strip forbidden control characters from outgoing XML stream
	TCHAR *q = str;
	for (TCHAR *p = str; *p; ++p) {

		WCHAR c = *p;

		if (c < 0x9 || c > 0x9 && c < 0xA || c > 0xA && c < 0xD || c > 0xD && c < 0x20 || c > 0xD7FF && c < 0xE000 || c > 0xFFFD)
			continue;

		*q++ = *p;
	}
	*q = 0;


	char* utfStr = mir_utf8encodeT(str);
	int result = send(utfStr, (int)strlen(utfStr));
	mir_free(utfStr);

	xi.freeMem(str);
	return result;
}
