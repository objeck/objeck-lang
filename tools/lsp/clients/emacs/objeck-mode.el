;;; objeck-mode.el --- Major mode for Objeck with Eglot LSP support -*- lexical-binding: t; -*-

;; Author: Objeck Contributors
;; URL: https://github.com/objeck/objeck-lsp
;; Version: 2026.2.0
;; Package-Requires: ((emacs "29.1"))

;;; Commentary:
;;
;; Major mode for editing Objeck source files (.obs) with LSP support
;; via Eglot (built into Emacs 29+).
;;
;; Installation:
;;   1. Copy this file to your Emacs load-path (e.g. ~/.emacs.d/lisp/)
;;   2. Add to your init.el:
;;        (add-to-list 'load-path "~/.emacs.d/lisp")
;;        (require 'objeck-mode)
;;
;; To start the LSP server, open a .obs file and run: M-x eglot

;;; Code:

(require 'eglot)

;; Set environment variables for the LSP server
(setenv "OBJECK_LIB_PATH" "<objeck_server_path>/lib")
(setenv "OBJECK_STDIO" "binary")

;; Syntax highlighting
(defvar objeck-mode-font-lock-keywords
  (let ((keywords '("use" "leaving" "if" "else" "do" "while" "select" "break"
                    "continue" "in" "other" "for" "each" "reverse" "label"
                    "return" "critical" "from" "implements" "virtual"))
        (builtins '("New" "Parent" "Try" "Otherwise"))
        (types '("Nil" "Byte" "ByteRef" "Int" "IntRef" "Float" "FloatRef"
                 "Char" "CharRef" "Bool" "BoolRef" "String"
                 "BaseArrayRef" "BoolArrayRef" "ByteArrayRef" "CharArrayRef"
                 "FloatArrayRef" "IntArrayRef" "StringArrayRef"
                 "Func2Ref" "Func3Ref" "Func4Ref" "FuncRef"))
        (constants '("true" "false"))
        (modifiers '("public" "private" "static" "native"))
        (operators '("As" "TypeOf" "and" "or" "xor" "not"))
        (declarations '("class" "method" "function" "interface" "enum"
                        "alias" "consts" "bundle")))
    `(
      ;; declarations
      (,(concat "\\<\\(" (regexp-opt declarations) "\\)\\>") . font-lock-keyword-face)
      ;; keywords
      (,(concat "\\<\\(" (regexp-opt keywords) "\\)\\>") . font-lock-keyword-face)
      ;; builtins
      (,(concat "\\<\\(" (regexp-opt builtins) "\\)\\>") . font-lock-builtin-face)
      ;; types
      (,(concat "\\<\\(" (regexp-opt types) "\\)\\>") . font-lock-type-face)
      ;; modifiers
      (,(concat "\\<\\(" (regexp-opt modifiers) "\\)\\>") . font-lock-constant-face)
      ;; operators
      (,(concat "\\<\\(" (regexp-opt operators) "\\)\\>") . font-lock-keyword-face)
      ;; constants
      (,(concat "\\<\\(" (regexp-opt constants) "\\)\\>") . font-lock-constant-face)
      ;; assignment operator
      (":=" . font-lock-keyword-face)
      ;; numbers (hex, binary, decimal, float)
      ("\\<0[xX][0-9a-fA-F]+\\>" . font-lock-constant-face)
      ("\\<0[bB][01]+\\>" . font-lock-constant-face)
      ("\\<[0-9]+\\(\\.[0-9]*\\)?\\([eE][+-]?[0-9]+\\)?\\>" . font-lock-constant-face)
      ;; instance variables (@name)
      ("@[a-zA-Z_][a-zA-Z0-9_]*" . font-lock-variable-name-face)
      ))
  "Font-lock keywords for Objeck mode.")

;; Syntax table for comments and strings
(defvar objeck-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; # starts a line comment
    (modify-syntax-entry ?# "<" st)
    (modify-syntax-entry ?\n ">" st)
    ;; strings
    (modify-syntax-entry ?\" "\"" st)
    ;; character literals
    (modify-syntax-entry ?\' "\"" st)
    st)
  "Syntax table for Objeck mode.")

;;;###autoload
(define-derived-mode objeck-mode prog-mode "Objeck"
  "Major mode for editing Objeck source files."
  :syntax-table objeck-mode-syntax-table
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "#+ *")
  (setq-local block-comment-start "#~")
  (setq-local block-comment-end "~#")
  (setq-local font-lock-defaults '(objeck-mode-font-lock-keywords)))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.obs\\'" . objeck-mode))

;; Eglot LSP integration
(add-to-list 'eglot-server-programs
             '(objeck-mode . ("<objeck_server_path>/bin/obr"
                              "<objeck_server_path>/objeck_lsp.obe"
                              "<objeck_server_path>/objk_apis.json"
                              "stdio")))

(provide 'objeck-mode)
;;; objeck-mode.el ends here
