/*
 * astyle --force-indent=tab=4 --brackets=linux --indent-switches
 *		  --pad=oper --one-line=keep-blocks  --unpad=paren
 *
 * Miranda NG: the free IM client for Microsoft* Windows*
 *
 * Copyright (c) 2000-09 Miranda ICQ/IM project,
 * all portions of this codebase are copyrighted to the people
 * listed in contributors.txt.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * you should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * part of tabSRMM messaging plugin for Miranda.
 *
 * (C) 2005-2010 by silvercircle _at_ gmail _dot_ com and contributors
 *
 * implements the richedit based message log and the template parser
 *
 */

#include "commonheaders.h"

struct TCpTable cpTable[] = {
	{ 874,	LPGENT("Thai")	 },
	{ 932,	LPGENT("Japanese") },
	{ 936,	LPGENT("Simplified Chinese") },
	{ 949,	LPGENT("Korean") },
	{ 950,	LPGENT("Traditional Chinese") },
	{ 1250,	LPGENT("Central European") },
	{ 1251,	LPGENT("Cyrillic") },
	{ 20866, LPGENT("Cyrillic KOI8-R") },
	{ 1252,	LPGENT("Latin I") },
	{ 1253,	LPGENT("Greek") },
	{ 1254,	LPGENT("Turkish") },
	{ 1255,	LPGENT("Hebrew") },
	{ 1256,	LPGENT("Arabic") },
	{ 1257,	LPGENT("Baltic") },
	{ 1258,	LPGENT("Vietnamese") },
	{ 1361,	LPGENT("Korean (Johab)") },
	{   -1,     NULL}
};

wchar_t* weekDays[7] = {LPGENT("Sunday"), LPGENT("Monday"), LPGENT("Tuesday"), LPGENT("Wednesday"), LPGENT("Thursday"), LPGENT("Friday"), LPGENT("Saturday")};

wchar_t* months[12] = 
{
	LPGENT("January"), LPGENT("February"), LPGENT("March"), LPGENT("April"), LPGENT("May"), LPGENT("June"), 
	LPGENT("July"), LPGENT("August"), LPGENT("September"), LPGENT("October"), LPGENT("November"), LPGENT("December")
};

static TCHAR *Template_MakeRelativeDate(TWindowData *dat, HANDLE hTimeZone, time_t check, int groupBreak, TCHAR code);
static void  ReplaceIcons(HWND hwndDlg, TWindowData *dat, LONG startAt, int fAppend, BOOL isSent);

static time_t today;

int g_groupBreak = TRUE;
static TCHAR *szMyName = NULL;
static TCHAR *szYourName = NULL;

static int logPixelSY;
static TCHAR szToday[22], szYesterday[22];
char  rtfFontsGlobal[MSGDLGFONTCOUNT + 2][RTFCACHELINESIZE];
char *rtfFonts;

LOGFONTA logfonts[MSGDLGFONTCOUNT + 2];
COLORREF fontcolors[MSGDLGFONTCOUNT + 2];

#define LOGICON_MSG  0
#define LOGICON_URL  1
#define LOGICON_FILE 2
#define LOGICON_OUT 3
#define LOGICON_IN 4
#define LOGICON_STATUS 5
#define LOGICON_ERROR 6

static HICON Logicons[NR_LOGICONS];

#define STREAMSTAGE_HEADER  0
#define STREAMSTAGE_EVENTS  1
#define STREAMSTAGE_TAIL    2
#define STREAMSTAGE_STOP    3
struct LogStreamData {
	int stage;
	HANDLE hContact;
	HANDLE hDbEvent, hDbEventLast;
	char *buffer;
	int bufferOffset, bufferLen;
	int eventsToInsert;
	int isEmpty;
	int isAppend;
	TWindowData *dlgDat;
	DBEVENTINFO *dbei;
};

__forceinline char *GetRTFFont(DWORD dwIndex)
{
	return rtfFonts + (dwIndex * RTFCACHELINESIZE);
}

/*
 * remove any empty line at the end of a message to avoid some RichEdit "issues" with
 * the highlight code (individual background colors).
 * Doesn't touch the message for sure, but empty lines at the end are ugly anyway.
 */

static void TrimMessage(TCHAR *msg)
{
	size_t iLen = lstrlen(msg) - 1;
	size_t i = iLen;

	while (i && (msg[i] == '\r' || msg[i] == '\n')) {
		i--;
	}
	if (i < iLen)
		msg[i+1] = '\0';
}

void TSAPI CacheLogFonts()
{
	int i;
	HDC hdc = GetDC(NULL);
	logPixelSY = GetDeviceCaps(hdc, LOGPIXELSY);
	ReleaseDC(NULL, hdc);

	ZeroMemory(logfonts, sizeof(LOGFONTA) * MSGDLGFONTCOUNT + 2);
	for (i=0; i < MSGDLGFONTCOUNT; i++) {
		LoadLogfont(i, &logfonts[i], &fontcolors[i], FONTMODULE);
		mir_snprintf(rtfFontsGlobal[i], SIZEOF(rtfFontsGlobal[i]), "\\f%u\\cf%u\\b%d\\i%d\\ul%d\\fs%u", i, i, logfonts[i].lfWeight >= FW_BOLD ? 1 : 0, logfonts[i].lfItalic,logfonts[i].lfUnderline, 2 * abs(logfonts[i].lfHeight) * 74 / logPixelSY);
	}
	mir_snprintf(rtfFontsGlobal[MSGDLGFONTCOUNT], SIZEOF(rtfFontsGlobal[MSGDLGFONTCOUNT]), "\\f%u\\cf%u\\b%d\\i%d\\fs%u", MSGDLGFONTCOUNT, MSGDLGFONTCOUNT, 0, 0, 0);

	_tcsncpy(szToday, TranslateT("Today"), 20);
	_tcsncpy(szYesterday, TranslateT("Yesterday"), 20);
	szToday[19] = szYesterday[19] = 0;

	/*
	 * cache/create the info panel fonts
	 */

	COLORREF clr;
	LOGFONTA lf;

	for (i=0; i < IPFONTCOUNT; i++) {
		if (CInfoPanel::m_ipConfig.hFonts[i])
			DeleteObject(CInfoPanel::m_ipConfig.hFonts[i]);
		LoadLogfont(i + 100, &lf, &clr, FONTMODULE);
		lf.lfUnderline = 0;
		CInfoPanel::m_ipConfig.hFonts[i] = CreateFontIndirectA(&lf);
		CInfoPanel::m_ipConfig.clrs[i] = clr;
	}

	hdc = GetDC(PluginConfig.g_hwndHotkeyHandler);

	HFONT hOldFont = (HFONT)SelectObject(hdc, CInfoPanel::m_ipConfig.hFonts[IPFONTID_NICK]);

	SIZE sz;
	GetTextExtentPoint32(hdc, _T("WMA"), 3, &sz);
	CInfoPanel::m_ipConfig.height1 = sz.cy;
	SelectObject(hdc, CInfoPanel::m_ipConfig.hFonts[IPFONTID_UIN]);
	GetTextExtentPoint32(hdc, _T("WMA"), 3, &sz);
	CInfoPanel::m_ipConfig.height2 = sz.cy;

	SelectObject(hdc, hOldFont);
	ReleaseDC(PluginConfig.g_hwndHotkeyHandler, hdc);
	PluginConfig.hFontCaption = CInfoPanel::m_ipConfig.hFonts[IPFONTCOUNT - 1];

	PluginConfig.crIncoming = M.GetDword(FONTMODULE, "inbg", SRMSGDEFSET_BKGINCOLOUR);
	PluginConfig.crOutgoing = M.GetDword(FONTMODULE, "outbg", SRMSGDEFSET_BKGOUTCOLOUR);
	PluginConfig.crStatus = M.GetDword(FONTMODULE, "statbg", SRMSGDEFSET_BKGCOLOUR);
	PluginConfig.crOldIncoming = M.GetDword(FONTMODULE, "oldinbg", SRMSGDEFSET_BKGINCOLOUR);
	PluginConfig.crOldOutgoing = M.GetDword(FONTMODULE, "oldoutbg", SRMSGDEFSET_BKGOUTCOLOUR);
}

void FreeLogFonts()
{
	for (int i=0; i < IPFONTCOUNT; i++)
		if (CInfoPanel::m_ipConfig.hFonts[i])
			DeleteObject(CInfoPanel::m_ipConfig.hFonts[i]);
}

void TSAPI CacheMsgLogIcons()
{
	Logicons[0] = LoadSkinnedIcon(SKINICON_EVENT_MESSAGE);
	Logicons[1] = LoadSkinnedIcon(SKINICON_EVENT_URL);
	Logicons[2] = LoadSkinnedIcon(SKINICON_EVENT_FILE);
	Logicons[3] = PluginConfig.g_iconOut;
	Logicons[4] = PluginConfig.g_iconIn;
	Logicons[5] = PluginConfig.g_iconStatus;
	Logicons[6] = PluginConfig.g_iconErr;
}

static int TSAPI GetColorIndex(char *rtffont)
{
	char *p;

	if ((p = strstr(rtffont, "\\cf")) != NULL)
		return atoi(p + 3);
	return 0;
}

static void AppendToBuffer(char **buffer, int *cbBufferEnd, int *cbBufferAlloced, const char *fmt, ...)
{
	va_list va;
	int charsDone;

	va_start(va, fmt);
	for (;;) {
		charsDone = mir_vsnprintf(*buffer + *cbBufferEnd, *cbBufferAlloced - *cbBufferEnd, fmt, va);
		if (charsDone >= 0)
			break;
		*cbBufferAlloced += 1024;
		*buffer = (char *)mir_realloc(*buffer, *cbBufferAlloced);
	}
	va_end(va);
	*cbBufferEnd += charsDone;
}

static int AppendUnicodeToBuffer(char **buffer, int *cbBufferEnd, int *cbBufferAlloced, const TCHAR * line, int mode)
{
	int  lineLen = (int)(wcslen(line)) * 9 + 8;
	if (*cbBufferEnd + lineLen > *cbBufferAlloced) {
		cbBufferAlloced[0] += (lineLen + 1024UL - lineLen % 1024UL);
		*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
	}

	char *d = *buffer + *cbBufferEnd;
	strcpy(d, "{\\uc1 ");
	d += 6;

	int textCharsCount = 0;
	for (; *line; line++, textCharsCount++) {
		if (*line == 127 && line[1] != 0) {
			TCHAR code = line[2];
			if (((code == '0' || code == '1') && line[3] == ' ') || (line[1] == 'c' && code == 'x')) {
				int begin = (code == '1');
				switch (line[1]) {
				case 'b':
					CopyMemory(d, begin ? "\\b " : "\\b0 ", begin ? 3 : 4);
					d += (begin ? 3 : 4);
					line += 3;
					continue;
				case 'i':
					CopyMemory(d, begin ? "\\i " : "\\i0 ", begin ? 3 : 4);
					d += (begin ? 3 : 4);
					line += 3;
					continue;
				case 'u':
					CopyMemory(d, begin ? "\\ul " : "\\ul0 ", begin ? 4 : 5);
					d += (begin ? 4 : 5);
					line += 3;
					continue;
				case 's':
					CopyMemory(d, begin ? "\\strike " : "\\strike0 ", begin ? 8 : 9);
					d += (begin ? 8 : 9);
					line += 3;
					continue;
				case 'c':
					begin = (code == 'x');
					CopyMemory(d, "\\cf", 3);
					if (begin) {
						d[3] = (char)line[3];
						d[4] = (char)line[4];
						d[5] = ' ';
					}
					else {
						char szTemp[10];
						int colindex = GetColorIndex(GetRTFFont(LOWORD(mode) ? (MSGFONTID_MYMSG + (HIWORD(mode) ? 8 : 0)) : (MSGFONTID_YOURMSG + (HIWORD(mode) ? 8 : 0))));
						mir_snprintf(szTemp, 4, "%02d", colindex);
						d[3] = szTemp[0];
						d[4] = szTemp[1];
						d[5] = ' ';
					}
					d += 6;
					line += (begin ? 6 : 3);
					continue;
				}
			}
		}
		if (*line == '\r' && line[1] == '\n') {
			CopyMemory(d, "\\line ", 6);
			line++;
			d += 6;
		}
		else if (*line == '\n') {
			CopyMemory(d, "\\line ", 6);
			d += 6;
		}
		else if (*line == '\t') {
			CopyMemory(d, "\\tab ", 5);
			d += 5;
		}
		else if (*line == '\\' || *line == '{' || *line == '}') {
			*d++ = '\\';
			*d++ = (char)* line;
		}
		else if (*line < 128) {
			*d++ = (char)* line;
		}
		else d += sprintf(d, "\\u%d ?", *line); //!!!!!!!!!!!!!
	}

	strcpy(d, "}");
	d++;

	*cbBufferEnd = (int)(d - *buffer);
	return textCharsCount;
}

/*
 * same as above but does "\r\n"->"\\par " and "\t"->"\\tab " too
 */

static int AppendToBufferWithRTF(int mode, char **buffer, int *cbBufferEnd, int *cbBufferAlloced, const char *fmt, ...)
{
	va_list va;
	int charsDone, i;

	va_start(va, fmt);
	for (;;) {
		charsDone = mir_vsnprintf(*buffer + *cbBufferEnd, *cbBufferAlloced - *cbBufferEnd, fmt, va);
		if (charsDone >= 0)
			break;
		*cbBufferAlloced += 1024;
		*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
	}
	va_end(va);
	*cbBufferEnd += charsDone;
	for (i = *cbBufferEnd - charsDone; (*buffer)[i]; i++) {

		if (1) {
			if ((*buffer)[i] == '' && (*buffer)[i + 1] != 0) {
				char code = (*buffer)[i + 2];
				char tag = (*buffer)[i + 1];

				if (((code == '0' || code == '1') && (*buffer)[i + 3] == ' ') || (tag == 'c' && (code == 'x' || code == '0'))) {
					int begin = (code == '1');

					if (*cbBufferEnd + 5 > *cbBufferAlloced) {
						*cbBufferAlloced += 1024;
						*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
					}
					switch (tag) {
						case 'b':
							CopyMemory(*buffer + i, begin ? "\\b1 " : "\\b0 ", 4);
							continue;
						case 'i':
							CopyMemory(*buffer + i, begin ? "\\i1 " : "\\i0 ", 4);
							continue;
						case 'u':
							MoveMemory(*buffer + i + 2, *buffer + i + 1, *cbBufferEnd - i);
							CopyMemory(*buffer + i, begin ? "\\ul1 " : "\\ul0 ", 5);
							*cbBufferEnd += 1;
							continue;
						case 's':
							*cbBufferAlloced += 20;
							*buffer = (char *)mir_realloc(*buffer, *cbBufferAlloced);
							MoveMemory(*buffer + i + 6, *buffer + i + 1, (*cbBufferEnd - i) + 1);
							CopyMemory(*buffer + i, begin ? "\\strike1 " : "\\strike0 ", begin ? 9 : 9);
							*cbBufferEnd += 5;
							continue;
						case 'c':
							begin = (code == 'x');
							CopyMemory(*buffer + i, "\\cf", 3);
							if (begin) {
							} else {
								char szTemp[10];
								int colindex = GetColorIndex(GetRTFFont(LOWORD(mode) ? (MSGFONTID_MYMSG + (HIWORD(mode) ? 8 : 0)) : (MSGFONTID_YOURMSG + (HIWORD(mode) ? 8 : 0))));
								mir_snprintf(szTemp, 4, "%02d", colindex);
								(*buffer)[i + 3] = szTemp[0];
								(*buffer)[i + 4] = szTemp[1];
							}
							continue;
					}
				}
			}
		}

		if ((*buffer)[i] == '\r' && (*buffer)[i + 1] == '\n') {
			if (*cbBufferEnd + 5 > *cbBufferAlloced) {
				*cbBufferAlloced += 1024;
				*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
			}
			MoveMemory(*buffer + i + 6, *buffer + i + 2, *cbBufferEnd - i - 1);
			CopyMemory(*buffer + i, "\\line ", 6);
			*cbBufferEnd += 4;
		} else if ((*buffer)[i] == '\n') {
			if (*cbBufferEnd + 6 > *cbBufferAlloced) {
				*cbBufferAlloced += 1024;
				*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
			}
			MoveMemory(*buffer + i + 6, *buffer + i + 1, *cbBufferEnd - i);
			CopyMemory(*buffer + i, "\\line ", 6);
			*cbBufferEnd += 5;
		} else if ((*buffer)[i] == '\t') {
			if (*cbBufferEnd + 5 > *cbBufferAlloced) {
				*cbBufferAlloced += 1024;
				*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
			}
			MoveMemory(*buffer + i + 5, *buffer + i + 1, *cbBufferEnd - i);
			CopyMemory(*buffer + i, "\\tab ", 5);
			*cbBufferEnd += 4;
		} else if ((*buffer)[i] == '\\' || (*buffer)[i] == '{' || (*buffer)[i] == '}') {
			if (*cbBufferEnd + 2 > *cbBufferAlloced) {
				*cbBufferAlloced += 1024;
				*buffer = (char *) mir_realloc(*buffer, *cbBufferAlloced);
			}
			MoveMemory(*buffer + i + 1, *buffer + i, *cbBufferEnd - i + 1);
			(*buffer)[i] = '\\';
			++*cbBufferEnd;
			i++;
		}
	}
	return (int)(_mbslen((unsigned char *)*buffer + *cbBufferEnd));
}

static void Build_RTF_Header(char **buffer, int *bufferEnd, int *bufferAlloced, TWindowData *dat)
{
	COLORREF 		colour;
	int      		i;
	char     		szTemp[30];
	LOGFONTA*		logFonts = dat->pContainer->theme.logFonts;
	COLORREF*		fontColors = dat->pContainer->theme.fontColors;
	TLogTheme *theme = &dat->pContainer->theme;

	// rtl
	if (dat->dwFlags & MWF_LOG_RTL)
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "{\\rtf1\\ansi\\deff0{\\fonttbl");
	else
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "{\\rtf1\\ansi\\deff0{\\fonttbl");

	for (i=0; i < MSGDLGFONTCOUNT; i++)
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "{\\f%u\\fnil\\fcharset%u %s;}", i, logFonts[i].lfCharSet, logFonts[i].lfFaceName);
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "{\\f%u\\fnil\\fcharset%u %s;}", MSGDLGFONTCOUNT, logFonts[i].lfCharSet, "Arial");

	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "}{\\colortbl ");
	for (i=0; i < MSGDLGFONTCOUNT; i++)
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(fontColors[i]), GetGValue(fontColors[i]), GetBValue(fontColors[i]));
	if (GetSysColorBrush(COLOR_HOTLIGHT) == NULL)
		colour = RGB(0, 0, 255);
	else
		colour = GetSysColor(COLOR_HOTLIGHT);
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));

	/* OnO: Create incoming and outcoming colours */
	colour = theme->inbg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->outbg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->bg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->hgrid;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->oldinbg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->oldoutbg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	colour = theme->statbg;
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));

	// custom template colors...

	for (i = 1; i <= 5; i++) {
		mir_snprintf(szTemp, 10, "cc%d", i);
		colour = theme->custom_colors[i - 1];
		if (colour == 0)
			colour = RGB(1, 1, 1);
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(colour), GetGValue(colour), GetBValue(colour));
	}

	// bbcode colors...

	for (i=0; i < Utils::rtf_ctable_size; i++)
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\red%u\\green%u\\blue%u;", GetRValue(Utils::rtf_ctable[i].clr), GetGValue(Utils::rtf_ctable[i].clr), GetBValue(Utils::rtf_ctable[i].clr));

	/*
	 * paragraph header
	 */
	AppendToBuffer(buffer, bufferEnd, bufferAlloced, "}");

	/*
	 * indent:
	 * real indent is set in msgdialog.c (DM_OPTIONSAPPLIED)
	 */

	if (!(dat->dwFlags & MWF_LOG_INDENT))
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "\\li%u\\ri%u\\fi%u\\tx%u", 2*15, 2*15, 0, 70 * 15);
}


//mir_free() the return value
static char *CreateRTFHeader(TWindowData *dat)
{
	char *buffer;
	int bufferAlloced, bufferEnd;

	bufferEnd = 0;
	bufferAlloced = 1024;
	buffer = (char *) mir_alloc(bufferAlloced);
	buffer[0] = '\0';

	Build_RTF_Header(&buffer, &bufferEnd, &bufferAlloced, dat);
	return buffer;
}

static void AppendTimeStamp(TCHAR *szFinalTimestamp, int isSent, char **buffer, int *bufferEnd, int *bufferAlloced, int skipFont,
							TWindowData *dat, int iFontIDOffset)
{
	if (skipFont)
		AppendUnicodeToBuffer(buffer, bufferEnd, bufferAlloced, szFinalTimestamp, MAKELONG(isSent, dat->isHistory));
	else {
		AppendToBuffer(buffer, bufferEnd, bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset));
		AppendUnicodeToBuffer(buffer, bufferEnd, bufferAlloced, szFinalTimestamp, MAKELONG(isSent, dat->isHistory));
	}
}

//mir_free() the return value
static char *CreateRTFTail(TWindowData *dat)
{
	char *buffer;
	int bufferAlloced, bufferEnd;

	bufferEnd = 0;
	bufferAlloced = 1024;
	buffer = (char *) mir_alloc(bufferAlloced);
	buffer[0] = '\0';
	AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "}");
	return buffer;
}

int TSAPI DbEventIsShown(TWindowData *dat, DBEVENTINFO *dbei)
{
	if (!IsCustomEvent(dbei->eventType))
		return 1;

	if (DbEventIsForMsgWindow(dbei))
		return 1;

	return IsStatusEvent(dbei->eventType);
}

int DbEventIsForMsgWindow(DBEVENTINFO *dbei)
{
	DBEVENTTYPEDESCR* et = ( DBEVENTTYPEDESCR* )CallService(MS_DB_EVENT_GETTYPE, (WPARAM)dbei->szModule, (LPARAM)dbei->eventType);
	return et && ( et->flags & DETF_MSGWINDOW );
}

static char *Template_CreateRTFFromDbEvent(TWindowData *dat, HANDLE hContact, HANDLE hDbEvent, int prefixParaBreak, LogStreamData *streamData)
{
	char *buffer, c;
	TCHAR ci, cc;
	TCHAR	*szFinalTimestamp;
	int   bufferAlloced, bufferEnd;
	size_t iTemplateLen, i = 0;
	DBEVENTINFO dbei = { 0 };
	int isSent = 0;
	int iFontIDOffset = 0;
	TCHAR *szTemplate;
	HANDLE hTimeZone;
	BOOL skipToNext = FALSE, showTime = TRUE, showDate = TRUE, skipFont = FALSE;
	struct tm event_time;
	TTemplateSet *this_templateset;
	BOOL isBold = FALSE, isItalic = FALSE, isUnderline = FALSE;
	DWORD dwEffectiveFlags;
	DWORD dwFormattingParams = MAKELONG(PluginConfig.m_FormatWholeWordsOnly, 0);
	BOOL  bIsStatusChangeEvent = FALSE;
	TCHAR *msg, *formatted = NULL;
	char *rtfMessage = NULL;

	bufferEnd = 0;
	bufferAlloced = 1024;
	buffer = (char *) mir_alloc(bufferAlloced);
	buffer[0] = '\0';

	if (streamData->dbei != 0)
		dbei = *(streamData->dbei);
	else {
		dbei.cbSize = sizeof(dbei);
		dbei.cbBlob = db_event_getBlobSize(hDbEvent);
		if (dbei.cbBlob == -1) {
			mir_free(buffer);
			return NULL;
		}
		dbei.pBlob = (PBYTE) mir_alloc(dbei.cbBlob);
		db_event_get(hDbEvent, &dbei);
		if (!DbEventIsShown(dat, &dbei)) {
			mir_free(dbei.pBlob);
			mir_free(buffer);
			return NULL;
		}
	}

	if (dbei.eventType == EVENTTYPE_MESSAGE && !(dbei.flags & (DBEF_SENT | DBEF_READ)))
		dat->cache->updateStats(TSessionStats::SET_LAST_RCV, lstrlenA((char *) dbei.pBlob));

	if (rtfMessage == NULL) {
		msg = DbGetEventTextT(&dbei, dat->codePage);
		if (!msg) {
			mir_free(dbei.pBlob);
			mir_free(buffer);
			return NULL;
		}
		TrimMessage(msg);
		formatted = const_cast<TCHAR *>(Utils::FormatRaw(dat, msg, dwFormattingParams, isSent));
		mir_free(msg);
	}

	bIsStatusChangeEvent = IsStatusEvent(dbei.eventType);

	if (dat->isAutoRTL & 2) {                                     // means: last \\par was deleted to avoid new line at end of log
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\par");
		dat->isAutoRTL &= ~2;
	}

	if (dat->dwFlags & MWF_LOG_RTL)
		dbei.flags |= DBEF_RTL;

	if (dbei.flags & DBEF_RTL)
		dat->isAutoRTL |= 1;

	dwEffectiveFlags = dat->dwFlags;

	dat->isHistory = (dbei.timestamp < dat->cache->getSessionStart() && (dbei.flags & DBEF_READ || dbei.flags & DBEF_SENT));
	iFontIDOffset = dat->isHistory ? 8 : 0;     // offset into the font table for either history (old) or new events... (# of fonts per configuration set)
	isSent = (dbei.flags & DBEF_SENT);

	if (!isSent && (bIsStatusChangeEvent || dbei.eventType == EVENTTYPE_MESSAGE || DbEventIsForMsgWindow(&dbei))) {
		db_event_markRead(hContact, hDbEvent);
		CallService(MS_CLIST_REMOVEEVENT, (WPARAM)hContact, (LPARAM)hDbEvent);
	}

	g_groupBreak = TRUE;

	if (dwEffectiveFlags & MWF_DIVIDERWANTED) {
		static char szStyle_div[128] = "\0";
		if (szStyle_div[0] == 0)
			mir_snprintf(szStyle_div, 128, "\\f%u\\cf%u\\ul0\\b%d\\i%d\\fs%u", H_MSGFONTID_DIVIDERS, H_MSGFONTID_DIVIDERS, 0, 0, 5);

		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\sl-1\\slmult0\\highlight%d\\cf%d\\-\\par\\sl0", H_MSGFONTID_DIVIDERS, H_MSGFONTID_DIVIDERS);
		dat->dwFlags &= ~MWF_DIVIDERWANTED;
	}
	if (dwEffectiveFlags & MWF_LOG_GROUPMODE && ((dbei.flags & (DBEF_SENT | DBEF_READ | DBEF_RTL)) == LOWORD(dat->iLastEventType)) && dbei.eventType == EVENTTYPE_MESSAGE && HIWORD(dat->iLastEventType) == EVENTTYPE_MESSAGE && (dbei.timestamp - dat->lastEventTime) < 86400) {
		g_groupBreak = FALSE;
		if ((time_t)dbei.timestamp > today && dat->lastEventTime < today)
			g_groupBreak = TRUE;
	}
	if (!streamData->isEmpty && g_groupBreak && (dwEffectiveFlags & MWF_LOG_GRID))
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\sl-1\\slmult0\\highlight%d\\cf%d\\-\\par\\sl0", MSGDLGFONTCOUNT + 4, MSGDLGFONTCOUNT + 4);

	if (dbei.flags & DBEF_RTL)
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\rtlpar");
	else
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\ltrpar");

	/* OnO: highlight start */
	if (bIsStatusChangeEvent)
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\highlight%d\\cf%d", MSGDLGFONTCOUNT + 7, MSGDLGFONTCOUNT + 7);
	else
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\highlight%d\\cf%d", MSGDLGFONTCOUNT + (dat->isHistory?5:1) + ((isSent) ? 1 : 0), MSGDLGFONTCOUNT + (dat->isHistory?5:1) + ((isSent) ? 1 : 0));

	streamData->isEmpty = FALSE;

	if (dat->isAutoRTL & 1) {
		if (dbei.flags & DBEF_RTL) {
			AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\ltrch\\rtlch");
		} else {
			AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\rtlch\\ltrch");
		}
	}

	/*
	 * templated code starts here
	 */
	if (dwEffectiveFlags & MWF_LOG_SHOWTIME) {
		hTimeZone = ((dat->dwFlags & MWF_LOG_LOCALTIME) && !isSent) ? dat->hTimeZone : NULL;
		time_t local_time = tmi.timeStampToTimeZoneTimeStamp(hTimeZone, dbei.timestamp);
		event_time = *gmtime(&local_time);
	}
	this_templateset = dbei.flags & DBEF_RTL ? dat->pContainer->rtl_templates : dat->pContainer->ltr_templates;

	if (bIsStatusChangeEvent)
		szTemplate = this_templateset->szTemplates[TMPL_STATUSCHG];
	else if (dbei.eventType == EVENTTYPE_ERRMSG)
		szTemplate = this_templateset->szTemplates[TMPL_ERRMSG];
	else {
		if (dwEffectiveFlags & MWF_LOG_GROUPMODE)
			szTemplate = isSent ? (g_groupBreak ? this_templateset->szTemplates[TMPL_GRPSTARTOUT] : this_templateset->szTemplates[TMPL_GRPINNEROUT]) :
								 (g_groupBreak ? this_templateset->szTemplates[TMPL_GRPSTARTIN] : this_templateset->szTemplates[TMPL_GRPINNERIN]);
		else
			szTemplate = isSent ? this_templateset->szTemplates[TMPL_MSGOUT] : this_templateset->szTemplates[TMPL_MSGIN];
	}

	iTemplateLen = lstrlen(szTemplate);
	showTime = dwEffectiveFlags & MWF_LOG_SHOWTIME;
	showDate = dwEffectiveFlags & MWF_LOG_SHOWDATES;

	if (dat->hHistoryEvents) {
		if (dat->curHistory == dat->maxHistory) {
			MoveMemory(dat->hHistoryEvents, &dat->hHistoryEvents[1], sizeof(HANDLE)* (dat->maxHistory - 1));
			dat->curHistory--;
		}
		dat->hHistoryEvents[dat->curHistory++] = hDbEvent;
	}

	AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\ul0\\b0\\i0 ");

	while (i < iTemplateLen) {
		ci = szTemplate[i];
		if (ci == '%') {
			cc = szTemplate[i + 1];
			skipToNext = FALSE;
			skipFont = FALSE;
			/*
			 * handle modifiers
			 */
			while (cc == '#' || cc == '$' || cc == '&' || cc == '?' || cc == '\\') {
				switch (cc) {
				case '#':
					if (!dat->isHistory) {
						skipToNext = TRUE;
						goto skip;
					}
					i++;
					cc = szTemplate[i + 1];
					continue;

				case '$':
					if (dat->isHistory) {
						skipToNext = TRUE;
						goto skip;
					}
					i++;
					cc = szTemplate[i + 1];
					continue;

				case '&':
					i++;
					cc = szTemplate[i + 1];
					skipFont = TRUE;
					break;

				case '?':
					if (dwEffectiveFlags & MWF_LOG_NORMALTEMPLATES) {
						i++;
						cc = szTemplate[i + 1];
						continue;
					}
					i++;
					skipToNext = TRUE;
					goto skip;

				case '\\':
					if (!(dwEffectiveFlags & MWF_LOG_NORMALTEMPLATES)) {
						i++;
						cc = szTemplate[i + 1];
						continue;
					}
					i++;
					skipToNext = TRUE;
					goto skip;
				}
			}

			TCHAR color, code;
			switch (cc) {
			case 'V':
				//AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\fs0\\\expnd-40 ~-%d-~", hDbEvent);
				break;
			case 'I':
				if (dwEffectiveFlags & MWF_LOG_SHOWICONS) {
					int icon;
					if ((dwEffectiveFlags & MWF_LOG_INOUTICONS) && dbei.eventType == EVENTTYPE_MESSAGE)
						icon = isSent ? LOGICON_OUT : LOGICON_IN;
					else {
						switch (dbei.eventType) {
						case EVENTTYPE_FILE:
							icon = LOGICON_FILE;
							break;
						case EVENTTYPE_ERRMSG:
							icon = LOGICON_ERROR;
							break;
						default:
							icon = LOGICON_MSG;
							break;
						}
						if (bIsStatusChangeEvent)
							icon = LOGICON_STATUS;
					}
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s\\fs1  #~#%01d%c%s ", GetRTFFont(MSGFONTID_SYMBOLS_IN), icon, isSent ? '>' : '<', GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));
				}
				else skipToNext = TRUE;
				break;
			case 'D': // long date
				if (showTime && showDate) {
					szFinalTimestamp = Template_MakeRelativeDate(dat, hTimeZone, dbei.timestamp, g_groupBreak, (TCHAR)'D');
					AppendTimeStamp(szFinalTimestamp, isSent, &buffer, &bufferEnd, &bufferAlloced, skipFont, dat, iFontIDOffset);
				}
				else skipToNext = TRUE;
				break;
			case 'E': // short date...
				if (showTime && showDate) {
					szFinalTimestamp = Template_MakeRelativeDate(dat, hTimeZone, dbei.timestamp, g_groupBreak, (TCHAR)'E');
					AppendTimeStamp(szFinalTimestamp, isSent, &buffer, &bufferEnd, &bufferAlloced, skipFont, dat, iFontIDOffset);
				}
				else skipToNext = TRUE;
				break;
			case 'a': // 12 hour
			case 'h': // 24 hour
				if (showTime) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, cc == 'h' ? "%02d" : "%2d", cc == 'h' ? event_time.tm_hour : (event_time.tm_hour > 12 ? event_time.tm_hour - 12 : event_time.tm_hour));
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, cc == 'h' ? "%s %02d" : "%s %2d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), cc == 'h' ? event_time.tm_hour : (event_time.tm_hour > 12 ? event_time.tm_hour - 12 : event_time.tm_hour));
				}
				else skipToNext = TRUE;
				break;
			case 'm': // minute
				if (showTime) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%02d", event_time.tm_min);
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %02d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_min);
				}
				else skipToNext = TRUE;
				break;
			case 's': //second
				if (showTime && dwEffectiveFlags & MWF_LOG_SHOWSECONDS) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%02d", event_time.tm_sec);
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %02d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_sec);
				}
				else skipToNext = TRUE;
				break;
			case 'p': // am/pm symbol
				if (showTime) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s", event_time.tm_hour > 11 ? "PM" : "AM");
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %s", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_hour > 11 ? "PM" : "AM");
				}
				else skipToNext = TRUE;
				break;
			case 'o':            // month
				if (showTime && showDate) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%02d", event_time.tm_mon + 1);
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %02d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_mon + 1);
				}
				else skipToNext = TRUE;
				break;
			case 'O': // month (name)
				if (showTime && showDate) {
					if (skipFont)
						AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, TranslateTS(months[event_time.tm_mon]), MAKELONG(isSent, dat->isHistory));
					else {
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset));
						AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, TranslateTS(months[event_time.tm_mon]), MAKELONG(isSent, dat->isHistory));
					}
				}
				else skipToNext = TRUE;
				break;
			case 'd': // day of month
				if (showTime && showDate) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%02d", event_time.tm_mday);
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %02d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_mday);
				}
				else skipToNext = TRUE;
				break;
			case 'w': // day of week
				if (showTime && showDate) {
					if (skipFont)
						AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, TranslateTS(weekDays[event_time.tm_wday]), MAKELONG(isSent, dat->isHistory));
					else {
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset));
						AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, TranslateTS(weekDays[event_time.tm_wday]), MAKELONG(isSent, dat->isHistory));
					}
				}
				else skipToNext = TRUE;
				break;
			case 'y': // year
				if (showTime && showDate) {
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%04d", event_time.tm_year + 1900);
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %04d", GetRTFFont(isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset), event_time.tm_year + 1900);
				}
				else skipToNext = TRUE;
				break;
			case 'R':
			case 'r': // long date
				if (showTime && showDate) {
					szFinalTimestamp = Template_MakeRelativeDate(dat, hTimeZone, dbei.timestamp, g_groupBreak, cc);
					AppendTimeStamp(szFinalTimestamp, isSent, &buffer, &bufferEnd, &bufferAlloced, skipFont, dat, iFontIDOffset);
				}
				else skipToNext = TRUE;
				break;
			case 't':
			case 'T':
				if (showTime) {
					szFinalTimestamp = Template_MakeRelativeDate(dat, hTimeZone, dbei.timestamp, g_groupBreak, (TCHAR)((dwEffectiveFlags & MWF_LOG_SHOWSECONDS) ? cc : (TCHAR)'t'));
					AppendTimeStamp(szFinalTimestamp, isSent, &buffer, &bufferEnd, &bufferAlloced, skipFont, dat, iFontIDOffset);
				}
				else skipToNext = TRUE;
				break;
			case 'S': // symbol
				if (dwEffectiveFlags & MWF_LOG_SYMBOLS) {
					if ((dwEffectiveFlags & MWF_LOG_INOUTICONS) && dbei.eventType == EVENTTYPE_MESSAGE)
						c = isSent ? 0x37 : 0x38;
					else {
						switch (dbei.eventType) {
						case EVENTTYPE_MESSAGE:
							c = (char)0xaa;
							break;
						case EVENTTYPE_FILE:
							c = (char)0xcd;
							break;
						case EVENTTYPE_ERRMSG:
							c = (char)0x72;
							break;
						default:
							c = (char)0xaa;
							break;
						}
						if (bIsStatusChangeEvent)
							c = 0x4e;
					}
					if (skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%c%s ", c, GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));
					else
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s %c%s ", isSent ? GetRTFFont(MSGFONTID_SYMBOLS_OUT) : GetRTFFont(MSGFONTID_SYMBOLS_IN), c, GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));
				}
				else skipToNext = TRUE;
				break;
			case 'n': // hard line break
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, dbei.flags & DBEF_RTL ? "\\rtlpar\\par\\rtlpar" : "\\par\\ltrpar");
				break;
			case 'l': // soft line break
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\line");
				break;
			case 'N': // nickname
				if (!skipFont)
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYNAME + iFontIDOffset : MSGFONTID_YOURNAME + iFontIDOffset));
				if (isSent)
					AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, szMyName, MAKELONG(isSent, dat->isHistory));
				else
					AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, szYourName, MAKELONG(isSent, dat->isHistory));
				break;
			case 'U': // UIN
				if (!skipFont)
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYNAME + iFontIDOffset : MSGFONTID_YOURNAME + iFontIDOffset));
				if (!isSent)
					AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, dat->cache->getUIN(), MAKELONG(isSent, dat->isHistory));
				else
					AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, dat->myUin, MAKELONG(isSent, dat->isHistory));
				break;
			case 'e': // error message
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(MSGFONTID_ERROR));
				AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, (wchar_t *)dbei.szModule, MAKELONG(isSent, dat->isHistory));
				break;
			case 'M': // message
				if (bIsStatusChangeEvent)
					dbei.eventType = EVENTTYPE_STATUSCHANGE;

				switch (dbei.eventType) {
				case EVENTTYPE_MESSAGE:
				case EVENTTYPE_ERRMSG:
				case EVENTTYPE_STATUSCHANGE:
					if (bIsStatusChangeEvent || dbei.eventType == EVENTTYPE_ERRMSG) {
						if (dbei.eventType == EVENTTYPE_ERRMSG && dbei.cbBlob == 0)
							break;
						if (dbei.eventType == EVENTTYPE_ERRMSG) {
							if (!skipFont)
								AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\line%s ", GetRTFFont(bIsStatusChangeEvent ? H_MSGFONTID_STATUSCHANGES : MSGFONTID_MYMSG));
							else
								AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\line ");
						}
						else if (!skipFont)
							AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(bIsStatusChangeEvent ? H_MSGFONTID_STATUSCHANGES : MSGFONTID_MYMSG));
					}
					else if (!skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));

					if (rtfMessage != NULL)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s", rtfMessage);
					else
						AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, formatted, MAKELONG(isSent, dat->isHistory));

					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s", "\\b0\\ul0\\i0 ");
					break;

				case EVENTTYPE_FILE:
					if (!skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYMISC + iFontIDOffset : MSGFONTID_YOURMISC + iFontIDOffset));
					{
						char *szFileName = (char *)dbei.pBlob + sizeof(DWORD);
						char *szDescr = szFileName + lstrlenA(szFileName) + 1;
						TCHAR *tszFileName = DbGetEventStringT(&dbei, szFileName);
						if (*szDescr != 0) {
							TCHAR *tszDescr = DbGetEventStringT(&dbei, szDescr);
							TCHAR buf[1000];
							mir_sntprintf(buf, SIZEOF(buf), _T("%s (%s)"), tszFileName, tszDescr);
							AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, buf, 0);
							mir_free(tszDescr);
						}
						else AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, tszFileName, 0);

						mir_free(tszFileName);
					}
					break;

				default:
					if (!skipFont)
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));
					
					ptrT tszText(DbGetEventTextT(&dbei, CP_ACP));
					AppendUnicodeToBuffer(&buffer, &bufferEnd, &bufferAlloced, tszText, 0);
				}
				break;
			case '*':       // bold
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, isBold ? "\\b0 " : "\\b ");
				isBold = !isBold;
				break;
			case '/': // italic
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, isItalic ? "\\i0 " : "\\i ");
				isItalic = !isItalic;
				break;
			case '_': // italic
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, isUnderline ? "\\ul0 " : "\\ul ");
				isUnderline = !isUnderline;
				break;
			case '-': // grid line
				color = szTemplate[i + 2];
				if (color >= '0' && color <= '4') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\par\\sl-1\\slmult0\\highlight%d\\cf%d\\-\\par\\sl0", MSGDLGFONTCOUNT + 8 + (color - '0'), MSGDLGFONTCOUNT + 7 + (color - '0'));
					i++;
				}
				else AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\par\\sl-1\\slmult0\\highlight%d\\cf%d\\-\\par\\sl0", MSGDLGFONTCOUNT + 4, MSGDLGFONTCOUNT + 4);
				break;
			case '~':       // font break (switch to default font...)
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, GetRTFFont(isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset));
				break;
			case 'H':        // highlight
				color = szTemplate[i + 2];
				if (color >= '0' && color <= '4') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\highlight%d", MSGDLGFONTCOUNT + 8 + (color - '0'));
					i++;
				}
				else AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\highlight%d", (MSGDLGFONTCOUNT + (dat->isHistory ? 5 : 1) + ((isSent) ? 1 : 0)));
				break;
			case '|':       // tab
				if (dwEffectiveFlags & MWF_LOG_INDENT)
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\tab");
				else
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, " ");
				break;
			case 'f':      // font tag...
				code = szTemplate[i + 2];
				{
					int fontindex = -1;
					switch (code) {
					case 'd':
						fontindex = isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset;
						break;
					case 'n':
						fontindex = isSent ? MSGFONTID_MYNAME + iFontIDOffset : MSGFONTID_YOURNAME + iFontIDOffset;
						break;
					case 'm':
						fontindex = isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset;
						break;
					case 'M':
						fontindex = isSent ? MSGFONTID_MYMISC + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset;
						break;
					case 's':
						fontindex = isSent ? MSGFONTID_SYMBOLS_OUT : MSGFONTID_SYMBOLS_IN;
						break;
					}
					if (fontindex != -1) {
						i++;
						AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "%s ", GetRTFFont(fontindex));
					}
					else skipToNext = TRUE;
				}
				break;

			case 'c':      // font color (using one of the predefined 5 colors) or one of the standard font colors (m = message, d = date/time, n = nick)
				color = szTemplate[i + 2];
				if (color >= '0' && color <= '4') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\cf%d ", MSGDLGFONTCOUNT + 8 + (color - '0'));
					i++;
				}
				else if (color == (TCHAR)'d') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\cf%d ", isSent ? MSGFONTID_MYTIME + iFontIDOffset : MSGFONTID_YOURTIME + iFontIDOffset);
					i++;
				}
				else if (color == (TCHAR)'m') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\cf%d ", isSent ? MSGFONTID_MYMSG + iFontIDOffset : MSGFONTID_YOURMSG + iFontIDOffset);
					i++;
				}
				else if (color == (TCHAR)'n') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\cf%d ", isSent ? MSGFONTID_MYNAME + iFontIDOffset : MSGFONTID_YOURNAME + iFontIDOffset);
					i++;
				}
				else if (color == (TCHAR)'s') {
					AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\cf%d ", isSent ? MSGFONTID_SYMBOLS_OUT : MSGFONTID_SYMBOLS_IN);
					i++;
				}
				else skipToNext = TRUE;
				break;

			case '<':		// bidi tag
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\rtlmark\\rtlch ");
				break;
			case '>':		// bidi tag
				AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\ltrmark\\ltrch ");
				break;
			}
skip:
			if (skipToNext) {
				i++;
				while (szTemplate[i] != '%' && i < iTemplateLen) i++;
			}
			else i += 2;
		}
		else {
			char temp[24];
			mir_snprintf(temp, 24, "{\\uc1\\u%d?}", (int)ci);
			AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, temp);
			i++;
		}
	}

	if (dat->hHistoryEvents)
		AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, dat->szMicroLf, MSGDLGFONTCOUNT + 1 + ((isSent) ? 1 : 0), hDbEvent);

	AppendToBuffer(&buffer, &bufferEnd, &bufferAlloced, "\\par");

	if (streamData->dbei == 0)
		mir_free(dbei.pBlob);

	dat->iLastEventType = MAKELONG((dbei.flags & (DBEF_SENT | DBEF_READ | DBEF_RTL)), dbei.eventType);
	dat->lastEventTime = dbei.timestamp;
	return buffer;
}

static DWORD CALLBACK LogStreamInEvents(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG * pcb)
{
	LogStreamData *dat = (LogStreamData *) dwCookie;

	if (dat->buffer == NULL) {
		dat->bufferOffset = 0;
		switch (dat->stage) {
		case STREAMSTAGE_HEADER:
			if (dat->buffer) mir_free(dat->buffer);
			dat->buffer = CreateRTFHeader(dat->dlgDat);
			dat->stage = STREAMSTAGE_EVENTS;
			break;

		case STREAMSTAGE_EVENTS:
			if (dat->eventsToInsert) {
				do {
					if (dat->buffer) mir_free(dat->buffer);
					dat->buffer = Template_CreateRTFFromDbEvent(dat->dlgDat, dat->hContact, dat->hDbEvent, !dat->isEmpty, dat);
					if (dat->buffer)
						dat->hDbEventLast = dat->hDbEvent;
					dat->hDbEvent = db_event_next(dat->hDbEvent);
					if (--dat->eventsToInsert == 0)
						break;
				}
				while (dat->buffer == NULL && dat->hDbEvent);
				if (dat->buffer) {
					//dat->isEmpty = 0;
					break;
				}
			}
			dat->stage = STREAMSTAGE_TAIL;

			//fall through
		case STREAMSTAGE_TAIL:
			if (dat->buffer) mir_free(dat->buffer);
			dat->buffer = CreateRTFTail(dat->dlgDat);
			dat->stage = STREAMSTAGE_STOP;
			break;

		case STREAMSTAGE_STOP:
			*pcb = 0;
			return 0;
		}
		dat->bufferLen = lstrlenA(dat->buffer);
	}
	*pcb = min(cb, dat->bufferLen - dat->bufferOffset);
	CopyMemory(pbBuff, dat->buffer + dat->bufferOffset, *pcb);
	dat->bufferOffset += *pcb;
	if (dat->bufferOffset == dat->bufferLen) {
		mir_free(dat->buffer);
		dat->buffer = NULL;
	}
	return 0;
}

static void SetupLogFormatting(TWindowData *dat)
{
	if (dat->hHistoryEvents)
		mir_snprintf(dat->szMicroLf, sizeof(dat->szMicroLf), "%s", "\\v\\cf%d \\ ~-+%d+-~\\v0 ");
	else
		mir_snprintf(dat->szMicroLf, sizeof(dat->szMicroLf), "%s\\par\\ltrpar\\sl-1%s ", GetRTFFont(MSGDLGFONTCOUNT), GetRTFFont(MSGDLGFONTCOUNT));
}

void TSAPI StreamInEvents(HWND hwndDlg, HANDLE hDbEventFirst, int count, int fAppend, DBEVENTINFO *dbei_s)
{
	TWindowData *dat = (TWindowData*) GetWindowLongPtr(hwndDlg, GWLP_USERDATA);
	CHARRANGE oldSel, sel;
	POINT pt = {0};
	BOOL wasFirstAppend = (dat->isAutoRTL & 2) ? TRUE : FALSE;

	/*
	* calc time limit for grouping
	*/
	HWND hwndrtf = dat->hwndIEView ? dat->hwndIWebBrowserControl : GetDlgItem(hwndDlg, IDC_LOG);

	SCROLLINFO si = { 0 }, *psi = &si;
	si.cbSize = sizeof(si);

	rtfFonts = dat->pContainer->theme.rtfFonts ? dat->pContainer->theme.rtfFonts : &(rtfFontsGlobal[0][0]);
	time_t now = time(NULL);

	struct tm tm_now = *localtime(&now);
	struct tm tm_today = tm_now;
	tm_today.tm_hour = tm_today.tm_min = tm_today.tm_sec = 0;
	today = mktime(&tm_today);

	if (dat->hwndIEView != 0) {
		IEVIEWEVENT event = { sizeof(event) };
		event.cbSize = sizeof(IEVIEWEVENT);
		event.hwnd = dat->hwndIEView;
		event.hContact = dat->hContact;
		event.dwFlags = (dat->dwFlags & MWF_LOG_RTL) ? IEEF_RTL : 0;
		if (dat->sendMode & SMODE_FORCEANSI) {
			event.dwFlags |= IEEF_NO_UNICODE;
			event.codepage = dat->codePage;
		}
		else event.codepage = 0;

		if (!fAppend) {
			event.iType = IEE_CLEAR_LOG;
			CallService(MS_IEVIEW_EVENT, 0, (LPARAM)&event);
		}
		event.iType = IEE_LOG_DB_EVENTS;
		event.hDbEventFirst = hDbEventFirst;
		event.count = count;
		event.pszProto = dat->szProto;
		CallService(MS_IEVIEW_EVENT, 0, (LPARAM)&event);
		DM_ScrollToBottom(dat, 0, 0);
		if (fAppend)
			dat->hDbEventLast = hDbEventFirst;
		else
			dat->hDbEventLast = db_event_last(dat->hContact);
		return;
	}

	if (dat->hwndHPP != 0) {
		IEVIEWEVENT event = { sizeof(event) };
		event.hwnd = dat->hwndHPP;
		event.hContact = dat->hContact;
		event.dwFlags = (dat->dwFlags & MWF_LOG_RTL) ? IEEF_RTL : 0;
		if (dat->sendMode & SMODE_FORCEANSI) {
			event.dwFlags |= IEEF_NO_UNICODE;
			event.codepage = dat->codePage;
		}
		else event.codepage = 0;

		if (!fAppend) {
			event.iType = IEE_CLEAR_LOG;
			CallService(MS_HPP_EG_EVENT, 0, (LPARAM)&event);
		}
		event.iType = IEE_LOG_DB_EVENTS;
		event.hDbEventFirst = hDbEventFirst;
		event.count = count;
		CallService(MS_HPP_EG_EVENT, 0, (LPARAM)&event);
		//SendMessage(hwndDlg, DM_FORCESCROLL, (WPARAM)&pt, (LPARAM)&si);
		DM_ScrollToBottom(dat, 0, 0);
		if (fAppend)
			dat->hDbEventLast = hDbEventFirst;
		else
			dat->hDbEventLast = db_event_last(dat->hContact);
		return;
	}

	// separator strings used for grid lines, message separation and so on...

	dat->clr_added = FALSE;

	if (dat->szMicroLf[0] == 0)
		SetupLogFormatting(dat);

	szYourName = const_cast<TCHAR *>(dat->cache->getNick());
	szMyName = dat->szMyNickname;

	SendDlgItemMessage(hwndDlg, IDC_LOG, EM_HIDESELECTION, TRUE, 0);
	SendDlgItemMessage(hwndDlg, IDC_LOG, EM_EXGETSEL, 0, (LPARAM)&oldSel);
	
	LogStreamData streamData = { 0 };
	streamData.hContact = dat->hContact;
	streamData.hDbEvent = hDbEventFirst;
	streamData.dlgDat = dat;
	streamData.eventsToInsert = count;
	streamData.isEmpty = fAppend ? GetWindowTextLength(GetDlgItem(hwndDlg, IDC_LOG)) == 0 : 1;
	streamData.dbei = dbei_s;

	EDITSTREAM stream = { 0 };
	stream.pfnCallback = LogStreamInEvents;
	stream.dwCookie = (DWORD_PTR)& streamData;
	streamData.isAppend = fAppend;

	LONG startAt;
	if (fAppend) {
		GETTEXTLENGTHEX gtxl = { 0 };
		gtxl.codepage = 1200;
		gtxl.flags = GTL_DEFAULT | GTL_PRECISE | GTL_NUMCHARS;
		startAt = SendDlgItemMessage(hwndDlg, IDC_LOG, EM_GETTEXTLENGTHEX, (WPARAM)& gtxl, 0);
		sel.cpMin = sel.cpMax = GetWindowTextLength(GetDlgItem(hwndDlg, IDC_LOG));
		SendDlgItemMessage(hwndDlg, IDC_LOG, EM_EXSETSEL, 0, (LPARAM)&sel);
	}
	else {
		SetDlgItemText(hwndDlg, IDC_LOG, _T(""));
		sel.cpMin = 0;
		sel.cpMax = GetWindowTextLength(hwndrtf);
		SendDlgItemMessage(hwndDlg, IDC_LOG, EM_EXSETSEL, 0, (LPARAM)&sel);
		startAt = 0;
		dat->isAutoRTL = 0;
	}

	SendMessage(hwndrtf, WM_SETREDRAW, FALSE, 0);

	SendDlgItemMessage(hwndDlg, IDC_LOG, EM_STREAMIN, fAppend ? SFF_SELECTION | SF_RTF : SFF_SELECTION |  SF_RTF, (LPARAM)&stream);
	SendDlgItemMessage(hwndDlg, IDC_LOG, EM_EXSETSEL, 0, (LPARAM)&oldSel);
	SendDlgItemMessage(hwndDlg, IDC_LOG, EM_HIDESELECTION, FALSE, 0);
	dat->hDbEventLast = streamData.hDbEventLast;

	if (dat->isAutoRTL & 1) 
		SendMessage(hwndrtf, EM_SETBKGNDCOLOR, 0, (LOWORD(dat->iLastEventType) & DBEF_SENT) ? (fAppend ? dat->pContainer->theme.outbg : dat->pContainer->theme.oldoutbg) :
			(fAppend ? dat->pContainer->theme.inbg : dat->pContainer->theme.oldinbg));

	if (!(dat->isAutoRTL & 1)) {
		GETTEXTLENGTHEX gtxl = {0};
		gtxl.codepage = 1200;
		gtxl.flags = GTL_DEFAULT | GTL_PRECISE | GTL_NUMCHARS;

		PARAFORMAT2 pf2;
		ZeroMemory(&pf2, sizeof(PARAFORMAT2));
		sel.cpMax = SendDlgItemMessage(hwndDlg, IDC_LOG, EM_GETTEXTLENGTHEX, (WPARAM)& gtxl, 0);
		sel.cpMin = sel.cpMax - 1;
		SendDlgItemMessage(hwndDlg, IDC_LOG, EM_EXSETSEL, 0, (LPARAM)&sel);
		SendDlgItemMessage(hwndDlg, IDC_LOG, EM_REPLACESEL, FALSE, (LPARAM)_T(""));
		dat->isAutoRTL |= 2;
	}

	BOOL isSent;
	if (streamData.dbei != 0)
		isSent = (streamData.dbei->flags & DBEF_SENT) != 0;
	else {
		DBEVENTINFO dbei = { sizeof(dbei) };
		db_event_get(hDbEventFirst, &dbei);
		isSent = (dbei.flags & DBEF_SENT) != 0;
	}

	ReplaceIcons(hwndDlg, dat, startAt, fAppend, isSent);
	dat->clr_added = FALSE;

	SendMessage(hwndDlg, DM_FORCESCROLL, (WPARAM)&pt, (LPARAM)psi);
	SendDlgItemMessage(hwndDlg, IDC_LOG, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(GetDlgItem(hwndDlg, IDC_LOG), NULL, FALSE);
	EnableWindow(GetDlgItem(hwndDlg, IDC_QUOTE), dat->hDbEventLast != NULL);
	if (streamData.buffer) mir_free(streamData.buffer);
}

static void ReplaceIcons(HWND hwndDlg, TWindowData *dat, LONG startAt, int fAppend, BOOL isSent)
{
	DWORD dwScale = M.GetDword("iconscale", 0);

	TCHAR trbuffer[40];
	TEXTRANGE tr;
	tr.lpstrText = trbuffer;

	HWND hwndrtf = GetDlgItem(hwndDlg, IDC_LOG);

	FINDTEXTEX fi;
	fi.chrg.cpMin = startAt;
	if (dat->clr_added) {
		unsigned int length;
		int index;
		CHARRANGE cr;
		fi.lpstrText = _T("##col##");
		fi.chrg.cpMax = -1;
		CHARFORMAT2 cf2;
		ZeroMemory(&cf2, sizeof(cf2));
		cf2.cbSize = sizeof(cf2);
		cf2.dwMask = CFM_COLOR;
		while (SendMessage(hwndrtf, EM_FINDTEXTEX, FR_DOWN, (LPARAM)&fi) > -1) {
			tr.chrg.cpMin = fi.chrgText.cpMin;
			tr.chrg.cpMax = tr.chrg.cpMin + 18;
			trbuffer[0] = 0;
			SendMessage(hwndrtf, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
			trbuffer[18] = 0;
			cr.cpMin = fi.chrgText.cpMin;
			cr.cpMax = cr.cpMin + 18;
			SendMessage(hwndrtf, EM_EXSETSEL, 0, (LPARAM)&cr);
			SendMessage(hwndrtf, EM_REPLACESEL, FALSE, (LPARAM)_T(""));
			length = (unsigned int)_ttol(&trbuffer[7]);
			index = _ttol(&trbuffer[14]);
			if (length > 0 && length < 20000 && index >= RTF_CTABLE_DEFSIZE && index < Utils::rtf_ctable_size) {
				cf2.crTextColor = Utils::rtf_ctable[index].clr;
				cr.cpMin = fi.chrgText.cpMin;
				cr.cpMax = cr.cpMin + length;
				SendMessage(hwndrtf, EM_EXSETSEL, 0, (LPARAM)&cr);
				SendMessage(hwndrtf, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf2);
			}
		}
	}
	fi.chrg.cpMin = startAt;
	if (dat->dwFlags & MWF_LOG_SHOWICONS) {
		BYTE bIconIndex = 0;
		char bDirection = 0;
		fi.lpstrText = _T("#~#");
		fi.chrg.cpMax = -1;

		CHARFORMAT2 cf2;
		ZeroMemory(&cf2, sizeof(cf2));
		cf2.cbSize = sizeof(cf2);
		cf2.dwMask = CFM_BACKCOLOR;

		IRichEditOle *ole;
		SendMessage(hwndrtf, EM_GETOLEINTERFACE, 0, (LPARAM)&ole);
		while (SendMessageA(hwndrtf, EM_FINDTEXTEX, FR_DOWN, (LPARAM)&fi) > -1) {
			CHARRANGE cr;
			cr.cpMin = fi.chrgText.cpMin;
			cr.cpMax = fi.chrgText.cpMax + 2;
			SendMessage(hwndrtf, EM_EXSETSEL, 0, (LPARAM)&cr);

			tr.chrg.cpMin = fi.chrgText.cpMin + 3;
			tr.chrg.cpMax = fi.chrgText.cpMin + 5;
			SendMessage(hwndrtf, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
			bIconIndex = ((BYTE)trbuffer[0] - (BYTE)'0');
			if (bIconIndex >= NR_LOGICONS) {
				fi.chrg.cpMin = fi.chrgText.cpMax + 6;
				continue;
			}
			bDirection = trbuffer[1];
			SendMessage(hwndrtf, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf2);
			COLORREF crDefault = cf2.crBackColor == 0 ? (true ? (bDirection == '>' ? (fAppend ? dat->pContainer->theme.outbg : dat->pContainer->theme.oldoutbg) :
				(fAppend ? dat->pContainer->theme.inbg : dat->pContainer->theme.oldinbg)) : dat->pContainer->theme.bg) : cf2.crBackColor;

			TLogIcon theIcon;
			CacheIconToBMP(&theIcon, Logicons[bIconIndex], crDefault, dwScale, dwScale);
			ImageDataInsertBitmap(ole, theIcon.hBmp);
			DeleteCachedIcon(&theIcon);
			fi.chrg.cpMin = cr.cpMax + 6;
		}
		ole->Release();
	}
	
	/*
	* do smiley replacing, using the service
	*/
	if (PluginConfig.g_SmileyAddAvail) {
		CHARRANGE sel;
		sel.cpMin = startAt;
		sel.cpMax = -1;

		SMADD_RICHEDIT3 smadd = { sizeof(smadd) };
		smadd.hwndRichEditControl = GetDlgItem(hwndDlg, IDC_LOG);
		smadd.Protocolname = const_cast<char *>(dat->cache->getActiveProto());
		smadd.hContact = dat->cache->getActiveContact();
		smadd.flags = isSent ? SAFLRE_OUTGOING : 0;
		if (startAt > 0)
			smadd.rangeToReplace = &sel;
		else
			smadd.rangeToReplace = NULL;
		smadd.disableRedraw = TRUE;
		if (dat->doSmileys)
			CallService(MS_SMILEYADD_REPLACESMILEYS, TABSRMM_SMILEYADD_BKGCOLORMODE, (LPARAM)&smadd);
	}

	if (dat->hHistoryEvents && dat->curHistory == dat->maxHistory) {
		char szPattern[50];
		mir_snprintf(szPattern, 40, "~-+%d+-~", (INT_PTR)dat->hHistoryEvents[0]);

		FINDTEXTEXA fi;
		fi.lpstrText = szPattern;
		fi.chrg.cpMin = 0;
		fi.chrg.cpMax = -1;
		if (SendMessageA(hwndrtf, EM_FINDTEXTEX, FR_DOWN, (LPARAM)&fi) != 0) {
			CHARRANGE sel;
			sel.cpMin = 0;
			sel.cpMax = 20;
			SendMessage(hwndrtf, EM_SETSEL, 0, fi.chrgText.cpMax + 1);
			SendMessageA(hwndrtf, EM_REPLACESEL, TRUE, (LPARAM)"");
		}
	}
}

/*
* NLS functions (for unicode version only) encoding stuff..
*/

static BOOL CALLBACK LangAddCallback(LPTSTR str)
{
	int i, count;
	UINT cp = _ttoi(str);
	count = SIZEOF(cpTable);
	for (i=0; i < count && cpTable[i].cpId != cp; i++);
	if (i < count)
		AppendMenu(PluginConfig.g_hMenuEncoding, MF_STRING, cp, TranslateTS(cpTable[i].cpName));

	return TRUE;
}

void TSAPI BuildCodePageList()
{
	PluginConfig.g_hMenuEncoding = CreateMenu();
	AppendMenu(PluginConfig.g_hMenuEncoding, MF_STRING, 500, TranslateT("Use default codepage"));
	AppendMenuA(PluginConfig.g_hMenuEncoding, MF_SEPARATOR, 0, 0);
	EnumSystemCodePages(LangAddCallback, CP_INSTALLED);
}

static TCHAR *Template_MakeRelativeDate(TWindowData *dat, HANDLE hTimeZone, time_t check, int groupBreak, TCHAR code)
{
	static TCHAR szResult[100];
	const TCHAR *szFormat;

	if ((code == (TCHAR)'R' || code == (TCHAR)'r') && check >= today)
		_tcscpy(szResult, szToday);
	else if ((code == (TCHAR)'R' || code == (TCHAR)'r') && check > (today - 86400))
		_tcscpy(szResult, szYesterday);
	else {
		if (code == 'D' || code == 'R')
			szFormat = _T("D");
		else if (code == 'T')
			szFormat = _T("s");
		else if (code == 't')
			szFormat = _T("t");
		else
			szFormat = _T("d");

		tmi.printTimeStamp(hTimeZone, check, szFormat, szResult, SIZEOF(szResult), 0);
	}
	return szResult;
}
