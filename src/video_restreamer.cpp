
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "spdlog/spdlog.h"

#include "video_restreamer.hpp"
#include "rtp-payload.h"
#include "rtp-profile.h"

// Source timestamps are milliseconds; RTP video runs on a 90kHz clock.
static const int64_t MS_TO_90K = 90;

// Throttle for the send-failure warning. A destination with nothing listening can fail on every
// packet, which at ~1400 bytes per packet is hundreds per second.
static const uint64_t DROP_WARN_INTERVAL = 500;

// How often restreaming throughput is reported. on_tick() fires once per encoded frame - tens
// of times a second - so the interval is enforced here rather than by the caller.
static const int64_t STATS_INTERVAL_MS = 5000;

static int64_t monotonic_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

VideoRestreamer::VideoRestreamer() {}

VideoRestreamer::~VideoRestreamer() {
    if (packets_sent_ > 0 || packets_dropped_ > 0) {
        spdlog::info("[ Restreamer ] restream to {} ended: {} access units, {} packets sent "
                     "({:.1f} MB), {} dropped",
                     dest_desc_, access_units_, packets_sent_,
                     bytes_sent_ / (1024.0 * 1024.0), packets_dropped_);
    }
    if (packer_) {
        rtp_payload_encode_destroy(packer_);
        packer_ = nullptr;
    }
}

bool VideoRestreamer::active() const {
    return socket_ && packer_ != nullptr;
}

bool VideoRestreamer::start_packer() {
    // Randomise SSRC and the starting sequence number, as RFC 3550 asks, so two runs (or two
    // senders to the same receiver) are distinguishable.
    const uint32_t ssrc = (uint32_t)(((uint64_t)time(nullptr) << 16) ^ (uint64_t)getpid());
    const uint16_t seq  = (uint16_t)(ssrc & 0xFFFF);

    // The callbacks hold no per-instance state - `param` carries the VideoRestreamer - so one
    // shared handler is enough. Function-local so the private statics are in scope and librtp
    // stays out of the header; its address is stable for the life of the process, which is what
    // the packer requires.
    static struct rtp_payload_t handler = { cb_alloc, cb_free, cb_packet };

    packer_ = rtp_payload_encode_create(RTP_PAYLOAD_DYNAMIC, "H265", seq, ssrc, &handler, this);
    if (!packer_) {
        spdlog::error("[ Restreamer ] failed to create the H265 RTP packetizer");
        socket_.reset();
        return false;
    }

    spdlog::info("[ Restreamer ] restreaming H265 to {} (payload type {}, ssrc {:#x})",
                 dest_desc_, (int)RTP_PAYLOAD_DYNAMIC, ssrc);
    return true;
}

bool VideoRestreamer::open_udp(const std::string &address, uint16_t port) {
    auto sock = std::make_unique<SocketHandler>(address, (int)port, SocketHandler::Direction::Send);
    if (!sock->init_connection()) {
        return false;
    }
    socket_ = std::move(sock);
    dest_desc_ = address + ":" + std::to_string(port);
    return start_packer();
}

bool VideoRestreamer::open_unix(const std::string &path) {
    if (path.empty()) {
        spdlog::error("[ Restreamer ] empty unix socket path");
        return false;
    }

    auto sock = std::make_unique<SocketHandler>(path.c_str(), SocketHandler::Direction::Send,
                                                SocketHandler::UnixNamespace::Path);
    if (!sock->init_connection()) {
        return false;
    }
    socket_ = std::move(sock);
    dest_desc_ = path;
    return start_packer();
}

void VideoRestreamer::on_access_unit(const AccessUnit &au) {
    if (!active() || au.len <= 0) {
        return;
    }
    access_units_++;

    rtp_payload_encode_input(packer_, au.data, au.len, (uint32_t)(au.pts_ms * MS_TO_90K));
}

void VideoRestreamer::on_encoder_reset(int width, int height) {
    spdlog::info("[ Restreamer ] restream is now {}x{}", width, height);
}

void VideoRestreamer::on_tick() {
    if (!active()) {
        return;
    }
    const int64_t now = monotonic_ms();
    if (last_stats_ms_ == 0) {
        last_stats_ms_ = now;
        return;
    }
    const int64_t elapsed = now - last_stats_ms_;
    if (elapsed < STATS_INTERVAL_MS) {
        return;
    }

    const double secs = elapsed / 1000.0;
    const uint64_t d_aus   = access_units_ - last_stats_aus_;
    const uint64_t d_pkts  = packets_sent_ - last_stats_packets_;
    const uint64_t d_bytes = bytes_sent_ - last_stats_bytes_;

    spdlog::debug("[ Restreamer ] {} | {:.0f} au/s, {:.0f} pkt/s, {:.2f} Mbit/s | total {} sent, {} dropped",
                 dest_desc_, d_aus / secs, d_pkts / secs, (d_bytes * 8.0) / secs / 1e6,
                 packets_sent_, packets_dropped_);

    last_stats_ms_      = now;
    last_stats_aus_     = access_units_;
    last_stats_packets_ = packets_sent_;
    last_stats_bytes_   = bytes_sent_;
}

void VideoRestreamer::send_packet(const void *packet, int bytes) {
    const ssize_t n = socket_->send(packet, (size_t)bytes);
    if (n == (ssize_t)bytes) {
        if (packets_sent_ == 0) {
            spdlog::info("[ Restreamer ] first packet sent to {} ({} bytes)", dest_desc_, bytes);
        }
        packets_sent_++;
        bytes_sent_ += (uint64_t)bytes;
        return;
    }
    if (packets_dropped_ % DROP_WARN_INTERVAL == 0) {
        spdlog::warn("[ Restreamer ] dropping packets to {}: {} ({} dropped, {} sent)",
                     dest_desc_, strerror(errno), packets_dropped_ + 1, packets_sent_);
    }
    packets_dropped_++;
}

void *VideoRestreamer::cb_alloc(void *param, int bytes) {
    auto *self = (VideoRestreamer *)param;
    self->pkt_.resize((size_t)bytes);
    return self->pkt_.data();
}

void VideoRestreamer::cb_free(void *, void *) { }

int VideoRestreamer::cb_packet(void *param, const void *packet, int bytes, uint32_t, int) {
    ((VideoRestreamer *)param)->send_packet(packet, bytes);
    return 0;
}