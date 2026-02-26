import AppKit

class AppDelegate: NSObject, NSApplicationDelegate {
  var window: NSWindow!
  var installPath: String = ""

  func applicationDidFinishLaunching(_ notification: Notification) {
    let bundlePath = Bundle.main.bundlePath
    installPath = URL(fileURLWithPath: bundlePath)
      .deletingLastPathComponent()  // MacOS
      .deletingLastPathComponent()  // Contents
      .deletingLastPathComponent()  // Objeck.app
      .deletingLastPathComponent()  // app
      .path

    setupWindow()

    // Deferred version check
    DispatchQueue.global().asyncAfter(deadline: .now() + 2) {
      self.checkVersion()
    }
  }

  func setupWindow() {
    let windowWidth: CGFloat = 340
    let windowHeight: CGFloat = 380

    window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: windowWidth, height: windowHeight),
      styleMask: [.titled, .closable, .miniaturizable],
      backing: .buffered,
      defer: false
    )
    window.title = "Objeck"
    window.center()
    window.isMovableByWindowBackground = true
    window.titlebarAppearsTransparent = true
    window.titleVisibility = .hidden

    let contentView = NSView(frame: NSRect(x: 0, y: 0, width: windowWidth, height: windowHeight))
    contentView.translatesAutoresizingMaskIntoConstraints = false
    window.contentView = contentView

    // Logo
    let logoView = NSImageView()
    logoView.translatesAutoresizingMaskIntoConstraints = false
    logoView.imageScaling = .scaleProportionallyUpOrDown

    let bannerPath = "\(installPath)/doc/api/style/objeck-logo-alt.png"
    if let image = NSImage(contentsOfFile: bannerPath) {
      logoView.image = image
    }
    contentView.addSubview(logoView)

    // Separator
    let separator = NSBox()
    separator.translatesAutoresizingMaskIntoConstraints = false
    separator.boxType = .separator
    contentView.addSubview(separator)

    // Button stack
    let stack = NSStackView()
    stack.translatesAutoresizingMaskIntoConstraints = false
    stack.orientation = .vertical
    stack.spacing = 8
    stack.alignment = .centerX
    contentView.addSubview(stack)

    let actions: [(String, String, Selector)] = [
      ("Terminal", "terminal", #selector(openTerminal)),
      ("API Documentation", "book", #selector(openAPIDocs)),
      ("Code Examples", "folder", #selector(openExamples)),
      ("Text Editor Support", "pencil", #selector(openEditorSupport)),
      ("Read Me", "info.circle", #selector(openReadMe)),
    ]

    for (title, symbolName, action) in actions {
      let button = makeButton(title: title, symbol: symbolName, action: action)
      stack.addArrangedSubview(button)
      button.widthAnchor.constraint(equalToConstant: windowWidth - 48).isActive = true
      button.heightAnchor.constraint(equalToConstant: 40).isActive = true
    }

    // Layout
    NSLayoutConstraint.activate([
      logoView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 24),
      logoView.centerXAnchor.constraint(equalTo: contentView.centerXAnchor),
      logoView.widthAnchor.constraint(equalToConstant: 120),
      logoView.heightAnchor.constraint(equalToConstant: 46),

      separator.topAnchor.constraint(equalTo: logoView.bottomAnchor, constant: 16),
      separator.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 24),
      separator.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -24),

      stack.topAnchor.constraint(equalTo: separator.bottomAnchor, constant: 16),
      stack.centerXAnchor.constraint(equalTo: contentView.centerXAnchor),
    ])

    window.makeKeyAndOrderFront(nil)
    NSApp.activate(ignoringOtherApps: true)
  }

  func makeButton(title: String, symbol: String, action: Selector) -> NSButton {
    let button = NSButton()
    button.translatesAutoresizingMaskIntoConstraints = false
    button.title = "  " + title
    button.bezelStyle = .rounded
    button.controlSize = .large
    button.font = NSFont.systemFont(ofSize: 13, weight: .medium)
    button.target = self
    button.action = action
    button.imagePosition = .imageLeft
    button.contentTintColor = .controlAccentColor

    if #available(macOS 11.0, *) {
      let config = NSImage.SymbolConfiguration(pointSize: 14, weight: .medium)
      if let icon = NSImage(systemSymbolName: symbol, accessibilityDescription: title)?
        .withSymbolConfiguration(config) {
        button.image = icon
      }
    }

    return button
  }

  @objc func openTerminal() {
    let binPath = "\(installPath)/bin"
    let libPath = "\(installPath)/lib"
    let sdlPath = "\(installPath)/lib/sdl"
    let script = """
      tell application "Terminal"
        activate
        do script "export PATH=\\\"\(binPath):\(sdlPath):$PATH\\\"; \
      export OBJECK_LIB_PATH=\\\"\(libPath)\\\"; \
      echo \\\"Objeck Command Prompt ready\\\"; cd ~"
      end tell
      """
    var error: NSDictionary?
    NSAppleScript(source: script)?.executeAndReturnError(&error)
  }

  @objc func openAPIDocs() {
    NSWorkspace.shared.open(URL(fileURLWithPath: "\(installPath)/doc/api/index.html"))
  }

  @objc func openExamples() {
    NSWorkspace.shared.open(URL(fileURLWithPath: "\(installPath)/examples"))
  }

  @objc func openEditorSupport() {
    NSWorkspace.shared.open(URL(fileURLWithPath: "\(installPath)/doc/syntax/howto.html"))
  }

  @objc func openReadMe() {
    NSWorkspace.shared.open(URL(fileURLWithPath: "\(installPath)/readme.html"))
  }

  func checkVersion() {
    guard let url = URL(string: "https://www.objeck.org/doc/api/version.txt"),
          let data = try? String(contentsOf: url, encoding: .utf8),
          let remote = Int(data.trimmingCharacters(in: .whitespacesAndNewlines))
    else { return }

    let localPath = "\(installPath)/doc/api/version.txt"
    guard let localData = try? String(contentsOfFile: localPath, encoding: .utf8),
          let local = Int(localData.trimmingCharacters(in: .whitespacesAndNewlines))
    else { return }

    if local < remote {
      DispatchQueue.main.async {
        let alert = NSAlert()
        alert.messageText = "Update Available"
        alert.informativeText = "A newer version of Objeck is available."
        alert.addButton(withTitle: "Visit Website")
        alert.addButton(withTitle: "Later")
        if alert.runModal() == .alertFirstButtonReturn {
          NSWorkspace.shared.open(URL(string: "https://www.objeck.org")!)
        }
      }
    }
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return true
  }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
