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
        versionCode = 9
        versionName = "0.4.0"
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

compose.desktop {
    application {
        mainClass = "io.github.ml3rc.sna.client.MainKt"

        nativeDistributions {
            targetFormats(
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Dmg,
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Msi,
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Deb,
            )
            packageName = "SNA Client"
            // Packaging tools (DMG/MSI) require MAJOR > 0, so desktop and Android versions differ. Bump this
            // on every rebuild meant for the user to actually install -- confirmed risk,
            // 2026-10-02: Windows Installer can treat "same product, same version" as a no-op on
            // reinstall and silently keep the OLD files in place even if you "reinstall" over it,
            // which would make testing a just-shipped fix indistinguishable from testing the
            // still-broken old build. Bumping the version forces a real upgrade every time.
            packageVersion = "1.4.0"

            // jpackage's MSI does NOT create a Start Menu shortcut by default -- confirmed
            // against this exact pinned plugin version's WindowsPlatformSettings, 2026-10-02,
            // after the user installed the MSI and couldn't find it in Windows Search (only in
            // the uninstall list, which Windows populates from the MSI's registry entries
            // regardless of shortcuts). Without `menu = true`, nothing gets added to the Start
            // Menu for search to index at all.
            windows {
                menu = true
                menuGroup = "SNA"
                shortcut = true // also drop a desktop shortcut, same reasoning
            }
        }
    }
}
