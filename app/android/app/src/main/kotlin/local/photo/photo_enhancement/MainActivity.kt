package local.photo.photo_enhancement

import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.util.UUID

class MainActivity : FlutterActivity() {
    private var pending: MethodChannel.Result? = null
    private var exportPath: String? = null
    private val requestCode = 701

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, "local.photo/files")
            .setMethodCallHandler { call, result ->
                if (call.method == "supportDirectory") {
                    result.success(filesDir.absolutePath)
                } else if (call.method == "open" || call.method == "save") {
                    if (pending != null) {
                        result.error("busy", "已有文件选择窗口打开", null)
                        return@setMethodCallHandler
                    }
                    val saving = call.method == "save"
                    val path = call.argument<String>("path")
                    if (saving && (path == null || !File(path).isFile)) {
                        result.error("file", "导出文件不存在", null)
                        return@setMethodCallHandler
                    }
                    pending = result
                    exportPath = if (saving) path else null
                    val intent = Intent(if (saving) Intent.ACTION_CREATE_DOCUMENT else Intent.ACTION_OPEN_DOCUMENT)
                    intent.addCategory(Intent.CATEGORY_OPENABLE)
                    intent.type = "*/*"
                    if (saving) intent.putExtra(Intent.EXTRA_TITLE, call.argument<String>("name"))
                    else intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, call.argument<Boolean>("multiple") == true)
                    try { startActivityForResult(intent, requestCode) }
                    catch (error: Exception) {
                        pending = null; exportPath = null
                        result.error("picker", error.message, null)
                    }
                } else result.notImplemented()
            }
    }

    @Deprecated("Activity result bridge for Flutter document access")
    override fun onActivityResult(request: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(request, resultCode, data)
        if (request != requestCode) return
        val result = pending ?: return
        val path = exportPath
        if (resultCode != Activity.RESULT_OK || data == null) {
            pending = null; exportPath = null
            result.success(if (path == null) emptyList<String>() else false)
            return
        }
        // Stream providers on a worker; full-resolution files may be large.
        Thread {
            val created = mutableListOf<File>()
            try {
                if (path != null) {
                    val uri = data.data ?: error("未选择导出位置")
                    contentResolver.openOutputStream(uri, "w")?.use { output ->
                        File(path).inputStream().use { it.copyTo(output) }
                    } ?: error("无法打开导出位置")
                    runOnUiThread { pending = null; exportPath = null; result.success(true) }
                } else {
                    val uris = mutableListOf<Uri>()
                    val clips = data.clipData
                    if (clips != null) for (i in 0 until clips.itemCount) uris.add(clips.getItemAt(i).uri)
                    else data.data?.let { uris.add(it) }
                    val imports = File(filesDir, "imports").apply { mkdirs() }
                    val paths = uris.map { uri ->
                        var name = "import"
                        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                            if (it.moveToFirst()) name = it.getString(0) ?: name
                        }
                        val directory = File(imports, UUID.randomUUID().toString()).apply { mkdirs() }
                        val file = File(directory, File(name).name)
                        created.add(file)
                        contentResolver.openInputStream(uri)?.use { input ->
                            file.outputStream().use { output ->
                                val buffer = ByteArray(65536); var size = 0L
                                while (true) {
                                    val count = input.read(buffer); if (count < 0) break
                                    size += count
                                    require(size <= 256L * 1024 * 1024) { "文件超过移动端 256MB 上限" }
                                    output.write(buffer, 0, count)
                                }
                            }
                        } ?: error("无法读取文件")
                        file.absolutePath
                    }
                    runOnUiThread { pending = null; result.success(paths) }
                }
            } catch (error: Exception) {
                created.forEach { it.delete() }
                runOnUiThread { pending = null; exportPath = null; result.error("file", error.message, null) }
            }
        }.start()
    }
}
