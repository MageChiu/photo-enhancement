import Flutter
import UIKit
import UniformTypeIdentifiers

@main
@objc class AppDelegate: FlutterAppDelegate, FlutterImplicitEngineDelegate {
  private var files: PhotoFiles?
  override func application(
    _ application: UIApplication,
    didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?
  ) -> Bool {
    return super.application(application, didFinishLaunchingWithOptions: launchOptions)
  }

  func didInitializeImplicitFlutterEngine(_ engineBridge: FlutterImplicitEngineBridge) {
    GeneratedPluginRegistrant.register(with: engineBridge.pluginRegistry)
    let registrar = engineBridge.pluginRegistry.registrar(forPlugin: "PhotoFiles")!
    files = PhotoFiles(messenger: registrar.messenger())
  }
}

final class PhotoFiles: NSObject, UIDocumentPickerDelegate {
  private var pending: FlutterResult?
  private var saving = false
  private let channel: FlutterMethodChannel

  init(messenger: FlutterBinaryMessenger) {
    channel = FlutterMethodChannel(name: "local.photo/files", binaryMessenger: messenger)
    super.init()
    channel.setMethodCallHandler { [weak self] call, result in self?.handle(call, result) }
  }

  private func handle(_ call: FlutterMethodCall, _ result: @escaping FlutterResult) {
    if call.method == "supportDirectory" {
      result(FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].path)
      return
    }
    guard call.method == "open" || call.method == "save" else {
      result(FlutterMethodNotImplemented); return
    }
    guard pending == nil else {
      result(FlutterError(code: "busy", message: "已有文件选择窗口打开", details: nil)); return
    }
    guard let root = UIApplication.shared.connectedScenes.compactMap({ $0 as? UIWindowScene })
        .flatMap({ $0.windows }).first(where: { $0.isKeyWindow })?.rootViewController else {
      result(FlutterError(code: "window", message: "无法打开文件选择器", details: nil)); return
    }
    var presenter = root
    while let next = presenter.presentedViewController { presenter = next }
    let arguments = call.arguments as? [String: Any] ?? [:]
    saving = call.method == "save"
    let picker: UIDocumentPickerViewController
    if saving {
      guard let path = arguments["path"] as? String, FileManager.default.fileExists(atPath: path) else {
        result(FlutterError(code: "file", message: "导出文件不存在", details: nil)); return
      }
      picker = UIDocumentPickerViewController(forExporting: [URL(fileURLWithPath: path)], asCopy: true)
    } else {
      // RAW and .cube have no universal UTI; allow general documents.
      picker = UIDocumentPickerViewController(forOpeningContentTypes: [.item], asCopy: false)
      picker.allowsMultipleSelection = arguments["multiple"] as? Bool ?? false
    }
    pending = result
    picker.delegate = self
    presenter.present(picker, animated: true)
  }

  func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
    let result = pending; pending = nil
    result?(saving ? false : [String]() as Any)
  }

  func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
    guard let result = pending else { return }
    if saving { pending = nil; result(true); return }
    DispatchQueue.global(qos: .userInitiated).async {
      var created: [URL] = []
      do {
        let root = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
          .appendingPathComponent("imports").appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        created.append(root)
        let paths = try urls.map { url -> String in
          let access = url.startAccessingSecurityScopedResource()
          defer { if access { url.stopAccessingSecurityScopedResource() } }
          let values = try url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
          guard values.isRegularFile == true, let size = values.fileSize, size <= 256 * 1024 * 1024 else {
            throw NSError(domain: "PhotoFiles", code: 1,
              userInfo: [NSLocalizedDescriptionKey: "文件不可读取或超过移动端 256MB 上限"])
          }
          let directory = root.appendingPathComponent(UUID().uuidString)
          try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
          let target = directory.appendingPathComponent(url.lastPathComponent)
          try FileManager.default.copyItem(at: url, to: target)
          return target.path
        }
        DispatchQueue.main.async { self.pending = nil; result(paths) }
      } catch {
        for file in created { try? FileManager.default.removeItem(at: file) }
        DispatchQueue.main.async {
          self.pending = nil
          result(FlutterError(code: "file", message: error.localizedDescription, details: nil))
        }
      }
    }
  }
}
