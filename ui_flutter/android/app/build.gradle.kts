import java.util.Properties

val releaseKeystorePropertiesFile = rootProject.file("key.properties")
val releaseKeystoreProperties = Properties()
val releaseSigningConfigured = releaseKeystorePropertiesFile.exists()
val releaseTaskRequested = gradle.startParameter.taskNames.any {
    it.contains("Release", ignoreCase = true)
}
if (releaseTaskRequested && !releaseSigningConfigured) {
    throw GradleException(
        "Missing android/key.properties; configure the production keystore before a release build."
    )
}
if (releaseSigningConfigured) {
    releaseKeystorePropertiesFile.inputStream().use { releaseKeystoreProperties.load(it) }
}

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

val supportedAbis = mapOf("android-arm64" to "arm64-v8a", "android-x64" to "x86_64")
val targets = (project.findProperty("target-platform") as? String)?.split(",")
val selectedAbis = targets?.mapNotNull { supportedAbis[it] } ?: supportedAbis.values.toList()
require(selectedAbis.isNotEmpty()) { "GeoScatter3D supports android-arm64 and android-x64." }

android {
    namespace = "com.geoscatter3d.ui_flutter"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "com.geoscatter3d.ui_flutter"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        ndk { abiFilters.addAll(selectedAbis) }
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    if (releaseSigningConfigured) {
        signingConfigs {
            create("release") {
                keyAlias = releaseKeystoreProperties["keyAlias"] as String
                keyPassword = releaseKeystoreProperties["keyPassword"] as String
                // key.properties 位于 android/ 根目录；相对路径也应相对
                // android/ 解析，而不是相对 android/app/ 模块目录。
                storeFile = rootProject.file(releaseKeystoreProperties["storeFile"] as String)
                storePassword = releaseKeystoreProperties["storePassword"] as String
            }
        }
    }

    packaging {
        jniLibs.excludes.addAll(supportedAbis.values.filterNot { it in selectedAbis }.map { "**/$it/**" })
    }

    buildTypes {
        release {
            if (releaseSigningConfigured) {
                signingConfig = signingConfigs.getByName("release")
            }
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
