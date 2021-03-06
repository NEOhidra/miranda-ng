/*
Copyright (c) 2013-14 Miranda NG project (http://miranda-ng.org)

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#define PS_CREATECHAT "/CreateNewChat"

struct CVkProto;
typedef void (CVkProto::*VK_REQUEST_HANDLER)(NETLIBHTTPREQUEST*, struct AsyncHttpRequest*);

struct AsyncHttpRequest : public NETLIBHTTPREQUEST, public MZeroedObject
{
	AsyncHttpRequest();
	AsyncHttpRequest(CVkProto*, int iRequestType, LPCSTR szUrl, bool bSecure, VK_REQUEST_HANDLER pFunc);
	~AsyncHttpRequest();

	void AddHeader(LPCSTR, LPCSTR);
	void Redirect(NETLIBHTTPREQUEST*);

	CMStringA m_szUrl;
	bool bNeedsRestart, bIsMainConn, m_bHasParams;
	VK_REQUEST_HANDLER m_pFunc;
	void *pUserInfo;
};

struct PARAM
{
	LPCSTR szName;
	__forceinline PARAM(LPCSTR _name) : szName(_name) {}
};

struct INT_PARAM : public PARAM
{
	int iValue;
	__forceinline INT_PARAM(LPCSTR _name, int _value) :
		PARAM(_name), iValue(_value)
	{
	}
};
AsyncHttpRequest* operator<<(AsyncHttpRequest*, const INT_PARAM&);

struct CHAR_PARAM : public PARAM
{
	LPCSTR szValue;
	__forceinline CHAR_PARAM(LPCSTR _name, LPCSTR _value) :
		PARAM(_name), szValue(_value)
	{
	}
};
AsyncHttpRequest* operator<<(AsyncHttpRequest*, const CHAR_PARAM&);

struct TCHAR_PARAM : public PARAM
{
	LPCTSTR tszValue;
	__forceinline TCHAR_PARAM(LPCSTR _name, LPCTSTR _value) :
		PARAM(_name), tszValue(_value)
	{
	}
};
AsyncHttpRequest* operator<<(AsyncHttpRequest*, const TCHAR_PARAM&);

struct CVkChatMessage : public MZeroedObject
{
	CVkChatMessage(int _id) : m_mid(_id) {}

	int  m_mid, m_uid, m_date;
	bool m_bHistory;
	ptrT m_tszBody;
};

struct CVkChatUser : public MZeroedObject
{
	CVkChatUser(int _id) : m_uid(_id) {}

	int  m_uid;
	bool m_bDel, m_bUnknown;
	ptrT m_tszNick;
};

struct CVkChatInfo : public MZeroedObject
{
	CVkChatInfo(int _id) :
		m_users(10, NumericKeySortT),
		m_msgs(10, NumericKeySortT),
		m_chatid(_id)
		{}

	int m_chatid, m_admin_id;
	bool m_bHistoryRead;
	ptrT m_tszTopic, m_tszId;
	HANDLE m_hContact;
	OBJLIST<CVkChatUser> m_users;
	OBJLIST<CVkChatMessage> m_msgs;

	CVkChatUser* GetUserById(LPCTSTR);
};

struct CVkProto : public PROTO<CVkProto>
{
				CVkProto(const char*, const TCHAR*);
				~CVkProto();

	//====================================================================================
	// PROTO_INTERFACE
	//====================================================================================

	virtual	HANDLE __cdecl AddToList(int flags, PROTOSEARCHRESULT* psr);
	virtual	HANDLE __cdecl AddToListByEvent(int flags, int iContact, HANDLE hDbEvent);

	virtual	int    __cdecl Authorize(HANDLE hDbEvent);
	virtual	int    __cdecl AuthDeny(HANDLE hDbEvent, const TCHAR *szReason);
	virtual	int    __cdecl AuthRecv(HANDLE hContact, PROTORECVEVENT*);
	virtual	int    __cdecl AuthRequest(HANDLE hContact, const TCHAR *szMessage);

	virtual	HANDLE __cdecl ChangeInfo(int iInfoType, void* pInfoData);

	virtual	HANDLE __cdecl FileAllow(HANDLE hContact, HANDLE hTransfer, const TCHAR *szPath);
	virtual	int    __cdecl FileCancel(HANDLE hContact, HANDLE hTransfer);
	virtual	int    __cdecl FileDeny(HANDLE hContact, HANDLE hTransfer, const TCHAR *szReason);
	virtual	int    __cdecl FileResume(HANDLE hTransfer, int* action, const TCHAR** szFilename);

	virtual	DWORD_PTR __cdecl GetCaps(int type, HANDLE hContact = NULL);
	virtual	int    __cdecl GetInfo(HANDLE hContact, int infoType);

	virtual	HANDLE __cdecl SearchBasic(const TCHAR *id);
	virtual	HANDLE __cdecl SearchByEmail(const TCHAR *email);
	virtual	HANDLE __cdecl SearchByName(const TCHAR *nick, const TCHAR *firstName, const TCHAR *lastName);
	virtual	HWND   __cdecl SearchAdvanced(HWND owner);
	virtual	HWND   __cdecl CreateExtendedSearchUI(HWND owner);

	virtual	int    __cdecl RecvContacts(HANDLE hContact, PROTORECVEVENT*);
	virtual	int    __cdecl RecvFile(HANDLE hContact, PROTORECVFILET*);
	virtual	int    __cdecl RecvMsg(HANDLE hContact, PROTORECVEVENT*);
	virtual	int    __cdecl RecvUrl(HANDLE hContact, PROTORECVEVENT*);

	virtual	int    __cdecl SendContacts(HANDLE hContact, int flags, int nContacts, HANDLE* hContactsList);
	virtual	HANDLE __cdecl SendFile(HANDLE hContact, const TCHAR *szDescription, TCHAR** ppszFiles);
	virtual	int    __cdecl SendMsg(HANDLE hContact, int flags, const char* msg);
	virtual	int    __cdecl SendUrl(HANDLE hContact, int flags, const char* url);

	virtual	int    __cdecl SetApparentMode(HANDLE hContact, int mode);
	virtual	int    __cdecl SetStatus(int iNewStatus);

	virtual	HANDLE __cdecl GetAwayMsg(HANDLE hContact);
	virtual	int    __cdecl RecvAwayMsg(HANDLE hContact, int mode, PROTORECVEVENT* evt);
	virtual	int    __cdecl SetAwayMsg(int m_iStatus, const TCHAR *msg);

	virtual	int    __cdecl UserIsTyping(HANDLE hContact, int type);

	virtual	int    __cdecl OnEvent(PROTOEVENTTYPE eventType, WPARAM wParam, LPARAM lParam);

	//==== Events ========================================================================

	int __cdecl OnModulesLoaded(WPARAM, LPARAM);
	int __cdecl OnOptionsInit(WPARAM, LPARAM);
	int __cdecl OnPreShutdown(WPARAM, LPARAM);

	void OnOAuthAuthorize(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	void OnReceiveAvatar(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	void OnReceiveSmth(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	//==== Services ======================================================================

	INT_PTR __cdecl SvcCreateAccMgrUI(WPARAM, LPARAM);
	INT_PTR __cdecl SvcGetAvatarInfo(WPARAM, LPARAM);
	INT_PTR __cdecl SvcGetAvatarCaps(WPARAM, LPARAM);

	//==== Misc ==========================================================================

	TCHAR* GetUserStoredPassword(void);

	void RetrieveMyInfo(void);
	void OnReceiveMyInfo(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void RetrieveUserInfo(LONG userId);
	void OnReceiveUserInfo(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void RetrieveFriends();
	void OnReceiveFriends(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void MarkMessagesRead(const CMStringA &mids);
	void RetrieveMessagesByIds(const CMStringA &mids);
	void RetrieveUnreadMessages();
	void OnReceiveMessages(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	void OnSendMessage(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void RetrievePollingInfo();
	void OnReceivePollingInfo(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void __cdecl PollingThread(void*);
	int  PollServer();
	void PollUpdates(JSONNODE*);
	void OnReceivePolling(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	void SetServerStatus(int);

	void CreateNewChat(LPCSTR uids, LPCTSTR ptrszTitle);
	void OnCreateNewChat(NETLIBHTTPREQUEST*, AsyncHttpRequest*);

	__forceinline bool IsOnline() const { return m_bOnline; }
	
	__forceinline LPCTSTR getGroup() const { return m_defaultGroup; }
	__forceinline void setGroup(LPCTSTR grp) { m_defaultGroup = mir_tstrdup(grp); }

	UINT_PTR m_timer;

private:
	friend struct AsyncHttpRequest;

	LIST<AsyncHttpRequest> m_arRequestsQueue;
	CRITICAL_SECTION m_csRequestsQueue;
	CMStringA m_prevUrl;
	HANDLE m_evRequestsQueue;
	HANDLE m_hWorkerThread;
	bool   m_bTerminated, m_bServerDelivery;

	struct Cookie
	{
		Cookie(const CMStringA& name, const CMStringA& value, const CMStringA& domain) :
			m_name(name),
			m_value(value),
			m_domain(domain)
		{}

		CMStringA m_name, m_value, m_domain;
	};
	OBJLIST<Cookie> m_cookies;
	void   GrabCookies(NETLIBHTTPREQUEST *nhr);
	void   ApplyCookies(AsyncHttpRequest*);

	void   InitQueue();
	void   UninitQueue();
	void   ExecuteRequest(AsyncHttpRequest*);
	void   __cdecl WorkerThread(void*);

	AsyncHttpRequest* Push(AsyncHttpRequest*, int iTimeout = 10000);

	bool   RunCaptchaForm(LPCSTR szUrl, CMStringA&);
	bool   AutoFillForm(char*, CMStringA&, CMStringA&);

	bool      CheckJsonResult(AsyncHttpRequest *pReq, NETLIBHTTPREQUEST *reply, JSONNODE*);
	JSONNODE* CheckJsonResponse(AsyncHttpRequest *pReq, NETLIBHTTPREQUEST *reply, JSONROOT&);
	bool      ApplyCaptcha(AsyncHttpRequest *pReq, JSONNODE*);

	void   ConnectionFailed(int iReason);
	void   OnLoggedIn();
	void   OnLoggedOut();
	void   ShutdownSession();

	void   SetAvatarUrl(HANDLE hContact, LPCTSTR ptszUrl);
	void   GetAvatarFileName(HANDLE hContact, TCHAR* pszDest, size_t cbLen);

	HANDLE FindUser(LONG userid, bool bCreate = false);

	void   SetAllContactStatuses(int status);

	void   __cdecl SendMsgAck(void *param);

	bool   m_bOnline, m_bHideChats;

	LONG   m_myUserId;
	ptrA   m_szAccessToken;
	ptrT   m_defaultGroup;

	ptrA   m_pollingServer, m_pollingKey, m_pollingTs;
	HANDLE m_pollingConn, m_hPollingThread;
	ULONG  m_msgId;
	bool   m_prevError;

	LIST<void> m_sendIds;
	bool   CheckMid(int guid);

	static INT_PTR CALLBACK OptionsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

	OBJLIST<CVkChatInfo> m_chats;
	CVkChatInfo* AppendChat(int id, JSONNODE *pNode);
	void AppendChatMessage(int id, JSONNODE *pMsg, bool bIsHistory);
	void AppendChatMessage(CVkChatInfo *cc, int mid, int uid, int msgTime, LPCTSTR ptszBody, bool bIsHistory);
	void RetrieveChatInfo(CVkChatInfo*);
	void OnReceiveChatInfo(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	void OnSendChatMsg(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	void OnChatDestroy(NETLIBHTTPREQUEST*, AsyncHttpRequest*);
	int __cdecl OnChatEvent(WPARAM, LPARAM);
	int __cdecl OnGcMenuHook(WPARAM, LPARAM);
	void LogMenuHook(CVkChatInfo*, GCHOOK*);
	void NickMenuHook(CVkChatInfo*, GCHOOK*);
	LPTSTR ChangeChatTopic(CVkChatInfo*);
	void SetChatStatus(HANDLE hContact, int iStatus);
	CVkChatInfo* GetChatById(LPCTSTR ptszId);
	INT_PTR __cdecl SvcCreateChat(WPARAM, LPARAM);

	CMString GetAttachmentDescr(JSONNODE*);
};
