/***************************************************************************
 * Key bindings for the full-screen editor.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_KEYMAP_H__
#define __TUI_KEYMAP_H__

#include "term.h"

// Bindings are data, not control flow: a table maps a decoded Key to a named
// action, and the editor executes actions. The vi profile in a later phase is
// another table over the same actions, not a second editor.

namespace Tui {

  enum Action {
    ACT_NONE = 0,
    ACT_INSERT_CHAR,   // not in the table; any unbound printable character
    ACT_LEFT,
    ACT_RIGHT,
    ACT_UP,
    ACT_DOWN,
    ACT_LINE_START,
    ACT_LINE_END,
    ACT_PAGE_UP,
    ACT_PAGE_DOWN,
    ACT_DOC_START,
    ACT_DOC_END,
    ACT_NEWLINE,
    ACT_BACKSPACE,
    ACT_DELETE,
    ACT_DELETE_LINE,
    ACT_TAB,
    ACT_SAVE,
    ACT_QUIT
  };

  struct Binding {
    KeyCode code;
    wchar_t ch;      // significant only for KEY_CHAR entries
    bool ctrl;
    Action action;
  };

  // the notepad-style default profile; discoverable, matches the hint bar
  inline const Binding* SimpleProfile(size_t& count) {
    static const Binding bindings[] = {
      { KEY_LEFT, 0, false, ACT_LEFT },
      { KEY_RIGHT, 0, false, ACT_RIGHT },
      { KEY_UP, 0, false, ACT_UP },
      { KEY_DOWN, 0, false, ACT_DOWN },
      { KEY_HOME, 0, false, ACT_LINE_START },
      { KEY_END, 0, false, ACT_LINE_END },
      { KEY_HOME, 0, true, ACT_DOC_START },
      { KEY_END, 0, true, ACT_DOC_END },
      { KEY_PGUP, 0, false, ACT_PAGE_UP },
      { KEY_PGDN, 0, false, ACT_PAGE_DOWN },
      { KEY_ENTER, 0, false, ACT_NEWLINE },
      { KEY_BACKSPACE, 0, false, ACT_BACKSPACE },
      { KEY_DELETE, 0, false, ACT_DELETE },
      { KEY_TAB, 0, false, ACT_TAB },
      { KEY_CHAR, L's', true, ACT_SAVE },
      { KEY_CHAR, L'q', true, ACT_QUIT },
      { KEY_CHAR, L'k', true, ACT_DELETE_LINE },
    };
    count = sizeof(bindings) / sizeof(bindings[0]);
    return bindings;
  }

  inline Action Resolve(const Key& key) {
    size_t count;
    const Binding* bindings = SimpleProfile(count);
    for(size_t i = 0; i < count; ++i) {
      const Binding& binding = bindings[i];
      if(binding.code != key.code || binding.ctrl != key.ctrl) {
        continue;
      }
      if(binding.code == KEY_CHAR && binding.ch != key.ch) {
        continue;
      }
      return binding.action;
    }

    if(key.code == KEY_CHAR && !key.ctrl && !key.alt && key.ch >= 32) {
      return ACT_INSERT_CHAR;
    }
    return ACT_NONE;
  }
}

#endif
