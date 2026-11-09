package com.ngirim.pc

object NativeCore {
    init {
        try {
            System.loadLibrary("native-lib")
        } catch (e: UnsatisfiedLinkError) {
            println("Peringatan: Gagal memuat library native (native-lib.dll). Pesan: ${e.message}")
        }
    }

    external fun stringFromJNI(): String
}