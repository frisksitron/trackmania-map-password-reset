// Map-free checks for map_password.h. Expected values come from maps saved by
// Trackmania itself (see README.md).
#include <cstdio>
#include <random>

#include "map_password.h"

using namespace tmpw;

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static Hash hex16(const char* s) {
    Hash h{};
    for (int i = 0; i < 16; i++) std::sscanf(s + 2 * i, "%2hhx", &h[i]);
    return h;
}

static std::string md5_hex(const std::string& s) {
    Md5 m;
    m.update((const uint8_t*)s.data(), s.size());
    Hash h = m.finish();
    return to_hex(h.data(), h.size());
}

static void roundtrip(const Bytes& data) {
    Bytes comp = lzo1x_compress(data);
    CHECK(lzo1x_decompress(comp, data.size()) == data);
}

int main() {
    // MD5 / HMAC-MD5 (RFC 1321, RFC 2104)
    CHECK(md5_hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(md5_hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    CHECK(md5_hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890") ==
          "57edf4a22be3c955ac49da2e2107b67a");
    {
        Bytes key(16, 0x0b);
        std::string msg = "Hi There";
        Hash h = hmac_md5(key.data(), key.size(), (const uint8_t*)msg.data(), msg.size());
        CHECK(to_hex(h.data(), h.size()) == "9294727a3638bb1c13f48ef8158bfc9d");
    }

    // password hash: map saved by the game with password "123456"
    const Hash pw123456 = hex16("e0ce0b59b86e6e5ae8419589a9579c17");
    CHECK(hash_password("123456", 0x590bcee0) == pw123456);
    CHECK(check_password("123456", pw123456));
    CHECK(!check_password("12345", pw123456));
    CHECK(!check_password("", pw123456));
    CHECK(is_empty(hash_password("")));
    CHECK(check_password("", Hash{}));
    CHECK(check_password("pässwörd ✓", hash_password("pässwörd ✓")));

    // CRC: maps saved by the game, without and with a password
    CHECK(password_crc(Hash{}, "GRmaodoxfwatctvpYlJUyiCzIte") == 0xee249b5b);
    CHECK(password_crc(pw123456, "oEksnLbbBuvKIZEhJ6UJONxFcA4") == 0xae862f38);

    // LZO1X round trips: empty, tiny, incompressible, long runs, far matches
    std::mt19937 rng(1234);
    for (size_t n : {0, 1, 2, 3, 4, 17, 18, 19, 238, 239, 300}) {
        Bytes b(n);
        for (auto& x : b) x = (uint8_t)rng();
        roundtrip(b);
    }
    roundtrip(Bytes(200000, 0));
    {
        Bytes b(300000);
        for (auto& x : b) x = (uint8_t)rng();
        for (size_t i = 0xC000; i + 64 < b.size(); i += 997)  // copies 0x4000..0xBFFF back: M4 matches
            std::copy_n(b.begin() + (i - 0x4000 - (i % 0x7fff)), 64, b.begin() + i);
        roundtrip(b);
    }
    {
        Bytes b;
        for (int i = 0; i < 20000; i++) {
            std::string w = "block" + std::to_string(rng() % 50) + (rng() % 3 ? " " : "\n");
            b.insert(b.end(), w.begin(), w.end());
        }
        roundtrip(b);
    }

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
