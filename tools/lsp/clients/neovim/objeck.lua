-- Objeck LSP configuration for Neovim 0.11+
--
-- Installation:
--   1. Copy this file to ~/.config/nvim/lsp/objeck.lua
--   2. Add to your init.lua:
--        vim.lsp.enable('objeck')

-- File type detection for .obs files
vim.filetype.add({
  extension = {
    obs = 'objeck',
  },
})

-- LSP server configuration
return {
  cmd = {
    '<objeck_server_path>/bin/obr',
    '<objeck_server_path>/objeck_lsp.obe',
    '<objeck_server_path>/objk_apis.json',
    'stdio',
  },
  cmd_env = {
    OBJECK_LIB_PATH = '<objeck_server_path>/lib',
    OBJECK_STDIO = 'binary',
  },
  filetypes = { 'objeck' },
  root_markers = { 'build.json' },
  settings = {},
}
