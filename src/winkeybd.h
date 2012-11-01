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

#ifndef WINKEYBD_H
#define WINKEYBD_H

void winKeybdReleaseKeys(void);
void winSendKeyEvent(DWORD dwKey, bool fDown);
bool winIsFakeCtrl_L(UINT message, WPARAM wParam, LPARAM lParam);
bool winCheckKeyPressed(WPARAM wParam, LPARAM lParam);
int winTranslateKey(WPARAM wParam, LPARAM lParam);
void winFixShiftKeys(int iScanCode);

#endif /* WINKEYBD_H */
