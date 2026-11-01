/*
 * fb_payload.cpp — see fb_payload.h. MeowArchMobile glue, not an AOSP file.
 *
 * Layout of an Android payload.bin (update_engine's DeltaArchiveManifest):
 *
 *   offset 0   "CrAU"                       4 bytes, big endian magic
 *   offset 4   major version                u64 BE (must be 2)
 *   offset 12  manifest length              u64 BE
 *   offset 20  manifest signature length    u32 BE
 *   offset 24  manifest (protobuf)          manifest_len bytes
 *              manifest signature           manifest_sig_len bytes
 *              data section                 <- op.data_offset is relative here
 *
 * The protobuf is parsed by hand: only the handful of fields a full payload
 * needs are read, everything else is skipped by wire type. That keeps this
 * build free of libprotobuf (which it deliberately does not link).
 *
 * Operations relevant to a *full* payload (minor_version == 0):
 *   REPLACE(0)     blob is the raw image bytes for dst_extents
 *   REPLACE_BZ(1)  blob is bzip2-compressed
 *   REPLACE_XZ(8)  blob is xz-compressed
 *   ZERO(6)        dst_extents read as zeros
 *   DISCARD(7)     dst_extents are undefined (zeros are a valid rendering)
 * Everything else (MOVE, BSDIFF, SOURCE_COPY, SOURCE_BSDIFF, PUFFDIFF,
 * BROTLI_BSDIFF, ZUCCHINI, LZ4DIFF_*) needs the partition as it currently
 * exists on the device, which fastboot cannot read — those only appear in
 * delta payloads, which are rejected up front.
 *
 * Blob data is streamed in 1 MiB pieces rather than buffered: a super
 * partition's op can be hundreds of megabytes, and the Windows build is 32-bit
 * (i686), where one contiguous allocation that size is a coin flip.
 *
 * The dst_extents of one op are filled in order by its (decompressed) blob, and
 * full-payload ops have disjoint destinations, so a single sequential writer
 * over the extent list is enough.
 */
#include "fb_payload.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <android-base/file.h>
#include <android-base/unique_fd.h>
#include <bzlib.h>
// SHA-256 for the manifest's own integrity checks: the real OpenSSL on Linux
// and shim/win_compat/openssl/sha.h (a 3-function shim over libavb, the same
// one liblp uses) on Windows — one API, identical digests. The low-level
// SHA256_Init/Update/Final trio is what that shim exports, so use it (and
// silence OpenSSL 3's deprecation notice) rather than EVP, which the shim does
// not provide.
#ifndef OPENSSL_SUPPRESS_DEPRECATED
#define OPENSSL_SUPPRESS_DEPRECATED
#endif
#include <openssl/sha.h>
#include <xz.h>

namespace fbpayload {
namespace {

// ---------------------------------------------------------------- constants

constexpr char kMagic[4] = {'C', 'r', 'A', 'U'};
constexpr uint64_t kMajorVersion = 2;
constexpr size_t kHeaderSize = 4 + 8 + 8 + 4;  // magic, major, manifest_len, sig_len
constexpr uint32_t kDefaultBlockSize = 4096;

// Streaming granularity. 1 MiB matches the progress bar's transport chunking,
// so extraction and flashing report at the same cadence.
constexpr size_t kChunk = 1024 * 1024;

// Sets the temp image's exact length. Deliberately not plain ftruncate: on the
// 32-bit Windows build (mingw i686) off_t is 32 bits unless
// _FILE_OFFSET_BITS=64 is defined, which fastboot_glue does not define, so a
// `super` image over 2 GiB would be silently truncated. ftruncate64 is declared
// unconditionally by mingw and is the same call _FILE_OFFSET_BITS=64 would have
// assembled ftruncate into.
int TruncateTo(int fd, uint64_t length) {
#ifdef _WIN32
    return ftruncate64(fd, static_cast<off64_t>(length));
#else
    return ftruncate(fd, static_cast<off64_t>(length));
#endif
}

// Sanity bounds for values read out of the file. Real payloads stay far below
// these; the point is that a corrupt manifest fails with a message instead of
// making us allocate or ftruncate something absurd.
constexpr uint64_t kMaxBlob = 4ull << 30;      // one op's stored blob
constexpr uint64_t kMaxImage = 64ull << 30;    // one partition image (64 GiB)
constexpr uint64_t kMaxBlocks = 1ull << 32;    // 16 TiB at 4 KiB blocks

// InstallOperation::Type (update_metadata.proto).
enum OpType : uint32_t {
    kReplace = 0,
    kReplaceBz = 1,
    kMove = 2,
    kBsdiff = 3,
    kSourceCopy = 4,
    kSourceBsdiff = 5,
    kZero = 6,
    kDiscard = 7,
    kReplaceXz = 8,
    kPuffdiff = 9,
    kBrotliBsdiff = 10,
    kZucchini = 11,
    kLz4diffBsdiff = 12,
    kLz4diffPuffdiff = 13,
};

// DeltaArchiveManifest field numbers that matter.
enum ManifestField : uint32_t {
    kMfBlockSize = 3,
    kMfMinorVersion = 12,
    kMfPartitions = 13,
};
// PartitionUpdate field numbers that matter.
enum PartitionField : uint32_t {
    kPfName = 1,
    kPfNewInfo = 7,
    kPfOperations = 8,
};
// PartitionInfo field numbers.
enum InfoField : uint32_t {
    kIfSize = 1,
    kIfHash = 2,
};
// InstallOperation field numbers.
enum OpField : uint32_t {
    kOfType = 1,
    kOfDataOffset = 2,
    kOfDataLength = 3,
    kOfDstExtents = 6,
    kOfDstLength = 7,
    kOfDataHash = 8,
};
// Extent field numbers.
enum ExtentField : uint32_t {
    kEfStartBlock = 1,
    kEfNumBlocks = 2,
};

Result Err(Error code, std::string a = "", std::string b = "") {
    Result r;
    r.code = code;
    r.a = std::move(a);
    r.b = std::move(b);
    return r;
}

Result Other(const std::string& detail) { return Err(Error::kOther, "", detail); }

// ------------------------------------------------------- protobuf wire reader

// Minimal forward-only protobuf cursor. Unknown fields are skipped by wire
// type, which is what makes this tolerant of manifests newer than the fields
// decoded here.
class Cursor {
  public:
    Cursor(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}

    bool eof() const { return p_ >= end_; }
    size_t remaining() const { return static_cast<size_t>(end_ - p_); }

    bool ReadVarint(uint64_t* out) {
        uint64_t v = 0;
        int shift = 0;
        while (p_ < end_) {
            uint8_t b = *p_++;
            if (shift > 63) return false;
            v |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80)) {
                *out = v;
                return true;
            }
            shift += 7;
        }
        return false;
    }

    bool ReadTag(uint32_t* field, uint32_t* wire) {
        uint64_t key;
        if (!ReadVarint(&key)) return false;
        *wire = static_cast<uint32_t>(key & 0x7);
        *field = static_cast<uint32_t>(key >> 3);
        return *field != 0;
    }

    // Length-delimited field: hands back a pointer into the buffer (no copy).
    bool ReadBytes(const uint8_t** data, size_t* len) {
        uint64_t n;
        if (!ReadVarint(&n)) return false;
        if (n > remaining()) return false;
        *data = p_;
        *len = static_cast<size_t>(n);
        p_ += n;
        return true;
    }

    bool ReadString(std::string* out) {
        const uint8_t* d;
        size_t n;
        if (!ReadBytes(&d, &n)) return false;
        out->assign(reinterpret_cast<const char*>(d), n);
        return true;
    }

    bool Skip(uint32_t wire) {
        switch (wire) {
            case 0: {  // varint
                uint64_t v;
                return ReadVarint(&v);
            }
            case 1:  // fixed64
                if (remaining() < 8) return false;
                p_ += 8;
                return true;
            case 2: {  // length-delimited
                uint64_t n;
                if (!ReadVarint(&n) || n > remaining()) return false;
                p_ += n;
                return true;
            }
            case 5:  // fixed32
                if (remaining() < 4) return false;
                p_ += 4;
                return true;
            default:  // 3, 4 (groups) and anything else: unsupported
                return false;
        }
    }

  private:
    const uint8_t* p_;
    const uint8_t* end_;
};

// ------------------------------------------------------------------ manifest

struct Extent {
    uint64_t start_block = 0;
    uint64_t num_blocks = 0;
};

struct Op {
    uint32_t type = 0;
    uint64_t data_offset = 0;
    uint64_t data_length = 0;
    uint64_t dst_length = 0;
    std::vector<Extent> dst;
    std::string data_hash;  // 32 raw bytes when present
};

struct Partition {
    std::string name;
    uint64_t new_size = 0;
    std::string new_hash;  // 32 raw bytes when present
    std::vector<Op> ops;
};

struct Manifest {
    uint32_t block_size = kDefaultBlockSize;
    uint32_t minor_version = 0;
    std::vector<Partition> partitions;
};

bool IsDeltaOp(uint32_t type) {
    switch (type) {
        case kReplace:
        case kReplaceBz:
        case kReplaceXz:
        case kZero:
        case kDiscard:
            return false;
        default:
            return true;  // MOVE/BSDIFF/SOURCE_*/PUFFDIFF/ZUCCHINI/LZ4DIFF_*
    }
}

bool ParseExtent(const uint8_t* data, size_t len, Extent* out) {
    Cursor c(data, len);
    while (!c.eof()) {
        uint32_t field, wire;
        if (!c.ReadTag(&field, &wire)) return false;
        if (wire == 0 && field == kEfStartBlock) {
            uint64_t v;
            if (!c.ReadVarint(&v)) return false;
            out->start_block = v;
        } else if (wire == 0 && field == kEfNumBlocks) {
            uint64_t v;
            if (!c.ReadVarint(&v)) return false;
            out->num_blocks = v;
        } else if (!c.Skip(wire)) {
            return false;
        }
    }
    return true;
}

bool ParsePartitionInfo(const uint8_t* data, size_t len, uint64_t* size, std::string* hash) {
    Cursor c(data, len);
    while (!c.eof()) {
        uint32_t field, wire;
        if (!c.ReadTag(&field, &wire)) return false;
        if (wire == 0 && field == kIfSize) {
            uint64_t v;
            if (!c.ReadVarint(&v)) return false;
            *size = v;
        } else if (wire == 2 && field == kIfHash) {
            if (!c.ReadString(hash)) return false;
        } else if (!c.Skip(wire)) {
            return false;
        }
    }
    return true;
}

bool ParseOperation(const uint8_t* data, size_t len, Op* out) {
    Cursor c(data, len);
    while (!c.eof()) {
        uint32_t field, wire;
        if (!c.ReadTag(&field, &wire)) return false;
        if (wire == 0 && field == kOfType) {
            uint64_t v;
            if (!c.ReadVarint(&v)) return false;
            out->type = static_cast<uint32_t>(v);
        } else if (wire == 0 && field == kOfDataOffset) {
            if (!c.ReadVarint(&out->data_offset)) return false;
        } else if (wire == 0 && field == kOfDataLength) {
            if (!c.ReadVarint(&out->data_length)) return false;
        } else if (wire == 0 && field == kOfDstLength) {
            if (!c.ReadVarint(&out->dst_length)) return false;
        } else if (wire == 2 && field == kOfDstExtents) {
            const uint8_t* d;
            size_t n;
            if (!c.ReadBytes(&d, &n)) return false;
            Extent e;
            if (!ParseExtent(d, n, &e)) return false;
            out->dst.push_back(e);
        } else if (wire == 2 && field == kOfDataHash) {
            if (!c.ReadString(&out->data_hash)) return false;
        } else if (!c.Skip(wire)) {
            return false;
        }
    }
    return true;
}

bool ParsePartitionUpdate(const uint8_t* data, size_t len, Partition* out) {
    Cursor c(data, len);
    while (!c.eof()) {
        uint32_t field, wire;
        if (!c.ReadTag(&field, &wire)) return false;
        if (wire == 2 && field == kPfName) {
            if (!c.ReadString(&out->name)) return false;
        } else if (wire == 2 && field == kPfNewInfo) {
            const uint8_t* d;
            size_t n;
            if (!c.ReadBytes(&d, &n)) return false;
            if (!ParsePartitionInfo(d, n, &out->new_size, &out->new_hash)) return false;
        } else if (wire == 2 && field == kPfOperations) {
            const uint8_t* d;
            size_t n;
            if (!c.ReadBytes(&d, &n)) return false;
            Op op;
            if (!ParseOperation(d, n, &op)) return false;
            out->ops.push_back(std::move(op));
        } else if (!c.Skip(wire)) {
            return false;
        }
    }
    return !out->name.empty();
}

bool ParseManifest(const uint8_t* data, size_t len, Manifest* out, std::string* err) {
    Cursor c(data, len);
    while (!c.eof()) {
        uint32_t field, wire;
        if (!c.ReadTag(&field, &wire)) {
            *err = "malformed manifest";
            return false;
        }
        if (wire == 0 && field == kMfBlockSize) {
            uint64_t v;
            if (!c.ReadVarint(&v) || v == 0 || v > (1u << 24)) {
                *err = "bad block_size";
                return false;
            }
            out->block_size = static_cast<uint32_t>(v);
        } else if (wire == 0 && field == kMfMinorVersion) {
            uint64_t v;
            if (!c.ReadVarint(&v) || v > UINT32_MAX) {
                *err = "bad minor_version";
                return false;
            }
            out->minor_version = static_cast<uint32_t>(v);
        } else if (wire == 2 && field == kMfPartitions) {
            const uint8_t* d;
            size_t n;
            if (!c.ReadBytes(&d, &n)) {
                *err = "malformed manifest";
                return false;
            }
            Partition p;
            if (!ParsePartitionUpdate(d, n, &p)) {
                *err = "malformed partition entry";
                return false;
            }
            out->partitions.push_back(std::move(p));
        } else if (!c.Skip(wire)) {
            *err = "unsupported manifest field";
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------- header + read

uint64_t Be64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

uint32_t Be32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v = (v << 8) | p[i];
    return v;
}

struct Header {
    uint64_t manifest_len = 0;
    uint32_t manifest_sig_len = 0;
    uint64_t data_base = 0;  // absolute offset of the data section
};

// Reads and validates the CrAU header, then parses the manifest.
Result ReadManifest(android::base::borrowed_fd fd, Manifest* manifest, Header* hdr) {
    uint8_t head[kHeaderSize];
    if (!android::base::ReadFullyAtOffset(fd, head, sizeof(head), 0)) {
        return Err(Error::kOpen);
    }
    if (memcmp(head, kMagic, sizeof(kMagic)) != 0) {
        return Err(Error::kNotPayload, "", "bad magic (not a payload.bin)");
    }
    uint64_t major = Be64(head + 4);
    if (major != kMajorVersion) {
        return Err(Error::kNotPayload, "",
                   "unsupported major version " + std::to_string(major) + " (want 2)");
    }
    hdr->manifest_len = Be64(head + 12);
    hdr->manifest_sig_len = Be32(head + 20);
    if (hdr->manifest_len == 0 || hdr->manifest_len > (512u << 20)) {
        return Err(Error::kNotPayload, "", "implausible manifest length");
    }
    hdr->data_base = kHeaderSize + hdr->manifest_len + hdr->manifest_sig_len;

    std::vector<uint8_t> blob(static_cast<size_t>(hdr->manifest_len));
    if (!android::base::ReadFullyAtOffset(fd, blob.data(), blob.size(),
                                          static_cast<off64_t>(kHeaderSize))) {
        return Err(Error::kNotPayload, "", "truncated manifest");
    }
    std::string err;
    if (!ParseManifest(blob.data(), blob.size(), manifest, &err)) {
        return Err(Error::kNotPayload, "", err);
    }
    if (manifest->partitions.empty()) {
        return Err(Error::kNotPayload, "", "manifest lists no partitions");
    }
    return Result{};
}

// -------------------------------------------------------------- temp image

// Writes a decompressed/raw stream into a list of destination extents, in
// order. The output is a fresh temp file, so regions nobody wrote read as
// zeros — exactly what ZERO and DISCARD want, which therefore only have to make
// sure the file is long enough.
class ExtentWriter {
  public:
    ExtentWriter(int fd, uint64_t block_size, const std::vector<Extent>& dst)
        : fd_(fd), block_size_(block_size), dst_(dst) {}

    // Sequential write across the extent list. False when the stream overruns
    // the destinations, i.e. a corrupt manifest.
    bool Write(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        while (len > 0) {
            if (!EnsureRoom()) return false;
            const Extent& e = dst_[idx_];
            uint64_t cap = e.num_blocks * block_size_;
            size_t n = static_cast<size_t>(std::min<uint64_t>(cap - off_, len));
            off64_t at = static_cast<off64_t>(e.start_block * block_size_ + off_);
            if (!android::base::WriteFullyAtOffset(fd_, p, n, at)) return false;
            p += n;
            len -= n;
            off_ += n;
            written_ += n;
            if (off_ >= cap) Advance();
        }
        return true;
    }

    // ZERO/DISCARD: materialize the region as a hole (reads back as zeros).
    bool Skip(uint64_t bytes) {
        while (bytes > 0) {
            if (!EnsureRoom()) return false;
            const Extent& e = dst_[idx_];
            uint64_t cap = e.num_blocks * block_size_;
            uint64_t n = std::min(cap - off_, bytes);
            // One byte at the end of the range extends the file; the gap before
            // it stays a hole. Full payloads never write a range twice, so
            // nothing already written is clobbered.
            off64_t last = static_cast<off64_t>(e.start_block * block_size_ + off_ + n - 1);
            char zero = 0;
            if (!android::base::WriteFullyAtOffset(fd_, &zero, 1, last)) return false;
            bytes -= n;
            written_ += n;
            off_ += n;
            if (off_ >= cap) Advance();
        }
        return true;
    }

    uint64_t written() const { return written_; }

  private:
    // True while the cursor sits inside a valid extent with space left.
    bool EnsureRoom() {
        while (idx_ < dst_.size()) {
            if (off_ < dst_[idx_].num_blocks * block_size_) return true;
            Advance();
        }
        return false;
    }
    void Advance() {
        idx_++;
        off_ = 0;
    }

    int fd_;
    uint64_t block_size_;
    const std::vector<Extent>& dst_;
    size_t idx_ = 0;
    uint64_t off_ = 0;
    uint64_t written_ = 0;
};

// ------------------------------------------------------------- decompressors

struct Sha256 {
    SHA256_CTX ctx;
    Sha256() { SHA256_Init(&ctx); }
    void Update(const void* d, size_t n) { SHA256_Update(&ctx, d, n); }
    std::string Final() {
        unsigned char dg[SHA256_DIGEST_LENGTH];
        SHA256_Final(dg, &ctx);
        return std::string(reinterpret_cast<char*>(dg), SHA256_DIGEST_LENGTH);
    }
};

// xz-embedded builds its CRC tables at run time and requires xz_crc32_init()
// before *any* xz_* call — the kernel gets them from its own CRC module, the
// standalone build does not. Skipping this is not a subtle bug: every stream
// fails CRC and xz_dec_run returns XZ_DATA_ERROR, which is exactly what the
// first flash-x test caught.
void XzInitOnce() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        xz_crc32_init();
#ifdef XZ_USE_CRC64
        xz_crc64_init();
#endif
    });
}

using Sink = bool (*)(void* ud, const void* data, size_t len);

struct SinkCtx {
    ExtentWriter* writer;
    Sha256* out_hash;  // hash of the produced image, when the manifest has one
    uint64_t* produced;
};

bool WriteSink(void* ud, const void* data, size_t len) {
    SinkCtx* c = static_cast<SinkCtx*>(ud);
    if (c->out_hash) c->out_hash->Update(data, len);
    if (!c->writer->Write(data, len)) return false;
    *c->produced += len;
    return true;
}

// Reads one op's blob out of the payload in pieces, hashing the bytes exactly
// as stored so the manifest's data_sha256_hash can be checked afterwards.
class BlobSource {
  public:
    BlobSource(android::base::borrowed_fd fd, uint64_t base, uint64_t offset, uint64_t length)
        : fd_(fd), at_(static_cast<off64_t>(base + offset)), left_(length) {}

    // Fills buf with up to |want| bytes. Returns the count, 0 at end of blob.
    size_t Read(uint8_t* buf, size_t want) {
        size_t n = static_cast<size_t>(std::min<uint64_t>(want, left_));
        if (n == 0) return 0;
        if (!android::base::ReadFullyAtOffset(fd_, buf, n, at_)) {
            truncated_ = true;
            return 0;
        }
        at_ += static_cast<off64_t>(n);
        left_ -= n;
        hash_.Update(buf, n);
        return n;
    }

    bool truncated() const { return truncated_; }
    std::string hash() { return hash_.Final(); }

  private:
    android::base::borrowed_fd fd_;
    off64_t at_;
    uint64_t left_;
    Sha256 hash_;
    bool truncated_ = false;
};

// xz, streamed: refill the input buffer whenever the decoder drains it.
// XZ_DYNALLOC sizes the dictionary from the stream itself; UINT32_MAX means no
// artificial cap (the blob is bounded by data_length).
bool InflateXz(BlobSource* src, uint64_t want, Sink sink, void* ud, std::string* err) {
    XzInitOnce();
    std::unique_ptr<struct xz_dec, decltype(&xz_dec_end)> dec(
        xz_dec_init(XZ_DYNALLOC, UINT32_MAX), xz_dec_end);
    if (!dec) {
        *err = "xz_dec_init failed";
        return false;
    }
    std::vector<uint8_t> in(kChunk), out(kChunk);
    struct xz_buf b {};
    b.in = in.data();
    b.in_pos = 0;
    b.in_size = 0;
    b.out = out.data();
    b.out_pos = 0;
    b.out_size = out.size();

    uint64_t produced = 0;
    bool eof = false;
    while (true) {
        if (b.in_pos == b.in_size && !eof) {
            b.in_size = src->Read(in.data(), in.size());
            b.in_pos = 0;
            if (b.in_size == 0) eof = true;
        }
        enum xz_ret ret = xz_dec_run(dec.get(), &b);
        if (b.out_pos > 0) {
            produced += b.out_pos;
            if (want && produced > want) {
                *err = "xz output exceeds the expected size";
                return false;
            }
            if (!sink(ud, out.data(), b.out_pos)) {
                *err = "write failed";
                return false;
            }
            b.out_pos = 0;
        }
        if (ret == XZ_STREAM_END) break;
        // XZ_UNSUPPORTED_CHECK means the stream carries an integrity check
        // xz-embedded cannot verify (SHA-256, which is what releasetools emits).
        // Decoding continues; the manifest's own data_sha256_hash is checked by
        // the caller, and the whole image against new_partition_info.hash.
        if (ret != XZ_OK && ret != XZ_UNSUPPORTED_CHECK) {
            *err = "xz decode failed (code " + std::to_string((int)ret) + ")";
            return false;
        }
        if (eof && b.in_pos == b.in_size) {
            *err = "xz stream ended prematurely";
            return false;
        }
    }
    if (src->truncated()) {
        *err = "truncated payload: cannot read operation data";
        return false;
    }
    if (want && produced != want) {
        *err = "xz output " + std::to_string(produced) + " bytes, expected " +
               std::to_string(want);
        return false;
    }
    return true;
}

// bzip2, streamed the same way.
bool InflateBz2(BlobSource* src, uint64_t want, Sink sink, void* ud, std::string* err) {
    bz_stream strm;
    memset(&strm, 0, sizeof(strm));
    strm.bzalloc = nullptr;
    strm.bzfree = nullptr;
    strm.opaque = nullptr;
    // small=0: the faster decoder; its dictionary is at most 900 KiB.
    int rc = BZ2_bzDecompressInit(&strm, 0, 0);
    if (rc != BZ_OK) {
        *err = "BZ2_bzDecompressInit failed (" + std::to_string(rc) + ")";
        return false;
    }
    std::vector<uint8_t> in(kChunk);
    std::vector<char> out(kChunk);
    strm.next_in = reinterpret_cast<char*>(in.data());
    strm.avail_in = 0;

    uint64_t produced = 0;
    bool eof = false;
    bool ok = true;
    while (true) {
        if (strm.avail_in == 0 && !eof) {
            size_t n = src->Read(in.data(), in.size());
            if (n == 0) {
                eof = true;
            } else {
                strm.next_in = reinterpret_cast<char*>(in.data());
                strm.avail_in = static_cast<unsigned int>(n);
            }
        }
        strm.next_out = out.data();
        strm.avail_out = static_cast<unsigned int>(out.size());
        rc = BZ2_bzDecompress(&strm);
        size_t got = out.size() - strm.avail_out;
        if (got > 0) {
            produced += got;
            if (want && produced > want) {
                *err = "bzip2 output exceeds the expected size";
                ok = false;
                break;
            }
            if (!sink(ud, out.data(), got)) {
                *err = "write failed";
                ok = false;
                break;
            }
        }
        if (rc == BZ_STREAM_END) break;
        if (rc != BZ_OK) {
            *err = "bzip2 decode failed (" + std::to_string(rc) + ")";
            ok = false;
            break;
        }
        if (eof && strm.avail_in == 0 && got == 0) {
            *err = "bzip2 stream ended prematurely";
            ok = false;
            break;
        }
    }
    BZ2_bzDecompressEnd(&strm);
    if (!ok) return false;
    if (src->truncated()) {
        *err = "truncated payload: cannot read operation data";
        return false;
    }
    if (want && produced != want) {
        *err = "bzip2 output " + std::to_string(produced) + " bytes, expected " +
               std::to_string(want);
        return false;
    }
    return true;
}

// REPLACE: the blob is already raw image bytes.
bool CopyRaw(BlobSource* src, uint64_t want, Sink sink, void* ud, std::string* err) {
    std::vector<uint8_t> in(kChunk);
    uint64_t produced = 0;
    while (true) {
        size_t n = src->Read(in.data(), in.size());
        if (src->truncated()) {
            *err = "truncated payload: cannot read operation data";
            return false;
        }
        if (n == 0) break;
        produced += n;
        if (want && produced > want) {
            *err = "operation data exceeds the expected size";
            return false;
        }
        if (!sink(ud, in.data(), n)) {
            *err = "write failed";
            return false;
        }
    }
    if (want && produced != want) {
        *err = "operation data is " + std::to_string(produced) + " bytes, expected " +
               std::to_string(want);
        return false;
    }
    return true;
}

// --------------------------------------------------------------- extraction

std::string JoinNames(const std::vector<Partition>& parts) {
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += ", ";
        out += p.name;
    }
    return out;
}

// Strips an A/B slot suffix so `flash-x boot_a payload.bin` finds "boot".
std::string StripSlot(const std::string& name) {
    if (name.size() > 2) {
        std::string tail = name.substr(name.size() - 2);
        if (tail == "_a" || tail == "_b") return name.substr(0, name.size() - 2);
    }
    return name;
}

const Partition* FindPartition(const Manifest& m, const std::string& target,
                               const std::string& inner) {
    if (!inner.empty()) {
        for (const auto& p : m.partitions) {
            if (p.name == inner) return &p;
        }
        return nullptr;
    }
    // No explicit name: try the target verbatim, then with any slot suffix off.
    for (const auto& p : m.partitions) {
        if (p.name == target) return &p;
    }
    std::string bare = StripSlot(target);
    if (bare != target) {
        for (const auto& p : m.partitions) {
            if (p.name == bare) return &p;
        }
    }
    return nullptr;
}

// Destroys a partially written image and hands back a failure.
Result Fail(Image* out, const Result& r) {
    out->Discard();
    return r;
}

Result Fail(Image* out, const std::string& detail) { return Fail(out, Other(detail)); }

}  // namespace

// ------------------------------------------------------------------- Image

Image::~Image() { Discard(); }

void Image::Discard() {
    if (fd_ >= 0) {
        close(fd_);
        unlink(path_.c_str());
    }
    fd_ = -1;
    path_.clear();
    size_ = 0;
}

Image::Image(Image&& o) noexcept : fd_(o.fd_), path_(std::move(o.path_)), size_(o.size_) {
    o.fd_ = -1;
    o.size_ = 0;
}

// ------------------------------------------------------------------ public

bool ListPartitions(const std::string& path, std::vector<std::string>* out, Result* err) {
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_BINARY)));
    if (fd < 0) {
        if (err) *err = Err(Error::kOpen, path);
        return false;
    }
    Manifest m;
    Header h;
    Result r = ReadManifest(fd, &m, &h);
    if (!r) {
        if (err) {
            r.a = path;
            *err = r;
        }
        return false;
    }
    out->clear();
    for (const auto& p : m.partitions) out->push_back(p.name);
    return true;
}

Result Extract(const std::string& path, const std::string& target, const std::string& inner,
               Image* out, void (*progress)(uint64_t, uint64_t, void*), void* ud) {
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_BINARY)));
    if (fd < 0) return Err(Error::kOpen, path);

    Manifest m;
    Header h;
    Result r = ReadManifest(fd, &m, &h);
    if (!r) {
        r.a = path;
        return r;
    }

    const Partition* part = FindPartition(m, target, inner);
    if (!part) {
        return Err(Error::kNoPartition, inner.empty() ? target : inner, JoinNames(m.partitions));
    }

    if (m.minor_version != 0) {
        // A delta payload's SOURCE_*/BSDIFF/PUFFDIFF/ZUCCHINI operations read
        // the partition as it exists on the device; fastboot cannot do that.
        return Err(Error::kDelta, part->name);
    }
    for (const auto& op : part->ops) {
        if (IsDeltaOp(op.type)) return Err(Error::kDelta, part->name);
    }

    const uint64_t block_size = m.block_size;

    // Output size: the manifest's new_partition_info.size when present, else
    // the end of the last destination extent. Both are attacker-controlled
    // values read straight out of a file, so every step is overflow-checked and
    // bounded — a corrupt manifest must produce an error, never a multi-exabyte
    // ftruncate().
    uint64_t total = part->new_size;
    if (total == 0) {
        for (const auto& op : part->ops) {
            for (const auto& e : op.dst) {
                if (e.num_blocks == 0) continue;
                if (e.start_block > kMaxBlocks || e.num_blocks > kMaxBlocks ||
                    e.start_block + e.num_blocks > kMaxBlocks) {
                    return Other("implausible extent in '" + part->name +
                                 "' (payload.bin is corrupt)");
                }
                total = std::max(total, (e.start_block + e.num_blocks) * block_size);
            }
        }
    }
    if (total == 0) return Other("partition '" + part->name + "' has no data to extract");
    if (total > kMaxImage) {
        return Other("partition '" + part->name + "' declares " + std::to_string(total) +
                     " bytes, larger than any real image (payload.bin is corrupt)");
    }

    // Temp file for the extracted image. libbase's TemporaryFile handles both
    // platforms (it carries its own mkstemp shim for Windows); DoNotRemove() +
    // release() hand ownership to Image, which unlinks after the flash that
    // consumed it.
    ::TemporaryFile tmp;
    tmp.DoNotRemove();
    int out_fd = tmp.release();
    if (out_fd < 0) return Other(std::string("cannot create temp file: ") + strerror(errno));
    out->Adopt(out_fd, tmp.path);

    uint64_t done = 0;
    for (const auto& op : part->ops) {
        if (op.dst.empty()) continue;  // nothing to place
        if (op.data_length > kMaxBlob) {
            return Fail(out, "implausible operation data length " +
                                 std::to_string(op.data_length) + " in '" + part->name + "'");
        }

        ExtentWriter writer(out_fd, block_size, op.dst);

        // Hash of the produced image, verified against new_partition_info at
        // the end. Only worth computing when the manifest carries one.
        Sha256 out_hash;
        Sha256* out_hash_p = part->new_hash.empty() ? nullptr : &out_hash;
        uint64_t produced = 0;
        SinkCtx sink_ctx{&writer, out_hash_p, &produced};

        if (op.type == kZero || op.type == kDiscard) {
            uint64_t bytes = op.dst_length;
            if (bytes == 0) {
                for (const auto& e : op.dst) {
                    if (e.num_blocks > kMaxBlocks) {
                        return Fail(out, "implausible extent in '" + part->name +
                                             "' (payload.bin is corrupt)");
                    }
                    bytes += e.num_blocks * block_size;
                    if (bytes > kMaxImage) {
                        return Fail(out, "zero/discard range too large in '" + part->name +
                                             "' (payload.bin is corrupt)");
                    }
                }
            }
            if (!writer.Skip(bytes)) {
                return Fail(out, "destination extents overflow (corrupt manifest)");
            }
        } else {
            BlobSource src(fd, h.data_base, op.data_offset, op.data_length);

            // Expected decoded size. dst_length is what the proto documents for
            // BSDIFF ops, and delta_generator omits it for REPLACE*; fall back
            // to the sum of the destination extents, which is authoritative for
            // every op type. Without a bound, a blob that ends early would
            // decode "successfully" into a short image and only the manifest
            // hash (when present) would notice.
            uint64_t want = op.dst_length;
            if (want == 0) {
                for (const auto& e : op.dst) want += e.num_blocks * block_size;
            }
            std::string derr;
            bool ok;
            if (op.type == kReplace) {
                ok = CopyRaw(&src, want, WriteSink, &sink_ctx, &derr);
            } else if (op.type == kReplaceXz) {
                ok = InflateXz(&src, want, WriteSink, &sink_ctx, &derr);
            } else if (op.type == kReplaceBz) {
                ok = InflateBz2(&src, want, WriteSink, &sink_ctx, &derr);
            } else {
                ok = false;
                derr = "unsupported operation type " + std::to_string(op.type);
            }
            if (!ok) {
                return Fail(out, "extracting '" + part->name + "': " + derr);
            }
            // Per-operation integrity check. The manifest hashes the blob
            // exactly as stored — compressed, for REPLACE_BZ/REPLACE_XZ.
            if (op.data_hash.size() == SHA256_DIGEST_LENGTH && src.hash() != op.data_hash) {
                return Fail(out, "SHA-256 mismatch in payload data for '" + part->name +
                                     "' (payload.bin is corrupt or truncated)");
            }
        }

        done += produced ? produced : writer.written();
        if (progress) progress(std::min(done, total), total, ud);
    }

    // Size the file exactly: cut a too-long tail, or extend with a hole when
    // the extents stop short of the declared image size.
    if (TruncateTo(out_fd, total) != 0) {
        return Fail(out, std::string("cannot size temp image: ") + strerror(errno));
    }

    // Whole-image check — what update_engine's filesystem verifier does.
    if (!part->new_hash.empty()) {
        Sha256 whole;
        std::vector<uint8_t> buf(kChunk);
        uint64_t left = total;
        off64_t at = 0;
        while (left > 0) {
            size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
            if (!android::base::ReadFullyAtOffset(android::base::borrowed_fd(out_fd), buf.data(), n,
                                                  at)) {
                return Fail(out, "cannot re-read the extracted image for verification");
            }
            whole.Update(buf.data(), n);
            left -= n;
            at += static_cast<off64_t>(n);
        }
        if (whole.Final() != part->new_hash) {
            return Fail(out, "extracted image for '" + part->name +
                                 "' does not match the manifest hash (payload.bin is corrupt)");
        }
    }

    out->SetSize(total);
    if (progress) progress(total, total, ud);
    return Result{};
}

}  // namespace fbpayload
