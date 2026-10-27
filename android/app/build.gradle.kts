plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val releaseTag: String = run {
    val fromProp = (project.findProperty("versionName") as? String)?.trim().orEmpty()
    val fromEnv = if (System.getenv("GITHUB_REF")?.startsWith("refs/tags/") == true)
        System.getenv("GITHUB_REF_NAME")?.trim().orEmpty() else ""
    val picked = fromProp.ifEmpty { fromEnv }
    picked.takeIf { it.matches(Regex("v[0-9][0-9A-Za-z.\\-]*")) } ?: ""
}

android {
    namespace = "com.allperiph"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.allperiph"
        minSdk = 34
        targetSdk = 35
        versionCode = 4
        versionName = releaseTag.ifEmpty { "1.0.1" }
        buildConfigField("String", "RELEASE_TAG", "\"${releaseTag}\"")
    }

    signingConfigs {
        create("release") {
            storeFile = file("../apx-release.keystore")
            storePassword = "allperiph2026"
            keyAlias = "apx"
            keyPassword = "allperiph2026"
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
        }
    }

    lint {
        checkReleaseBuilds = false
    }

    if (file("src/main/cpp/CMakeLists.txt").exists()) {
        externalNativeBuild {
            cmake {
                path = file("src/main/cpp/CMakeLists.txt")
            }
        }
        defaultConfig {
            externalNativeBuild {
                cmake {
                    cppFlags += "-std=c++17"
                }
            }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
        freeCompilerArgs += listOf("-Xno-param-assertions")
    }

    buildFeatures {
        buildConfig = true
    }

    packaging {
        jniLibs.useLegacyPackaging = true
    }

    testOptions {
        unitTests.isReturnDefaultValues = true
    }
}

dependencies {
    implementation(project(":shared"))
    testImplementation("junit:junit:4.13.2")
}
