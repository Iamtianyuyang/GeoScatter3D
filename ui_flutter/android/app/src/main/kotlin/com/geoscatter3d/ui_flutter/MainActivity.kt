package com.geoscatter3d.ui_flutter

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream

/** Android 文档选择器桥接。
 *
 * 选择结果会立即复制到应用私有持久目录，之后交给现有 FFI 引擎读取；
 * 不向 C++ 层暴露 content:// URI，也不申请外部存储权限。
 */
class MainActivity : FlutterActivity() {
    companion object {
        private const val DOCUMENT_CHANNEL = "com.geoscatter3d.ui_flutter/documents"
        private const val PICK_FILE_REQUEST = 4101
        private const val PICK_DIRECTORY_REQUEST = 4102
        private const val SAVE_FILE_REQUEST = 4103
        private const val IMPORT_BUFFER_SIZE = 64 * 1024
    }

    private val mainHandler = Handler(Looper.getMainLooper())
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
