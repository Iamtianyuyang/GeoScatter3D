package com.geoscatter3d.ui_flutter

import android.app.Activity
import android.content.Intent
import android.graphics.PixelFormat
import android.graphics.SurfaceTexture
import android.media.ImageReader
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.view.Surface
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream

/** Android 文档选择器桥接 + 零拷贝呈现通路。
 *
 * 选择结果会立即复制到应用私有持久目录，之后交给现有 FFI 引擎读取；
 * 不向 C++ 层暴露 content:// URI，也不申请外部存储权限。
 *
 * 零拷贝呈现：Flutter 的 TextureRegistry.SurfaceProducer 提供一个
 * 可被 GPU 写入的 Surface（背后是 SurfaceTexture/BufferQueue），引擎直接把
 * 交换链 present 到它，合成器把它当作普通 Flutter 纹理层合成——不再需要
 * RGBA 回读 + RawImage 上传。
 */
class MainActivity : FlutterActivity() {
    companion object {
        private const val DOCUMENT_CHANNEL = "com.geoscatter3d.ui_flutter/documents"
        private const val SURFACE_CHANNEL = "com.geoscatter3d.ui_flutter/surface"
        private const val PICK_FILE_REQUEST = 4101
        private const val PICK_DIRECTORY_REQUEST = 4102
        private const val SAVE_FILE_REQUEST = 4103
        private const val IMPORT_BUFFER_SIZE = 64 * 1024

        init {
            // JNI 符号必须经 JVM 的库注册表加载（Dart 侧 dlopen 不注册）。
            System.loadLibrary("gs3d_ffi")
        }
    }

    private val mainHandler = Handler(Looper.getMainLooper())

    /** 零拷贝通路的 Flutter 纹理条目（为 null 表示未接管，走回读路径）。 */
    private var presentProducer: TextureRegistry.SurfaceProducer? = null
    private var presentEngine = 0L
    private var presentWidth = 0
    private var presentHeight = 0

    private external fun nativeAttachSurface(
        engine: Long,
        surface: Surface,
        width: Int,
        height: Int,
    ): Int

    private external fun nativePresentFrame(engine: Long): Int

    private external fun nativeClearSurface(engine: Long): Int

    private external fun nativeHasSurface(engine: Long): Int

    /** 供 Dart 侧把引擎句柄交给平台层做 surface 附加。 */
    private fun engineHandleOf(resultHandle: Number?): Long = resultHandle?.toLong() ?: 0L
    private var pendingPickerResult: MethodChannel.Result? = null
    private var pendingSaveSource: String? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            DOCUMENT_CHANNEL,
        ).setMethodCallHandler { call, result ->
            when (call.method) {
                "prepareEngineShaders" -> {
                    Thread {
                        try {
                            val directory = File(cacheDir, "tmp/engine-shaders")
                            check(directory.mkdirs() || directory.isDirectory)
                            for (name in listOf("point.vert.spv", "point.frag.spv")) {
                                val key = io.flutter.FlutterInjector.instance()
                                    .flutterLoader().getLookupKeyForAsset("assets/shaders/$name")
                                assets.open(key).use { input ->
                                    FileOutputStream(File(directory, name)).use { output ->
                                        input.copyTo(output)
                                    }
                                }
                            }
                            mainHandler.post { result.success(directory.absolutePath) }
                        } catch (error: Exception) {
                            mainHandler.post {
                                result.error("shader_prepare_failed", error.message, null)
                            }
                        }
                    }.start()
                }

                "pickFile" -> openPicker(
                    Intent.ACTION_OPEN_DOCUMENT,
                    PICK_FILE_REQUEST,
                    result,
                )

                "pickDirectory" -> openPicker(
                    Intent.ACTION_OPEN_DOCUMENT_TREE,
                    PICK_DIRECTORY_REQUEST,
                    result,
                )

                "getPersistentDirectory" -> result.success(filesDir.absolutePath)

                // ── 窗口呈现烟囱测试（真机验收交换链通路，不依赖 Flutter 纹理）──
                // ImageReader 提供一个可被 GPU 写入的 Surface；原生侧附加给引擎
                // 并 present 若干帧，随后回读像素确认真的画进去了。
                "surfaceSmokeTest" -> {
                    val engine = engineHandleOf(call.argument<Number>("engineHandle"))
                    val frames = call.argument<Number>("frames")?.toInt() ?: 60
                    if (engine == 0L) {
                        result.error("invalid_engine", "缺少引擎句柄", null)
                    } else {
                        Thread { runSurfaceSmokeTest(engine, frames, result) }.start()
                    }
                }

                "saveFile" -> {
                    val sourcePath = call.argument<String>("sourcePath")
                    val suggestedName = call.argument<String>("suggestedName")
                    val mimeType = call.argument<String>("mimeType")
                    if (sourcePath.isNullOrBlank() || suggestedName.isNullOrBlank() || mimeType.isNullOrBlank()) {
                        result.error("invalid_save_args", "保存文件参数不完整", null)
                    } else {
                        openSavePicker(sourcePath, suggestedName, mimeType, result)
                    }
                }

                else -> result.notImplemented()
            }
        }
        MethodChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            SURFACE_CHANNEL,
        ).setMethodCallHandler { call, result ->
            when (call.method) {
                "attach" -> attachPresentSurface(flutterEngine, call, result)
                "resize" -> resizePresentSurface(call, result)
                "detach" -> {
                    detachPresentSurface()
                    result.success(null)
                }

                else -> result.notImplemented()
            }
        }
    }

    // ── 零拷贝呈现（Flutter Texture）────────────────────────────────────

    /**
     * 建立纹理通路：SurfaceProducer → Surface → 引擎交换链。
     *
     * 返回 Flutter 纹理 id（Dart 侧用它渲染 Texture widget）。
     * 任何一步失败都返回 error，Dart 侧保持回读路径（不得静默地“假装可用”）。
     */
    private fun attachPresentSurface(
        flutterEngine: FlutterEngine,
        call: MethodCall,
        result: MethodChannel.Result,
    ) {
        val engine = engineHandleOf(call.argument<Number>("engineHandle"))
        val width = call.argument<Number>("width")?.toInt() ?: 0
        val height = call.argument<Number>("height")?.toInt() ?: 0
        if (engine == 0L || width <= 0 || height <= 0) {
            result.error("invalid_attach_args", "引擎句柄或尺寸无效", null)
            return
        }
        try {
            detachPresentSurface()
            val producer = flutterEngine.renderer.createSurfaceProducer(
                TextureRegistry.SurfaceLifecycle.resetInBackground,
            )
            producer.setCallback(object : TextureRegistry.SurfaceProducer.Callback {
                override fun onSurfaceCleanup() {
                    // 合成器丢弃了 Surface（如切后台）：引擎不得继续持有失效窗口。
                    if (presentProducer === producer) detachPresentSurface()
                }

                override fun onSurfaceCreated() {
                    // 新 Surface 已就绪（resetInBackground 前后台切换）：重新附加。
                    if (presentProducer === producer && presentEngine != 0L) {
                        reattachCurrentSurface()
                    }
                }
            })
            producer.setSize(width, height)
            val surface = producer.getSurface()
                ?: throw IllegalStateException("SurfaceProducer 未提供 Surface")
            val attach = nativeAttachSurface(engine, surface, width, height)
            if (attach != 0) {
                producer.release()
                result.error("attach_failed", "引擎附加 Surface 失败 (rc=$attach)", attach)
                return
            }
            presentProducer = producer
            presentEngine = engine
            presentWidth = width
            presentHeight = height
            result.success(
                mapOf(
                    "textureId" to producer.id(),
                    "width" to width,
                    "height" to height,
                ),
            )
        } catch (error: Throwable) {
            detachPresentSurface()
            result.error("surface_unavailable", error.message, null)
        }
    }

    /**
     * 视口尺寸变化：setSize 后必须重新附加。
     *
     * 试过「Surface 对象未变时只 setSize、让引擎自己按 OUT_OF_DATE 重建交换链」的
     * 偷懒路径：真机上交换链并不会被判为过期（没有 recreate 日志），结果引擎继续
     * 以旧尺寸 present，Flutter 侧留出大片黑边 —— 正确性优先，这里始终重新附加。
     * 重新附加的卡顿由原生侧解决：Surface 重建不再重跑 CPU LOD 建链，
     * 只重传 GPU 缓冲（kernel `rebuild_lod_locked(reuse_cpu_chain=true)`）。
     */
    private fun resizePresentSurface(
        call: MethodCall,
        result: MethodChannel.Result,
    ) {
        val width = call.argument<Number>("width")?.toInt() ?: 0
        val height = call.argument<Number>("height")?.toInt() ?: 0
        if (width <= 0 || height <= 0) {
            result.error("invalid_resize_args", "尺寸无效", null)
            return
        }
        val producer = presentProducer
        if (producer == null) {
            result.error("no_surface", "当前没有纹理通路", null)
            return
        }
        if (width == presentWidth && height == presentHeight) {
            result.success(mapOf("width" to width, "height" to height))
            return
        }
        try {
            producer.setSize(width, height)
            presentWidth = width
            presentHeight = height
            val surface = producer.getSurface()
                ?: throw IllegalStateException("SurfaceProducer 未提供 Surface")
            val attach = nativeAttachSurface(presentEngine, surface, width, height)
            if (attach != 0) {
                result.error(
                    "attach_failed",
                    "引擎重新附加 Surface 失败 (rc=$attach)",
                    attach,
                )
                return
            }
            result.success(mapOf("width" to width, "height" to height))
        } catch (error: Throwable) {
            result.error("surface_resize_failed", error.message, null)
        }
    }

    private fun reattachCurrentSurface() {
        val producer = presentProducer ?: return
        if (presentEngine == 0L) return
        try {
            val surface = producer.getSurface() ?: return
            nativeAttachSurface(presentEngine, surface, presentWidth, presentHeight)
        } catch (_: Throwable) {
            // 重新附加失败：保持已清空的状态，下一次 attach 会重试。
        }
    }

    /** 释放纹理通路；幂等，可在任意线程调用。 */
    private fun detachPresentSurface() {
        val producer = presentProducer ?: return
        presentProducer = null
        if (presentEngine != 0L) {
            try {
                nativeClearSurface(presentEngine)
            } catch (_: Throwable) {
                // 引擎可能已释放；纹理仍需回收。
            }
        }
        presentEngine = 0L
        presentWidth = 0
        presentHeight = 0
        try {
            producer.release()
        } catch (_: Throwable) {
            // release 失败无需处理（纹理已由 Flutter 侧持有失效）。
        }
    }

    private fun runSurfaceSmokeTest(
        engine: Long,
        frames: Int,
        result: MethodChannel.Result,
    ) {
        val width = 640
        val height = 360
        var reader: ImageReader? = null
        try {
            reader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 4)
            val attachResult = nativeAttachSurface(engine, reader.surface, width, height)
            var presented = 0
            var lastPresent = 0
            for (frame in 0 until frames) {
                lastPresent = nativePresentFrame(engine)
                if (lastPresent == 0) presented += 1
            }
            val hasSurface = nativeHasSurface(engine)

            // present 是异步显示：给合成器一点时间后回读最新一帧。
            var litPixels = -1
            var sampledPixels = 0
            for (attempt in 0 until 10) {
                Thread.sleep(120)
                val image = reader.acquireLatestImage() ?: continue
                image.use {
                    val plane = it.planes[0]
                    val buffer = plane.buffer
                    val rowStride = plane.rowStride
                    val pixelStride = plane.pixelStride
                    val background = ByteArray(4)
                    buffer.position(0)
                    buffer.get(background)
                    val row = ByteArray(rowStride)
                    var lit = 0
                    var sampled = 0
                    var y = 0
                    while (y < it.height) {
                        buffer.position(y * rowStride)
                        buffer.get(row, 0, minOf(rowStride, buffer.remaining()))
                        var x = 0
                        while (x < it.width) {
                            val offset = x * pixelStride
                            sampled += 1
                            if (row[offset] != background[0] ||
                                row[offset + 1] != background[1] ||
                                row[offset + 2] != background[2]
                            ) {
                                lit += 1
                            }
                            x += 3
                        }
                        y += 3
                    }
                    litPixels = lit
                    sampledPixels = sampled
                }
                break
            }

            val clearResult = nativeClearSurface(engine)
            mainHandler.post {
                result.success(
                    mapOf(
                        "attach" to attachResult,
                        "presented" to presented,
                        "lastPresent" to lastPresent,
                        "hasSurface" to hasSurface,
                        "litPixels" to litPixels,
                        "sampledPixels" to sampledPixels,
                        "clear" to clearResult,
                    ),
                )
            }
        } catch (error: Exception) {
            mainHandler.post {
                result.error("surface_smoke_failed", error.message, null)
            }
        } finally {
            reader?.close()
        }
    }

    private fun openPicker(
        action: String,
        requestCode: Int,
        result: MethodChannel.Result,
    ) {
        if (pendingPickerResult != null) {
            result.error("picker_busy", "已有一个文件选择器正在打开", null)
            return
        }

        val intent = Intent(action).apply {
            if (action == Intent.ACTION_OPEN_DOCUMENT) {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = "*/*"
                putExtra(Intent.EXTRA_MIME_TYPES, arrayOf(
                    "text/*",
                    "application/octet-stream",
                    "application/zip",
                ))
            } else {
                putExtra("android.content.extra.SHOW_ADVANCED", true)
            }
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }

        pendingPickerResult = result
        pendingSaveSource = null
        try {
            startActivityForResult(intent, requestCode)
        } catch (error: Exception) {
            pendingPickerResult = null
            result.error("picker_open_failed", error.message, null)
        }
    }

    private fun openSavePicker(
        sourcePath: String,
        suggestedName: String,
        mimeType: String,
        result: MethodChannel.Result,
    ) {
        if (pendingPickerResult != null) {
            result.error("picker_busy", "已有一个文件选择器正在打开", null)
            return
        }
        val source = File(sourcePath)
        if (!source.isFile) {
            result.error("save_source_missing", "待保存文件不存在", null)
            return
        }

        val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = mimeType
            putExtra(Intent.EXTRA_TITLE, suggestedName)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }

        pendingPickerResult = result
        pendingSaveSource = source.absolutePath
        try {
            startActivityForResult(intent, SAVE_FILE_REQUEST)
        } catch (error: Exception) {
            pendingPickerResult = null
            pendingSaveSource = null
            result.error("picker_open_failed", error.message, null)
        }
    }

    override fun onDestroy() {
        // 活动销毁前释放纹理通路：避免引擎持有已失效的 ANativeWindow。
        detachPresentSurface()
        super.onDestroy()
    }

    @Suppress("DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != PICK_FILE_REQUEST &&
            requestCode != PICK_DIRECTORY_REQUEST &&
            requestCode != SAVE_FILE_REQUEST) {
            return
        }

        val result = pendingPickerResult
        pendingPickerResult = null
        val saveSource = pendingSaveSource
        pendingSaveSource = null
        if (result == null) return

        if (requestCode == SAVE_FILE_REQUEST) {
            if (resultCode != Activity.RESULT_OK || data?.data == null || saveSource == null) {
                result.success(false)
                return
            }

            val uri = data.data!!
            tryPersistReadWritePermission(uri, data.flags)
            Thread {
                try {
                    copyFileToUri(saveSource, uri)
                    mainHandler.post { result.success(true) }
                } catch (error: Exception) {
                    mainHandler.post {
                        result.error("save_copy_failed", error.message, null)
                    }
                }
            }.start()
            return
        }

        if (resultCode != Activity.RESULT_OK || data?.data == null) {
            result.success(null)
            return
        }

        val uri = data.data!!
        tryPersistReadWritePermission(uri, data.flags)
        Thread {
            try {
                val importedPath = if (requestCode == PICK_DIRECTORY_REQUEST) {
                    importDirectory(uri)
                } else {
                    importFile(uri)
                }
                mainHandler.post { result.success(importedPath) }
            } catch (error: Exception) {
                mainHandler.post {
                    result.error("picker_import_failed", error.message, null)
                }
            }
        }.start()
    }

    private fun tryPersistReadWritePermission(uri: Uri, flags: Int) {
        try {
            val persistableFlags = flags and
                (Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            contentResolver.takePersistableUriPermission(uri, persistableFlags)
        } catch (_: SecurityException) {
            // 复制已经在当前授权窗口内完成；部分 Provider 不支持持久化授权。
        }
    }

    private fun importRoot(): File = File(filesDir, "GeoScatter3D/imports").apply {
        mkdirs()
    }

    private fun importFile(uri: Uri): String {
        val originalName = queryDisplayName(uri) ?: "point-cloud-data"
        val target = uniqueFile(importRoot(), sanitizeName(originalName))
        copyStream(uri, target)
        return target.absolutePath
    }

    private fun importDirectory(treeUri: Uri): String {
        val rootDocumentId = DocumentsContract.getTreeDocumentId(treeUri)
        val rootUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, rootDocumentId)
        val rootName = sanitizeName(queryDisplayName(rootUri) ?: "gs3d.bundle")
        val target = uniqueFile(importRoot(), rootName)
        try {
            if (!target.mkdirs() && !target.isDirectory) {
                throw IllegalStateException("无法创建导入目录")
            }
            copyChildren(treeUri, rootDocumentId, target)
            return target.absolutePath
        } catch (error: Exception) {
            target.deleteRecursively()
            throw error
        }
    }

    private fun copyChildren(treeUri: Uri, parentDocumentId: String, destination: File) {
        val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
            treeUri,
            parentDocumentId,
        )
        contentResolver.query(
            childrenUri,
            arrayOf(
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                OpenableColumns.DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
            ),
            null,
            null,
            null,
        )?.use { cursor ->
            val idIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID)
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            val mimeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE)
            if (idIndex < 0 || nameIndex < 0 || mimeIndex < 0) {
                throw IllegalStateException("文档提供方返回了不完整的目录信息")
            }
            while (cursor.moveToNext()) {
                val documentId = cursor.getString(idIndex)
                val name = sanitizeName(cursor.getString(nameIndex) ?: "item")
                val mime = cursor.getString(mimeIndex)
                val childUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, documentId)
                val child = uniqueFile(destination, name)
                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    if (!child.mkdirs() && !child.isDirectory) {
                        throw IllegalStateException("无法创建导入子目录: $name")
                    }
                    copyChildren(treeUri, documentId, child)
                } else {
                    copyStream(childUri, child)
                }
            }
        }
    }

    private fun queryDisplayName(uri: Uri): String? {
        contentResolver.query(
            uri,
            arrayOf(OpenableColumns.DISPLAY_NAME),
            null,
            null,
            null,
        )?.use { cursor ->
            if (cursor.moveToFirst()) {
                return cursor.getString(cursor.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
            }
        }
        return null
    }

    private fun copyStream(uri: Uri, target: File) {
        val input = contentResolver.openInputStream(uri)
            ?: throw IllegalStateException("无法读取所选文档")
        try {
            FileOutputStream(target).use { output ->
                val buffer = ByteArray(IMPORT_BUFFER_SIZE)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    output.write(buffer, 0, count)
                }
                output.flush()
            }
        } catch (error: Exception) {
            target.delete()
            throw error
        } finally {
            input.close()
        }
    }

    private fun copyFileToUri(sourcePath: String, uri: Uri) {
        val source = File(sourcePath)
        FileInputStream(source).use { input ->
            val output = contentResolver.openOutputStream(uri, "wt")
                ?: throw IllegalStateException("无法写入目标文档")
            output.use {
                val buffer = ByteArray(IMPORT_BUFFER_SIZE)
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    it.write(buffer, 0, count)
                }
                it.flush()
            }
        }
    }

    private fun uniqueFile(parent: File, requestedName: String): File {
        val safeName = if (requestedName.isBlank()) "imported-data" else requestedName
        var candidate = File(parent, safeName)
        var suffix = 2
        while (candidate.exists()) {
            candidate = File(parent, "$safeName-$suffix")
            suffix += 1
        }
        return candidate
    }

    private fun sanitizeName(value: String): String {
        val normalized = value.replace(Regex("[\\\\/:*?\"<>|]"), "_").trim()
        // Provider 返回的显示名不应改变导入根目录；显式拒绝路径遍历名称。
        return if (normalized.isBlank() || normalized == "." || normalized == "..") {
            "imported-data"
        } else {
            normalized
        }
    }
}
