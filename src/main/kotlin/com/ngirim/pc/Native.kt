package com.ngirim.pc

object NativeCore {
    init {
        val os = System.getProperty("os.name").lowercase()
        val libName = when {
            os.contains("win") -> "native-lib.dll"
            os.contains("mac") -> "libnative-lib.dylib"
            else -> "libnative-lib.so"
        }
        
        try {
            // Catatan: Dalam produksi PC, Anda mungkin perlu mengekstrak .dll/.so 
            // dari resource JAR ke folder temporary (System.getProperty("java.io.tmpdir"))
            // lalu memanggil System.load(absolutePath).
            // System.loadLibrary() akan mencari di java.library.path
            System.loadLibrary("native-lib")
        } catch (e: UnsatisfiedLinkError) {
            println("Peringatan: Gagal memuat library native ($libName). Pesan: ${e.message}")
        }
    }

    // Deklarasi fungsi JNI
    external fun stringFromJNI(): String
}