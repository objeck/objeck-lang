" Objeck integration for Vim 9.2+
" -------------------------------
" Drop this into your `_vimrc` (Windows) or `.vimrc` (POSIX), or source
" it: `source <path>/objeck_vimrc.vim`. The Objeck installer can also
" install it directly via `scripts/install.cmd <objeck_dir> vim`.
"
" Requirements:
"   - Vim 9.0+ with `+python3` (gvim Huge build has it)
"   - yegappan/lsp plugin in `~/vimfiles/pack/objeck/start/lsp/` or
"     `~/.vim/pack/objeck/start/lsp/`
"   - puremourning/vimspector in the same pack/start dir
"   - Objeck LSP install at `~/.objeck-lsp/` (created by the install
"     script)

set nocompatible
syntax on
filetype plugin indent on

" --- LSP (yegappan/lsp) -----------------------------------------------------
let s:objeck_lsp_home = expand('$USERPROFILE') . '\.objeck-lsp'
if !isdirectory(s:objeck_lsp_home)
    " POSIX fallback path
    let s:objeck_lsp_home = expand('$HOME') . '/.objeck-lsp'
endif

if isdirectory(s:objeck_lsp_home)
    if has('win32') || has('win64')
        let s:obr  = s:objeck_lsp_home . '\bin\obr.exe'
        let s:obe  = s:objeck_lsp_home . '\objeck_lsp.obe'
        let s:apis = s:objeck_lsp_home . '\objk_apis.json'
        let s:lib  = s:objeck_lsp_home . '\lib'
    else
        let s:obr  = s:objeck_lsp_home . '/bin/obr'
        let s:obe  = s:objeck_lsp_home . '/objeck_lsp.obe'
        let s:apis = s:objeck_lsp_home . '/objk_apis.json'
        let s:lib  = s:objeck_lsp_home . '/lib'
    endif

    let s:objeck_lsp_cfg = {
        \ 'name': 'objeck',
        \ 'filetype': ['objeck'],
        \ 'path': s:obr,
        \ 'args': [s:obe, s:apis, 'stdio'],
        \ 'syncInit': v:false
        \ }

    autocmd VimEnter * call g:LspAddServer([s:objeck_lsp_cfg])

    " yegappan/lsp doesn't expose env vars per server through LspAddServer,
    " so set OBJECK_LIB_PATH and OBJECK_STDIO at the Vim process level.
    let $OBJECK_LIB_PATH = s:lib
    let $OBJECK_STDIO = 'binary'
endif

" Convenient LSP keybindings (only active in Objeck buffers)
augroup objeck_lsp_keys
    autocmd!
    autocmd FileType objeck nnoremap <buffer> gd :LspGotoDefinition<CR>
    autocmd FileType objeck nnoremap <buffer> gr :LspShowReferences<CR>
    autocmd FileType objeck nnoremap <buffer> K  :LspHover<CR>
    autocmd FileType objeck nnoremap <buffer> <leader>rn :LspRename<CR>
augroup END

" --- DAP (vimspector) -------------------------------------------------------
" Vimspector reads `.vimspector.json` from the project root for per-project
" launch configs and `~/.vimspector.json` for adapter definitions. The
" install script writes that adapter file pointing at obd.
let g:vimspector_install_gadgets = []
let g:vimspector_enable_mappings = 'HUMAN'

" --- Objeck build (compile current file) ------------------------------------
augroup objeck_build
    autocmd!
    autocmd FileType objeck setlocal makeprg=obc\ -src\ %\ -dest\ %:r.obe
    autocmd FileType objeck setlocal errorformat=%f:(%l\\,%c):\ %m
augroup END
