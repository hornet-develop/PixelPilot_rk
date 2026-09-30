#ifndef DVR_TS_WRITER_H
#define DVR_TS_WRITER_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Muxes H265 access units into an MPEG-TS file. Passive and synchronous: every call writes on the
// caller's thread, so the caller must be one that can afford to block on storage. The DVR owns a
// worker thread for exactly that and drives this class from it; nothing here may be called from the
// encoder thread.
//
// TS is append-only: there is no index and no finalization step, so a file cut short by a power
// loss or a wedged card stays playable up to the last flushed byte. That is the reason this
// container replaced MP4 - an MP4 without its moov is unplayable without a repair pass.
class TsWriter {
public:
    TsWriter();
    ~TsWriter();

    bool open(const std::string &path);
    bool begin_video(int width, int height);            // CODEC H265; resets the per-file mux state
    // Mux and write one complete Annex-B access unit (the MPP encoder emits exactly one per packet,
    // with VPS/SPS/PPS prepended on every IDR). False on a write failure.
    bool write_nal(const uint8_t *data, int len, int duration_90k);
    bool close();                                       // flushes and closes the file
    bool is_open() const { return file != nullptr; }

    // Bytes written so far = the current on-disk file size. Used to enforce the FAT32 4GB per-file
    // limit without a per-frame fstat.
    uint64_t size() const { return file_size_bytes; }

    // Consecutive failed writes (disk full / I/O error); the DVR reads this to fail-stop.
    uint32_t consecutive_write_failures() const { return write_fail_streak; }

private:
    bool sync_now();          // fflush + fdatasync; false if either failed
    void sync_if_due();       // sync once SYNC_BYTES or SYNC_INTERVAL_MS has passed (writer thread)
    static void sync_dir_of(const std::string &path); // make a freshly created file's dirent durable

    // --- muxing ---
    bool mux_access_unit(const uint8_t *data, int len, int duration_90k);
    void emit_psi(std::vector<uint8_t> &out);
    // Wraps one PSI section in a TS packet (pointer_field + section + CRC32, stuffed to 188).
    void emit_section(std::vector<uint8_t> &out, uint16_t pid, uint8_t &cc,
                      const uint8_t *section, size_t section_len);
    void emit_pes(std::vector<uint8_t> &out, const uint8_t *au, size_t au_len,
                  int64_t pts, int64_t pcr, bool keyframe);
    bool write_block(const uint8_t *data, size_t len);   // sequential append

    FILE *file = nullptr;
    bool  video_started_ = false;
    uint64_t file_size_bytes = 0;
    uint32_t discard_count_ = 0;                    // access units dropped with no muxer started
    uint32_t write_fail_count = 0;                  // warn throttle
    uint32_t write_fail_streak = 0;                 // consecutive failures; the DVR fail-stops on it
    uint64_t bytes_since_sync_ = 0;
    int64_t  last_sync_ms_ = 0;

    // Per-file mux state, reset by begin_video() so every file is independently playable.
    int64_t media_ticks_ = 0;        // 90kHz ticks elapsed; also the next access unit's PCR
    int64_t last_psi_ticks_ = 0;
    bool    psi_pending_ = true;
    uint8_t cc_video_ = 0;
    uint8_t cc_pat_ = 0;
    uint8_t cc_pmt_ = 0;
    std::vector<uint8_t> pkt_buf_;   // reused per AU so each frame costs one fwrite
};

#endif
