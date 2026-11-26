/***************************************************************************
 *
 *
 * Copyright (c) 2023, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#pragma once

#include "instrs.h"
#include "traps.h"

// The magic says what kind of file this is and which layout it has. Every reader
// first requires the file's VER_NUM to equal this toolchain's exactly, so a file
// from any other build is refused before its magic is looked at; only these two
// values are ever accepted, and an older magic is just "not a file of ours".
//
// History, for the next person who changes a layout without changing its look:
// since 0xffbf a program allocates an Int[] and an object array with distinct
// opcodes (NEW_INT_ARY, NEW_OBJ_ARY: GC roots phase 6). A library records its
// methods' parameter names and which take defaults, so a caller can name a
// library method's arguments (#1244) (0xffb7); where a method's own type
// parameters stand in its signature (#1242) (0xffb8); a bundle-level enum named
// with its bundle, as a class is ('Query.Structured.ColumnType'), in its own
// entry and in every signature (0xffb9); and, since 0xffba, the same distinct
// array opcodes as a program.
#define MAGIC_NUM_EXE 0xffbf
#define MAGIC_NUM_LIB 0xffba

#define VER_NUM 2026114
#define VERSION_STRING L"2026.11.4"
