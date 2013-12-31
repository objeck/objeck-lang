// Scintilla source code edit control
/** @file LexObjeck.cxx
** Lexer for Objeck.
** Based heavily on LexCPP.cxx
**/
// Copyright 2014 - by Randy Hollines objeck@gmail.com
// Copyright 2001- by Vamsi Potluru <vamsi@who.net> & Praveen Ambekar <ambekarpraveen@yahoo.com>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

#ifdef SCI_NAMESPACE
using namespace Objeck;
#endif

static const char *const emptyWordListDesc[] = {
  0
};

static void ColouriseObjeckDoc(unsigned int startPos, int length, int initStyle, WordList *keywordlists[], Accessor &styler) {
  styler.StartAt(startPos);

  WordList &keywords = *keywordlists[0];
  WordList &keywords2 = *keywordlists[1];
  
  int state = initStyle;
  char next_char = styler.SafeGetCharAt(startPos);
  styler.StartSegment(startPos);
  const int end = startPos + length;
  
  const int buffer_max = 80;
  char buffer[buffer_max + 1];
  int buffer_pos = 0;
  
  for(int i = startPos; i < end; i++) {
    char cur_char = next_char;
    next_char = styler.SafeGetCharAt(i + 1);

    switch(state) {
    case SCE_OBJK_DEFAULT:
      // ident
      if(isalpha(cur_char) || cur_char == '_') {
        buffer_pos = 0;
        buffer[buffer_pos++] = cur_char;
        state = SCE_OBJK_IDENTIFIER;
      }
      // number
      else if(isdigit(cur_char) || cur_char == '.') {
        buffer_pos = 0;
        buffer[buffer_pos++] = cur_char;
        state = SCE_OBJK_NUMBER;
      }
      // other
      else {
        switch(cur_char) {
        case ':':
          if(next_char == '=') {
            styler.ColourTo(i + 1, SCE_OBJK_OPERATOR);
            continue;
          }
          break;

        case '%':
        case '=':
        case '&':
        case '|':
        case '?':
        case '!':
          styler.ColourTo(i, SCE_OBJK_OPERATOR);
          break;

        case '<':
          if(next_char == '=' || next_char == '>' || next_char == '<') {
            styler.ColourTo(i + 1, SCE_OBJK_OPERATOR);
            continue;
          }
          else {
            styler.ColourTo(i, SCE_OBJK_OPERATOR);
          }
          break;

        case '>':
          if(next_char == '=' || next_char == '>') {
            styler.ColourTo(i + 1, SCE_OBJK_OPERATOR);
            continue;
          }
          else {
            styler.ColourTo(i, SCE_OBJK_OPERATOR);
          }
          break;

        case '+':
        case '*':
        case '/':
          if(next_char == '=') {
            styler.ColourTo(i + 1, SCE_OBJK_OPERATOR);
            continue;
          }
          else {
            styler.ColourTo(i, SCE_OBJK_OPERATOR);
          }
          break;

        case '-':
          if(next_char == '>' || next_char == '=') {
            styler.ColourTo(i + 1, SCE_OBJK_OPERATOR);
            continue;
          }
          else {
            styler.ColourTo(i, SCE_OBJK_OPERATOR);
          }
          break;

        case '#':
          if(next_char == '~') {
            state = SCE_OBJK_COMMENTDOC;
          }
          else {
            state = SCE_OBJK_COMMENTLINE;
          }
          break;

        default:
          // reset
          state = SCE_OBJK_DEFAULT;
          styler.ColourTo(i, SCE_OBJK_DEFAULT);
          break;
        }
      }
      break;

    case SCE_OBJK_COMMENTDOC:
      if(cur_char == '~' && next_char == '#') {
        styler.ColourTo(i + 1, SCE_OBJK_COMMENTDOC);
        // reset
        state = SCE_OBJK_DEFAULT;
        styler.ColourTo(i + 2, SCE_OBJK_DEFAULT);
      }
      break;

    case SCE_OBJK_COMMENTLINE:
      if(cur_char == '\r' || cur_char == '\n') {
        styler.ColourTo(i - 1, SCE_OBJK_COMMENTLINE);
        // reset
        state = SCE_OBJK_DEFAULT;
        styler.ColourTo(i, SCE_OBJK_DEFAULT);
      }
      break;

    case SCE_OBJK_IDENTIFIER:
      if(buffer_pos < buffer_max && (isalpha(cur_char) || cur_char == '_' || isdigit(cur_char))) {
        buffer[buffer_pos++] = cur_char;
      }
      // check for word in wordlist
      else {
        buffer[buffer_pos] = '\0';
        if(keywords.InList(buffer)) {
          styler.ColourTo(i - 1, SCE_OBJK_IDENTIFIER);
        }
        else if(keywords2.InList(buffer)) {
          styler.ColourTo(i - 1, SCE_OBJK_WORD2);
        }
        // reset
        state = SCE_OBJK_DEFAULT;
        styler.ColourTo(i, SCE_OBJK_DEFAULT);
      }
      break;

    case SCE_OBJK_NUMBER:
      if(buffer_pos < buffer_max && (isdigit(cur_char) || cur_char == '.')) {
        buffer[buffer_pos++] = cur_char;
      }
      // look for word in wordlist
      else {
        buffer[buffer_pos] = '\0';
        styler.ColourTo(i - 1, SCE_OBJK_NUMBER);
        // reset
        state = SCE_OBJK_DEFAULT;
        styler.ColourTo(i, SCE_OBJK_DEFAULT);
      }
      break;

    default:
      // reset
      state = SCE_OBJK_DEFAULT;
      styler.ColourTo(i, SCE_OBJK_DEFAULT);
      break;
    }
  }

  styler.ColourTo(end - 1, state);
}

static void FoldObjeckDoc(unsigned int startPos, int length, int initStyle, WordList *[], Accessor &styler) {
  bool foldComment = styler.GetPropertyInt("fold.comment") != 0;
  bool foldCompact = styler.GetPropertyInt("fold.compact", 1) != 0;
  unsigned int endPos = startPos + length;
  int visibleChars = 0;
  int lineCurrent = styler.GetLine(startPos);
  int levelPrev = styler.LevelAt(lineCurrent) & SC_FOLDLEVELNUMBERMASK;
  int levelCurrent = levelPrev;
  char next_char = styler[startPos];
  int styleNext = styler.StyleAt(startPos);
  int style = initStyle;

  for(unsigned int i = startPos; i < endPos; i++) {
    char ch = next_char;
    next_char = styler.SafeGetCharAt(i + 1);
    int stylePrev = style;
    style = styleNext;
    styleNext = styler.StyleAt(i + 1);

    bool atEOL = (ch == '\r' && next_char != '\n') || (ch == '\n');

    if(foldComment && (style == SCE_OBJK_COMMENTDOC)) {
      if(style != stylePrev) {
        levelCurrent++;
      }
      else if((style != styleNext) && !atEOL) {
        // Comments don't end at end of line and the next character may be unstyled.
        levelCurrent--;
      }
    }
    
    if(style != SCE_OBJK_COMMENTLINE && style != SCE_OBJK_COMMENTDOC) {
      if(ch == '{') {
        levelCurrent++;
      }
      else if(ch == '}') {
        levelCurrent--;
      }
    }

    if(atEOL) {
      int lev = levelPrev;
      if(visibleChars == 0 && foldCompact)
        lev |= SC_FOLDLEVELWHITEFLAG;
      if((levelCurrent > levelPrev) && (visibleChars > 0))
        lev |= SC_FOLDLEVELHEADERFLAG;
      if(lev != styler.LevelAt(lineCurrent)) {
        styler.SetLevel(lineCurrent, lev);
      }
      lineCurrent++;
      levelPrev = levelCurrent;
      visibleChars = 0;
    }
    if(!isspacechar(ch))
      visibleChars++;
  }
  // Fill in the real level of the next line, keeping the current flags as they will be filled in later
  int flagsNext = styler.LevelAt(lineCurrent) & ~SC_FOLDLEVELNUMBERMASK;
  styler.SetLevel(lineCurrent, levelPrev | flagsNext);
}

LexerModule lmObjeck(SCLEX_OBJK, ColouriseObjeckDoc, "objk", FoldObjeckDoc, emptyWordListDesc);
