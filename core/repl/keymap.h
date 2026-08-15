/***************************************************************************
 * Key bindings for the full-screen editor.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_KEYMAP_H__
#define __TUI_KEYMAP_H__

#include "term.h"

// Bindings are data, not control flow: tables map a decoded Key to a named
// action, and the editor executes actions. The vi profile is another table
// plus a mode field over the same actions -- not a second editor.

namespace Tui {

  enum Action {
    ACT_NONE = 0,
    ACT_INSERT_CHAR,   // not in the tables; any unbound printable character
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
    ACT_QUIT,
    ACT_RUN,          // compile and execute; output lands in the pane
    ACT_TOGGLE_PANE,
    ACT_NEXT_ERROR,
    ACT_PANE_UP,
    ACT_PANE_DOWN,
    ACT_UNDO,
    ACT_REDO,
    // selection: the movement it wraps, with the anchor held
    ACT_SEL_LEFT,
    ACT_SEL_RIGHT,
    ACT_SEL_UP,
    ACT_SEL_DOWN,
    ACT_SEL_HOME,
    ACT_SEL_END,
    ACT_COPY,
    ACT_CUT,
    ACT_PASTE,
    // vi profile
    ACT_MODE_INSERT,      // 'i'
    ACT_MODE_APPEND,      // 'a': insert after the cursor
    ACT_OPEN_BELOW,       // 'o': new line below, insert mode
    ACT_MODE_NORMAL,      // Esc
    ACT_MODE_VISUAL,      // 'v'
    ACT_TOGGLE_PROFILE,   // F2 flips simple <-> vi
    ACT_WORD_NEXT,        // 'w': start of the next word
    ACT_WORD_PREV,        // 'b': start of the previous word
    ACT_APPEND_EOL,       // 'A': insert at end of line
    ACT_INSERT_BOL,       // 'I': insert at first non-blank
    ACT_OPEN_ABOVE,       // 'O': new line above, insert mode
    ACT_DELETE_EOL        // 'D': delete to end of line
  };

  enum Profile {
    PROFILE_SIMPLE = 0,   // the notepad model; deliberately the default
    PROFILE_VI
  };

  enum Mode {
    MODE_INSERT = 0,   // the only mode the simple profile ever uses
    MODE_NORMAL,
    MODE_VISUAL
  };

  // resolver state the view owns: which profile, which mode, and a pending
  // first key of a two-key vi sequence ('dd', 'yy', 'gg')
  struct KeymapState {
    Profile profile;
    Mode mode;
    wchar_t pending;

    KeymapState() : profile(PROFILE_SIMPLE), mode(MODE_INSERT), pending(0) {
    }
  };

  struct Binding {
    KeyCode code;
    wchar_t ch;      // significant only for KEY_CHAR entries
    bool ctrl;
    bool alt;
    bool shift;
    Action action;
  };

  inline Action Lookup(const Binding* bindings, size_t count, const Key& key) {
    for(size_t i = 0; i < count; ++i) {
      const Binding& binding = bindings[i];
      if(binding.code != key.code || binding.ctrl != key.ctrl || binding.alt != key.alt) {
        continue;
      }
      if(binding.code == KEY_CHAR) {
        // Shift is not part of a character's identity -- the character itself
        // already carries its case, and Windows reports shift on a capital
        // while POSIX does not. Comparing it would force every capital to be
        // bound twice (and a half-added binding would then work on one OS
        // only), so char bindings match on `ch` alone.
        if(binding.ch != key.ch) {
          continue;
        }
      }
      else if(binding.shift != key.shift) {
        continue;
      }
      return binding.action;
    }
    return ACT_NONE;
  }

  // shared by the simple profile and vi insert mode
  inline Action ResolveInsert(const Key& key) {
    static const Binding bindings[] = {
      { KEY_LEFT, 0, false, false, false, ACT_LEFT },
      { KEY_RIGHT, 0, false, false, false, ACT_RIGHT },
      { KEY_UP, 0, false, false, false, ACT_UP },
      { KEY_DOWN, 0, false, false, false, ACT_DOWN },
      { KEY_LEFT, 0, false, false, true, ACT_SEL_LEFT },
      { KEY_RIGHT, 0, false, false, true, ACT_SEL_RIGHT },
      { KEY_UP, 0, false, false, true, ACT_SEL_UP },
      { KEY_DOWN, 0, false, false, true, ACT_SEL_DOWN },
      { KEY_HOME, 0, false, false, true, ACT_SEL_HOME },
      { KEY_END, 0, false, false, true, ACT_SEL_END },
      { KEY_UP, 0, false, true, false, ACT_PANE_UP },
      { KEY_DOWN, 0, false, true, false, ACT_PANE_DOWN },
      { KEY_HOME, 0, false, false, false, ACT_LINE_START },
      { KEY_END, 0, false, false, false, ACT_LINE_END },
      { KEY_HOME, 0, true, false, false, ACT_DOC_START },
      { KEY_END, 0, true, false, false, ACT_DOC_END },
      { KEY_PGUP, 0, false, false, false, ACT_PAGE_UP },
      { KEY_PGDN, 0, false, false, false, ACT_PAGE_DOWN },
      { KEY_ENTER, 0, false, false, false, ACT_NEWLINE },
      { KEY_BACKSPACE, 0, false, false, false, ACT_BACKSPACE },
      { KEY_DELETE, 0, false, false, false, ACT_DELETE },
      { KEY_TAB, 0, false, false, false, ACT_TAB },
      { KEY_F2, 0, false, false, false, ACT_TOGGLE_PROFILE },
      { KEY_F5, 0, false, false, false, ACT_RUN },
      { KEY_F6, 0, false, false, false, ACT_TOGGLE_PANE },
      { KEY_F8, 0, false, false, false, ACT_NEXT_ERROR },
      { KEY_CHAR, L's', true, false, false, ACT_SAVE },
      { KEY_CHAR, L'q', true, false, false, ACT_QUIT },
      { KEY_CHAR, L'k', true, false, false, ACT_DELETE_LINE },
      { KEY_CHAR, L'z', true, false, false, ACT_UNDO },
      { KEY_CHAR, L'y', true, false, false, ACT_REDO },
      { KEY_CHAR, L'c', true, false, false, ACT_COPY },
      { KEY_CHAR, L'x', true, false, false, ACT_CUT },
      { KEY_CHAR, L'v', true, false, false, ACT_PASTE },
    };
    const Action action = Lookup(bindings, sizeof(bindings) / sizeof(bindings[0]), key);
    if(action != ACT_NONE) {
      return action;
    }
    if(key.code == KEY_CHAR && !key.ctrl && !key.alt && key.ch >= 32) {
      return ACT_INSERT_CHAR;
    }
    return ACT_NONE;
  }

  // vi normal and visual modes; unbound characters do nothing here
  inline Action ResolveNormal(const Key& key) {
    static const Binding bindings[] = {
      { KEY_CHAR, L'h', false, false, false, ACT_LEFT },
      { KEY_CHAR, L'j', false, false, false, ACT_DOWN },
      { KEY_CHAR, L'k', false, false, false, ACT_UP },
      { KEY_CHAR, L'l', false, false, false, ACT_RIGHT },
      { KEY_LEFT, 0, false, false, false, ACT_LEFT },
      { KEY_RIGHT, 0, false, false, false, ACT_RIGHT },
      { KEY_UP, 0, false, false, false, ACT_UP },
      { KEY_DOWN, 0, false, false, false, ACT_DOWN },
      { KEY_CHAR, L'0', false, false, false, ACT_LINE_START },
      { KEY_CHAR, L'$', false, false, false, ACT_LINE_END },
      { KEY_CHAR, L'G', false, false, false, ACT_DOC_END },
      { KEY_CHAR, L'x', false, false, false, ACT_DELETE },
      { KEY_CHAR, L'u', false, false, false, ACT_UNDO },
      { KEY_CHAR, L'r', true, false, false, ACT_REDO },
      { KEY_CHAR, L'p', false, false, false, ACT_PASTE },
      { KEY_CHAR, L'i', false, false, false, ACT_MODE_INSERT },
      { KEY_CHAR, L'a', false, false, false, ACT_MODE_APPEND },
      { KEY_CHAR, L'o', false, false, false, ACT_OPEN_BELOW },
      { KEY_CHAR, L'w', false, false, false, ACT_WORD_NEXT },
      { KEY_CHAR, L'b', false, false, false, ACT_WORD_PREV },
      { KEY_CHAR, L'A', false, false, false, ACT_APPEND_EOL },
      { KEY_CHAR, L'I', false, false, false, ACT_INSERT_BOL },
      { KEY_CHAR, L'O', false, false, false, ACT_OPEN_ABOVE },
      { KEY_CHAR, L'D', false, false, false, ACT_DELETE_EOL },
      { KEY_CHAR, L'v', false, false, false, ACT_MODE_VISUAL },
      { KEY_ESC, 0, false, false, false, ACT_MODE_NORMAL },
      { KEY_PGUP, 0, false, false, false, ACT_PAGE_UP },
      { KEY_PGDN, 0, false, false, false, ACT_PAGE_DOWN },
      { KEY_F2, 0, false, false, false, ACT_TOGGLE_PROFILE },
      { KEY_F5, 0, false, false, false, ACT_RUN },
      { KEY_F6, 0, false, false, false, ACT_TOGGLE_PANE },
      { KEY_F8, 0, false, false, false, ACT_NEXT_ERROR },
      { KEY_CHAR, L's', true, false, false, ACT_SAVE },
      { KEY_CHAR, L'q', true, false, false, ACT_QUIT },
    };
    return Lookup(bindings, sizeof(bindings) / sizeof(bindings[0]), key);
  }

  inline Action Resolve(const Key& key, KeymapState& state) {
    if(state.profile == PROFILE_SIMPLE) {
      return ResolveInsert(key);
    }

    if(state.mode == MODE_INSERT) {
      if(key.code == KEY_ESC) {
        state.pending = 0;
        return ACT_MODE_NORMAL;
      }
      return ResolveInsert(key);
    }

    // two-key sequences: the first key parks in `pending`
    if(state.pending) {
      const wchar_t first = state.pending;
      state.pending = 0;
      if(key.code == KEY_CHAR && key.ch == first) {
        switch(first) {
        case L'd': return ACT_CUT;        // dd: no selection means cut the line
        case L'y': return ACT_COPY;       // yy: no selection means copy the line
        case L'g': return ACT_DOC_START;  // gg
        }
      }
      return ACT_NONE;   // an unrecognized sequence is dropped whole
    }
    if(key.code == KEY_CHAR && !key.ctrl && !key.alt &&
       (key.ch == L'd' || key.ch == L'y' || key.ch == L'g')) {
      // in visual mode d/y act on the selection immediately
      if(state.mode == MODE_VISUAL) {
        return key.ch == L'd' ? ACT_CUT : (key.ch == L'y' ? ACT_COPY : ACT_NONE);
      }
      state.pending = key.ch;
      return ACT_NONE;
    }

    return ResolveNormal(key);
  }
}

#endif
