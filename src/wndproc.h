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

#ifndef WNDPROC_H
#define WNDPROC_H

#include <dwmapi.h>

void UpdateName(xcwm_window_t *window);
void UpdateImage(xcwm_window_t *window);
void UpdateStyle(xcwm_window_t *window);
void UpdateShape(xcwm_window_t *window);
void UpdateIcon(xcwm_window_t *window);
void UpdateState(xcwm_window_t *window);
void winCreateWindowsWindow(xcwm_window_t *window);
void winDestroyWindowsWindow(xcwm_window_t *window);
void winAdjustWindowsWindow(xcwm_window_t *window);

typedef HRESULT WINAPI (*PFNDWMENABLEBLURBEHINDWINDOW)(HWND hWnd, const DWM_BLURBEHIND *pBlurBehind);
extern PFNDWMENABLEBLURBEHINDWINDOW pDwmEnableBlurBehindWindow;

/* Undocumented */
typedef struct _ACCENTPOLICY
{
  ULONG AccentState;
  ULONG AccentFlags;
  ULONG GradientColor;
  ULONG AnimationId;
} ACCENTPOLICY;

#define ACCENT_ENABLE_BLURBEHIND 3

typedef struct _WINCOMPATTR
{
    DWORD attribute;
    PVOID pData;
    ULONG dataSize;
}  WINCOMPATTR;

#define WCA_ACCENT_POLICY 19

typedef HRESULT WINAPI (*PFNSETWINDOWCOMPOSITIONATTRIBUTE)(HWND, WINCOMPATTR *);
extern PFNSETWINDOWCOMPOSITIONATTRIBUTE pSetWindowCompositionAttribute;

extern int blur;

#endif /* WNDPROC_H */
