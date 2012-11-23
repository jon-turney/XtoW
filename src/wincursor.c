//
// Copyright © Jon TURNEY 2012
//
// This file is part of XtoW.
//
// XtoW is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// XtoW is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with XtoW.  If not, see <http://www.gnu.org/licenses/>.
//

#include <windows.h>
#include <XWinWMUtil/cursor_convert.h>
#include <xcb/xfixes.h>
#include "global.h"
#include "debug.h"
#include "wincursor.h"

// we keep a handle to the current cursor around, so we can set it again on WM_SETCURSOR
static HCURSOR hCursor;

void InitCursor(void)
{
  UpdateCursor();
}

void
UpdateCursor(void)
{
  xcb_xfixes_get_cursor_image_cookie_t cookie = xcb_xfixes_get_cursor_image(xcwm_context_get_connection(context));
  xcb_xfixes_get_cursor_image_reply_t* reply = xcb_xfixes_get_cursor_image_reply(xcwm_context_get_connection(context), cookie, NULL);

  DEBUG("Got cursor image, serial %d\n", reply->cursor_serial);

  WMUTIL_CURSOR cursor;
  memset(&cursor, 0, sizeof(cursor));
  cursor.width = reply->width;
  cursor.height = reply->height;
  cursor.xhot = reply->xhot;
  cursor.yhot = reply->yhot;
  cursor.argb = xcb_xfixes_get_cursor_image_cursor_image(reply);

  hCursor = winXCursorToHCURSOR(&cursor);

  // XXX: We should only change the cursor if the cursor is within one of our windows...
  // it's hard to notice this as a problem, as windows don't normally try to change the cursor except in response to something being clicked...
  HCURSOR hPreviousCursor = SetCursor(hCursor);

  DEBUG("cursor 0x%08x, previous cursor 0x%08x\n", hCursor, hPreviousCursor);

  DestroyCursor(hPreviousCursor);

  free(reply);
}

HCURSOR winGetCursor(void)
{
  return hCursor;
}
