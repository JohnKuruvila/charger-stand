import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// Screenshot tests (Roborazzi on Robolectric) exist only with -Pscreenshots: their sources and libraries are added on
// demand and never run in CI. See scripts/screenshots.sh.
val screenshots = providers.gradleProperty("screenshots").isPresent

// Release signing lives outside the repository: W6_SIGNING, or ~/.config/w6-android/signing.properties with
// storeFile, storePassword, keyAlias and keyPassword. Without it a release build is unsigned.
val signingProps = Properties().apply {
    val f = file(System.getenv("W6_SIGNING") ?: "${System.getProperty("user.home")}/.config/w6-android/signing.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "app.w6.companion"
    compileSdk = 37
    buildToolsVersion = "36.0.0"

    defaultConfig {
        applicationId = "app.w6.companion"
        minSdk = 26
        targetSdk = 36
        versionCode = 14
        versionName = "0.5.0"
    }

    signingConfigs {
        if (signingProps.getProperty("storeFile") != null) {
            create("release") {
                storeFile = file(signingProps.getProperty("storeFile"))
                storePassword = signingProps.getProperty("storePassword")
                keyAlias = signingProps.getProperty("keyAlias")
                keyPassword = signingProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release")
        }
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
    }
    testOptions {
        unitTests.isReturnDefaultValues = true
        if (screenshots) unitTests.isIncludeAndroidResources = true
    }
    if (screenshots) {
        sourceSets.getByName("test").kotlin.srcDir("src/screenshots/kotlin")
    }
    lint {
        warningsAsErrors = false
        abortOnError = true
    }
    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {
    val composeBom = platform(libs.androidx.compose.bom)
    implementation(composeBom)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    debugImplementation(libs.androidx.compose.ui.tooling)

    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.core.ktx)
    implementation(libs.okhttp)
    implementation(libs.code.scanner)  // Google's QR scanner screen, for pairing; no camera permission needed

    testImplementation(libs.junit)
    testImplementation(libs.json)
    if (screenshots) {
        testImplementation(composeBom)
        testImplementation("org.robolectric:robolectric:4.17")
        testImplementation("io.github.takahirom.roborazzi:roborazzi:1.75.0")
        testImplementation("io.github.takahirom.roborazzi:roborazzi-compose:1.75.0")
        testImplementation("androidx.compose.ui:ui-test-junit4")
        debugImplementation("androidx.compose.ui:ui-test-manifest")
    }
}

tasks.withType<Test>().configureEach {
    if (screenshots) {
        filter.includeTestsMatching("*ScreenshotTest*")
        // Robolectric reaches into JDK internals on current JDKs.
        jvmArgs("--add-opens=java.base/java.io=ALL-UNNAMED", "--add-opens=java.base/java.lang=ALL-UNNAMED",
            "--add-opens=java.base/java.util=ALL-UNNAMED", "--add-opens=java.base/java.nio=ALL-UNNAMED",
            "--add-opens=java.base/sun.nio.fs=ALL-UNNAMED", "--add-opens=java.base/java.lang.reflect=ALL-UNNAMED",
            "--add-exports=java.base/jdk.internal.access=ALL-UNNAMED", "--add-opens=java.base/jdk.internal.access=ALL-UNNAMED")
        maxHeapSize = "2g"
        systemProperty("roborazzi.test.record", "true")
        systemProperty("roborazzi.record.filePathStrategy", "relativePathFromRoborazziContextOutputDirectory")
        systemProperty("screenshots.dir", layout.buildDirectory.dir("screenshots").get().asFile.absolutePath)
    }
}
