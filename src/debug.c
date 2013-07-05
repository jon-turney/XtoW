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

#include <stdarg.h>
#include <stdio.h>

#include "debug.h"

int verbosity = 0;

static
int winVDebugVerbosity(int level, const char *format, va_list ap)
{
  int count = 0;
  if (level <= verbosity)
    {
      count = fprintf(stderr, "xtow: ");
      count += vfprintf(stderr, format, ap);
    }
  return count;
}

int winDebugVerbosity(int level, const char *format, ...)
{
  int count;
  va_list ap;
  va_start(ap, format);
  count = winVDebugVerbosity(level, format, ap);
  va_end(ap);
  return count;
}

#define DEBUG_VERBOSITY_DEFAULT 2

int
winDebug(const char *format, ...)
{
  int count;
  va_list ap;
  va_start(ap, format);
  count = winVDebugVerbosity(DEBUG_VERBOSITY_DEFAULT, format, ap);
  va_end(ap);
  return count;
}

int
winError(const char *format, ...)
{
  int count;
  va_list ap;
  va_start(ap, format);
  count = vfprintf(stderr, format, ap);
  va_end(ap);
  return count;
}
