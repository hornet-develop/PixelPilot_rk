
#ifndef VIDEO_RESTREAMER_HPP
#define VIDEO_RESTREAMER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "encoder/stream_consumer.h"
#include "socket_handler.hpp"

// Restreams encoded video access units as RTP/H.265 (RFC 7798) to a UDP host:port or an abstract
// Unix datagram socket. Active from the moment the destination is opened, so the encoder runs
// whenever restreaming is enabled, regardless of whether DVR recording is in progress.
class VideoRestreamer : public StreamConsumer {
public:
    VideoRestreamer();
    ~VideoRestreamer() override;

    // Start restreaming to a UDP address or a filesystem Unix datagram socket bound by the receiver.
    // Log and return false if the destination is invalid or the socket cannot be created.
    bool open_udp(const std::string &address, uint16_t port);
    bool open_unix(const std::string &path);

    bool active() const override;
    void on_access_unit(const AccessUnit &au) override;
    void on_encoder_reset(int width, int height) override;
    void on_tick() override;

private:
    static void *cb_alloc(void *param, int bytes);
    static void  cb_free(void *param, void *packet);
    static int   cb_packet(void *param, const void *packet, int bytes, uint32_t timestamp, int flags);

    bool start_packer();   // shared initialization for both output transports
    void send_packet(const void *packet, int bytes);

    void *packer_ = nullptr;          // opaque librtp payload encoder
    std::unique_ptr<SocketHandler> socket_;
    std::string dest_desc_;           // human-readable destination for logging
    std::vector<uint8_t> pkt_;        // reusable packetizer buffer

    // Restreaming statistics. Access units indicate encoder output, while sent and dropped
    // packets indicate the status of RTP packetization and socket transmission.
    uint64_t access_units_ = 0;
    uint64_t packets_sent_ = 0;
    uint64_t packets_dropped_ = 0;
    uint64_t bytes_sent_ = 0;
    int64_t  last_stats_ms_ = 0;
    uint64_t last_stats_aus_ = 0;
    uint64_t last_stats_packets_ = 0;
    uint64_t last_stats_bytes_ = 0;
};

#endif // VIDEO_RESTREAMER_HPP
