import org.jetbrains.kotlin.gradle.ExperimentalKotlinGradlePluginApi
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.kotlinMultiplatform)
    alias(libs.plugins.kotlinSerialization)
    alias(libs.plugins.androidApplication)
    alias(libs.plugins.composeMultiplatform)
    alias(libs.plugins.composeCompiler)
}

kotlin {
    compilerOptions {
        // FileStore is an expect object (one native file dialog per platform).
        freeCompilerArgs.add("-Xexpect-actual-classes")
    }

    jvm("desktop")

    androidTarget {
        @OptIn(ExperimentalKotlinGradlePluginApi::class)
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_17)
        }
    }

    sourceSets {
        val commonMain by getting {
            dependencies {
                implementation(compose.runtime)
                implementation(compose.foundation)
                implementation(compose.material3)
                implementation(compose.ui)
                implementation(compose.components.resources)
                implementation(libs.kotlinx.coroutines.core)
                implementation(libs.kotlinx.serialization.json)
            }
        }
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
            }
        }

        val desktopMain by getting {
            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(libs.jserialcomm)
            }
        }
        val desktopTest by getting {
            dependencies {
                implementation(compose.desktop.uiTestJUnit4)
            }
        }

        val androidMain by getting {
            dependencies {
                implementation(libs.androidx.activity.compose)
                implementation(libs.androidx.core.ktx)
                implementation(libs.usbSerialForAndroid)
            }
        }
        val androidInstrumentedTest by getting {
            dependencies {
                implementation("androidx.compose.ui:ui-test-junit4:1.8.2")
                implementation("androidx.test:runner:1.7.0")
                implementation("androidx.test:core-ktx:1.7.0")
                // New Android versions removed the reflective InputManager API used by 3.6.
                implementation("androidx.test.espresso:espresso-core:3.7.0")
            }
        }
    }
}

android {
    namespace = "io.github.ml3rc.sna.client"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.ml3rc.sna.client"
        minSdk = 26 // USB host mode / UsbManager APIs used here need API 26+
        targetSdk = 35
        versionCode = 11
        versionName = "1.0.0"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    sourceSets["main"].apply {
        manifest.srcFile("src/androidMain/AndroidManifest.xml")
        res.srcDirs("src/androidMain/res")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    add("debugImplementation", "androidx.compose.ui:ui-test-manifest:1.8.2")
}

// MAJOR.MINOR here; on Windows the MSI's BUILD field is generated per build (see windows {} below).
val desktopVersion = "1.0.0"

compose.desktop {
    application {
        mainClass = "io.github.ml3rc.sna.client.MainKt"

        // Memory budget: the whole process stays under 0.5 GB. Measured 2026-10-24 on the real device
        // (loop sweep, 4096 points x 4 S-parameters, every tab visited): without these flags ~470 MB
        // working set and ~1 GB committed; with them ~300 MB for both. Most of the old footprint was
        // the Direct3D renderer (~750 MB native commit), so Skia renders in software instead: same
        // pixels, about 1 % more CPU while a loop sweep redraws the chart. The Java heap itself peaks
        // around 35 MB, so 128 MB leaves room; SerialGC returns unused heap and has no G1 overhead.
        jvmArgs += listOf(
            "-Dskiko.renderApi=SOFTWARE_FAST",
            "-Xms16m", "-Xmx128m", "-XX:+UseSerialGC",
            "-XX:MaxMetaspaceSize=128m", "-XX:ReservedCodeCacheSize=48m", "-XX:MaxDirectMemorySize=64m",
            "-XX:CICompilerCount=2", "-Xss512k",
        )

        nativeDistributions {
            targetFormats(
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Dmg,
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Msi,
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Deb,
            )
            packageName = "SNA Client"
                        packageVersion = desktopVersion

            // jpackage's MSI does NOT create a Start Menu shortcut by default -- confirmed
            // against this exact pinned plugin version's WindowsPlatformSettings, 2026-10-02,
            // after the user installed the MSI and couldn't find it in Windows Search (only in
            // the uninstall list, which Windows populates from the MSI's registry entries
            // regardless of shortcuts). Without `menu = true`, nothing gets added to the Start
            // Menu for search to index at all.
            windows {
                // In-place MSI updates: jpackage derives the ProductCode from the version, so two
                // builds with the same version are the "same product" and Windows Installer refuses
                // the second one ("already installed") -- that is what forced a full uninstall/
                // reinstall for every rebuild. The BUILD field is therefore a 10-minute tick since
                // 2026-10-20 (MSI ignores a 4th field and caps BUILD at 65535, ~15 months), so every
                // MSI is a newer version and its RemoveExistingProducts replaces the installed one.
                // upgradeUuid pins the code the already-installed 1.x builds carry (jpackage had
                // derived it from vendor+name), so renaming either can't orphan existing installs.
                val tick = (System.currentTimeMillis() / 1000 - 1_792_454_400L) / 600
                msiPackageVersion = desktopVersion.split('.').take(2).joinToString(".") + ".$tick"
                upgradeUuid = "29DCA976-C08B-3AC4-86EA-7E76DB3EB9AE"
                iconFile.set(project.file("icons/sna.ico"))
                menu = true
                menuGroup = "SNA"
                shortcut = true // also drop a desktop shortcut, same reasoning
            }
            macOS { iconFile.set(project.file("icons/sna.icns")) }
            linux { iconFile.set(project.file("icons/sna.png")) }
        }
    }
}
