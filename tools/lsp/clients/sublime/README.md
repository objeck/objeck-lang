# Objeck LSP

Sublime TCP client shim.

## Installation
* Install syntax [highlighting](https://github.com/objeck/objeck-lang/tree/master/docs/syntax/sublime)
* Install the Sublime [LSP support](https://lsp.sublimetext.io/language_servers/)
* Open Preferences > Package Settings > LSP > Settings and add the "objeck" client configuration to the "clients":

Standard I/O
```
{
	"clients": {
		"objeck": {
			"enabled": false,
			"command": [
				"<objeck_path>/bin/obr.exe",
				"<objeck_server_path>/objeck_lsp.obe",
				"<objeck_server_path>/objk_apis.json",
				"stdio"
			],
			"env": {
				"OBJECK_LIB_PATH": "<objeck_path>/lib",
				"OBJECK_STDIO": "binary"
			},
			"selector": "source.objeck-obs"
		}
	}
}
```

TCP sockets
```
{
	"clients": {
		"objeck": {
			"enabled": false,
			"command": [
				"<objeck_path>/bin/obr.exe",
				"<objeck_server_path>/objeck_lsp.obe",
				"<objeck_server_path>/objk_apis.json",
				"6013"
			],
			"env": {
				"OBJECK_LIB_PATH": "<objeck_path>/lib"
			},
			"selector": "source.objeck-obs",
			"tcp_port": 6013
		}
	}
}
```

## Running client

* Open Tools > LSP > Enable Language and select objeck

## Debugging (DAP)

Sublime debugging uses the [Debugger](https://packagecontrol.io/packages/Debugger) package by Dave Leroy. After installing it via Package Control:

1. Copy `dap/objeck_dap_adapter.py` into your `Packages/Objeck/` directory (the `install.sh` / `install.cmd` script does this for you).
2. Create `Packages/User/Objeck.sublime-settings` pointing at your `obd` binary:
   ```json
   {
       "obd_path": "C:\\Program Files\\Objeck\\bin\\obd.exe",
       "objeck_lib_path": "C:\\Program Files\\Objeck\\lib"
   }
   ```
3. Compile your source with debug symbols: `obc -src myprog.obs --debug -dest myprog.obe`.
   Skipping this is the most common mistake — the session starts but no breakpoint ever binds.
4. Add a debug configuration, either per project or globally (below).
5. **Debugger > Start**, pick the configuration, and click the gutter to set breakpoints.

The adapter is registered on Sublime startup and runs `obd --dap` over stdio with `OBJECK_LIB_PATH` set per `Objeck.sublime-settings`.

### Creating a debug configuration

**Per project** — put a `debugger_configurations` block in a `.sublime-project` and open it
with *Project > Open Project*. Configurations are only read from an open project, not from a
plain folder. See [`dap/objeck.sublime-project.example`](dap/objeck.sublime-project.example).

**Globally, without a project** — the Debugger package normally insists on a sublime project
and shows *"Debugger requires a sublime project"*. A non-empty `global_debugger_configurations`
in `Packages/User/Debugger.sublime-settings` lifts that requirement and makes the configuration
available in every window. See [`dap/Debugger.sublime-settings.example`](dap/Debugger.sublime-settings.example):

```json
{
    "global_debugger_configurations": [
        {
            "name": "Objeck: debug current file",
            "type": "objeck",
            "request": "launch",
            "program": "${file_path}/${file_base_name}.obe",
            "sourceDir": "${file_path}"
        }
    ]
}
```

`${file_path}` and `${file_base_name}` are expanded per window, so this one entry debugs whichever
`.obs` file is open — provided the matching `.obe` sits beside it. Use `${folder}` instead only
inside a project, since it has no meaning without one.

### Troubleshooting

- **"Add or select a configuration to begin debugging"** — the Debugger package found no
  configurations. Either no `.sublime-project` is open, or it has no `debugger_configurations`
  block. Add a global configuration as above to sidestep projects entirely.
- **Breakpoints never hit** — the `.obe` was built without `--debug`, or it is stale relative to
  the source. Recompile after every edit; the debugger runs the bytecode, not the `.obs`.
- **`objeck` missing from the adapter list** — the adapter registers at startup, so restart
  Sublime after installing it. `View > Show Console` logs `[objeck-dap] registered adapter types:`
  and warns explicitly if registration failed.