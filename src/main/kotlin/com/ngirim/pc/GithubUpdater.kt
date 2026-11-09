package com.ngirim.pc

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.net.HttpURLConnection
import java.net.URL
import java.io.InputStreamReader

object GithubUpdater {
    const val CURRENT_VERSION = "v1.0.0" // Ganti saat rilis versi baru
    private const val GITHUB_API_URL = "https://api.github.com/repos/UdyneO2/NgirimPC/releases/latest"
    private const val RELEASE_PAGE_URL = "https://github.com/UdyneO2/NgirimPC/releases"

    suspend fun checkForUpdates(): UpdateInfo? = withContext(Dispatchers.IO) {
        try {
            val url = URL(GITHUB_API_URL)
            val connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            connection.setRequestProperty("Accept", "application/vnd.github.v3+json")

            if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                val response = InputStreamReader(connection.inputStream).readText()
                
                // Regex diperbaiki menggunakan standar escape string
                val tagRegex = "\"tag_name\"\\s*:\\s*\"([^\"]+)\"".toRegex()
                val latestVersion = tagRegex.find(response)?.groupValues?.get(1)

                if (latestVersion != null && latestVersion != CURRENT_VERSION) {
                    val os = System.getProperty("os.name").lowercase()
                    val ext = when {
                        os.contains("win") -> ".msi"
                        os.contains("mac") -> ".dmg"
                        else -> ".deb"
                    }
                    
                    // Regex diperbaiki menggunakan standar escape string
                    val browserUrlRegex = "\"browser_download_url\"\\s*:\\s*\"([^\"]+$ext)\"".toRegex()
                    val downloadUrl = browserUrlRegex.find(response)?.groupValues?.get(1)

                    return@withContext UpdateInfo(
                        version = latestVersion, 
                        downloadUrl = downloadUrl ?: "$RELEASE_PAGE_URL/latest"
                    )
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return@withContext null
    }
}

data class UpdateInfo(val version: String, val downloadUrl: String)
