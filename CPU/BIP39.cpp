#include "CPU/BIP39.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>

// Forward declaration for SHA-256 used in BIP39 checksum
static void SHA256(const uint8_t* data, size_t len, uint8_t out[32]);

namespace {

// ---------- SHA-512 ----------

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202ULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = ((uint64_t)block[i*8+0] << 56) | ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) | ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) | ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8)  | ((uint64_t)block[i*8+7] << 0);
    }
    for (int i = 16; i < 80; ++i) {
        uint64_t s0 = rotr64(w[i-15], 1) ^ rotr64(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = rotr64(w[i-2], 19) ^ rotr64(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint64_t a=state[0], b=state[1], c=state[2], d=state[3], e=state[4], f=state[5], g=state[6], h=state[7];
    for (int i = 0; i < 80; ++i) {
        uint64_t S1 = rotr64(e,14) ^ rotr64(e,18) ^ rotr64(e,41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t temp1 = h + S1 + ch + K512[i] + w[i];
        uint64_t S0 = rotr64(a,28) ^ rotr64(a,34) ^ rotr64(a,39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

} // namespace

namespace BIP39 {

void SHA512(const uint8_t* data, size_t len, uint8_t out[64]) {
    uint64_t state[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };
    uint8_t block[128];
    size_t rem = len;
    const uint8_t* p = data;
    while (rem >= 128) {
        sha512_transform(state, p);
        p += 128; rem -= 128;
    }
    // Padding
    size_t r = rem;
    memset(block, 0, 128);
    if (r) memcpy(block, p, r);
    block[r] = 0x80;
    if (r >= 112) {
        sha512_transform(state, block);
        memset(block, 0, 128);
    }
    uint64_t bitlen_hi = 0;
    uint64_t bitlen_lo = (uint64_t)len * 8ULL;
    // write big-endian 128-bit length
    block[112] = (uint8_t)(bitlen_hi >> 56);
    block[113] = (uint8_t)(bitlen_hi >> 48);
    block[114] = (uint8_t)(bitlen_hi >> 40);
    block[115] = (uint8_t)(bitlen_hi >> 32);
    block[116] = (uint8_t)(bitlen_hi >> 24);
    block[117] = (uint8_t)(bitlen_hi >> 16);
    block[118] = (uint8_t)(bitlen_hi >> 8);
    block[119] = (uint8_t)(bitlen_hi);
    block[120] = (uint8_t)(bitlen_lo >> 56);
    block[121] = (uint8_t)(bitlen_lo >> 48);
    block[122] = (uint8_t)(bitlen_lo >> 40);
    block[123] = (uint8_t)(bitlen_lo >> 32);
    block[124] = (uint8_t)(bitlen_lo >> 24);
    block[125] = (uint8_t)(bitlen_lo >> 16);
    block[126] = (uint8_t)(bitlen_lo >> 8);
    block[127] = (uint8_t)(bitlen_lo);
    sha512_transform(state, block);

    for (int i = 0; i < 8; ++i) {
        out[i*8+0] = (uint8_t)(state[i] >> 56);
        out[i*8+1] = (uint8_t)(state[i] >> 48);
        out[i*8+2] = (uint8_t)(state[i] >> 40);
        out[i*8+3] = (uint8_t)(state[i] >> 32);
        out[i*8+4] = (uint8_t)(state[i] >> 24);
        out[i*8+5] = (uint8_t)(state[i] >> 16);
        out[i*8+6] = (uint8_t)(state[i] >> 8);
        out[i*8+7] = (uint8_t)(state[i] >> 0);
    }
}

void HMAC_SHA512(const uint8_t* key, size_t keylen, const uint8_t* msg, size_t msglen, uint8_t out[64]) {
    const size_t blockSize = 128;
    uint8_t kopad[blockSize];
    uint8_t kipad[blockSize];
    uint8_t khash[64];
    if (keylen > blockSize) {
        SHA512(key, keylen, khash);
        key = khash; keylen = 64;
    }
    memset(kopad, 0, blockSize);
    memset(kipad, 0, blockSize);
    memcpy(kopad, key, keylen);
    memcpy(kipad, key, keylen);
    for (size_t i = 0; i < blockSize; ++i) {
        kopad[i] ^= 0x5c; kipad[i] ^= 0x36;
    }
    // inner = SHA512(kipad || msg)
    std::vector<uint8_t> buf;
    buf.reserve(blockSize + msglen);
    buf.insert(buf.end(), kipad, kipad + blockSize);
    buf.insert(buf.end(), msg, msg + msglen);
    uint8_t inner[64];
    SHA512(buf.data(), buf.size(), inner);
    // outer = SHA512(kopad || inner)
    uint8_t outerIn[blockSize + 64];
    memcpy(outerIn, kopad, blockSize);
    memcpy(outerIn + blockSize, inner, 64);
    SHA512(outerIn, blockSize + 64, out);
}

void PBKDF2_HMAC_SHA512(const std::string& mnemonic, const std::string& passphrase, uint8_t outSeed[64], int iterations) {
    std::string salt = std::string("mnemonic") + passphrase;
    // single block (dkLen=64 => 1 block)
    // U1 = PRF(P, S || INT_32_BE(1))
    std::vector<uint8_t> sbytes(salt.begin(), salt.end());
    uint8_t be1[4] = {0,0,0,1};
    sbytes.insert(sbytes.end(), be1, be1 + 4);
    uint8_t u[64];
    HMAC_SHA512(reinterpret_cast<const uint8_t*>(mnemonic.data()), mnemonic.size(), sbytes.data(), sbytes.size(), u);
    uint8_t t[64]; memcpy(t, u, 64);
    for (int i = 2; i <= iterations; ++i) {
        HMAC_SHA512(reinterpret_cast<const uint8_t*>(mnemonic.data()), mnemonic.size(), u, 64, u);
        for (int j = 0; j < 64; ++j) t[j] ^= u[j];
    }
    memcpy(outSeed, t, 64);
}

static void write32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)(v);
}

bool BIP32_MasterFromSeed(const uint8_t seed[64], Int &k_m, uint8_t chainCode[32]) {
    static const uint8_t key[] = {'B','i','t','c','o','i','n',' ','s','e','e','d'};
    uint8_t I[64];
    HMAC_SHA512(key, sizeof(key), seed, 64, I);
    // IL -> master private key, IR -> chain code
    k_m.Set32Bytes(I);
    memcpy(chainCode, I + 32, 32);
    // Check IL in [1, n-1]; we only check non-zero here (n-check omitted for simplicity as Mod ops wrap)
    return !k_m.IsZero();
}

bool BIP32_CKDPriv(const Int &k_par, const uint8_t chainCodePar[32], uint32_t index, Int &k_i, uint8_t chainCodeChild[32], Secp256K1 &secp) {
    uint8_t data[1 + 32 + 4];
    if (index & 0x80000000U) {
        // hardened: 0x00 || ser256(k_par) || ser32(i)
        data[0] = 0x00;
        uint8_t ser[32]; Int tmp = k_par; tmp.Get32Bytes(ser);
        memcpy(data + 1, ser, 32);
    } else {
        // non-hardened: serP(K_par) || ser32(i)
        Point P = secp.ComputePublicKey(const_cast<Int*>(&k_par));
        uint8_t x[32]; P.x.Get32Bytes(x);
        data[0] = (uint8_t)(0x02 + (P.y.IsOdd() ? 1 : 0));
        memcpy(data + 1, x, 32);
    }
    write32(data + 33, index);
    uint8_t I[64];
    HMAC_SHA512(chainCodePar, 32, data, sizeof(data), I);
    Int IL; IL.Set32Bytes(I);
    // k_i = (IL + k_par) mod n
    k_i.ModAddK1order(&IL, const_cast<Int*>(&k_par));
    memcpy(chainCodeChild, I + 32, 32);
    return !k_i.IsZero();
}

bool DerivePath(const Int &k_m, const uint8_t chainCodeM[32], const std::vector<uint32_t>& path, Int &k_out, uint8_t chainCodeOut[32], Secp256K1 &secp) {
    Int key = k_m; uint8_t c[32]; memcpy(c, chainCodeM, 32);
    for (uint32_t index : path) {
        Int child; uint8_t cc[32];
        if (!BIP32_CKDPriv(key, c, index, child, cc, secp)) return false;
        key = child; memcpy(c, cc, 32);
    }
    k_out = key; memcpy(chainCodeOut, c, 32);
    return true;
}

bool ParsePath(const std::string &pathStr, std::vector<uint32_t> &out) {
    out.clear();
    if (pathStr.empty()) return false;
    size_t pos = 0;
    if (pathStr[0] == 'm' || pathStr[0] == 'M') {
        if (pathStr.size() == 1) return true; // root
        if (pathStr[1] != '/') return false;
        pos = 2;
    }
    while (pos < pathStr.size()) {
        size_t next = pathStr.find('/', pos);
        std::string elem = pathStr.substr(pos, (next == std::string::npos ? pathStr.size() : next) - pos);
        if (elem.empty()) return false;
        bool hardened = false;
        if (elem.back() == '\'') { hardened = true; elem.pop_back(); }
        uint64_t v = 0; for (char ch : elem) { if (ch<'0'||ch>'9') return false; v = v*10 + (ch-'0'); if (v > 0x7FFFFFFFUL) return false; }
        uint32_t idx = (uint32_t)v; if (hardened) idx |= 0x80000000U;
        out.push_back(idx);
        if (next == std::string::npos) break; else pos = next + 1;
    }
    return true;
}

bool BuildPrivListFromMnemonics(const std::vector<std::string>& mnemonics,
                                const std::string& passphrase,
                                const std::vector<uint32_t>& basePath,
                                uint32_t rangeStart,
                                uint32_t rangeCount,
                                std::vector<uint8_t>& outPrivKeys) {
    outPrivKeys.clear();
    Secp256K1 secp; secp.Init();

    // Parallelize over mnemonics; collect per-thread buffers then merge
    #pragma omp parallel
    {
        std::vector<uint8_t> local;
        local.reserve(32 * ((mnemonics.size() * (size_t)rangeCount) / 8 + 1));
        #pragma omp for schedule(dynamic, 8)
        for (int idx = 0; idx < (int)mnemonics.size(); ++idx) {
            const std::string &mn = mnemonics[idx];
            uint8_t seed[64]; PBKDF2_HMAC_SHA512(mn, passphrase, seed, 2048);
            Int km; uint8_t cm[32]; if (!BIP32_MasterFromSeed(seed, km, cm)) continue;
            Int kbase; uint8_t cbase[32];
            if (!basePath.empty()) {
                std::vector<uint32_t> upToLeaf = basePath; if (!upToLeaf.empty()) upToLeaf.pop_back();
                if (!DerivePath(km, cm, upToLeaf, kbase, cbase, secp)) continue;
            } else { kbase = km; memcpy(cbase, cm, 32); }
            uint32_t leafTemplate = basePath.empty() ? 0 : basePath.back();
            for (uint32_t i = 0; i < rangeCount; ++i) {
                uint32_t leaf = (leafTemplate & 0x80000000U) | (rangeStart + i);
                Int kchild; uint8_t cchild[32];
                if (!BIP32_CKDPriv(kbase, cbase, leaf, kchild, cchild, secp)) continue;
                uint8_t ser[32]; kchild.Get32Bytes(ser);
                local.insert(local.end(), ser, ser + 32);
            }
        }
        #pragma omp critical
        {
            outPrivKeys.insert(outPrivKeys.end(), local.begin(), local.end());
        }
    }
    return !outPrivKeys.empty();
}

} // namespace BIP39

// ------- Optional wordlist helpers (checksum validation) -------
namespace BIP39 {

bool LoadWordlist(const std::string& path, std::vector<std::string>& wl) {
    wl.clear(); std::ifstream in(path.c_str()); if (!in) return false; std::string w; while (std::getline(in, w)) { if (!w.empty() && w.back()=='\r') w.pop_back(); if (!w.empty()) wl.push_back(w); } return !wl.empty();
}

static bool bits_to_bytes(const std::vector<int>& bits, std::vector<uint8_t>& out) {
    int nbits = (int)bits.size(); int nbytes = (nbits + 7) / 8; out.assign(nbytes, 0);
    for (int i = 0; i < nbits; ++i) { if (bits[i]) out[i>>3] |= (uint8_t)(1 << (7 - (i & 7))); }
    return true;
}

bool IsValidMnemonicWithWordlist(const std::vector<std::string>& words, const std::vector<std::string>& wl, const std::unordered_map<std::string,int>& index) {
    int n = (int)words.size(); if (n % 3 != 0) return false; if (!(n==12||n==15||n==18||n==21||n==24)) return false;
    std::vector<int> idx(n);
    for (int i=0;i<n;++i){ auto it=index.find(words[i]); if (it==index.end()) return false; idx[i]=it->second; if (idx[i]<0||idx[i]>= (int)wl.size()) return false; }
    int ENT = 32 * n / 3; int CS = ENT / 32; int totalBits = n * 11;
    std::vector<int> bits; bits.reserve(totalBits);
    for (int i=0;i<n;++i){ for (int b=10;b>=0;--b){ bits.push_back((idx[i]>>b)&1); } }
    std::vector<int> entBits(bits.begin(), bits.begin()+ENT);
    std::vector<int> csBits(bits.begin()+ENT, bits.end());
    std::vector<uint8_t> entropy; bits_to_bytes(entBits, entropy);
    uint8_t h256[32]; SHA256(entropy.data(), entropy.size(), h256);
    // Compute first CS bits of h256
    for (int i=0;i<CS; ++i){ int bit = (h256[0] >> (7 - i)) & 1; if (bit != csBits[i]) return false; }
    return true;
}

} // namespace BIP39
// ---- SHA-256 (for BIP39 checksum) ----
static const uint32_t K256[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static inline uint32_t rotr32(uint32_t x, int n){return (x>>n)|(x<<(32-n));}

static void sha256_transform(uint32_t s[8], const uint8_t block[64]){
  uint32_t w[64];
  for(int i=0;i<16;++i){ w[i]=(uint32_t)block[i*4]<<24 | (uint32_t)block[i*4+1]<<16 | (uint32_t)block[i*4+2]<<8 | (uint32_t)block[i*4+3]; }
  for(int i=16;i<64;++i){ uint32_t s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3); uint32_t s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1; }
  uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f=s[5],g=s[6],h=s[7];
  for(int i=0;i<64;++i){ uint32_t S1=rotr32(e,6)^rotr32(e,11)^rotr32(e,25); uint32_t ch=(e&f)^((~e)&g); uint32_t t1=h+S1+ch+K256[i]+w[i]; uint32_t S0=rotr32(a,2)^rotr32(a,13)^rotr32(a,22); uint32_t maj=(a&b)^(a&c)^(b&c); uint32_t t2=S0+maj; h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2; }
  s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e; s[5]+=f; s[6]+=g; s[7]+=h;
}

static void SHA256(const uint8_t* data, size_t len, uint8_t out[32]){
  uint32_t s[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint8_t block[64]; size_t rem=len; const uint8_t* p=data;
  while(rem>=64){ sha256_transform(s,p); p+=64; rem-=64; }
  size_t r=rem; memset(block,0,64); if(r) memcpy(block,p,r); block[r]=0x80; if(r>=56){ sha256_transform(s,block); memset(block,0,64); }
  uint64_t bitlen=(uint64_t)len*8ULL; for(int i=0;i<8;++i) block[63-i] = (uint8_t)(bitlen>>(i*8));
  sha256_transform(s,block);
  for(int i=0;i<8;++i){ out[i*4+0]=(uint8_t)(s[i]>>24); out[i*4+1]=(uint8_t)(s[i]>>16); out[i*4+2]=(uint8_t)(s[i]>>8); out[i*4+3]=(uint8_t)(s[i]); }
}
