package com.ngirim.pc

import androidx.compose.desktop.ui.tooling.preview.Preview
import androidx.compose.foundation.layout.*
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import kotlinx.coroutines.launch

@Composable
@Preview
fun App() {
    var statusText by remember { mutableStateOf("Ngirim PC Version - Siap digunakan") }
    var updateInfo by remember { mutableStateOf<UpdateInfo?>(null) }
    val coroutineScope = rememberCoroutineScope()

    // Cek pembaruan saat aplikasi pertama kali dibuka
    LaunchedEffect(Unit) {
        updateInfo = GithubUpdater.checkForUpdates()
    }

    MaterialTheme {
        if (updateInfo != null) {
            AlertDialog(
                onDismissRequest = { updateInfo = null },
                title = { Text("Update Tersedia!") },
                text = { Text("Versi ${updateInfo!!.version} tersedia.\n\nSilakan unduh pembaruan dari:\n${updateInfo!!.downloadUrl}") },
                confirmButton = {
                    Button(onClick = { updateInfo = null }) {
                        Text("Tutup")
                    }
                }
            )
        }

        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(text = "Ngirim Desktop", style = MaterialTheme.typography.h4)
            Spacer(Modifier.height(16.dp))
            
            Text(statusText, style = MaterialTheme.typography.body1)
            Spacer(Modifier.height(24.dp))
            
            Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                Button(onClick = {
                    try {
                        statusText = NativeCore.stringFromJNI()
                    } catch (e: Exception) {
                        statusText = "Error: Library C++ belum di-build atau dimuat.\nJalankan CMake terlebih dahulu."
                    }
                }) {
                    Text("Test Modul C++")
                }
                
                Button(onClick = {
                    coroutineScope.launch {
                        statusText = "Mengecek pembaruan dari GitHub..."
                        val info = GithubUpdater.checkForUpdates()
                        if (info != null) {
                            updateInfo = info
                            statusText = "Update ${info.version} tersedia!"
                        } else {
                            statusText = "Aplikasi sudah versi terbaru (${GithubUpdater.CURRENT_VERSION})."
                        }
                    }
                }) {
                    Text("Cek Pembaruan")
                }
            }
        }
    }
}

fun main() = application {
    Window(onCloseRequest = ::exitApplication, title = "Ngirim PC") {
        App()
    }
}