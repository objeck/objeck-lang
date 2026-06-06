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
3. Compile your source with debug symbols: `obc -src myprog.obs -debug`.
4. Add a launch configuration to your `.sublime-project` — see `dap/objeck.sublime-project.example`.
5. Open the project, then **Debugger > Start**.

The adapter is registered on Sublime startup and runs `obd --dap` over stdio with `OBJECK_LIB_PATH` set per `Objeck.sublime-settings`.