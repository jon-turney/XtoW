/*
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <windows.h>
#include "resource.h"
#include "winicons.h"

HICON g_hIconX = NULL;
HICON g_hSmallIconX = NULL;

static void
winInitGlobalIcons(void)
{
  if (!g_hIconX) {
    HINSTANCE hInstance = GetModuleHandle(NULL);

    g_hIconX = (HICON) LoadImage(hInstance,
                                 MAKEINTRESOURCE(IDI_XWIN),
                                 IMAGE_ICON,
                                 GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    g_hSmallIconX = (HICON) LoadImage(hInstance,
                                      MAKEINTRESOURCE(IDI_XWIN),
                                      IMAGE_ICON,
                                      GetSystemMetrics(SM_CXSMICON),
                                      GetSystemMetrics(SM_CYSMICON),
                                      LR_DEFAULTSIZE);
  }
}

void
winSelectIcons(HICON *pIcon, HICON *pSmallIcon)
{
  HICON hIcon, hSmallIcon;

  winInitGlobalIcons();

  /* Use default X icon */
  hIcon = g_hIconX;
  hSmallIcon = g_hSmallIconX;

  if (pIcon)
    *pIcon = hIcon;

  if (pSmallIcon)
    *pSmallIcon = hSmallIcon;
}
