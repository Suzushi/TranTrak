#include "trackheader/udp_pose_sink.h"

#include <cstdio>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace trackheader {

namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
#endif

void close_socket(Socket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

}  // namespace

struct UdpPoseSink::Impl
{
    std::string host;
    std::uint16_t port = 0;
    Socket socket = kInvalidSocket;
    sockaddr_storage destination{};
    int destination_length = 0;
#ifdef _WIN32
    bool wsa_started = false;
#endif
};

UdpPoseSink::UdpPoseSink(std::string host, std::uint16_t port)
    : impl_(new Impl{std::move(host), port})
{
}

UdpPoseSink::~UdpPoseSink()
{
    close();
    delete impl_;
}

bool UdpPoseSink::open()
{
    close();
    if (impl_->host.empty() || impl_->port == 0)
        return false;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "udp: WSAStartup failed\n");
        return false;
    }
    impl_->wsa_started = true;
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    const std::string service = std::to_string(impl_->port);
    addrinfo* addresses = nullptr;
    const int result = getaddrinfo(impl_->host.c_str(), service.c_str(),
                                   &hints, &addresses);
    if (result != 0) {
#ifdef _WIN32
        std::fprintf(stderr, "udp: can't resolve '%s': error %d\n",
                     impl_->host.c_str(), result);
#else
        std::fprintf(stderr, "udp: can't resolve '%s': %s\n",
                     impl_->host.c_str(), gai_strerror(result));
#endif
        close();
        return false;
    }

    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const Socket socket = ::socket(address->ai_family, address->ai_socktype,
                                       address->ai_protocol);
        if (socket == kInvalidSocket)
            continue;

        std::memcpy(&impl_->destination, address->ai_addr,
                    static_cast<size_t>(address->ai_addrlen));
        impl_->destination_length = static_cast<int>(address->ai_addrlen);
        impl_->socket = socket;
        break;
    }
    freeaddrinfo(addresses);

    if (impl_->socket == kInvalidSocket) {
        std::fprintf(stderr, "udp: can't create socket for '%s:%u'\n",
                     impl_->host.c_str(), static_cast<unsigned>(impl_->port));
        close();
        return false;
    }
    return true;
}

void UdpPoseSink::close()
{
    if (impl_->socket != kInvalidSocket) {
        close_socket(impl_->socket);
        impl_->socket = kInvalidSocket;
    }
#ifdef _WIN32
    if (impl_->wsa_started) {
        WSACleanup();
        impl_->wsa_started = false;
    }
#endif
}

bool UdpPoseSink::is_open() const
{
    return impl_->socket != kInvalidSocket;
}

std::array<double, 6> UdpPoseSink::encode_packet(const Pose& pose)
{
    // OpenTrack's UDP convention is translation in centimeters followed by
    // yaw, pitch, roll in degrees. Preserve the legacy axis conversion.
    return {
        -pose.translation_m[1] * 100.0,
        -pose.translation_m[2] * 100.0,
        -pose.translation_m[0] * 100.0,
        pose.rotation_deg[0],
        pose.rotation_deg[1],
        -pose.rotation_deg[2],
    };
}

void UdpPoseSink::send(const Pose& pose, std::int64_t)
{
    if (!is_open())
        return;

    const auto packet = encode_packet(pose);
    (void)sendto(impl_->socket,
                 reinterpret_cast<const char*>(packet.data()),
                 static_cast<int>(sizeof(packet)),
                 0,
                 reinterpret_cast<const sockaddr*>(&impl_->destination),
                 impl_->destination_length);
}

}  // namespace trackheader
