package com.ngirim.pc

object NativeCore {
    init {
        try {
            System.loadLibrary("native-lib")
        } catch (e: UnsatisfiedLinkError) {
            println("Gagal memuat native-lib.dll. Harap build C++ terlebih dahulu. Pesan: ${e.message}")
        }
    }

    // Entry point untuk mengambil alih layar
    external fun startWindow()
    
    // Bridge data dari C++ ke Kotlin
    external fun pollAction(): String
    
    // TODO: Definisikan sisa jembatan state C++ disini
    external fun setShizuku(ok: Boolean, msg: String)
    external fun log(msg: String)
}