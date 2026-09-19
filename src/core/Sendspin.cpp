#include "Sendspin.h"
#include "Json.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace tasamp {

namespace {
const int kBufferCapacityBytes = 1536 * 1024;   // ~8 s of 48 kHz stereo 16-bit
const int kRequiredLeadTimeMs = 250;
const int kMinBufferMs = 150;
}

int64_t SendspinClient::NowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

SendspinClient::SendspinClient(SendspinSink* sink)
    : fSink(sink)
{
}

SendspinClient::~SendspinClient()
{
    Stop();
}

void SendspinClient::Configure(const std::string& host, int port, const std::string& token,
    const std::string& clientId, const std::string& name)
{
    std::lock_guard<std::mutex> lock(fMutex);
    fHost = host;
    fPort = port;
    fToken = token;
    fClientId = clientId;
    fName = name;
}

void SendspinClient::Start()
{
    if (fRunning)
        return;
    fRunning = true;
    fThread = std::thread([this] { Run(); });
}

void SendspinClient::Stop()
{
    fRunning = false;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        if (fSocket)
            fSocket->Close();
    }
    if (fThread.joinable())
        fThread.join();
    fConnected = false;
    fStreamActive = false;
}

void SendspinClient::ReportVolume(int volume, bool muted)
{
    {
        std::lock_guard<std::mutex> lock(fMutex);
        fVolume = volume;
        fMuted = muted;
    }
    fStateDirty = true;
}

void SendspinClient::Run()
{
    int backoff = 2;
    while (fRunning) {
        bool ok = Session();
        fConnected = false;
        if (fStreamActive) {
            fStreamActive = false;
            fSink->SendspinStreamEnded();
        }
        if (!fRunning)
            break;
        fSink->SendspinConnection(false, ok ? "Disconnected from Music Assistant player service"
            : (fSocket ? fSocket->Error() : "connection failed"));
        for (int i = 0; i < backoff * 10 && fRunning; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        backoff = std::min(backoff * 2, 30);
    }
}

bool SendspinClient::SendJson(const std::string& text)
{
    std::lock_guard<std::mutex> lock(fMutex);
    return fSocket && fSocket->SendText(text);
}

void SendspinClient::SendClientTime()
{
    Json msg = {{"type", "client/time"}, {"payload", {{"client_transmitted", NowUs()}}}};
    SendJson(msg.dump());
    fLastTimeSync = NowUs();
}

void SendspinClient::SendClientState()
{
    int volume;
    bool muted;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        volume = fVolume;
        muted = fMuted;
    }
    // Note: aiosendspin 9.x only accepts "set_static_delay" in the state-level supported_commands
    // (volume/mute belong to client/hello) and drops the connection otherwise, so none are listed.
    Json player = {{"volume", volume}, {"muted", muted}, {"static_delay_ms", 0},
        {"required_lead_time_ms", kRequiredLeadTimeMs}, {"min_buffer_ms", kMinBufferMs},
        {"supported_commands", Json::array()}};
    Json msg = {{"type", "client/state"}, {"payload", {{"available", fFilter.Converged()},
        {"player", player}}}};
    SendJson(msg.dump());
    fStateDirty = false;
}

bool SendspinClient::Session()
{
    std::string host, token, clientId, name;
    int port;
    {
        std::lock_guard<std::mutex> lock(fMutex);
        host = fHost;
        port = fPort;
        token = fToken;
        clientId = fClientId;
        name = fName;
        fSocket.reset(new WebSocket());
    }
    fFilter.Reset();
    fTimeSyncCount = 0;
    fAvailableReported = false;
    WebSocket* ws = fSocket.get();
    if (!ws->Connect(host, port, "/sendspin", {}, 10))
        return false;
    // 1. authenticate against the Music Assistant proxy
    Json auth = {{"type", "auth"}, {"token", token}, {"client_id", clientId}};
    if (!ws->SendText(auth.dump()))
        return false;
    WsFrame reply = ws->Receive(10000);
    if (reply.type != WsFrame::Text)
        return false;
    Json authReply = Json::parse(reply.data, nullptr, false);
    if (!authReply.is_object() || authReply.value("type", "") != "auth_ok") {
        fprintf(stderr, "sendspin: auth failed: %s\n", reply.data.c_str());
        return false;
    }
    // 2. unencrypted client/hello (transition mode)
    Json formats = Json::array();
    for (int rate : {44100, 48000})
        formats.push_back({{"codec", "pcm"}, {"channels", 2}, {"sample_rate", rate}, {"bit_depth", 16}});
    Json hello = {{"type", "client/hello"}, {"payload", {
        {"client_id", clientId}, {"name", name}, {"version", 1},
        {"supported_roles", Json::array({"player@v1"})},
        {"device_info", {{"product_name", "TasAmp"}, {"manufacturer", "Haiku"}, {"software_version", "0.1.0"}}},
        {"player@v1_support", {{"supported_formats", formats}, {"buffer_capacity", kBufferCapacityBytes},
            {"supported_commands", Json::array({"volume", "mute"})}}}}}};
    if (!ws->SendText(hello.dump()))
        return false;
    fConnected = true;
    fSink->SendspinConnection(true, "Registered with Music Assistant as player \"" + name + "\"");
    // 3. burst of time sync messages
    for (int i = 0; i < 6; i++) {
        SendClientTime();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        WsFrame f = ws->Receive(300);
        if (f.type == WsFrame::Text)
            HandleText(f.data);
        else if (f.type == WsFrame::Binary)
            HandleBinary(f.data);
        else if (f.type == WsFrame::Error || f.type == WsFrame::Close)
            return true;
    }
    // 4. main loop
    while (fRunning) {
        WsFrame frame = ws->Receive(100);
        switch (frame.type) {
            case WsFrame::Text:
                HandleText(frame.data);
                break;
            case WsFrame::Binary:
                HandleBinary(frame.data);
                break;
            case WsFrame::Close:
            case WsFrame::Error:
                return true;
            default:
                break;
        }
        int64_t now = NowUs();
        int64_t interval = fTimeSyncCount < 20 ? 500000 : (fTimeSyncCount < 60 ? 2000000 : 5000000);
        if (now - fLastTimeSync > interval)
            SendClientTime();
        if (fStateDirty || (!fAvailableReported && fFilter.Converged())) {
            fAvailableReported = fFilter.Converged();
            SendClientState();
        }
    }
    return true;
}

void SendspinClient::HandleText(const std::string& text)
{
    Json msg = Json::parse(text, nullptr, false);
    if (!msg.is_object())
        return;
    std::string type = msg.value("type", "");
    Json payload = msg.value("payload", Json::object());
    if (type == "server/time") {
        int64_t t1 = payload.value("client_transmitted", (int64_t)0);
        int64_t t2 = payload.value("server_received", (int64_t)0);
        int64_t t3 = payload.value("server_transmitted", (int64_t)0);
        int64_t t4 = NowUs();
        int64_t measurement = ((t2 - t1) + (t3 - t4)) / 2;
        int64_t maxError = ((t4 - t1) - (t3 - t2)) / 2;
        fFilter.Update(measurement, maxError, t4);
        fTimeSyncCount++;
    } else if (type == "server/hello") {
        // legacy hello carries active_roles; nothing to negotiate for a single-role player
    } else if (type == "server/activate") {
        // encrypted-mode activation; ignored in transition mode
    } else if (type == "stream/start") {
        Json player = payload.value("player", Json::object());
        if (!player.is_object() || player.empty())
            return;
        SendspinFormat format;
        format.codec = player.value("codec", "pcm");
        format.sampleRate = player.value("sample_rate", 44100);
        format.channels = player.value("channels", 2);
        format.bitDepth = player.value("bit_depth", 16);
        fFormat = format;
        fStreamActive = true;
        fSink->SendspinStreamStarted(format);
    } else if (type == "stream/clear") {
        fSink->SendspinStreamCleared();
    } else if (type == "stream/end") {
        fStreamActive = false;
        fSink->SendspinStreamEnded();
    } else if (type == "server/command") {
        Json player = payload.value("player", Json::object());
        std::string command = player.value("command", "");
        if (command == "volume") {
            int volume = player.value("volume", 100);
            {
                std::lock_guard<std::mutex> lock(fMutex);
                fVolume = volume;
            }
            fSink->SendspinVolume(volume);
            fStateDirty = true;
        } else if (command == "mute") {
            bool muted = player.value("mute", false);
            {
                std::lock_guard<std::mutex> lock(fMutex);
                fMuted = muted;
            }
            fSink->SendspinMute(muted);
            fStateDirty = true;
        }
    } else if (type == "group/update") {
        fSink->SendspinGroupState(payload.value("playback_state", ""));
    } else if (type == "client/goodbye" || type == "server/goodbye") {
        fprintf(stderr, "sendspin: goodbye: %s\n", text.c_str());
    }
}

void SendspinClient::HandleBinary(const std::string& data)
{
    // header (big-endian): type(1) + timestamp_us(8); aiosendspin 9.x does not add send_ahead
    if (data.size() < 9)
        return;
    uint8_t type = (uint8_t)data[0];
    if (type != 4)
        return;
    int64_t serverTs = 0;
    for (int i = 1; i <= 8; i++)
        serverTs = (serverTs << 8) | (uint8_t)data[i];
    size_t headerSize = 9;
    if (!fStreamActive)
        return;
    int64_t localTs = fFilter.ComputeClientTime(serverTs);
    fSink->SendspinChunk(localTs, data.data() + headerSize, data.size() - headerSize);
}

} // namespace tasamp
