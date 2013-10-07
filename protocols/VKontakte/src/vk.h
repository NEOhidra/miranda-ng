/*
Copyright (C) 2013 Miranda NG Project (http://miranda-ng.org)

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

#define VK_APP_ID  "3917910"

#define VK_API_URL "api.vk.com"
#define VK_REDIRECT_URL "http://" VK_API_URL "/blank.html"

typedef NETLIBHTTPHEADER HttpParam;

extern HINSTANCE hInst;

LPCSTR findHeader(NETLIBHTTPREQUEST *hdr, LPCSTR szField);