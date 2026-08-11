package org.rackdroid

import android.app.Activity
import android.system.Os
import java.io.File
import java.io.FileOutputStream
import org.json.JSONObject

object ModulePackageStore {
    private fun root(activity: Activity) = File(activity.filesDir, "user/module-packages").apply { mkdirs() }
    fun inbox(activity: Activity) = File(root(activity), "inbox").apply { mkdirs() }
    fun work(activity: Activity) = File(root(activity), "work").apply { mkdirs() }
    fun backups(activity: Activity) = File(root(activity), "backups").apply { mkdirs() }
    fun transactions(activity: Activity) = File(root(activity), "transactions").apply { mkdirs() }
    fun metadata(activity: Activity) = File(root(activity), "metadata").apply { mkdirs() }
    fun legacyBackups(activity: Activity) = File(root(activity), "legacy-backups").apply { mkdirs() }
    fun installedRoot(activity: Activity) = File(activity.filesDir, "user/plugins").apply { mkdirs() }
    fun externalModules(activity: Activity) = File(activity.getExternalFilesDir(null), "Modules").apply { mkdirs() }

    fun uniqueInboxFile(activity: Activity, requestedName: String): File {
        val root = inbox(activity).canonicalFile
        val clean = requestedName.replace('\\', '/').substringAfterLast('/').take(160).ifBlank { "pack.rdmod" }
        val dot = clean.lastIndexOf('.')
        val stem = if (dot > 0) clean.substring(0, dot) else clean
        val ext = if (dot > 0) clean.substring(dot) else ""
        for (i in 0..9999) {
            val f = File(root, if (i == 0) clean else "$stem ($i)$ext").canonicalFile
            if (f.parentFile != root) throw SecurityException("inbox path escapes package store")
            if (!f.exists()) return f
        }
        throw IllegalStateException("too many files with the same name")
    }

    fun installed(activity: Activity, slug: String): File = safeChild(installedRoot(activity), slug)
    fun backup(activity: Activity, slug: String): File = safeChild(backups(activity), slug)
    fun transactionFile(activity: Activity, slug: String): File = safeChild(transactions(activity), "$slug.json")
    fun metadataFile(activity: Activity, slug: String): File = safeChild(metadata(activity), "$slug.json")

    private fun safeChild(parent: File, name: String): File {
        val root = parent.canonicalFile
        val child = File(root, name).canonicalFile
        if (child.parentFile != root) throw SecurityException("package path escapes package store")
        return child
    }

    /**
     * Process-death-safe metadata replacement. Do not delete the old file first:
     * that creates a window where a crash loses both the old transaction state
     * and the new one. rename(2) atomically replaces the destination on Android.
     */
    fun writeJsonAtomic(file: File, json: JSONObject) {
        file.parentFile?.mkdirs()
        val tmp = File(file.parentFile, ".${file.name}.${System.nanoTime()}.tmp")
        try {
            val bytes = json.toString(2).toByteArray(Charsets.UTF_8)
            FileOutputStream(tmp).use { out ->
                out.write(bytes)
                out.fd.sync()
            }
            Os.rename(tmp.absolutePath, file.absolutePath)
        } catch (t: Throwable) {
            tmp.delete()
            throw IllegalStateException("cannot atomically replace metadata", t)
        }
    }

    fun readJson(file: File): JSONObject? = try { if (file.isFile) JSONObject(file.readText()) else null } catch (_: Throwable) { null }

    fun cleanWork(activity: Activity) {
        work(activity).listFiles()?.forEach { it.deleteRecursively() }
    }
}
