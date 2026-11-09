plugins {
    kotlin("jvm") version "1.9.22"
    id("org.jetbrains.compose") version "1.6.0"
}

repositories {
    mavenCentral()
    google()
}

dependencies {
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.8.0")
}

// Digunakan khusus untuk membungkus .msi / .exe saja, bukan untuk merender UI
compose.desktop {
    application {
        mainClass = "com.ngirim.pc.MainKt"
        nativeDistributions {
            targetFormats(
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Msi,
                org.jetbrains.compose.desktop.application.dsl.TargetFormat.Exe
            )
            packageName = "NgirimPC"
            packageVersion = "1.0.0"
        }
    }
}