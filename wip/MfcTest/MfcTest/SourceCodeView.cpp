// SourceCodeView.cpp : implementation file
//

#include "stdafx.h"
#include "MfcTest.h"
#include "SourceCodeView.h"

template< > UINT AFXAPI HashKey( CString& key )
{
  LPCTSTR pp = (LPCTSTR)(key);
  UINT uiRet = 0;
  while (*pp)
  {
    uiRet = (uiRet<<5) + uiRet + *pp++;
  }

  return uiRet;
}

// SourceCodeView

IMPLEMENT_DYNCREATE(SourceCodeView, CRichEditView)

  SourceCodeView::SourceCodeView()
{
  // red3
  mTokenColors[CString("if")] = RGB(205, 0, 0);
  mTokenColors[CString("else")] = RGB(205, 0, 0);
  mTokenColors[CString("do")] = RGB(205, 0, 0);
  mTokenColors[CString("while")] = RGB(205, 0, 0);
  mTokenColors[CString("for")] = RGB(205, 0, 0);
  mTokenColors[CString("select")] = RGB(205, 0, 0);
  mTokenColors[CString("return")] = RGB(205, 0, 0);
  mTokenColors[CString("other")] = RGB(205, 0, 0);
  mTokenColors[CString("label")] = RGB(205, 0, 0);
  // blue3
  mTokenColors[CString("class")] = RGB(0, 0, 205);
  mTokenColors[CString("enum")] = RGB(0, 0, 205);
  mTokenColors[CString("method")] = RGB(0, 0, 205);
  mTokenColors[CString("function")] = RGB(0, 0, 205);
  mTokenColors[CString("Parent")] = RGB(0, 0, 205);
  mTokenColors[CString("bundle")] = RGB(0, 0, 205);
  mTokenColors[CString("use")] = RGB(0, 0, 205);
  mTokenColors[CString("virtual")] = RGB(0, 0, 205);
  // blue3
  mTokenColors[CString("true")] = RGB(0, 0, 205);
  mTokenColors[CString("false")] = RGB(0, 0, 205);
  mTokenColors[CString("Nil")] = RGB(0, 0, 205);
  mTokenColors[CString("Int")] = RGB(0, 0, 205);
  mTokenColors[CString("Float")] = RGB(0, 0, 205);
  mTokenColors[CString("Byte")] = RGB(0, 0, 205);
  mTokenColors[CString("Char")] = RGB(0, 0, 205);

}

SourceCodeView::~SourceCodeView()
{
}

BEGIN_MESSAGE_MAP(SourceCodeView, CRichEditView)
  ON_WM_KEYUP()
  ON_COMMAND(ID_EDIT_PASTE, &SourceCodeView::OnEditPaste)
END_MESSAGE_MAP()


// SourceCodeView diagnostics

#ifdef _DEBUG
void SourceCodeView::AssertValid() const
{
  CRichEditView::AssertValid();
}

#ifndef _WIN32_WCE
void SourceCodeView::Dump(CDumpContext& dc) const
{
  CRichEditView::Dump(dc);
}
#endif
#endif //_DEBUG


// SourceCodeView message handlers

void SourceCodeView::OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
  if((nChar < 48 || nChar > 90) && (nChar != VK_BACK && nChar != VK_DELETE &&
    nChar != VK_UP && nChar != VK_DOWN && nChar != VK_LEFT && nChar != VK_RIGHT &&
    nChar != VK_LCONTROL && nChar != VK_RCONTROL && nChar != VK_LSHIFT && nChar != VK_RSHIFT)) {
    CRichEditCtrl& editCtrl = GetRichEditCtrl();
    const int lineMax = 512;
    int lineOffset = editCtrl.LineIndex();
    int lineNbr = editCtrl.LineFromChar(lineOffset);
    TCHAR buffer[lineMax];

    if(lineNbr > -1) {
      if(nChar == VK_RETURN) {
        lineNbr--;
        lineOffset = editCtrl.LineIndex(lineNbr);
        lineNbr = editCtrl.LineFromChar(lineOffset);
      }
      const int numRead = editCtrl.GetLine(lineNbr, buffer, lineMax);
      buffer[numRead] = '\0';
      ParseLine(buffer, lineOffset, numRead);
    }
  }
  // TODO: Add your message handler code here and/or call default
  CRichEditView::OnKeyUp(nChar, nRepCnt, nFlags);
}

void SourceCodeView::ParseLine(TCHAR* buffer, const int lineOffset, const int numRead)
{
  CRichEditCtrl& editCtrl = GetRichEditCtrl();

  int start = 0;
  for(int end = 0; end < numRead; end++) {
    const TCHAR breakChar = buffer[end];
    switch(breakChar) {
    case ':':
    case '=':
    case '-':
    case '>':
    case '<':
    case '{':
    case '.':
    case '}':
    case '[':
    case ']':
    case '(':
    case ')':
    case ',':
    case ';':
    case '&':
    case '|':
    case '+':
    case '*':
    case '/':
    case '%':
    case ' ':
    case '\n':
    case '\r':{
      CString word(buffer + start, end - start);
      word = word.TrimLeft().TrimRight();
      TRACE("|");
      TRACE(word);
      TRACE("|\n");

      int color;
      int found = mTokenColors.Lookup(word, color);
      if(found) {
        CHARFORMAT cf;
        editCtrl.HideSelection(true, false);
        long oldStart, oldEnd;
        editCtrl.GetSel(oldStart, oldEnd);
        editCtrl.SetSel(start + lineOffset, end + lineOffset);

        editCtrl.GetDefaultCharFormat(cf);
        cf.cbSize = sizeof(CHARFORMAT);
        cf.dwMask = CFM_COLOR;
        cf.dwEffects = CFM_COLOR;
        cf.dwEffects &= ~CFE_AUTOCOLOR;
        cf.crTextColor = color;

        editCtrl.SetSelectionCharFormat(cf);

        editCtrl.SetSel(oldStart, oldEnd);
        editCtrl.HideSelection(false, false);
      }
      start = end + 1;
    }
      break;

    default: {
      CHARFORMAT cf;
      editCtrl.HideSelection(true, false);
      long oldStart, oldEnd;
      editCtrl.GetSel(oldStart, oldEnd);
      editCtrl.SetSel(start + lineOffset, oldEnd);

      editCtrl.GetDefaultCharFormat(cf);
      cf.cbSize = sizeof(CHARFORMAT);
      cf.dwMask = CFM_COLOR;
      cf.dwEffects = CFM_COLOR;
      cf.dwEffects &= ~CFE_AUTOCOLOR;
      cf.crTextColor = RGB(0, 0, 0);

      editCtrl.SetSelectionCharFormat(cf);

      editCtrl.SetSel(oldStart, oldEnd);
      editCtrl.HideSelection(false, false);
    }
      break;
    }
  }
}

void SourceCodeView::OnEditPaste()
{
  CRichEditCtrl& editCtrl = GetRichEditCtrl();

  long startStart, startEnd;
  editCtrl.GetSel(startStart, startEnd);

  editCtrl.PasteSpecial(CF_TEXT);

  long endStart, endEnd;
  editCtrl.GetSel(endStart, endEnd);


  const int lineMax = 512;
  int lineStart = -1;
  for(int i = startStart; i < endEnd; i++) {
    const int lineOffset = editCtrl.LineIndex(i);
    const int lineNbr = editCtrl.LineFromChar(lineOffset);
    TCHAR buffer[lineMax];

    if(lineNbr > -1) {
      const int numRead = editCtrl.GetLine(lineNbr, buffer, lineMax);
      buffer[numRead] = '\0';
      ParseLine(buffer, lineOffset, numRead);
      lineStart = lineNbr;
    }
  }
}
