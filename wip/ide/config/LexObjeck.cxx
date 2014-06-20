// Scintilla source code edit control
/** @file LexObjeck.cxx
** Lexer for Objeck.
**/
// Author Randy Hollines, Objeck Programming Langauge, http://www.objeck.org
// Based on Lexer for C++, C, Java, Prolog, ASM, and JavaScript.
// Copyright 1998-2005 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#ifdef _MSC_VER
#pragma warning(disable: 4786)
#endif

#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"
#include "OptionSet.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

static inline bool IsAWordChar(const int ch) {
  return (ch < 0x80) && (isalnum(ch) || ch == '_');
}

static inline bool IsAWordStart(const int ch) {
  return (ch < 0x80) && (isalnum(ch) || ch == '_' || ch == '@');
}

static inline bool IsObjeckOperator(const int ch) {
  if ((ch < 0x80) && (isalnum(ch)))
    return false;
  // '.' left out as it is used to make up numbers
  if (ch == '*' || ch == '/' || ch == '-' || ch == '+' ||
    ch == '(' || ch == ')' || ch == '{' || ch == '}' || 
    ch == '=' || ch == '[' || ch == ']' || ch == '<' || 
    ch == '&' || ch == '>' || ch == ',' || ch == '|' || 
    ch == '%' || ch == ':')
    return true;
  return false;
}

static inline int LowerCase(int c) {
  if (c >= 'A' && c <= 'Z') {
    return 'a' + c - 'A';
  }

  return c;
}

// Options used for LexerObjeck
struct OptionsObjeck {
    OptionsObjeck() {
    }
};

// TODO: fix up
static const char *const objeckWordLists[] = {
    "aaa",
    "bbb",
    "ccc",
    "ddd",
    0,
};

struct OptionSetObjeck : public OptionSet<OptionsObjeck> {
    OptionSetObjeck() {
        DefineWordListSets(objeckWordLists);
    }
};

class LexerObjeck : public ILexer {
    WordList words0;
    WordList words1;
    OptionsObjeck options;
    OptionSetObjeck osObjeck;

public:
    LexerObjeck() {
    }

    virtual ~LexerObjeck() {
    }

    void SCI_METHOD Release() {
        delete this;
    }

    int SCI_METHOD Version() const {
        return lvOriginal;
    }

    const char * SCI_METHOD PropertyNames() {
        return osObjeck.PropertyNames();
    }

    int SCI_METHOD PropertyType(const char *name) {
        return osObjeck.PropertyType(name);
    }

    const char * SCI_METHOD DescribeProperty(const char *name) {
        return osObjeck.DescribeProperty(name);
    }

    int SCI_METHOD PropertySet(const char *key, const char *val);
    const char * SCI_METHOD DescribeWordListSets() {
        return osObjeck.DescribeWordListSets();
    }

    int SCI_METHOD WordListSet(int n, const char *wl);
    void SCI_METHOD Lex(unsigned int startPos, int length, int initStyle, IDocument *pAccess);
    void SCI_METHOD Fold(unsigned int startPos, int length, int initStyle, IDocument *pAccess);

    void * SCI_METHOD PrivateCall(int, void *) {
        return 0;
    }

    static ILexer *LexerFactoryObjeck() {
        return new LexerObjeck();
    }
};

int SCI_METHOD LexerObjeck::PropertySet(const char *key, const char *val) {
    if (osObjeck.PropertySet(&options, key, val)) {
        return 0;
    }
    return -1;
}

int SCI_METHOD LexerObjeck::WordListSet(int n, const char *wl) {
    WordList *wordListN = 0;
    switch (n) {
    case 0:
        wordListN = &words0;
        break;
    case 1:
        wordListN = &words1;
        break;    
    }
    int firstModification = -1;
    if (wordListN) {
        WordList wlNew;
        wlNew.Set(wl);
        if (*wordListN != wlNew) {
            wordListN->Set(wl);
            firstModification = 0;
        }
    }
    return firstModification;
}

void SCI_METHOD LexerObjeck::Lex(unsigned int startPos, int length, int initStyle, IDocument *pAccess) {
  LexAccessor styler(pAccess);

  // Do not leak onto next line
  if (initStyle == SCE_OBJECK_STRINGEOL) {
    initStyle = SCE_OBJECK_DEFAULT;
  }

  int nestLevel = 0;
  int currentLine = styler.GetLine(startPos);
  if (currentLine >= 1) {
    nestLevel = styler.GetLineState(currentLine - 1);
  }

  StyleContext sc(startPos, length, initStyle, styler);

  for (; sc.More(); sc.Forward())
  {

    // Prevent SCE_OBJECK_STRINGEOL from leaking back to previous line
    if (sc.atLineStart && (sc.state == SCE_OBJECK_STRING)) {
      sc.SetState(SCE_OBJECK_STRING);
    }
    else if (sc.atLineStart && (sc.state == SCE_OBJECK_CHARACTER)) {
      sc.SetState(SCE_OBJECK_CHARACTER);
    }

    // Handle line continuation generically.
    if (sc.ch == '\\') {
      if (sc.chNext == '\n' || sc.chNext == '\r') {
        sc.Forward();
        if (sc.ch == '\r' && sc.chNext == '\n') {
          sc.Forward();
        }
        continue;
      }
    }

    // Determine if the current state should terminate.
    if (sc.state == SCE_OBJECK_OPERATOR) {
      if (!IsObjeckOperator(sc.ch)) {
        sc.SetState(SCE_OBJECK_DEFAULT);
      }
    }
    else if (sc.state == SCE_OBJECK_NUMBER) {
      if (!IsAWordChar(sc.ch)) {
        sc.SetState(SCE_OBJECK_DEFAULT);
      }
    }
    else if (sc.state == SCE_OBJECK_IDENTIFIER) {
      if (!IsAWordChar(sc.ch)) {
        char s[100];
        sc.GetCurrent(s, sizeof(s));
        bool IsDirective = false;

        if (words0.InList(s)) {
          sc.ChangeState(SCE_OBJECK_WORD0);
        }
        else if (words1.InList(s)) {
          sc.ChangeState(SCE_OBJECK_WORD1);
        }
        sc.SetState(SCE_OBJECK_DEFAULT);        
      }
    }
    else if (sc.state == SCE_OBJECK_COMMENT_LINE) {
      if (sc.atLineEnd) {
        sc.SetState(SCE_OBJECK_DEFAULT);
      }
    }
    else if (sc.state == SCE_OBJECK_STRING) {
      if (sc.ch == '\\') {
        if (sc.chNext == '\"' || sc.chNext == '\'' || sc.chNext == '\\') {
          sc.Forward();
        }
      }
      else if (sc.ch == '\"') {
        sc.ForwardSetState(SCE_OBJECK_DEFAULT);
      }
      else if (sc.atLineEnd) {
        sc.ChangeState(SCE_OBJECK_STRINGEOL);
        sc.ForwardSetState(SCE_OBJECK_DEFAULT);
      }
    }
    else if (sc.state == SCE_OBJECK_CHARACTER) {
      if (sc.ch == '\\') {
        if (sc.chNext == '\"' || sc.chNext == '\'' || sc.chNext == '\\') {
          sc.Forward();
        }
      }
      else if (sc.ch == '\'') {
        sc.ForwardSetState(SCE_OBJECK_DEFAULT);
      }
      else if (sc.atLineEnd) {
        sc.ChangeState(SCE_OBJECK_STRINGEOL);
        sc.ForwardSetState(SCE_OBJECK_DEFAULT);
      }
    }
    else if (sc.state == SCE_OBJECK_COMMENT_BLOCK) {
      if (sc.Match('*', '/')) {
        sc.Forward();
        nestLevel--;
        int nextState = (nestLevel == 0) ? SCE_OBJECK_DEFAULT : SCE_OBJECK_COMMENT_BLOCK;
        sc.ForwardSetState(nextState);
      }
      else if (sc.Match('/', '*')) {
        sc.Forward();
        nestLevel++;
      }
      else if (sc.ch == '%') {
        sc.SetState(SCE_OBJECK_COMMENT_LINE);
      }
    }

    // Determine if a new state should be entered.
    if (sc.state == SCE_OBJECK_DEFAULT) {
      if (sc.ch == '#'){
        sc.SetState(SCE_OBJECK_COMMENT_LINE);
      }
      else if (sc.Match('#', '~')) {
        sc.SetState(SCE_OBJECK_COMMENT_BLOCK);
        nestLevel = 1;
        sc.Forward();	// Eat the * so it isn't used for the end of the comment
      }
      else if (isascii(sc.ch) && (isdigit(sc.ch) || (sc.ch == '.' && isascii(sc.chNext) && isdigit(sc.chNext)))) {
        sc.SetState(SCE_OBJECK_NUMBER);
      }
      else if (IsAWordStart(sc.ch)) {
        sc.SetState(SCE_OBJECK_IDENTIFIER);
      }
      else if (sc.ch == '\"') {
        sc.SetState(SCE_OBJECK_STRING);
      }
      else if (sc.ch == '\'') {
        sc.SetState(SCE_OBJECK_CHARACTER);
      }
      else if (IsObjeckOperator(sc.ch)) {
        sc.SetState(SCE_OBJECK_OPERATOR);
      }
    }

  }
  sc.Complete();
}

// Store both the current line's fold level and the next lines in the
// level store to make it easy to pick up with each increment
// and to make it possible to fiddle the current level for "} else {".

void SCI_METHOD LexerObjeck::Fold(unsigned int startPos, int length, int initStyle, IDocument *pAccess) {

    LexAccessor styler(pAccess);

    unsigned int endPos = startPos + length;
    int visibleChars = 0;
    int currentLine = styler.GetLine(startPos);
    int levelCurrent = SC_FOLDLEVELBASE;
    if (currentLine > 0)
        levelCurrent = styler.LevelAt(currentLine-1) >> 16;
    int levelMinCurrent = levelCurrent;
    int levelNext = levelCurrent;
    char chNext = styler[startPos];
    int styleNext = styler.StyleAt(startPos);
    int style = initStyle;
    for (unsigned int i = startPos; i < endPos; i++) {
        char ch = chNext;
        chNext = styler.SafeGetCharAt(i + 1);
        style = styleNext;
        styleNext = styler.StyleAt(i + 1);
        bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
        if (style == SCE_OBJECK_OPERATOR) {
            if (ch == '{') {
                // Measure the minimum before a '{' to allow
                // folding on "} else {"
                if (levelMinCurrent > levelNext) {
                    levelMinCurrent = levelNext;
                }
                levelNext++;
            } else if (ch == '}') {
                levelNext--;
            }
        }
        if (!IsASpace(ch))
            visibleChars++;
        if (atEOL || (i == endPos-1)) {
            int levelUse = levelCurrent;
            int lev = levelUse | levelNext << 16;
            if (levelUse < levelNext)
                lev |= SC_FOLDLEVELHEADERFLAG;
            if (lev != styler.LevelAt(currentLine)) {
                styler.SetLevel(currentLine, lev);
            }
            currentLine++;
            levelCurrent = levelNext;
            levelMinCurrent = levelCurrent;
            if (atEOL && (i == static_cast<unsigned int>(styler.Length()-1))) {
                // There is an empty line at end of file so give it same level and empty
                styler.SetLevel(currentLine, (levelCurrent | levelCurrent << 16) | SC_FOLDLEVELWHITEFLAG);
            }
            visibleChars = 0;
        }
    }
}

LexerModule lmObjeck(SCLEX_OBJECK, LexerObjeck::LexerFactoryObjeck, "objeck", objeckWordLists);
