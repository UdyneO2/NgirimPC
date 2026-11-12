// Uji pembuat QR pairing: ukuran, pola penentu posisi, dan penolakan data terlalu panjang.
#include <cstdio>
#include <string>
#include "../src/core/qr.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("GAGAL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    const std::string url = "ngirim://pair?h=192.168.1.10&p=50412&pin=482913&n=DESKTOP-NGIRIM";
    auto m = qr::encode(url);
    const int n = (int)m.size();
    CHECK(n >= 21 && n <= 57 && (n - 17) % 4 == 0);        // versi 1..10 => 21..57 modul
    for (auto& r : m) CHECK((int)r.size() == n);
    // pola penentu posisi 7x7 di tiga sudut: bingkai gelap, cincin terang, inti 3x3 gelap
    auto finder = [&](int x0, int y0) {
        for (int i = 0; i < 7; i++)
            for (int j = 0; j < 7; j++) {
                bool ring = i == 0 || i == 6 || j == 0 || j == 6, core = i >= 2 && i <= 4 && j >= 2 && j <= 4;
                CHECK(m[y0 + i][x0 + j] == (ring || core));
            }
    };
    finder(0, 0); finder(n - 7, 0); finder(0, n - 7);
    // pola waktu: baris 6 bergantian gelap/terang di antara pola penentu
    for (int x = 8; x < n - 8; x++) CHECK(m[6][x] == (x % 2 == 0));
    // isi berbeda menghasilkan matriks berbeda; data yang sama deterministik
    CHECK(qr::encode(url) == m);
    CHECK(qr::encode(url + "x") != m);
    CHECK(qr::encode(std::string(5000, 'a')).empty());      // terlalu panjang
    std::puts(fails ? "ADA TES GAGAL" : "SELESAI: qr lulus");
    return fails ? 1 : 0;
}
