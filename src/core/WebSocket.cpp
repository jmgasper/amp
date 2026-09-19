#include "WebSocket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace tasamp {

namespace {

std::string Base64(const unsigned char* data, size_t size)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < size) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(table[(n >> 6) & 63]);
        out.push_back(table[n & 63]);
        i += 3;
    }
    if (i < size) {
        uint32_t n = data[i] << 16;
        if (i + 1 < size)
            n |= data[i + 1] << 8;
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(i + 1 < size ? table[(n >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

uint32_t RandomMask()
{
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng();
}

} // namespace

WebSocket::WebSocket() {}

WebSocket::~WebSocket()
{
    Close();
}

bool WebSocket::Connect(const std::string& host, int port, const std::string& path,
    const std::vector<std::string>& extraHeaders, int timeoutSeconds)
{
    Close();
    fError.clear();
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* result = nullptr;
    std::string portText = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portText.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        fError = "cannot resolve host " + host;
        return false;
    }
    int sock = -1;
    for (struct addrinfo* ai = result; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0)
            continue;
        struct timeval tv;
        tv.tv_sec = timeoutSeconds;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(result);
    if (sock < 0) {
        fError = "connection to " + host + ":" + portText + " failed";
        return false;
    }
    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    fSocket = sock;

    unsigned char keyBytes[16];
    for (unsigned char& b : keyBytes)
        b = (unsigned char)RandomMask();
    std::string key = Base64(keyBytes, sizeof(keyBytes));
    std::string request = "GET " + path + " HTTP/1.1\r\n"
        "Host: " + host + ":" + portText + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: TasAmp/0.1.0\r\n";
    for (const std::string& h : extraHeaders)
        request += h + "\r\n";
    request += "\r\n";
    if (!WriteAll(request.data(), request.size())) {
        fError = "sending upgrade request failed";
        Close();
        return false;
    }
    // Read the HTTP response headers byte-wise until the blank line.
    std::string response;
    char c;
    while (response.size() < 65536) {
        if (!ReadExact(&c, 1, timeoutSeconds * 1000)) {
            fError = "no upgrade response";
            Close();
            return false;
        }
        response.push_back(c);
        if (response.size() >= 4 && response.compare(response.size() - 4, 4, "\r\n\r\n") == 0)
            break;
    }
    if (response.compare(0, 12, "HTTP/1.1 101") != 0 && response.compare(0, 12, "HTTP/1.0 101") != 0) {
        fError = "upgrade rejected: " + response.substr(0, response.find("\r\n"));
        Close();
        return false;
    }
    return true;
}

void WebSocket::Close()
{
    if (fSocket >= 0) {
        {
            std::lock_guard<std::mutex> lock(fSendMutex);
            uint8_t code[2] = {0x03, 0xE8};
            SendFrame(0x8, code, 2);
        }
        shutdown(fSocket, SHUT_RDWR);
        close(fSocket);
        fSocket = -1;
    }
    fFragmentBuffer.clear();
    fReadAhead.clear();
}

bool WebSocket::WriteAll(const void* buffer, size_t size)
{
    const char* p = static_cast<const char*>(buffer);
    while (size > 0) {
        ssize_t n = send(fSocket, p, size, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool WebSocket::ReadExact(void* buffer, size_t size, int timeoutMs)
{
    char* p = static_cast<char*>(buffer);
    if (!fReadAhead.empty()) {
        size_t take = std::min(size, fReadAhead.size());
        memcpy(p, fReadAhead.data(), take);
        fReadAhead.erase(0, take);
        p += take;
        size -= take;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 1000000 : timeoutMs);
    while (size > 0) {
        if (fSocket < 0)
            return false;
        struct pollfd pfd;
        pfd.fd = fSocket;
        pfd.events = POLLIN;
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining < 0)
            return false;
        int rc = poll(&pfd, 1, timeoutMs < 0 ? 1000 : std::min(remaining, 1000));
        if (rc == 0) {
            if (timeoutMs < 0)
                continue;
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            continue;
        }
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        ssize_t n = recv(fSocket, p, size, 0);
        if (n == 0)
            return false;
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool WebSocket::SendFrame(uint8_t opcode, const void* data, size_t size)
{
    if (fSocket < 0)
        return false;
    std::string frame;
    frame.push_back((char)(0x80 | opcode));
    if (size < 126)
        frame.push_back((char)(0x80 | size));
    else if (size < 65536) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((char)((size >> 8) & 0xFF));
        frame.push_back((char)(size & 0xFF));
    } else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; i--)
            frame.push_back((char)((uint64_t)size >> (i * 8)) & 0xFF);
    }
    uint32_t mask = RandomMask();
    unsigned char maskBytes[4] = {(unsigned char)(mask >> 24), (unsigned char)(mask >> 16),
        (unsigned char)(mask >> 8), (unsigned char)mask};
    frame.append((const char*)maskBytes, 4);
    size_t start = frame.size();
    frame.resize(start + size);
    const unsigned char* src = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i++)
        frame[start + i] = (char)(src[i] ^ maskBytes[i & 3]);
    return WriteAll(frame.data(), frame.size());
}

bool WebSocket::SendText(const std::string& text)
{
    std::lock_guard<std::mutex> lock(fSendMutex);
    return SendFrame(0x1, text.data(), text.size());
}

bool WebSocket::SendBinary(const void* data, size_t size)
{
    std::lock_guard<std::mutex> lock(fSendMutex);
    return SendFrame(0x2, data, size);
}

bool WebSocket::SendPong(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(fSendMutex);
    return SendFrame(0xA, payload.data(), payload.size());
}

bool WebSocket::ReadFrame(uint8_t& opcode, bool& fin, std::string& payload, int timeoutMs)
{
    unsigned char header[2];
    if (!ReadExact(header, 2, timeoutMs))
        return false;
    fin = (header[0] & 0x80) != 0;
    opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t length = header[1] & 0x7F;
    if (length == 126) {
        unsigned char ext[2];
        if (!ReadExact(ext, 2, 5000))
            return false;
        length = (ext[0] << 8) | ext[1];
    } else if (length == 127) {
        unsigned char ext[8];
        if (!ReadExact(ext, 8, 5000))
            return false;
        length = 0;
        for (int i = 0; i < 8; i++)
            length = (length << 8) | ext[i];
    }
    unsigned char maskBytes[4] = {0, 0, 0, 0};
    if (masked && !ReadExact(maskBytes, 4, 5000))
        return false;
    if (length > (64u << 20))
        return false;
    payload.resize((size_t)length);
    if (length > 0 && !ReadExact(&payload[0], (size_t)length, 10000))
        return false;
    if (masked)
        for (size_t i = 0; i < payload.size(); i++)
            payload[i] = (char)((unsigned char)payload[i] ^ maskBytes[i & 3]);
    return true;
}

WsFrame WebSocket::Receive(int timeoutMs)
{
    WsFrame frame;
    if (fSocket < 0) {
        frame.type = WsFrame::Error;
        return frame;
    }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs < 0 ? 1000000 : timeoutMs);
    while (true) {
        uint8_t opcode;
        bool fin;
        std::string payload;
        int remaining = timeoutMs < 0 ? -1
            : (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (timeoutMs >= 0 && remaining <= 0)
            return frame; // None
        // Peek: wait for data with poll first so a timeout does not leave a half-read frame.
        struct pollfd pfd;
        pfd.fd = fSocket;
        pfd.events = POLLIN;
        if (fReadAhead.empty()) {
            int rc = poll(&pfd, 1, remaining < 0 ? 1000 : std::min(remaining, 1000));
            if (rc == 0) {
                if (timeoutMs < 0)
                    continue;
                if (std::chrono::steady_clock::now() >= deadline)
                    return frame;
                continue;
            }
            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                frame.type = WsFrame::Error;
                return frame;
            }
        }
        if (!ReadFrame(opcode, fin, payload, 10000)) {
            frame.type = WsFrame::Error;
            fError = "connection closed";
            return frame;
        }
        switch (opcode) {
            case 0x0: // continuation
                fFragmentBuffer += payload;
                if (fin) {
                    frame.type = fFragmentOpcode == 0x1 ? WsFrame::Text : WsFrame::Binary;
                    frame.data.swap(fFragmentBuffer);
                    fFragmentBuffer.clear();
                    return frame;
                }
                break;
            case 0x1:
            case 0x2:
                if (!fin) {
                    fFragmentOpcode = opcode;
                    fFragmentBuffer = payload;
                    break;
                }
                frame.type = opcode == 0x1 ? WsFrame::Text : WsFrame::Binary;
                frame.data.swap(payload);
                return frame;
            case 0x8:
                frame.type = WsFrame::Close;
                frame.data.swap(payload);
                return frame;
            case 0x9:
                SendPong(payload);
                frame.type = WsFrame::Ping;
                frame.data.swap(payload);
                return frame;
            case 0xA:
                frame.type = WsFrame::Pong;
                return frame;
            default:
                break;
        }
    }
}

} // namespace tasamp
