import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  private var windowChannel: FlutterMethodChannel?

  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)

    super.awakeFromNib()
    self.title = "光屿"

    let channel = FlutterMethodChannel(
      name: "local.photo/window",
      binaryMessenger: flutterViewController.engine.binaryMessenger)
    windowChannel = channel
    channel.setMethodCallHandler { [weak self] call, result in
      guard call.method == "setTitle" else {
        result(FlutterMethodNotImplemented)
        return
      }
      guard let arguments = call.arguments as? [String: Any],
            let title = arguments["title"] as? String else {
        result(FlutterError(code: "invalid_title", message: "Missing window title", details: nil))
        return
      }
      self?.title = title
      result(nil)
    }
  }
}
