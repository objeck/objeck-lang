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
  mTokenColors[CString("Int")] = RGB(0, 255, 0);
  mTokenColors[CString("Float")] = RGB(255, 0 , 0);
  mTokenColors[CString("function")] = RGB(0, 0, 255);
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
  CRichEditCtrl& editCtrl = GetRichEditCtrl();

  const int lineMax = 512;
  const int lineOffset = editCtrl.LineIndex();
  const int lineNbr = editCtrl.LineFromChar(lineOffset);
  TCHAR buffer[lineMax];

  if(lineNbr > -1) {
    const int numRead = editCtrl.GetLine(lineNbr, buffer, lineMax);
    buffer[numRead] = '\0';
    ParseLine(buffer, lineOffset, numRead);
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
    case ' ': {
      CString word(buffer + start, end - start);
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
