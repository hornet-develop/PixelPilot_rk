#ifndef VIDEO_STREAMER_HPP
#define VIDEO_STREAMER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "encoder/stream_consumer.h"
#include "socket_handler.hpp"

// Streams the encoded access units out as RTP/H.265 (RFC 7798) to a UDP host:port or an abstract
// unix datagram socket. Active from the moment it opens, so the encoder runs whenever this is
// configured, with or without a recording in progress.
//
// A receiver needs no SDP: VPS/SPS/PPS ride in-band on every IDR (the encoder asks for one every
// KEYFRAME_INTERVAL_MS), so a late joiner can configure its decoder from the stream itself.
class VideoStreamer : public StreamConsumer {
public:
    VideoStreamer();
    ~VideoStreamer() override;

    // Start streaming to a UDP address, or to a filesystem unix datagram socket the receiver has
    // bound. Log and return false on a bad destination or if the socket cannot be created.
    bool open_udp(const std::string &address, uint16_t port);
    bool open_unix(const std::string &path);

    bool active() const override;
    void on_access_unit(const AccessUnit &au) override;
    void on_encoder_reset(int width, int height) override;
    void on_tick() override;

private:
    // librtp packer callbacks. Plain void*/int signatures, so no librtp type reaches this header.
    static void *cb_alloc(void *param, int bytes);
    static void  cb_free(void *param, void *packet);
    static int   cb_packet(void *param, const void *packet, int bytes, uint32_t timestamp, int flags);

    bool start_packer();   // shared tail of both open_*: create the RTP packetizer
    void send_packet(const void *packet, int bytes);

    void *packer_ = nullptr;          // opaque librtp payload encoder
    std::unique_ptr<SocketHandler> socket_;
    std::string dest_desc_;           // human-readable destination, for logging
    std::vector<uint8_t> pkt_;        // reused buffer handed to the packer

    // Throughput accounting. access_units_ counts what the encoder handed us and packets_sent_
    // what reached the socket, so the two together say where a silent stream broke: no access
    // units means nothing upstream is feeding us, access units with no packets means the
    // packetizer, packets with nothing received means the network.
    uint64_t access_units_ = 0;
    uint64_t packets_sent_ = 0;
    uint64_t packets_dropped_ = 0;
    uint64_t bytes_sent_ = 0;
    int64_t  last_stats_ms_ = 0;
    uint64_t last_stats_aus_ = 0;
    uint64_t last_stats_packets_ = 0;
    uint64_t last_stats_bytes_ = 0;
};

#endif
