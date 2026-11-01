/*
 * fb_payload.h — extract one partition image out of an Android payload.bin.
 *
 * Not an AOSP file: MeowArchMobile glue, backing the `flash-x` command.
 *
 *   fastboot-m flash-x <partition> <payload.bin> [<partition-in-payload>]
 *
 * payload.bin is the OTA container update_engine consumes: a "CrAU" header, a
 * protobuf DeltaArchiveManifest, and the partition data as a series of install
 * operations, each optionally xz- or bzip2-compressed. Extracting a partition
 * means replaying its REPLACE and ZERO operations into a temp file, which the
 * caller then flashes through the normal `flash` path — so sparse re-splitting,
 * logical-partition resize, vbmeta rewriting and the progress bar all come for
 * free.
 *
 * The manifest is decoded by a small hand-written protobuf reader instead of
 * libprotobuf: this build deliberately carries no protobuf dependency, and only
 * a handful of fields matter. The decompressors are AOSP's own
 * external/xz-embedded and external/bzip2, compiled in statically for Linux and
 * Windows alike (mingw has no cross liblzma/libbz2 packages, and using AOSP's
 * copies matches update_engine exactly).
 *
 * Only *full* payloads (minor_version == 0) can be extracted: a delta payload's
 * SOURCE_COPY / SOURCE_BSDIFF / PUFFDIFF / ZUCCHINI operations read the
 * partition data currently on the device, which fastboot cannot do.
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace fbpayload {

// Why an extraction failed. The caller maps these to localized messages;
// |a| and |b| carry the context those messages format.
enum class Error {
    kNone,
    kOpen,         // a = path
    kNotPayload,   // a = path, b = reason (bad magic / version / manifest)
    kDelta,        // a = partition name (delta payloads are unsupported)
    kNoPartition,  // a = requested name, b = ", "-joined names in the payload
    kOther,        // b = technical detail (already formatted, English)
};

struct Result {
    Error code = Error::kNone;
    std::string a, b;
    explicit operator bool() const { return code == Error::kNone; }
};

// The extracted image: an RAII temp file that unlinks itself when it goes out
// of scope, i.e. after the flash that consumed it.
class Image {
  public:
    Image() = default;
    ~Image();
    Image(Image&& o) noexcept;
    Image& operator=(Image&&) = delete;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    bool valid() const { return fd_ >= 0; }
    const std::string& path() const { return path_; }
    uint64_t size() const { return size_; }

    // Adopt the extracted temp file (extraction hands ownership over) and give
    // it back up again on failure. Extraction is the only producer.
    void Adopt(int fd, std::string path) {
        fd_ = fd;
        path_ = std::move(path);
        size_ = 0;
    }
    void Discard();
    void SetSize(uint64_t size) { size_ = size; }

  private:
    int fd_ = -1;
    std::string path_;
    uint64_t size_ = 0;
};

// Partition names in the manifest, in payload order.
bool ListPartitions(const std::string& path, std::vector<std::string>* out, Result* err);

// Extract |inner| from the payload at |path| into |out|. When |inner| is empty
// the partition matching |target| is used, with any slot suffix ("_a"/"_b")
// stripped — so `flash-x boot_a payload.bin` finds the payload's "boot".
//
// Progress is reported through |progress| (bytes of output image written, and
// its total) so the caller can drive the same bar the flash path uses; pass
// nullptr for none. Every operation blob's SHA-256 is verified when the
// manifest carries one, and the finished image is verified against
// new_partition_info.hash, so a truncated or corrupt payload is caught before
// anything is written to the device.
Result Extract(const std::string& path, const std::string& target, const std::string& inner,
               Image* out, void (*progress)(uint64_t done, uint64_t total, void* ud), void* ud);

}  // namespace fbpayload