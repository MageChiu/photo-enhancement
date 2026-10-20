import Cocoa
import FlutterMacOS

@main
class AppDelegate: FlutterAppDelegate {
  override func applicationDidFinishLaunching(_ notification: Notification) {
    super.applicationDidFinishLaunching(notification)

    // Flutter's debugger also asks Launch Services to foreground the bundle.
    // Show the existing window ourselves so that request is not the only way
    // the development app can become visible.
    DispatchQueue.main.async { [weak self] in
      guard let window = self?.mainFlutterWindow else { return }
      NSApp.setActivationPolicy(.regular)
      window.deminiaturize(nil)
      window.makeKeyAndOrderFront(nil)
      NSApp.activate(ignoringOtherApps: true)
    }
  }

  override func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return true
  }

  override func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
    return true
  }
}
