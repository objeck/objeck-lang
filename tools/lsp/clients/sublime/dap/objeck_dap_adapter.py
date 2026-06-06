"""
Objeck DAP adapter for Sublime Text Debugger package.

Registers an 'objeck' adapter type with the Sublime Debugger package
(https://github.com/daveleroy/sublime_debugger). On plugin load, this
imports the Debugger API and defines an ObjeckAdapter subclass; the
Debugger metaclass auto-registers it.

REQUIREMENTS
------------
- A `.python-version` file containing `3.8` must exist next to this
  file (the Debugger package is Python 3.8). Without it, Sublime tries
  to load this plugin under Python 3.3 and the import fails.
- The "Debugger" package must be installed via Package Control.

CONFIGURE
---------
Create `Packages/User/Objeck.sublime-settings`:
    {
        "obd_path": "C:/Program Files/Objeck/bin/obd.exe",
        "objeck_lib_path": "C:/Program Files/Objeck/lib"
    }

USE
---
Add a launch config to your .sublime-project (see
objeck.sublime-project.example), then `Debugger > Start`.
"""
import os
import traceback
import sublime

LOG_PREFIX = "[objeck-dap]"


def plugin_loaded():
    print("{} plugin_loaded: starting registration".format(LOG_PREFIX))

    # Import the Debugger public API. The released Debugger package uses
    # `AdapterConfiguration` as the base class. The unreleased master
    # branch on GitHub renamed it to `Adapter`; try both for forward
    # compatibility.
    AdapterBase = None
    StdioTransport = None
    Process = None
    try:
        from Debugger.modules.dap import AdapterConfiguration as AdapterBase
        from Debugger.modules.dap import StdioTransport, Process
        print("{} imported AdapterConfiguration (released API)".format(LOG_PREFIX))
    except ImportError:
        try:
            from Debugger.modules.dap import Adapter as AdapterBase
            from Debugger.modules.dap import StdioTransport, Process
            print("{} imported Adapter (master API)".format(LOG_PREFIX))
        except ImportError as e:
            print("{} Debugger package not importable: {}".format(LOG_PREFIX, e))
            print("{} install the 'Debugger' package via Package Control and restart Sublime".format(LOG_PREFIX))
            return
    except Exception:
        print("{} unexpected error importing Debugger:".format(LOG_PREFIX))
        traceback.print_exc()
        return

    settings = sublime.load_settings("Objeck.sublime-settings")

    class ObjeckStdioTransport(StdioTransport):
        """StdioTransport that injects an env dict into the spawned obd
        process. Upstream StdioTransport.setup() does not pass env to
        Process; we need OBJECK_LIB_PATH so obd can find lang.obl etc."""
        env_overrides = None

        async def setup(self):
            self.log('transport', '-- objeck stdio transport: {}'.format(self.command))
            cwd = self.cwd
            if cwd is None:
                # released API: variables is a plain dict
                cwd = self.configuration.variables.get('folder')

            full_env = os.environ.copy()
            if self.env_overrides:
                full_env.update(self.env_overrides)

            self.process = Process(self.command, cwd, env=full_env)
            self.process.on_stderr(self._log_stderr)

    try:
        class ObjeckAdapter(AdapterBase):
            type = "objeck"
            docs = "https://www.objeck.org"

            async def start(self, log, configuration):
                obd_path = settings.get("obd_path") or "obd"
                lib_path = settings.get("objeck_lib_path") or os.environ.get("OBJECK_LIB_PATH", "")

                env_overrides = {}
                if lib_path:
                    env_overrides["OBJECK_LIB_PATH"] = lib_path

                transport = ObjeckStdioTransport(command=[obd_path, "--dap"])
                transport.env_overrides = env_overrides
                return transport
    except Exception:
        print("{} failed to define ObjeckAdapter:".format(LOG_PREFIX))
        traceback.print_exc()
        return

    # Verify it actually landed in the registry. The released API uses
    # AdapterConfiguration.registered_types; master uses
    # AdapterRegistery.registered_types. Try both.
    try:
        registered_types = getattr(AdapterBase, 'registered_types', None)
        if registered_types is None:
            try:
                from Debugger.modules.dap.adapter import AdapterConfigurationRegistery
                registered_types = AdapterConfigurationRegistery.registered_types
            except ImportError:
                pass

        if registered_types is not None:
            types = list(registered_types.keys())
            print("{} registered adapter types: {}".format(LOG_PREFIX, types))
            if "objeck" in registered_types:
                print("{} 'objeck' adapter registered OK".format(LOG_PREFIX))
            else:
                print("{} WARNING: 'objeck' NOT in registry after class definition".format(LOG_PREFIX))
        else:
            print("{} could not locate adapter registry to verify".format(LOG_PREFIX))
    except Exception:
        print("{} could not query registry:".format(LOG_PREFIX))
        traceback.print_exc()
