/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (c) 2012-14 Miranda NG project (http://miranda-ng.org),
Copyright (c) 2000-08 Miranda ICQ/IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

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

/*
*	Author Artem Shpynov aka FYR
*   Copyright 2000-2008 Artem Shpynov
*/

//////////////////////////////////////////////////////////////////////////
// Module to async parsing of texts

#include "hdr/modern_commonheaders.h"
#include "hdr/modern_gettextasync.h"
#include "newpluginapi.h"
#include "hdr/modern_sync.h"

int CLUI_SyncSetPDNCE(WPARAM wParam, LPARAM lParam);
int CLUI_SyncGetShortData(WPARAM wParam, LPARAM lParam);

#define gtalock EnterCriticalSection(&gtaCS)
#define gtaunlock LeaveCriticalSection( &gtaCS )

typedef struct _GetTextAsyncItem {
	HANDLE hContact;
	ClcData *dat;
	struct _GetTextAsyncItem *Next;
} GTACHAINITEM;

static GTACHAINITEM * gtaFirstItem = NULL;
static GTACHAINITEM * gtaLastItem = NULL;
static CRITICAL_SECTION gtaCS;
static HANDLE hgtaWakeupEvent = NULL;


static BOOL gtaGetItem(GTACHAINITEM * mpChain)
{
	gtalock;
	if (!gtaFirstItem)
	{
		gtaunlock;
		return FALSE;
	}
	else if (mpChain)
	{
		GTACHAINITEM * ch;
		ch = gtaFirstItem;
		*mpChain = *ch;
		gtaFirstItem = (GTACHAINITEM *)ch->Next;
		if (!gtaFirstItem) gtaLastItem = NULL;
		free(ch);
		gtaunlock;
		return TRUE;
	}
	gtaunlock;
	return FALSE;
}

static void gtaThreadProc(void *lpParam)
{
	thread_catcher lck(g_hGetTextAsyncThread);
	HWND hwnd = pcli->hwndContactList;
	SHORTDATA data = {0};

	while (!MirandaExiting()) {
		Sync(CLUI_SyncGetShortData,(WPARAM)pcli->hwndContactTree,(LPARAM)&data);
		while (true) {
			if ( MirandaExiting())
				return;

			SleepEx(0, TRUE); //1000 contacts per second

			GTACHAINITEM mpChain = {0};
			struct SHORTDATA dat2 = {0};
			if (!gtaGetItem(&mpChain))
				break;

			SHORTDATA *dat;
			if (mpChain.dat == NULL || (!IsBadReadPtr(mpChain.dat,sizeof(mpChain.dat)) && mpChain.dat->hWnd == data.hWnd))
				dat = &data;
			else {
				Sync(CLUI_SyncGetShortData,(WPARAM)mpChain.dat->hWnd,(LPARAM)&dat2);
				dat = &dat2;
			}
			if ( MirandaExiting())
				return;

			ClcCacheEntry cacheEntry;
			memset(&cacheEntry, 0, sizeof(cacheEntry));
			cacheEntry.hContact = mpChain.hContact;
			if (!Sync(CLUI_SyncGetPDNCE, (WPARAM) 0, (LPARAM)&cacheEntry)) {
				Cache_GetSecondLineText(dat, &cacheEntry);
				Cache_GetThirdLineText(dat, &cacheEntry);
				Sync(CLUI_SyncSetPDNCE, (WPARAM) CCI_LINES,(LPARAM)&cacheEntry);
				CListSettings_FreeCacheItemData(&cacheEntry);
			}

			KillTimer(dat->hWnd,TIMERID_INVALIDATE_FULL);
			CLUI_SafeSetTimer(dat->hWnd,TIMERID_INVALIDATE_FULL,500, NULL);
		}

		WaitForSingleObjectEx(hgtaWakeupEvent, INFINITE, TRUE);
		ResetEvent(hgtaWakeupEvent);
	}
}

BOOL gtaWakeThread()
{
	if (hgtaWakeupEvent && g_hGetTextAsyncThread) {
		SetEvent(hgtaWakeupEvent);
		return TRUE;
	}

	return FALSE;
}

int gtaAddRequest(ClcData *dat,ClcContact *contact,HANDLE hContact)
{
	if (MirandaExiting()) return 0;
	gtalock;
	{
		GTACHAINITEM * mpChain = (GTACHAINITEM *)malloc(sizeof(GTACHAINITEM));
		mpChain->hContact = hContact;
		mpChain->dat = dat;
		mpChain->Next = NULL;
		if (gtaLastItem)
		{
			gtaLastItem->Next = (GTACHAINITEM *)mpChain;
			gtaLastItem = mpChain;
		}
		else
		{
			gtaFirstItem = mpChain;
			gtaLastItem = mpChain;
			SetEvent(hgtaWakeupEvent);
		}
	}
	gtaunlock;
	return FALSE;
}

void gtaRenewText(HANDLE hContact)
{
	gtaAddRequest(NULL,NULL, hContact);
}

int gtaOnModulesUnload(WPARAM wParam,LPARAM lParam)
{
	SetEvent(hgtaWakeupEvent);
	return 0;
}

void InitCacheAsync()
{
	InitializeCriticalSection(&gtaCS);
	hgtaWakeupEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
	g_hGetTextAsyncThread = mir_forkthread(gtaThreadProc, 0);
	HookEvent(ME_SYSTEM_PRESHUTDOWN,  gtaOnModulesUnload);
}

void UninitCacheAsync()
{
	SetEvent(hgtaWakeupEvent);
	while(g_hGetTextAsyncThread)
		SleepEx(50, TRUE);

	CloseHandle(hgtaWakeupEvent);
	DeleteCriticalSection(&gtaCS);
}
