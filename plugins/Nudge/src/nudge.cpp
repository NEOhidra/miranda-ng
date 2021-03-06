#include "headers.h"

void CNudge::Save(void)
{
	char SectionName[512];
	mir_snprintf(SectionName, 512, "useByProtocol");
	db_set_b(NULL, MODULENAME, SectionName, this->useByProtocol);
	mir_snprintf(SectionName, 512, "RecvTimeSec");
	db_set_dw(NULL, MODULENAME, SectionName, this->recvTimeSec);
	mir_snprintf(SectionName, 512, "SendTimeSec");
	db_set_dw(NULL, MODULENAME, SectionName, this->sendTimeSec);
	mir_snprintf(SectionName, 512, "ResendDelaySec");
	db_set_dw(NULL, MODULENAME, SectionName, this->resendDelaySec);
}

void CNudge::Load(void)
{
	char SectionName[512];
	mir_snprintf(SectionName, 512, "useByProtocol");
	this->useByProtocol = db_get_b(NULL, MODULENAME, SectionName, FALSE) != 0;
	mir_snprintf(SectionName, 512, "RecvTimeSec");
	this->recvTimeSec = db_get_dw(NULL, MODULENAME, SectionName, 30);
	mir_snprintf(SectionName, 512, "SendTimeSec");
	this->sendTimeSec = db_get_dw(NULL, MODULENAME, SectionName, 30);
	mir_snprintf(SectionName, 512, "ResendDelaySec");
	this->resendDelaySec = db_get_dw(NULL, MODULENAME, SectionName, 3);
}

void CNudgeElement::Save(void)
{
	char SectionName[512];
	mir_snprintf(SectionName, 512, "%s-openMessageWindow", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->openMessageWindow);
	mir_snprintf(SectionName, 512, "%s-openContactList", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->openContactList);
	mir_snprintf(SectionName, 512, "%s-useIgnore", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->useIgnoreSettings);
	mir_snprintf(SectionName, 512, "%s-showStatus", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->showStatus);
	mir_snprintf(SectionName, 512, "%s-showPopup", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->showPopup);
	mir_snprintf(SectionName, 512, "%s-shakeClist", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->shakeClist);
	mir_snprintf(SectionName, 512, "%s-shakeChat", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->shakeChat);
	mir_snprintf(SectionName, 512, "%s-enabled", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->enabled);
	mir_snprintf(SectionName, 512, "%s-autoResend", ProtocolName);
	db_set_b(NULL, MODULENAME, SectionName, this->autoResend);
	mir_snprintf(SectionName, 512, "%s-statusFlags", ProtocolName);
	db_set_dw(NULL, MODULENAME, SectionName, this->statusFlags);
	mir_snprintf(SectionName, 512, "%s-recText", ProtocolName);
	db_set_ts(NULL, MODULENAME, SectionName, this->recText);
	mir_snprintf(SectionName, 512, "%s-senText", ProtocolName);
	db_set_ts(NULL, MODULENAME, SectionName, this->senText);
}

void CNudgeElement::Load(void)
{
	DBVARIANT dbv;
	char SectionName[512];
	mir_snprintf(SectionName, 512, "%s-openMessageWindow", ProtocolName);
	this->openMessageWindow = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-openContactList", ProtocolName);
	this->openContactList = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-useIgnore", ProtocolName);
	this->useIgnoreSettings = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-showStatus", ProtocolName);
	this->showStatus = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-showPopup", ProtocolName);
	this->showPopup = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-shakeClist", ProtocolName);
	this->shakeClist = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-shakeChat", ProtocolName);
	this->shakeChat = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-enabled", ProtocolName);
	this->enabled = db_get_b(NULL, MODULENAME, SectionName, TRUE) != 0;
	mir_snprintf(SectionName, 512, "%s-autoResend", ProtocolName);
	this->autoResend = db_get_b(NULL, MODULENAME, SectionName, FALSE) != 0;
	mir_snprintf(SectionName, 512, "%s-statusFlags", ProtocolName);
	this->statusFlags = db_get_dw(NULL, MODULENAME, SectionName, 967);

	mir_snprintf(SectionName,512,"%s-recText", ProtocolName);
	if (!db_get_ts(NULL, MODULENAME, SectionName, &dbv)) {
		_tcsncpy(this->recText, dbv.ptszVal, TEXT_LEN);
		if (_tcsclen(this->recText) < 1)
			_tcsncpy(this->recText, TranslateT("You received a nudge"), TEXT_LEN);
		db_free(&dbv);
	}
	else _tcsncpy(this->recText, TranslateT("You received a nudge"), TEXT_LEN);

	mir_snprintf(SectionName, 512, "%s-senText", ProtocolName);
	if (!db_get_ts(NULL, MODULENAME, SectionName, &dbv)) {
		_tcsncpy(this->senText, dbv.ptszVal, TEXT_LEN);
		if (_tcsclen(this->senText) < 1)
			_tcsncpy(this->senText, TranslateT("You sent a nudge"), TEXT_LEN);
		db_free(&dbv);
	}
	else _tcsncpy(this->senText, TranslateT("You sent a nudge"), TEXT_LEN);
}
