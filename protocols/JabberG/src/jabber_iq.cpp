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
#include "jabber_iq.h"
#include "jabber_caps.h"
#include "jabber_privacy.h"
#include "jabber_ibb.h"
#include "jabber_rc.h"

/////////////////////////////////////////////////////////////////////////////////////////
// CJabberIqManager class

CJabberIqManager::CJabberIqManager(CJabberProto* proto)
{
	InitializeCriticalSection(&m_cs);
	m_dwLastUsedHandle = 0;
	m_pIqs = NULL;
	m_hExpirerThread = NULL;
	m_pPermanentHandlers = NULL;
	ppro = proto;
}

CJabberIqManager::~CJabberIqManager()
{
	ExpireAll();
	{
		mir_cslock lck(m_cs);
		CJabberIqPermanentInfo *pInfo = m_pPermanentHandlers;
		while (pInfo) {
			CJabberIqPermanentInfo *pTmp = pInfo->m_pNext;
			delete pInfo;
			pInfo = pTmp;
		}
		m_pPermanentHandlers = NULL;
	}
	DeleteCriticalSection(&m_cs);
}

BOOL CJabberIqManager::Start()
{
	if (m_hExpirerThread || m_bExpirerThreadShutdownRequest)
		return FALSE;

	m_hExpirerThread = ppro->ForkThreadEx(&CJabberProto::ExpirerThread, this, 0);
	if (!m_hExpirerThread)
		return FALSE;

	return TRUE;
}

BOOL CJabberIqManager::Shutdown()
{
	if (m_bExpirerThreadShutdownRequest || !m_hExpirerThread)
		return TRUE;

	m_bExpirerThreadShutdownRequest = TRUE;

	WaitForSingleObject(m_hExpirerThread, INFINITE);
	CloseHandle(m_hExpirerThread);
	m_hExpirerThread = NULL;

	return TRUE;
}

BOOL CJabberIqManager::FillPermanentHandlers()
{
	// version requests (XEP-0092)
	AddPermanentHandler(&CJabberProto::OnIqRequestVersion, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_VERSION, FALSE, _T("query"));

	// last activity (XEP-0012)
	AddPermanentHandler(&CJabberProto::OnIqRequestLastActivity, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_LAST_ACTIVITY, FALSE, _T("query"));

	// ping requests (XEP-0199)
	AddPermanentHandler(&CJabberProto::OnIqRequestPing, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_PING, FALSE, _T("ping"));

	// entity time (XEP-0202)
	AddPermanentHandler(&CJabberProto::OnIqRequestTime, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_ENTITY_TIME, FALSE, _T("time"));

	// entity time (XEP-0090)
	AddPermanentHandler(&CJabberProto::OnIqProcessIqOldTime, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_ENTITY_TIME_OLD, FALSE, _T("query"));

	// old avatars support (deprecated XEP-0008)
	AddPermanentHandler(&CJabberProto::OnIqRequestAvatar, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_AVATAR, FALSE, _T("query"));

	// privacy lists (XEP-0016)
	AddPermanentHandler(&CJabberProto::OnIqRequestPrivacyLists, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR, JABBER_FEAT_PRIVACY_LISTS, FALSE, _T("query"));

	// in band bytestreams (XEP-0047)
	AddPermanentHandler(&CJabberProto::OnFtHandleIbbIq, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_CHILD_TAG_NODE | JABBER_IQ_PARSE_CHILD_TAG_NAME | JABBER_IQ_PARSE_CHILD_TAG_XMLNS, JABBER_FEAT_IBB, FALSE, NULL);

	// socks5-bytestreams (XEP-0065)
	AddPermanentHandler(&CJabberProto::FtHandleBytestreamRequest, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_BYTESTREAMS, FALSE, _T("query"));

	// session initiation (XEP-0095)
	AddPermanentHandler(&CJabberProto::OnSiRequest, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_SI, FALSE, _T("si"));

	// roster push requests
	AddPermanentHandler(&CJabberProto::OnRosterPushRequest, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_IQ_ROSTER, FALSE, _T("query"));

	// OOB file transfers
	AddPermanentHandler(&CJabberProto::OnIqRequestOOB, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_HCONTACT | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_OOB, FALSE, _T("query"));

	// disco#items requests (XEP-0030, XEP-0050)
	AddPermanentHandler(&CJabberProto::OnHandleDiscoItemsRequest, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_TO | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_DISCO_ITEMS, FALSE, _T("query"));

	// disco#info requests (XEP-0030, XEP-0050, XEP-0115)
	AddPermanentHandler(&CJabberProto::OnHandleDiscoInfoRequest, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_TO | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_DISCO_INFO, FALSE, _T("query"));

	// ad-hoc commands (XEP-0050) for remote controlling (XEP-0146)
	AddPermanentHandler(&CJabberProto::HandleAdhocCommandRequest, JABBER_IQ_TYPE_SET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_TO | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_COMMANDS, FALSE, _T("command"));

	// http auth (XEP-0070)
	AddPermanentHandler(&CJabberProto::OnIqHttpAuth, JABBER_IQ_TYPE_GET, JABBER_IQ_PARSE_FROM | JABBER_IQ_PARSE_ID_STR | JABBER_IQ_PARSE_CHILD_TAG_NODE, JABBER_FEAT_HTTP_AUTH, FALSE, _T("confirm"));

	return TRUE;
}

void __cdecl CJabberProto::ExpirerThread(void* pParam)
{
	CJabberIqManager *pManager = (CJabberIqManager *)pParam;
	pManager->ExpirerThread();
}

void CJabberIqManager::ExpirerThread()
{
	while (!m_bExpirerThreadShutdownRequest) {
		CJabberIqInfo *pInfo = DetachExpired();
		if (!pInfo) {
			for (int i=0; !m_bExpirerThreadShutdownRequest && (i < 10); i++)
				Sleep(50);

			// -1 thread :)
			ppro->m_adhocManager.ExpireSessions();
			continue;
		}
		ExpireInfo(pInfo);
		delete pInfo;
	}

	if (!m_bExpirerThreadShutdownRequest) {
		CloseHandle(m_hExpirerThread);
		m_hExpirerThread = NULL;
	}
}

void CJabberIqManager::ExpireInfo(CJabberIqInfo *pInfo, void*)
{
	if (!pInfo)
		return;

	if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_FROM)
		pInfo->m_szFrom = pInfo->m_szReceiver;
	if ((pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_HCONTACT) && (pInfo->m_szFrom))
		pInfo->m_hContact = ppro->HContactFromJID(pInfo->m_szFrom , 3);

	ppro->debugLogA("Expiring iq id %d, sent to %S", pInfo->m_nIqId, pInfo->m_szReceiver ? pInfo->m_szReceiver : _T("server"));

	pInfo->m_nIqType = JABBER_IQ_TYPE_FAIL;
	(ppro->*(pInfo->m_pHandler))(NULL, pInfo);
}

BOOL CJabberIqManager::ExpireIq(int nIqId)
{
	CJabberIqInfo *pInfo = DetachInfo(nIqId);
	if (pInfo) {
		do {
			ExpireInfo(pInfo);
			delete pInfo;
		}
			while ((pInfo = DetachInfo(nIqId)) != NULL);
		return TRUE;
	}
	return FALSE;
}
	
BOOL CJabberIqManager::ExpireByUserData(void *pUserData)
{
	while (true) {
		CJabberIqInfo *pInfo = DetachInfo(pUserData);
		if (!pInfo)
			return FALSE;
			
		ExpireInfo(pInfo, NULL);
		delete pInfo;
		return TRUE;
	}
}

BOOL CJabberIqManager::ExpireAll(void *pUserData)
{
	while (true) {
		CJabberIqInfo *pInfo;
		{
			mir_cslock lck(m_cs);
			pInfo = m_pIqs;
			if (pInfo)
				m_pIqs = m_pIqs->m_pNext;
		}
		if (!pInfo)
			break;
		pInfo->m_pNext = NULL;
		ExpireInfo(pInfo, pUserData);
		delete pInfo;
	}
	return TRUE;
}

CJabberIqInfo* CJabberIqManager::AddHandler(JABBER_IQ_HANDLER pHandler, int nIqType, const TCHAR *szReceiver, DWORD dwParamsToParse, int nIqId, void *pUserData, int iPriority)
{
	CJabberIqInfo *pInfo = new CJabberIqInfo();
	if (!pInfo)
		return NULL;

	pInfo->m_pHandler = pHandler;
	if (nIqId == -1)
		nIqId = ppro->SerialNext();
	pInfo->m_nIqId = nIqId;
	pInfo->m_nIqType = nIqType;
	pInfo->m_dwParamsToParse = dwParamsToParse;
	pInfo->m_pUserData = pUserData;
	pInfo->m_dwRequestTime = GetTickCount();
	pInfo->m_dwTimeout = JABBER_DEFAULT_IQ_REQUEST_TIMEOUT;
	pInfo->m_iPriority = iPriority;
	pInfo->SetReceiver(szReceiver);
	InsertIq(pInfo);
	return pInfo;
}

BOOL CJabberIqManager::HandleIq(int nIqId, HXML pNode)
{
	if (nIqId == -1 || pNode == NULL)
		return FALSE;

	const TCHAR *szType = xmlGetAttrValue(pNode, _T("type"));
	if (!szType)
		return FALSE;

	int nIqType = JABBER_IQ_TYPE_FAIL;
	if (!_tcsicmp(szType, _T("result")))
		nIqType = JABBER_IQ_TYPE_RESULT;
	else if (!_tcsicmp(szType, _T("error")))
		nIqType = JABBER_IQ_TYPE_ERROR;
	else
		return FALSE;

	CJabberIqInfo *pInfo = DetachInfo(nIqId);
	if (pInfo == NULL)
		return FALSE;

	do {
		pInfo->m_nIqType = nIqType;
		if (nIqType == JABBER_IQ_TYPE_RESULT) {
			if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_CHILD_TAG_NODE)
				pInfo->m_pChildNode = xmlGetChild(pNode , 0);

			if (pInfo->m_pChildNode && (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_CHILD_TAG_NAME))
				pInfo->m_szChildTagName = (TCHAR*)xmlGetName(pInfo->m_pChildNode);
			if (pInfo->m_pChildNode && (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_CHILD_TAG_XMLNS))
				pInfo->m_szChildTagXmlns = (TCHAR*)xmlGetAttrValue(pNode, _T("xmlns"));
		}

		if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_TO)
			pInfo->m_szTo = (TCHAR*)xmlGetAttrValue(pNode, _T("to"));

		if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_FROM)
			pInfo->m_szFrom = (TCHAR*)xmlGetAttrValue(pNode, _T("from"));
		if (pInfo->m_szFrom && (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_HCONTACT))
			pInfo->m_hContact = ppro->HContactFromJID(pInfo->m_szFrom, 3);

		if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_ID_STR)
			pInfo->m_szId = (TCHAR*)xmlGetAttrValue(pNode, _T("id"));

		(ppro->*(pInfo->m_pHandler))(pNode, pInfo);
		delete pInfo;
	}
		while ((pInfo = DetachInfo(nIqId)) != NULL);
	return TRUE;
}

BOOL CJabberIqManager::HandleIqPermanent(HXML pNode)
{
	mir_cslock lck(m_cs);
	CJabberIqPermanentInfo *pInfo = m_pPermanentHandlers;
	while (pInfo) {
		// have to get all data here, in the loop, because there's always possibility that previous handler modified it
		const TCHAR *szType = xmlGetAttrValue(pNode, _T("type"));
		if (!szType)
			return FALSE;

		CJabberIqInfo iqInfo;
		iqInfo.m_nIqType = JABBER_IQ_TYPE_FAIL;
		if (!_tcsicmp(szType, _T("get")))
			iqInfo.m_nIqType = JABBER_IQ_TYPE_GET;
		else if (!_tcsicmp(szType, _T("set")))
			iqInfo.m_nIqType = JABBER_IQ_TYPE_SET;
		else
			return FALSE;

		if (pInfo->m_nIqTypes & iqInfo.m_nIqType) {
			HXML pFirstChild = xmlGetChild(pNode , 0);
			if (!pFirstChild || !xmlGetName(pFirstChild))
				return FALSE;

			const TCHAR *szTagName = xmlGetName(pFirstChild);
			const TCHAR *szXmlns = xmlGetAttrValue(pFirstChild, _T("xmlns"));

			if ((!pInfo->m_szXmlns || (szXmlns && !_tcscmp(pInfo->m_szXmlns, szXmlns))) &&
			    (!pInfo->m_szTag || !_tcscmp(pInfo->m_szTag, szTagName)))
			{
				// node suits handler criteria, call the handler
				iqInfo.m_pChildNode = pFirstChild;
				iqInfo.m_szChildTagName = (TCHAR*)szTagName;
				iqInfo.m_szChildTagXmlns = (TCHAR*)szXmlns;
				iqInfo.m_szId = (TCHAR*)xmlGetAttrValue(pNode, _T("id"));
				iqInfo.m_pUserData = pInfo->m_pUserData;

				if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_TO)
					iqInfo.m_szTo = (TCHAR*)xmlGetAttrValue(pNode, _T("to"));

				if (pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_FROM)
					iqInfo.m_szFrom = (TCHAR*)xmlGetAttrValue(pNode, _T("from"));

				if ((pInfo->m_dwParamsToParse & JABBER_IQ_PARSE_HCONTACT) && (iqInfo.m_szFrom))
					iqInfo.m_hContact = ppro->HContactFromJID(iqInfo.m_szFrom, 3);

				ppro->debugLogA("Handling iq id %S, type %S, from %S", iqInfo.m_szId, szType, iqInfo.m_szFrom);
				if ((ppro->*(pInfo->m_pHandler))(pNode, &iqInfo))
					return TRUE;
			}
		}

		pInfo = pInfo->m_pNext;
	}

	return FALSE;
}

CJabberIqInfo* CJabberIqManager::DetachInfo(int nIqId)
{
	mir_cslock lck(m_cs);
	if (!m_pIqs)
		return NULL;

	CJabberIqInfo *pInfo = m_pIqs;
	if (m_pIqs->m_nIqId == nIqId) {
		m_pIqs = pInfo->m_pNext;
		pInfo->m_pNext = NULL;
		return pInfo;
	}

	while (pInfo->m_pNext) {
		if (pInfo->m_pNext->m_nIqId == nIqId) {
			CJabberIqInfo *pRetVal = pInfo->m_pNext;
			pInfo->m_pNext = pInfo->m_pNext->m_pNext;
			pRetVal->m_pNext = NULL;
			return pRetVal;
		}
		pInfo = pInfo->m_pNext;
	}
	return NULL;
}

CJabberIqInfo* CJabberIqManager::DetachInfo(void *pUserData)
{
	mir_cslock lck(m_cs);
	if (!m_pIqs)
		return NULL;

	CJabberIqInfo *pInfo = m_pIqs;
	if (m_pIqs->m_pUserData == pUserData) {
		m_pIqs = pInfo->m_pNext;
		pInfo->m_pNext = NULL;
		return pInfo;
	}

	while (pInfo->m_pNext) {
		if (pInfo->m_pNext->m_pUserData == pUserData) {
			CJabberIqInfo *pRetVal = pInfo->m_pNext;
			pInfo->m_pNext = pInfo->m_pNext->m_pNext;
			pRetVal->m_pNext = NULL;
			return pRetVal;
		}
		pInfo = pInfo->m_pNext;
	}
	return NULL;
}

CJabberIqInfo* CJabberIqManager::DetachExpired()
{
	mir_cslock lck(m_cs);
	if (!m_pIqs)
		return NULL;

	DWORD dwCurrentTime = GetTickCount();

	CJabberIqInfo *pInfo = m_pIqs;
	if (dwCurrentTime - pInfo->m_dwRequestTime > pInfo->m_dwTimeout) {
		m_pIqs = pInfo->m_pNext;
		pInfo->m_pNext = NULL;
		return pInfo;
	}

	while (pInfo->m_pNext) {
		if (dwCurrentTime - pInfo->m_pNext->m_dwRequestTime > pInfo->m_pNext->m_dwTimeout) {
			CJabberIqInfo *pRetVal = pInfo->m_pNext;
			pInfo->m_pNext = pInfo->m_pNext->m_pNext;
			pRetVal->m_pNext = NULL;
			return pRetVal;
		}
		pInfo = pInfo->m_pNext;
	}
	return NULL;
}
	
// inserts pInfo at a place determined by pInfo->m_iPriority
BOOL CJabberIqManager::InsertIq(CJabberIqInfo *pInfo)
{ 
	mir_cslock lck(m_cs);
	if (!m_pIqs)
		m_pIqs = pInfo;
	else {
		if (m_pIqs->m_iPriority > pInfo->m_iPriority) {
			pInfo->m_pNext = m_pIqs;
			m_pIqs = pInfo;
		}
		else {
			CJabberIqInfo *pTmp = m_pIqs;
			while (pTmp->m_pNext && pTmp->m_pNext->m_iPriority <= pInfo->m_iPriority)
				pTmp = pTmp->m_pNext;
			pInfo->m_pNext = pTmp->m_pNext;
			pTmp->m_pNext = pInfo;
		}
	}
	return TRUE;
}

// fucking params, maybe just return CJabberIqRequestInfo pointer ?
CJabberIqPermanentInfo* CJabberIqManager::AddPermanentHandler(
	JABBER_PERMANENT_IQ_HANDLER pHandler,
	int nIqTypes,
	DWORD dwParamsToParse,
	const TCHAR *szXmlns,
	BOOL bAllowPartialNs,
	const TCHAR *szTag,
	void *pUserData,
	IQ_USER_DATA_FREE_FUNC pUserDataFree,
	int iPriority)
{
	CJabberIqPermanentInfo *pInfo = new CJabberIqPermanentInfo();
	if (!pInfo)
		return NULL;

	pInfo->m_pHandler = pHandler;
	pInfo->m_nIqTypes = nIqTypes ? nIqTypes : JABBER_IQ_TYPE_ANY;
	replaceStrT(pInfo->m_szXmlns, szXmlns);
	pInfo->m_bAllowPartialNs = bAllowPartialNs;
	replaceStrT(pInfo->m_szTag, szTag);
	pInfo->m_dwParamsToParse = dwParamsToParse;
	pInfo->m_pUserData = pUserData;
	pInfo->m_pUserDataFree = pUserDataFree;
	pInfo->m_iPriority = iPriority;

	mir_cslock lck(m_cs);
	if (!m_pPermanentHandlers)
		m_pPermanentHandlers = pInfo;
	else {
		if (m_pPermanentHandlers->m_iPriority > pInfo->m_iPriority) {
			pInfo->m_pNext = m_pPermanentHandlers;
			m_pPermanentHandlers = pInfo;
		}
		else {
			CJabberIqPermanentInfo* pTmp = m_pPermanentHandlers;
			while (pTmp->m_pNext && pTmp->m_pNext->m_iPriority <= pInfo->m_iPriority)
				pTmp = pTmp->m_pNext;
			pInfo->m_pNext = pTmp->m_pNext;
			pTmp->m_pNext = pInfo;
		}
	}
	return pInfo;
}

// returns TRUE when pInfo found, or FALSE otherwise
BOOL CJabberIqManager::DeletePermanentHandler(CJabberIqPermanentInfo *pInfo)
{		
	mir_cslock lck(m_cs);
	if (!m_pPermanentHandlers)
		return FALSE;

	if (m_pPermanentHandlers == pInfo) { // check first item
		m_pPermanentHandlers = m_pPermanentHandlers->m_pNext;
		delete pInfo;
		return TRUE;
	}

	CJabberIqPermanentInfo* pTmp = m_pPermanentHandlers;
	while (pTmp->m_pNext) {
		if (pTmp->m_pNext == pInfo) {
			pTmp->m_pNext = pTmp->m_pNext->m_pNext;
			delete pInfo;
			return TRUE;
		}
		pTmp = pTmp->m_pNext;
	}
	return FALSE;
}

BOOL CJabberIqManager::DeleteHandler(CJabberIqInfo *pInfo)
{
	// returns TRUE when pInfo found, or FALSE otherwise
	mir_cslockfull lck(m_cs);
	if (!m_pIqs)
		return FALSE;

	if (m_pIqs == pInfo) { // check first item
		m_pIqs = m_pIqs->m_pNext;
		lck.unlock();
		ExpireInfo(pInfo); // must expire it to allow the handler to free m_pUserData if necessary
		delete pInfo;
		return TRUE;
	}

	CJabberIqInfo *pTmp = m_pIqs;
	while (pTmp->m_pNext) {
		if (pTmp->m_pNext == pInfo) {
			pTmp->m_pNext = pTmp->m_pNext->m_pNext;
			lck.unlock();
			ExpireInfo(pInfo); // must expire it to allow the handler to free m_pUserData if necessary
			delete pInfo;
			return TRUE;
		}
		pTmp = pTmp->m_pNext;
	}
	return FALSE;
}
