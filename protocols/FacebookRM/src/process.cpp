/*

Facebook plugin for Miranda Instant Messenger
_____________________________________________

Copyright © 2009-11 Michal Zelinka, 2011-13 Robert Pösel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "common.h"

void FacebookProto::ProcessBuddyList(void* data)
{
	if (data == NULL)
		return;

	ScopedLock s(facy.buddies_lock_);

	std::string* resp = (std::string*)data;

	if (isOffline())
		goto exit;

	debugLogA("***** Starting processing buddy list");

	CODE_BLOCK_TRY

	facebook_json_parser* p = new facebook_json_parser(this);
	p->parse_buddy_list(data, &facy.buddies);
	delete p;

	for (List::Item< facebook_user >* i = facy.buddies.begin(); i != NULL;)
	{		
		facebook_user* fbu = i->data;

		std::string status;
		switch (fbu->status_id) {
			case ID_STATUS_OFFLINE:
				status = "Offline"; break;
			case ID_STATUS_ONLINE:
				status = "Online"; break;
			case ID_STATUS_ONTHEPHONE:
				status = "Mobile"; break;
		}
		
		if (fbu->idle)
			status += " (idle)";

		debugLogA("      Now %s: %s", status.c_str(), fbu->user_id.c_str());

		if (!fbu->deleted)
		{
			HANDLE hContact = fbu->handle;
			if (!hContact)
				hContact = AddToContactList(fbu, CONTACT_FRIEND);
			
			ptrT client( getTStringA(hContact, "MirVer"));
			if (!client || _tcscmp(client, fbu->getMirVer()))
				setTString(hContact, "MirVer", fbu->getMirVer());

			if (getDword(fbu->handle, "IdleTS", 0) != fbu->last_active) {
				if ((fbu->idle || fbu->status_id == ID_STATUS_OFFLINE) && fbu->last_active > 0)
					setDword(fbu->handle, "IdleTS", fbu->last_active);
				else
					delSetting(fbu->handle, "IdleTS");
			}
		}

		if (fbu->status_id == ID_STATUS_OFFLINE || fbu->deleted)
		{
			if (fbu->handle)
				setWord(fbu->handle, "Status", ID_STATUS_OFFLINE);

			std::string to_delete(i->key);
			i = i->next;
			facy.buddies.erase(to_delete);
		} else {
			i = i->next;

			if (!fbu->handle) // just been added
				fbu->handle = AddToContactList(fbu, CONTACT_FRIEND);

			if (getWord(fbu->handle, "Status", 0) != fbu->status_id)
				setWord(fbu->handle, "Status", fbu->status_id);

			if (getDword(fbu->handle, "LastActiveTS", 0) != fbu->last_active) {
				if (fbu->last_active > 0)
					setDword(fbu->handle, "LastActiveTS", fbu->last_active);
				else
					delSetting(fbu->handle, "LastActiveTS");
			}

			if (getByte(fbu->handle, FACEBOOK_KEY_CONTACT_TYPE, 0) != CONTACT_FRIEND) {
				setByte(fbu->handle, FACEBOOK_KEY_CONTACT_TYPE, CONTACT_FRIEND);
				// TODO: remove that popup and use "Contact added you" event?
			}

			// Wasn't contact removed from "server-list" someday?
			if (getDword(fbu->handle, FACEBOOK_KEY_DELETED, 0)) {
				delSetting(fbu->handle, FACEBOOK_KEY_DELETED);

				std::string url = FACEBOOK_URL_PROFILE + fbu->user_id;					

				ptrT szTitle( mir_utf8decodeT(fbu->real_name.c_str()));
				NotifyEvent(szTitle, TranslateT("Contact is back on server-list."), fbu->handle, FACEBOOK_EVENT_OTHER, &url);
			}

			// Check avatar change
			CheckAvatarChange(fbu->handle, fbu->image_url);
		}
	}

	debugLogA("***** Buddy list processed");

	CODE_BLOCK_CATCH

	debugLogA("***** Error processing buddy list: %s", e.what());

	CODE_BLOCK_END

exit:
	delete resp;
}

void FacebookProto::ProcessFriendList(void* data)
{
	if (data == NULL)
		return;

	std::string* resp = (std::string*)data;

	debugLogA("***** Starting processing friend list");

	CODE_BLOCK_TRY

	std::map<std::string, facebook_user*> friends;

	facebook_json_parser* p = new facebook_json_parser(this);
	p->parse_friends(data, &friends);
	delete p;


	// Check and update old contacts
	for (HANDLE hContact = db_find_first(m_szModuleName); hContact; hContact = db_find_next(hContact, m_szModuleName)) {
		if ( isChatRoom(hContact))
			continue;

		DBVARIANT dbv;
		facebook_user *fbu;
		if (!getString(hContact, FACEBOOK_KEY_ID, &dbv)) {
			std::string id = dbv.pszVal;
			db_free(&dbv);
			
			std::map< std::string, facebook_user* >::iterator iter;
			
			if ((iter = friends.find(id)) != friends.end()) {
				// Found contact, update it and remove from map
				fbu = iter->second;

				// TODO RM: remove, because contacts cant change it, so its only for "first run"
					// - but what with contacts, that was added after logon?
				// Update gender
				if (getByte(hContact, "Gender", 0) != fbu->gender)
					setByte(hContact, "Gender", fbu->gender);

				// Update name
				DBVARIANT dbv;
				bool update_required = true;

				// TODO: remove in some future version?
				ptrA realname(getStringA(hContact, "RealName"));
				if (realname != NULL) {
					delSetting(hContact, "RealName");
				}
				else if (!getStringUtf(hContact, FACEBOOK_KEY_NICK, &dbv))
				{
					update_required = strcmp(dbv.pszVal, fbu->real_name.c_str()) != 0;
					db_free(&dbv);
				}
				if (update_required)
				{
					SaveName(hContact, fbu);
				}

				if (getByte(hContact, FACEBOOK_KEY_CONTACT_TYPE, 0) != CONTACT_FRIEND) {
					setByte(hContact, FACEBOOK_KEY_CONTACT_TYPE, CONTACT_FRIEND);
					// TODO: remove that popup and use "Contact added you" event?
				}

				// Wasn't contact removed from "server-list" someday?
				if (getDword(hContact, FACEBOOK_KEY_DELETED, 0)) {
					delSetting(hContact, FACEBOOK_KEY_DELETED);

					std::string url = FACEBOOK_URL_PROFILE + fbu->user_id;					

					ptrT szTitle( mir_utf8decodeT(fbu->real_name.c_str()));
					NotifyEvent(szTitle, TranslateT("Contact is back on server-list."), hContact, FACEBOOK_EVENT_OTHER, &url);
				}

				// Check avatar change
				CheckAvatarChange(hContact, fbu->image_url);

				// Mark this contact as deleted ("processed") and delete them later (as there may be some duplicit contacts to use)
				fbu->deleted = true;
			} else {
				// Contact was removed from "server-list", notify it

				// Wasnt we already been notified about this contact? And was this real friend?
				if (!getDword(hContact, FACEBOOK_KEY_DELETED, 0) && getByte(hContact, FACEBOOK_KEY_CONTACT_TYPE, 0) == CONTACT_FRIEND) {
					setDword(hContact, FACEBOOK_KEY_DELETED, ::time(NULL));
					setByte(hContact, FACEBOOK_KEY_CONTACT_TYPE, CONTACT_NONE);

					std::string contactname = id;
					if (!getStringUtf(hContact, FACEBOOK_KEY_NICK, &dbv)) {
						contactname = dbv.pszVal;
						db_free(&dbv);
					}

					std::string url = FACEBOOK_URL_PROFILE + id;

					ptrT szTitle( mir_utf8decodeT(contactname.c_str()));
					NotifyEvent(szTitle, TranslateT("Contact is no longer on server-list."), hContact, FACEBOOK_EVENT_OTHER, &url);
				}
			}
		}
	}

	// Check remaining contacts in map and add them to contact list
	for (std::map< std::string, facebook_user* >::iterator iter = friends.begin(); iter != friends.end(); ++iter) {
		facebook_user *fbu = iter->second;

		if (!fbu->deleted)
			HANDLE hContact = AddToContactList(fbu, CONTACT_FRIEND/*, true*/); // This contact is surely new ...am I sure? ...I'm not, so "true" is commented now

		delete fbu;
	}
	
	friends.clear();

	debugLogA("***** Friend list processed");

	CODE_BLOCK_CATCH

	debugLogA("***** Error processing friend list: %s", e.what());

	CODE_BLOCK_END

	delete resp;
}

void FacebookProto::ProcessUnreadMessages(void*)
{
	facy.handle_entry("ProcessUnreadMessages");

	// receive messages from all folders by default, use hidden setting to receive only inbox messages
	bool inboxOnly = getBool(FACEBOOK_KEY_INBOX_ONLY, 0);

	std::string data = "folders[0]=inbox";
	if (!inboxOnly)
		data += "&folders[1]=other";
	data += "&client=mercury";
	data += "__user=" + facy.self_.user_id;
	data += "&fb_dtsg=" + (facy.dtsg_.length() ? facy.dtsg_ : "0");
	data += "&__a=1&__dyn=&__req=&ttstamp=0";

	http::response resp = facy.flap(REQUEST_UNREAD_THREADS, &data);

	if (resp.code == HTTP_CODE_OK) {
		
		CODE_BLOCK_TRY
		
		std::vector<std::string> threads;

		facebook_json_parser* p = new facebook_json_parser(this);
		p->parse_unread_threads(&resp.data, &threads, inboxOnly);
		delete p;

		ForkThread(&FacebookProto::ProcessUnreadMessage, new std::vector<std::string>(threads));

		debugLogA("***** Unread threads list processed");

		CODE_BLOCK_CATCH

		debugLogA("***** Error processing unread threads list: %s", e.what());

		CODE_BLOCK_END
	
		facy.handle_success("ProcessUnreadMessages");
	} else {
		facy.handle_error("ProcessUnreadMessages");
	}	
}

void FacebookProto::ProcessUnreadMessage(void *p)
{
	if (p == NULL)
		return;

	facy.handle_entry("ProcessUnreadMessage");

	std::vector<std::string> threads = *(std::vector<std::string>*)p;
	delete (std::vector<std::string>*)p;

	int offset = 0;
	int limit = 21;

	// don't use local_timestamp for unread messages by default, use hidden setting to enable it
	bool local_timestamp = getBool(FACEBOOK_KEY_LOCAL_TIMESTAMP_UNREAD, 0);

	// receive messages from all folders by default, use hidden setting to receive only inbox messages
	bool inboxOnly = getBool(FACEBOOK_KEY_INBOX_ONLY, 0);

	http::response resp;

	while (!threads.empty()) {
		std::string data = "client=mercury";
		data += "&__user=" + facy.self_.user_id;
		data += "&fb_dtsg=" + (facy.dtsg_.length() ? facy.dtsg_ : "0");
		data += "&__a=1&__dyn=&__req=&ttstamp=0";
	
		for (std::vector<std::string>::size_type i = 0; i < threads.size(); i++) {
			std::string thread_id = utils::url::encode(threads[i]);

			// request messages from thread
			data += "&messages[thread_ids][" + thread_id;
			data += "][offset]=" + utils::conversion::to_string(&offset, UTILS_CONV_SIGNED_NUMBER);
			data += "&messages[thread_ids][" + thread_id;
			data += "][limit]=" + utils::conversion::to_string(&limit, UTILS_CONV_SIGNED_NUMBER);
			
			// request info about thread
			data += "&threads[thread_ids][" + utils::conversion::to_string(&i, UTILS_CONV_UNSIGNED_NUMBER);
			data += "]=" + thread_id;
		}

		resp = facy.flap(REQUEST_THREAD_INFO, &data);

		if (resp.code == HTTP_CODE_OK) {
		
			CODE_BLOCK_TRY
		
			std::vector<facebook_message*> messages;
			std::map<std::string, facebook_chatroom*> chatrooms;

			facebook_json_parser* p = new facebook_json_parser(this);
			p->parse_thread_messages(&resp.data, &messages, &chatrooms, true, inboxOnly, limit);
			delete p;	

			for (std::map<std::string, facebook_chatroom*>::iterator it = chatrooms.begin(); it != chatrooms.end(); ) {

				facebook_chatroom *room = it->second;
				HANDLE hChatContact = NULL;
				if (GetChatUsers(room->thread_id.c_str()) == NULL) {
					AddChat(room->thread_id.c_str(), room->chat_name.c_str());
					hChatContact = ChatIDToHContact(room->thread_id);
					// Set thread id (TID) for later
					setTString(hChatContact, FACEBOOK_KEY_TID, room->thread_id.c_str());

					for (std::map<std::string, std::string>::iterator jt = room->participants.begin(); jt != room->participants.end(); ) {
						AddChatContact(room->thread_id.c_str(), jt->first.c_str(), jt->second.c_str());
						++jt;
					}
				}

				if (!hChatContact)
					hChatContact = ChatIDToHContact(room->thread_id);

				setTString(hChatContact, FACEBOOK_KEY_MESSAGE_ID, room->thread_id.c_str());
				ForkThread(&FacebookProto::ReadMessageWorker, hChatContact);

				delete it->second;
				it = chatrooms.erase(it);
			}
			chatrooms.clear();


			for (std::vector<facebook_message*>::size_type i = 0; i < messages.size(); i++) {				
				if (messages[i]->isChat) {
					debugLogA("      Got chat message: %s", messages[i]->message_text.c_str());
					UpdateChat(_A2T(messages[i]->thread_id.c_str()), messages[i]->user_id.c_str(), messages[i]->sender_name.c_str(), messages[i]->message_text.c_str(), local_timestamp || !messages[i]->time ? ::time(NULL) : messages[i]->time);
				} else if (messages[i]->user_id != facy.self_.user_id) {
					debugLogA("      Got message: %s", messages[i]->message_text.c_str());
					facebook_user fbu;
					fbu.user_id = messages[i]->user_id;
					fbu.real_name = messages[i]->sender_name;

					// TODO: optimize this?
					HANDLE hContact = AddToContactList(&fbu, CONTACT_NONE);
					setString(hContact, FACEBOOK_KEY_MESSAGE_ID, messages[i]->message_id.c_str());
					
					// Save TID if not exists already
					ptrA tid( getStringA(hContact, FACEBOOK_KEY_TID));
					if (!tid || strcmp(tid, messages[i]->thread_id.c_str()))
						setString(hContact, FACEBOOK_KEY_TID, messages[i]->thread_id.c_str());

					// TODO: if contact is newly added, get his user info
					// TODO: maybe create new "receiveMsg" function and use it for offline and channel messages?

					ParseSmileys(messages[i]->message_text, hContact);

					if (messages[i]->isIncoming) {
						PROTORECVEVENT recv = {0};
						recv.flags = PREF_UTF;
						recv.szMessage = const_cast<char*>(messages[i]->message_text.c_str());
						recv.timestamp = local_timestamp || !messages[i]->time ? ::time(NULL) : messages[i]->time;
						ProtoChainRecvMsg(hContact, &recv);
					} else {
						DBEVENTINFO dbei = {0};
						dbei.cbSize = sizeof(dbei);
						dbei.eventType = EVENTTYPE_MESSAGE;
						dbei.flags = DBEF_SENT | DBEF_UTF;
						dbei.szModule = m_szModuleName;
						dbei.timestamp = local_timestamp || !messages[i]->time ? ::time(NULL) : messages[i]->time;
						dbei.cbBlob = (DWORD)messages[i]->message_text.length() + 1;
						dbei.pBlob = (PBYTE)messages[i]->message_text.c_str();
						db_event_add(hContact, &dbei);
					}
				}
				delete messages[i];
			}
			messages.clear();			

			debugLogA("***** Unread messages processed");

			CODE_BLOCK_CATCH

			debugLogA("***** Error processing unread messages: %s", e.what());

			CODE_BLOCK_END
	
			facy.handle_success("ProcessUnreadMessage");
		} else {
			facy.handle_error("ProcessUnreadMessage");
		}

		offset += limit;
		limit = 20; // TODO: use better limits?
		
		threads.clear(); // TODO: if we have limit messages from one user, there may be more unread messages... continue with it... otherwise remove that threadd from threads list -- or do it in json parser? hm			 = allow more than "limit" unread messages to be parsed
	}
}

// TODO: combine processmessages and processunreadmessages? (behavior of showing messages to user should be the same)
void FacebookProto::ProcessMessages(void* data)
{
	if (data == NULL)
		return;

	std::string* resp = (std::string*)data;

	// receive messages from all folders by default, use hidden setting to receive only inbox messages
	bool inboxOnly = getBool(FACEBOOK_KEY_INBOX_ONLY, 0);

	if (isOffline())
		goto exit;

	debugLogA("***** Starting processing messages");

	CODE_BLOCK_TRY

	std::vector< facebook_message* > messages;
	std::vector< facebook_notification* > notifications;

	facebook_json_parser* p = new facebook_json_parser(this);
	p->parse_messages(data, &messages, &notifications, inboxOnly);
	delete p;

	bool local_timestamp = getBool(FACEBOOK_KEY_LOCAL_TIMESTAMP, 0);

	for(std::vector<facebook_message*>::size_type i=0; i<messages.size(); i++)
	{
		debugLogA("      Got message: %s", messages[i]->message_text.c_str());
		facebook_user fbu;
		fbu.user_id = messages[i]->user_id;
		fbu.real_name = messages[i]->sender_name;

		HANDLE hContact = AddToContactList(&fbu, CONTACT_NONE);
		setString(hContact, FACEBOOK_KEY_MESSAGE_ID, messages[i]->message_id.c_str());

		// Save TID if not exists already
		ptrA tid(getStringA(hContact, FACEBOOK_KEY_TID));
		if (!tid || strcmp(tid, messages[i]->thread_id.c_str()))
			setString(hContact, FACEBOOK_KEY_TID, messages[i]->thread_id.c_str());

		// TODO: if contact is newly added, get his user info
		// TODO: maybe create new "receiveMsg" function and use it for offline and channel messages?

		ParseSmileys(messages[i]->message_text, hContact);

		if (messages[i]->isIncoming) {
			PROTORECVEVENT recv = {0};
			recv.flags = PREF_UTF;
			recv.szMessage = const_cast<char*>(messages[i]->message_text.c_str());
			recv.timestamp = local_timestamp || !messages[i]->time ? ::time(NULL) : messages[i]->time;
			ProtoChainRecvMsg(hContact, &recv);
		} else {
			DBEVENTINFO dbei = {0};
			dbei.cbSize = sizeof(dbei);
			dbei.eventType = EVENTTYPE_MESSAGE;
			dbei.flags = DBEF_SENT | DBEF_UTF;
			dbei.szModule = m_szModuleName;
			dbei.timestamp = local_timestamp || !messages[i]->time ? ::time(NULL) : messages[i]->time;
			dbei.cbBlob = (DWORD)messages[i]->message_text.length() + 1;
			dbei.pBlob = (PBYTE)messages[i]->message_text.c_str();
			db_event_add(hContact, &dbei);
		}
		delete messages[i];
	}
	messages.clear();

	for(std::vector<facebook_notification*>::size_type i=0; i<notifications.size(); i++)
	{
		debugLogA("      Got notification: %s", notifications[i]->text.c_str());
		ptrT szText( mir_utf8decodeT(notifications[i]->text.c_str()));
		NotifyEvent(m_tszUserName, szText, ContactIDToHContact(notifications[i]->user_id), FACEBOOK_EVENT_NOTIFICATION, &notifications[i]->link, &notifications[i]->id);
		delete notifications[i];
	}
	notifications.clear();

	debugLogA("***** Messages processed");

	CODE_BLOCK_CATCH

	debugLogA("***** Error processing messages: %s", e.what());

	CODE_BLOCK_END

exit:
	delete resp;
}

void FacebookProto::ProcessNotifications(void*)
{
	if (isOffline())
		return;

	if (!getByte(FACEBOOK_KEY_EVENT_NOTIFICATIONS_ENABLE, DEFAULT_EVENT_NOTIFICATIONS_ENABLE))
		return;

	facy.handle_entry("notifications");

	// Get notifications
	http::response resp = facy.flap(REQUEST_NOTIFICATIONS);

	if (resp.code != HTTP_CODE_OK) {
		facy.handle_error("notifications");
		return;
	}


	// Process notifications
	debugLogA("***** Starting processing notifications");

	CODE_BLOCK_TRY

	std::vector< facebook_notification* > notifications;

	facebook_json_parser* p = new facebook_json_parser(this);
	p->parse_notifications(&(resp.data), &notifications);
	delete p;

	for(std::vector<facebook_notification*>::size_type i=0; i<notifications.size(); i++)
	{
		debugLogA("      Got notification: %s", notifications[i]->text.c_str());
		ptrT szText( mir_utf8decodeT(notifications[i]->text.c_str()));
		NotifyEvent(m_tszUserName, szText, ContactIDToHContact(notifications[i]->user_id), FACEBOOK_EVENT_NOTIFICATION, &notifications[i]->link, &notifications[i]->id);
		delete notifications[i];
	}
	notifications.clear();

	debugLogA("***** Notifications processed");

	CODE_BLOCK_CATCH

	debugLogA("***** Error processing notifications: %s", e.what());

	CODE_BLOCK_END
}

void FacebookProto::ProcessFriendRequests(void*)
{
	facy.handle_entry("friendRequests");

	// Get notifications
	http::response resp = facy.flap(REQUEST_LOAD_REQUESTS);

	if (resp.code != HTTP_CODE_OK) {
		facy.handle_error("friendRequests");
		return;
	}
	
	// Parse it
	std::string reqs = utils::text::source_get_value(&resp.data, 2, "class=\"mRequestItem", "al aps\">");

	std::string::size_type pos = 0;
	std::string::size_type pos2 = 0;
	bool last = false;

	while (!last && !reqs.empty()) {
		std::string req;
		if ((pos2 = reqs.find("class=\"mRequestItem", pos)) != std::string::npos) {
			req = reqs.substr(pos, pos2 - pos);
			pos = pos2 + 19;
		} else {
			req = reqs.substr(pos);
			last = true;
		}
				
		std::string get = utils::text::source_get_value(&req, 3, "<form", "action=\"", "\">");
		std::string time = utils::text::source_get_value2(&get, "seenrequesttime=", "&\"");

		facebook_user *fbu = new facebook_user();
		fbu->real_name = utils::text::source_get_value(&req, 2, "class=\"actor\">", "</");
		fbu->user_id = utils::text::source_get_value(&get, 2, "id=", "&");

		if (fbu->user_id.length() && fbu->real_name.length())
		{
			HANDLE hContact = AddToContactList(fbu, CONTACT_APPROVE);
			setByte(hContact, FACEBOOK_KEY_CONTACT_TYPE, CONTACT_APPROVE);

			bool seen = false;

			DBVARIANT dbv;
			if (!getString(hContact, "RequestTime", &dbv)) {
				seen = !strcmp(dbv.pszVal, time.c_str());
				db_free(&dbv);
			}

			if (!seen) {
				// This is new request
				setString(hContact, "RequestTime", time.c_str());

				//blob is: uin(DWORD), hContact(HANDLE), nick(ASCIIZ), first(ASCIIZ), last(ASCIIZ), email(ASCIIZ), reason(ASCIIZ)
				//blob is: 0(DWORD), hContact(HANDLE), nick(ASCIIZ), ""(ASCIIZ), ""(ASCIIZ), ""(ASCIIZ), ""(ASCIIZ)
				DBEVENTINFO dbei = {0};
				dbei.cbSize = sizeof(DBEVENTINFO);
				dbei.szModule = m_szModuleName;
				dbei.timestamp = ::time(NULL);
				dbei.flags = DBEF_UTF;
				dbei.eventType = EVENTTYPE_AUTHREQUEST;
				dbei.cbBlob = (DWORD)(sizeof(DWORD)*2 + fbu->real_name.length() + 5);
					
				PBYTE pCurBlob = dbei.pBlob = (PBYTE) mir_alloc(dbei.cbBlob);					
				*(PDWORD)pCurBlob = 0; pCurBlob += sizeof(DWORD);                    // UID
				*(PDWORD)pCurBlob = (DWORD)hContact; pCurBlob += sizeof(DWORD);      // Contact Handle
				strcpy((char*)pCurBlob, fbu->real_name.data()); pCurBlob += fbu->real_name.length()+1;	// Nickname
				*pCurBlob = '\0'; pCurBlob++;                                        // First Name
				*pCurBlob = '\0'; pCurBlob++;                                        // Last Name
				*pCurBlob = '\0'; pCurBlob++;                                        // E-mail
				*pCurBlob = '\0';                                                    // Reason

				db_event_add(0, &dbei);				

				debugLogA("      (New) Friendship request from: %s (%s) [%s]", fbu->real_name.c_str(), fbu->user_id.c_str(), time.c_str());
			} else {
				debugLogA("      (Old) Friendship request from: %s (%s) [%s]", fbu->real_name.c_str(), fbu->user_id.c_str(), time.c_str());
			}
		} else {
			debugLogA(" !!!  Wrong friendship request");
			debugLogA(req.c_str());
		}
	}

	facy.handle_success("friendRequests");
}

void FacebookProto::ProcessFeeds(void* data)
{
	if (data == NULL)
		return;

	std::string* resp = (std::string*)data;

	if (!isOnline())
		goto exit;

	CODE_BLOCK_TRY

	debugLogA("***** Starting processing feeds");

	std::vector< facebook_newsfeed* > news;

	std::string::size_type pos = 0;
	UINT limit = 0;

	*resp = utils::text::slashu_to_utf8(*resp);	
	*resp = utils::text::source_get_value(resp, 2, "\"html\":\"", ">\"");

	while ((pos = resp->find("<div class=\\\"mainWrapper\\\"", pos)) != std::string::npos && limit <= 25)
	{		
		std::string::size_type pos2 = resp->find("<div class=\\\"mainWrapper\\\"", pos+5);
		if (pos2 == std::string::npos)
			pos2 = resp->length();
		
		std::string post = resp->substr(pos, pos2 - pos);
		pos += 5;

		std::string post_header = utils::text::source_get_value(&post, 4, "<h5 class=", "uiStreamHeadline", ">", "<\\/h5>");
		std::string post_message = utils::text::source_get_value(&post, 3, "<h5 class=\\\"uiStreamMessage userContentWrapper", ">", "<\\/h5>");
		std::string post_link = utils::text::source_get_value(&post, 3, "<span class=\\\"uiStreamSource\\\"", ">", "<\\/span>");
		std::string post_attach = utils::text::source_get_value(&post, 4, "<div class=", "uiStreamAttachments", ">", "<form");

		//std::string post_time = utils::text::source_get_value(&post_link, 2, "data-utime=\\\"", "\\\"");
		//std::string post_time_text = utils::text::source_get_value(&post_link, 3, "class=\\\"timestamp livetimestamp", ">", "<");

		// in title keep only name, end of events like "X shared link" put into message
		pos2 = post_header.find("<\\/a>") + 5;
		post_message = post_header.substr(pos2, post_header.length() - pos2) + post_message;
		post_header = post_header.substr(0, pos2);

		// append attachement to message (if any)
		post_message += utils::text::trim(post_attach);

		facebook_newsfeed* nf = new facebook_newsfeed;

		nf->title = utils::text::trim(
			utils::text::special_expressions_decode(
				utils::text::remove_html(post_header)));

		nf->user_id = utils::text::source_get_value(&post_header, 2, "user.php?id=", "\\\"");
		
		nf->link = utils::text::special_expressions_decode(
				utils::text::source_get_value(&post_link, 2, "href=\\\"", "\\\">"));

		nf->text = utils::text::trim(
			utils::text::special_expressions_decode(
				utils::text::remove_html(
					utils::text::edit_html(post_message))));

		//nf->text += "\n" + post_time_text;

		if (!nf->title.length() || !nf->text.length())
		{
			delete nf;
			continue;
		}

		if (nf->text.length() > 500)
		{
			nf->text = nf->text.substr(0, 500);
			nf->text += "...";
		}

		news.push_back(nf);
		pos++;
		limit++;
	}

	for(std::vector<facebook_newsfeed*>::size_type i=0; i<news.size(); i++)
	{
		debugLogA("      Got newsfeed: %s %s", news[i]->title.c_str(), news[i]->text.c_str());
		ptrT szTitle( mir_utf8decodeT(news[i]->title.c_str()));
		ptrT szText( mir_utf8decodeT(news[i]->text.c_str()));
		NotifyEvent(szTitle,szText,this->ContactIDToHContact(news[i]->user_id),FACEBOOK_EVENT_NEWSFEED, &news[i]->link);
		delete news[i];
	}
	news.clear();

	this->facy.last_feeds_update_ = ::time(NULL);

	debugLogA("***** Feeds processed");

	CODE_BLOCK_CATCH

	debugLogA("***** Error processing feeds: %s", e.what());

	CODE_BLOCK_END

exit:
	delete resp;
}

void FacebookProto::SearchAckThread(void *targ)
{
	facy.handle_entry("searchAckThread");

	int count = 0;

	char *arg = mir_utf8encodeT((TCHAR*)targ);
	std::string search = utils::url::encode(arg);
	std::string ssid;

	while (count < 50 && !isOffline())
	{
		std::string get_data = search + "&s=" + utils::conversion::to_string(&count, UTILS_CONV_UNSIGNED_NUMBER);
		if (!ssid.empty())
			get_data += "&ssid=" + ssid;

		http::response resp = facy.flap(REQUEST_SEARCH, NULL, &get_data);

		if (resp.code == HTTP_CODE_OK)
		{
			std::string items = utils::text::source_get_value(&resp.data, 2, "<body", "</body>");

			std::string::size_type pos = 0;
			std::string::size_type pos2 = 0;

			while ((pos = items.find("<td class=\"pic\">", pos)) != std::string::npos) {
				std::string item = items.substr(pos, items.find("</tr>", pos) - pos);
				pos++; count++;

				std::string id = utils::text::source_get_value2(&item, "?id=", "&\"");
				if (id.empty())
					id = utils::text::source_get_value2(&item, "?ids=", "&\"");

				std::string name = utils::text::source_get_value(&item, 4, "<td class=\"name\">", "<a", ">", "</");
				std::string surname;
				std::string nick;
				std::string common = utils::text::source_get_value(&item, 2, "<span class=\"mfss fcg\">", "</span>");

				if ((pos2 = name.find("<span class=\"alternate_name\">")) != std::string::npos) {
					nick = name.substr(pos2 + 30, name.length() - pos2 - 31); // also remove brackets around nickname
					name = name.substr(0, pos2 - 1);
				}

				if ((pos2 = name.find(" ")) != std::string::npos) {
					surname = name.substr(pos2 + 1, name.length() - pos2 - 1);
					name = name.substr(0, pos2);
				}

				// ignore self contact and empty ids
				if (id.empty() || id == facy.self_.user_id)
					continue;

				ptrT tid( mir_utf8decodeT(id.c_str()));
				ptrT tname( mir_utf8decodeT(utils::text::special_expressions_decode(name).c_str()));
				ptrT tsurname( mir_utf8decodeT(utils::text::special_expressions_decode(surname).c_str()));
				ptrT tnick( mir_utf8decodeT(utils::text::special_expressions_decode(nick).c_str()));
				ptrT tcommon( mir_utf8decodeT(utils::text::special_expressions_decode(common).c_str()));

				PROTOSEARCHRESULT isr = {0};
				isr.cbSize = sizeof(isr);
				isr.flags = PSR_TCHAR;
				isr.id  = tid;
				isr.nick  = tnick;
				isr.firstName = tname;
				isr.lastName = tsurname;
				isr.email = tcommon;

				ProtoBroadcastAck(NULL, ACKTYPE_SEARCH, ACKRESULT_DATA, targ, (LPARAM)&isr);
			}

			ssid = utils::text::source_get_value(&items, 3, "id=\"more_objects\"", "ssid=", "&");
			if (ssid.empty())
				break; // No more results
		}
	}

	ProtoBroadcastAck(NULL, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, targ, 0);

	facy.handle_success("searchAckThread");

	mir_free(targ);
	mir_free(arg);
}

void FacebookProto::SearchIdAckThread(void *targ)
{
	facy.handle_entry("searchIdAckThread");

	char *arg = mir_utf8encodeT((TCHAR*)targ);
	std::string search = utils::url::encode(arg) + "?";

	if (!isOffline())
	{
		http::response resp = facy.flap(REQUEST_USER_INFO, NULL, &search);

		if (resp.code == HTTP_CODE_FOUND && resp.headers.find("Location") != resp.headers.end()) {
			search = utils::text::source_get_value(&resp.headers["Location"], 2, FACEBOOK_SERVER_MOBILE"/", "_rdr", true);
			resp = facy.flap(REQUEST_USER_INFO, NULL, &search);
		}

		if (resp.code == HTTP_CODE_OK)
		{
			std::string about = utils::text::source_get_value(&resp.data, 2, "<div class=\"timeline", "id=\"footer");
		
			std::string id = utils::text::source_get_value2(&about, ";id=", "&\"");
			if (id.empty())
				id = utils::text::source_get_value2(&about, "?bid=", "&\"");
			std::string name = utils::text::source_get_value(&about, 3, "class=\"profileName", ">", "</");
			std::string surname;

			std::string::size_type pos;
			if ((pos = name.find(" ")) != std::string::npos) {
				surname = name.substr(pos + 1, name.length() - pos - 1);
				name = name.substr(0, pos);
			}

			// ignore self contact and empty ids
			if (!id.empty() && id != facy.self_.user_id){
				ptrT tid( mir_utf8decodeT(id.c_str()));
				ptrT tname( mir_utf8decodeT(name.c_str()));
				ptrT tsurname( mir_utf8decodeT(surname.c_str()));

				PROTOSEARCHRESULT isr = {0};
				isr.cbSize = sizeof(isr);
				isr.flags = PSR_TCHAR;
				isr.id = tid;
				isr.firstName = tname;
				isr.lastName = tsurname;

				ProtoBroadcastAck(NULL, ACKTYPE_SEARCH, ACKRESULT_DATA, targ, (LPARAM)&isr);
			}
		}
	}

	ProtoBroadcastAck(NULL, ACKTYPE_SEARCH, ACKRESULT_SUCCESS, targ, 0);	

	facy.handle_success("searchIdAckThread");	

	mir_free(targ);
	mir_free(arg);
}
