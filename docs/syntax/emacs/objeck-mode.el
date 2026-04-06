;;; objeck-mode.el --- Major mode for editing Objeck source files -*- lexical-binding: t; -*-

;; Author: Randy Hollines
;; Version: 0.1.0
;; Keywords: languages objeck
;; URL: https://github.com/objeck/objeck-lang

;;; Commentary:

;; Provides syntax highlighting, indentation, and comment support
;; for the Objeck programming language (.obs files).

;;; Code:

(defvar objeck-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; # starts a line comment
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    ;; Strings
    (modify-syntax-entry ?\" "\"" table)
    ;; Character literals
    (modify-syntax-entry ?\' "\"" table)
    ;; Parentheses
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    table)
  "Syntax table for `objeck-mode'.")

(defvar objeck-font-lock-keywords
  (let* ((control-keywords '("if" "else" "do" "while" "for" "each" "reverse"
                             "select" "other" "break" "continue" "return"
                             "label" "in" "critical"))
         (decl-keywords '("class" "interface" "enum" "consts" "bundle" "alias"
                          "method" "function" "native" "virtual" "static"
                          "public" "private"))
         (type-keywords '("Nil" "Int" "IntRef" "Float" "FloatRef" "Char" "CharRef"
                          "Byte" "ByteRef" "Bool" "BoolRef" "String"
                          "BaseArrayRef" "BoolArrayRef" "ByteArrayRef"
                          "CharArrayRef" "FloatArrayRef" "IntArrayRef"
                          "StringArrayRef" "Func2Ref" "Func3Ref" "Func4Ref" "FuncRef"))
         (other-keywords '("use" "leaving" "Parent" "As" "TypeOf" "from"
                           "implements" "Try" "Otherwise" "New"))
         (constants '("true" "false" "Nil"))
         (operators '("and" "or" "not" "xor")))
    `(
      ;; Block comments: #~ ... ~#
      ("#~\\(.\\|\n\\)*?~#" . font-lock-comment-face)
      ;; Control keywords
      (,(regexp-opt control-keywords 'words) . font-lock-keyword-face)
      ;; Declaration keywords
      (,(regexp-opt decl-keywords 'words) . font-lock-keyword-face)
      ;; Type keywords
      (,(regexp-opt type-keywords 'words) . font-lock-type-face)
      ;; Other keywords
      (,(regexp-opt other-keywords 'words) . font-lock-builtin-face)
      ;; Constants
      (,(regexp-opt constants 'words) . font-lock-constant-face)
      ;; Operators
      (,(regexp-opt operators 'words) . font-lock-keyword-face)
      ;; Numbers (hex, binary, decimal, float)
      ("\\<0[xX][0-9a-fA-F]+\\>" . font-lock-constant-face)
      ("\\<0[bB][01]+\\>" . font-lock-constant-face)
      ("\\<[0-9]+\\(\\.[0-9]*\\)?\\([eE][+-]?[0-9]+\\)?\\>" . font-lock-constant-face)
      ;; Assignment operators
      (":=\\|+=\\|-=\\|\\*=\\|/=" . font-lock-keyword-face)
      ;; Arrow operator
      ("->" . font-lock-keyword-face)
      ;; Class/method definitions
      ("\\(class\\|interface\\|enum\\|consts\\)\\s-+\\([A-Za-z_][A-Za-z0-9_]*\\)"
       (2 font-lock-type-face))
      ("\\(method\\|function\\)\\s-+.*:\\s-+\\([A-Za-z_][A-Za-z0-9_]*\\)"
       (2 font-lock-function-name-face))))
  "Font lock keywords for `objeck-mode'.")

(defun objeck-indent-line ()
  "Indent current line as Objeck code."
  (interactive)
  (let ((indent-level 2))
    (indent-line-to
     (save-excursion
       (beginning-of-line)
       (if (bobp)
           0
         (let ((cur-indent 0))
           (forward-line -1)
           (setq cur-indent (current-indentation))
           (cond
            ((looking-at ".*{\\s-*$")
             (+ cur-indent indent-level))
            ((save-excursion
               (forward-line 1)
               (looking-at "^\\s-*}"))
             (- cur-indent indent-level))
            (t cur-indent))))))))

;;;###autoload
(define-derived-mode objeck-mode prog-mode "Objeck"
  "Major mode for editing Objeck programming language source files."
  :syntax-table objeck-mode-syntax-table
  (setq-local font-lock-defaults '(objeck-font-lock-keywords))
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local indent-line-function #'objeck-indent-line)
  (setq-local tab-width 2)
  (setq-local indent-tabs-mode nil))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.obs\\'" . objeck-mode))

(provide 'objeck-mode)
;;; objeck-mode.el ends here
