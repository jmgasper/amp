// Minimal RFC 6455 client (plain ws://) on top of BSD sockets. One connection per object.
// Thread model: one thread may call Receive() while another calls Send*(); sends are serialised.
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tasamp {

struct WsFrame {
    enum Type { None, Text, Binary, Close, Ping, Pong, Error };
    Type type = None;
    std::string data;
};

class WebSocket {
public:
    WebSocket();
    ~WebSocket();

    // Performs the TCP connect and HTTP upgrade. extraHeaders: "Name: value" lines.
    bool Connect(const std::string& host, int port, const std::string& path,
        const std::vector<std::string>& extraHeaders = {}, int timeoutSeconds = 10);
    void Close();
    bool IsConnected() const { return fSocket >= 0; }
    const std::string& Error() const { return fError; }

    bool SendText(const std::string& text);
    bool SendBinary(const void* data, size_t size);
    bool SendPong(const std::string& payload);
    // Blocks up to timeoutMs (-1 forever). Returns None on timeout, Error on failure.
    // Pings are answered automatically and reported as Ping frames.
    WsFrame Receive(int timeoutMs);

private:
    bool ReadExact(void* buffer, size_t size, int timeoutMs);
    bool WriteAll(const void* buffer, size_t size);
    bool SendFrame(uint8_t opcode, const void* data, size_t size);
    bool ReadFrame(uint8_t& opcode, bool& fin, std::string& payload, int timeoutMs);

    int fSocket = -1;
    std::string fError;
    std::mutex fSendMutex;
    std::string fFragmentBuffer;
    uint8_t fFragmentOpcode = 0;
    std::string fReadAhead;
};

} // namespace tasamp
