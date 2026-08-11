package org.rackdroid

import java.io.File

data class ModulePackageDescriptor(
    val slug: String,
    val title: String,
    val version: String?,
    val sha256: String,
    val nativeLibraryName: String,
    val abi: String,
    val sourceFile: File
)

enum class ModuleInstallStatus {
    INSTALLED,
    UPDATED,
    ALREADY_INSTALLED,
    DOWNGRADE_REQUIRES_CONFIRMATION,
    CONFLICT,
    FAILED
}

data class ModuleInstallResult(
    val status: ModuleInstallStatus,
    val slug: String? = null,
    val title: String,
    val message: String,
    val restartRequired: Boolean = false
)

data class InstalledModulePack(
    val slug: String,
    val dir: File,
    val sizeBytes: Long,
    val version: String? = null,
    val pendingActivation: Boolean = false,
    val rollbackAvailable: Boolean = false
)
