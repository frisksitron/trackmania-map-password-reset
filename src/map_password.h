// Inspect, reset or change the editor password of a Trackmania (2020) .Map.Gbx.
//
// The password lives in the skippable body chunk 0x03043029 of CGameCtnChallenge:
//
//     byte[16] hashed_password   all zero -> no password
//     uint32   crc               crc32("0x" + HEX128(hashed_password) + "???" + MapUid)
//
// HEX128 is the 16 bytes read as a little-endian 128-bit integer, printed as 32
// uppercase hex digits (i.e. the bytes reversed).
//
// On load the game replaces a hash whose CRC doesn't match with 01 00 .. 00:
// locked, and no password matches. Zeroing the hash without fixing the CRC
// therefore locks a map for good instead of unlocking it.
//
// hashed_password:
//
//     s    = utf8("\uFEFF" + password)     // BOM prefixed
//     n    = len(s)
//     salt = random uint32
//     key  = le32(salt ^ 0xACA7686C) le32(n ^ 0x31415926) le32(n ^ 0xACA7686C) le32(salt ^ 0x31415926)
//     hash = le32(salt) + HMAC_MD5(key, s + "\0")[4:16]
//     empty password -> 16 zero bytes
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace tmpw {

using Bytes = std::vector<uint8_t>;
using Hash = std::array<uint8_t, 16>;

constexpr uint32_t CHUNK_PASSWORD = 0x03043029;
constexpr uint32_t CHUNK_MAP_INFO = 0x0304301F;  // starts with the map Ident (uid, collection, author)

inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
inline void put32(Bytes& b, uint32_t v) { uint8_t t[4]; wr32(t, v); b.insert(b.end(), t, t + 4); }

// ------------------------------------------------------------------ crypto

class Md5 {
public:
    void update(const uint8_t* p, size_t n) {
        len_ += n;
        while (n--) {
            buf_[fill_++] = *p++;
            if (fill_ == 64) { block(buf_); fill_ = 0; }
        }
    }
    Hash finish() {
        uint64_t bits = len_ * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (fill_ != 56) update(&pad, 1);
        for (int i = 0; i < 8; i++) buf_[56 + i] = (uint8_t)(bits >> (8 * i));
        block(buf_);
        Hash out;
        wr32(&out[0], s_[0]); wr32(&out[4], s_[1]); wr32(&out[8], s_[2]); wr32(&out[12], s_[3]);
        return out;
    }

private:
    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
        uint32_t M[16];
        for (int i = 0; i < 16; i++) M[i] = rd32(p + 4 * i);
        uint32_t a = s_[0], b = s_[1], c = s_[2], d = s_[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            switch (i / 16) {
            case 0: f = (b & c) | (~b & d); g = i; break;
            case 1: f = (d & b) | (~d & c); g = (5 * i + 1) % 16; break;
            case 2: f = b ^ c ^ d; g = (3 * i + 5) % 16; break;
            default: f = c ^ (b | ~d); g = (7 * i) % 16; break;
            }
            f += a + K[i] + M[g];
            int r = R[i / 16 * 4 + i % 4];
            a = d; d = c; c = b;
            b += (f << r) | (f >> (32 - r));
        }
        s_[0] += a; s_[1] += b; s_[2] += c; s_[3] += d;
    }

    uint32_t s_[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint8_t buf_[64];
    size_t fill_ = 0;
    uint64_t len_ = 0;
};

inline Hash hmac_md5(const uint8_t* key, size_t klen, const uint8_t* msg, size_t mlen) {
    uint8_t k[64] = {};
    if (klen > 64) {
        Md5 m; m.update(key, klen);
        Hash kh = m.finish();
        std::copy(kh.begin(), kh.end(), k);
    } else {
        std::copy(key, key + klen, k);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Md5 inner; inner.update(ipad, 64); inner.update(msg, mlen);
    Hash ih = inner.finish();
    Md5 outer; outer.update(opad, 64); outer.update(ih.data(), ih.size());
    return outer.finish();
}

inline uint32_t crc32(const std::string& s) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFF;
    for (unsigned char ch : s) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

inline std::string to_hex(const uint8_t* p, size_t n, bool upper = false) {
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) { s += digits[p[i] >> 4]; s += digits[p[i] & 15]; }
    return s;
}

inline bool is_empty(const Hash& h) {
    return std::all_of(h.begin(), h.end(), [](uint8_t b) { return b == 0; });
}

inline uint32_t password_crc(const Hash& h, const std::string& uid) {
    Hash rev;
    std::reverse_copy(h.begin(), h.end(), rev.begin());
    return crc32("0x" + to_hex(rev.data(), rev.size(), true) + "???" + uid);
}

inline uint32_t random_salt() {
    std::random_device rd;
    return rd() ^ (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

// password is UTF-8
inline Hash hash_password(const std::string& password, uint32_t salt) {
    Hash out{};
    if (password.empty()) return out;
    std::string s = "\xEF\xBB\xBF" + password;
    uint32_t n = (uint32_t)s.size();
    uint8_t key[16];
    wr32(key + 0, salt ^ 0xACA7686C);
    wr32(key + 4, n ^ 0x31415926);
    wr32(key + 8, n ^ 0xACA7686C);
    wr32(key + 12, salt ^ 0x31415926);
    Hash digest = hmac_md5(key, sizeof key, (const uint8_t*)s.c_str(), s.size() + 1);  // includes the NUL
    wr32(&out[0], salt);
    std::copy(digest.begin() + 4, digest.end(), out.begin() + 4);
    return out;
}

inline Hash hash_password(const std::string& password) { return hash_password(password, random_salt()); }

inline bool check_password(const std::string& password, const Hash& h) {
    if (is_empty(h)) return password.empty();
    return hash_password(password, rd32(h.data())) == h;
}

// ------------------------------------------------------------------- LZO1X

inline Bytes lzo1x_decompress(const Bytes& src, size_t out_len) {
    Bytes dst;
    dst.reserve(out_len);
    size_t ip = 0;
    auto byte = [&](size_t i) -> uint32_t {
        if (i >= src.size()) throw std::runtime_error("LZO: truncated input");
        return src[i];
    };
    auto ext_len = [&](size_t base) {
        size_t t = 0;
        while (byte(ip) == 0) { t += 255; ip++; }
        t += base + byte(ip++);
        return t;
    };
    auto literals = [&](size_t n) {
        if (ip + n > src.size() || dst.size() + n > out_len) throw std::runtime_error("LZO: corrupt literal run");
        dst.insert(dst.end(), src.begin() + ip, src.begin() + ip + n);
        ip += n;
    };
    auto match = [&](size_t back, size_t n) {
        if (back == 0 || back > dst.size() || dst.size() + n > out_len) throw std::runtime_error("LZO: corrupt match");
        size_t m = dst.size() - back;
        for (size_t i = 0; i < n; i++) { uint8_t c = dst[m + i]; dst.push_back(c); }
    };

    int state = 0;  // 0: after match w/o trailing literals, 1-3: trailing literals, 4: after literal run
    if (byte(0) > 17) {
        size_t t = src[0] - 17;
        ip = 1;
        literals(t);
        state = t >= 4 ? 4 : (int)t;
    }
    for (;;) {
        uint32_t t = byte(ip++);
        size_t back, n;
        uint32_t tl;
        if (t >= 64) {
            back = 1 + ((t >> 2) & 7) + (byte(ip++) << 3);
            n = (t >> 5) + 1;
            tl = t & 3;
        } else if (t >= 32) {
            n = t & 31;
            if (n == 0) n = ext_len(31);
            n += 2;
            uint32_t d = byte(ip) | byte(ip + 1) << 8;
            ip += 2;
            back = 1 + (d >> 2);
            tl = d & 3;
        } else if (t >= 16) {
            n = t & 7;
            if (n == 0) n = ext_len(7);
            n += 2;
            uint32_t d = byte(ip) | byte(ip + 1) << 8;
            ip += 2;
            back = ((t & 8) << 11) + (d >> 2);
            if (back == 0) break;  // end of stream
            back += 0x4000;
            tl = d & 3;
        } else if (state == 0) {
            n = t;
            if (n == 0) n = ext_len(15);
            literals(n + 3);
            state = 4;
            continue;
        } else if (state == 4) {
            back = 0x801 + (t >> 2) + (byte(ip++) << 2);
            n = 3;
            tl = t & 3;
        } else {
            back = 1 + (t >> 2) + (byte(ip++) << 2);
            n = 2;
            tl = t & 3;
        }
        match(back, n);
        literals(tl);
        state = (int)tl;
    }
    if (dst.size() != out_len) throw std::runtime_error("LZO: size mismatch");
    return dst;
}

inline void lzo_ext(Bytes& out, size_t n) {
    while (n > 255) { out.push_back(0); n -= 255; }
    out.push_back((uint8_t)n);
}

// Greedy LZO1X compressor using only literal runs, M3 and M4 matches.
inline Bytes lzo1x_compress(const Bytes& data) {
    const size_t n = data.size();
    const uint8_t* d = data.data();
    Bytes out;
    out.reserve(n / 2 + 64);
    std::vector<int64_t> table(1 << 16, -1);
    auto h3 = [&](size_t i) { return (uint32_t)((d[i] | d[i + 1] << 8 | d[i + 2] << 16) * 2654435761u) >> 16; };
    size_t lit_start = 0, i = 0;
    int64_t last_tl = -1;  // byte holding the previous match's trailing-literal bits; <0: none

    auto flush = [&](size_t end) {
        size_t L = end - lit_start;
        if (L == 0) return;
        if (last_tl < 0) {  // start of stream
            if (L <= 238) out.push_back((uint8_t)(17 + L));
            else { out.push_back(0); lzo_ext(out, L - 18); }
        } else if (L <= 3) {
            out[last_tl] |= (uint8_t)L;
        } else if (L <= 18) {
            out.push_back((uint8_t)(L - 3));
        } else {
            out.push_back(0);
            lzo_ext(out, L - 18);
        }
        out.insert(out.end(), d + lit_start, d + end);
        last_tl = -2;  // next item must be a match or EOF
    };

    while (i + 3 <= n) {
        uint32_t k = h3(i);
        int64_t cand = table[k];
        table[k] = (int64_t)i;
        if (cand < 0 || i - cand > 0xBFFF || std::memcmp(d + cand, d + i, 3) != 0) { i++; continue; }
        size_t len = 3;
        while (i + len < n && d[cand + len] == d[i + len] && len < 0x10000) len++;
        size_t dist = i - (size_t)cand;
        flush(i);
        uint32_t dd;
        if (dist <= 0x4000) {
            if (len - 2 <= 31) out.push_back((uint8_t)(32 | (len - 2)));
            else { out.push_back(32); lzo_ext(out, len - 2 - 31); }
            dd = (uint32_t)(dist - 1) << 2;
        } else {
            dist -= 0x4000;
            uint8_t hi = dist & 0x4000 ? 8 : 0;
            if (len - 2 <= 7) out.push_back((uint8_t)(16 | hi | (len - 2)));
            else { out.push_back(16 | hi); lzo_ext(out, len - 2 - 7); }
            dd = (uint32_t)(dist & 0x3FFF) << 2;
        }
        last_tl = (int64_t)out.size();
        out.push_back((uint8_t)dd);
        out.push_back((uint8_t)(dd >> 8));
        for (size_t j = i + 1; j < std::min(i + len, n - 2); j++) table[h3(j)] = (int64_t)j;
        i += len;
        lit_start = i;
    }
    flush(n);
    out.insert(out.end(), {0x11, 0x00, 0x00});
    return out;
}

// --------------------------------------------------------------------- GBX

class MapGbx {
public:
    explicit MapGbx(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open file");
        raw_.assign(std::istreambuf_iterator<char>(f), {});
        const Bytes& d = raw_;
        if (d.size() < 17 || std::memcmp(d.data(), "GBX", 3) != 0 || std::memcmp(&d[5], "BUC", 3) != 0)
            throw std::runtime_error("expected a binary GBX with compressed body");
        uint32_t class_id = rd32(&d[9]), user_size = rd32(&d[13]);
        if (class_id != 0x03043000) throw std::runtime_error("not a map");
        size_t p = 17 + (size_t)user_size;
        if (p + 16 > d.size()) throw std::runtime_error("truncated header");
        if (rd32(&d[p + 4]) != 0) throw std::runtime_error("maps with external references are not supported");
        body_off_ = p + 8;
        uint32_t usize = rd32(&d[body_off_]), csize = rd32(&d[body_off_ + 4]);
        if (body_off_ + 8 + (size_t)csize > d.size() || usize > (1u << 28)) throw std::runtime_error("bad body size");
        Bytes comp(d.begin() + body_off_ + 8, d.begin() + body_off_ + 8 + csize);
        tail_.assign(d.begin() + body_off_ + 8 + csize, d.end());
        body_ = lzo1x_decompress(comp, usize);
        pw_off_ = find_chunk(CHUNK_PASSWORD, 20);
        uid_ = read_uid();
    }

    const std::string& uid() const { return uid_; }
    Hash hashed_password() const { Hash h; std::copy_n(&body_[pw_off_], 16, h.begin()); return h; }
    uint32_t crc() const { return rd32(&body_[pw_off_ + 16]); }
    bool crc_valid() const { return password_crc(hashed_password(), uid_) == crc(); }

    // the hash the game actually uses after loading
    Hash effective_password() const {
        if (crc_valid()) return hashed_password();
        Hash h{};
        h[0] = 1;
        return h;
    }

    void set_hashed_password(const Hash& h) {
        std::copy(h.begin(), h.end(), &body_[pw_off_]);
        wr32(&body_[pw_off_ + 16], password_crc(h, uid_));
    }

    void save(const std::filesystem::path& path) const {
        Bytes comp = lzo1x_compress(body_);
        if (lzo1x_decompress(comp, body_.size()) != body_) throw std::runtime_error("LZO round-trip failed");
        Bytes out(raw_.begin(), raw_.begin() + body_off_);
        put32(out, (uint32_t)body_.size());
        put32(out, (uint32_t)comp.size());
        out.insert(out.end(), comp.begin(), comp.end());
        out.insert(out.end(), tail_.begin(), tail_.end());
        // write next to the target, then swap in, so a failed save never leaves a half-written map
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write((const char*)out.data(), (std::streamsize)out.size());
            if (!f) throw std::runtime_error("cannot write file");
        }
        std::filesystem::rename(tmp, path);
    }

private:
    size_t find_chunk(uint32_t id, uint32_t size) const {
        uint8_t needle[12];
        wr32(needle, id);
        std::memcpy(needle + 4, "PIKS", 4);
        wr32(needle + 8, size);
        auto it = std::search(body_.begin(), body_.end(), needle, needle + 12);
        if (it == body_.end() || std::search(it + 1, body_.end(), needle, needle + 12) != body_.end())
            throw std::runtime_error("password chunk not found exactly once");
        return (size_t)(it - body_.begin()) + 12;
    }

    std::string read_uid() const {
        // 0x0304301F begins with an Ident: lookback-string uid, collection, author
        uint8_t id[4];
        wr32(id, CHUNK_MAP_INFO);
        auto it = std::search(body_.begin(), body_.end(), id, id + 4);
        size_t p = (size_t)(it - body_.begin()) + 4;
        auto need = [&](size_t n) { if (p + n > body_.size()) throw std::runtime_error("map info chunk not found"); };
        need(12);
        if (rd32(&body_[p]) == 3) p += 4;  // lookback-string version, only present if not emitted earlier
        need(8);
        uint32_t idx = rd32(&body_[p]);
        if ((idx & 0xC0000000) != 0x40000000 && (idx & 0xC0000000) != 0x80000000)
            throw std::runtime_error("unexpected uid encoding");
        uint32_t n = rd32(&body_[p + 4]);
        p += 8;
        need(n);
        return std::string(body_.begin() + p, body_.begin() + p + n);
    }

    Bytes raw_, body_, tail_;
    size_t body_off_ = 0, pw_off_ = 0;
    std::string uid_;
};

}  // namespace tmpw
