package com.ngirim.pc

import kotlinx.coroutines.*

fun main() {
    println("Memulai aplikasi...")
    println("Memuat library C++ (ImGui + GLFW)...")
    
    // Jalankan background worker untuk menangkap antrean aksi dari C++ (pollAction)
    GlobalScope.launch(Dispatchers.IO) {
        while (true) {
            val action = NativeCore.pollAction()
            if (action.isNotEmpty()) {
                println("Aksi yang diklik di UI: $action")
                // TODO: Hubungkan logika Kotlin Anda di sini. 
                // Misal: if(action.startsWith("FM|GO")) { NativeCore.setFm(...) }
            }
            delay(50)
        }
    }

    // Blokir Main Thread dan serahkan kontrol jendela ke C++
    NativeCore.startWindow()
    
    println("Jendela ditutup, aplikasi selesai.")
}