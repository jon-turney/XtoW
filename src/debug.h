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

#ifndef DEBUG_H
#define DEBUG_H

int winError(const char *format, ...);

#if 1
int verbosity;
int winDebug(const char *format, ...);
int winDebugVerbosity(int level, const char *format, ...);
#define DEBUG(Args...) winDebugVerbosity(1, Args)
#else
#define DEBUG(Arg...)
#endif

#endif /* DEBUG_H */
