/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (C) 2025 Dmytro S <dmytriysemenchuk@gmail.com>
 */

#ifndef SOCKET_HANDLER_H
#define SOCKET_HANDLER_H

#include <string>
#include <sys/socket.h>

class SocketHandler {
public:
    // Receive binds the address as our own and the caller reads from the fd; Send remembers it as
    // the peer address and the caller uses send(). The socket itself (AF_INET or AF_UNIX,
    // SOCK_DGRAM) is identical either way - only bind-vs-sendto differs.
    enum class Direction { Receive, Send };

    // Which AF_UNIX namespace a socket name means. An Abstract name (leading NUL, shown as @name)
    // lives only as long as the process; a Path is a real filesystem entry. Explicit rather than
    // inferred, so the same string can never silently mean two different things.
    enum class UnixNamespace { Abstract, Path };

    SocketHandler(const std::string& address, int port, Direction dir = Direction::Receive);
    SocketHandler(const char *sock, Direction dir = Direction::Receive,
                  UnixNamespace ns = UnixNamespace::Abstract);
    ~SocketHandler();

    bool init_local_socket();
    bool init_internet_socket();
    bool init_connection();
    bool is_socket_connected();
    int get_socket_fd();

    // Send one datagram to the configured destination. Non-blocking: a datagram that cannot be
    // queued right now is dropped rather than stalling the caller, which matters because senders
    // run on latency-critical threads. Returns bytes sent, or -1 with errno set. Send direction
    // only; returns -1/ENOTCONN otherwise.
    ssize_t send(const void *data, size_t len);
private:
    int m_socket;
    int m_port;
    
    std::string m_ip;
    std::string m_unix_socket;

    Direction m_direction = Direction::Receive;
    UnixNamespace m_unix_namespace = UnixNamespace::Abstract;
    sockaddr_storage m_dest{};
    socklen_t m_dest_len = 0;

    bool socket_connected = false;
};

#endif // SOCKET_HANDLER_H

