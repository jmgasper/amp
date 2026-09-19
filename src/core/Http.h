// Small libcurl wrapper used for the Music Assistant API and artwork downloads.
#pragma once
#include <string>
#include <vector>

namespace tasamp {

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string contentType;
    std::string error;          // transport error text when status == 0
    bool ok() const { return status >= 200 && status < 300; }
};

class Http {
public:
    static void GlobalInit();
    static HttpResponse Get(const std::string& url, const std::vector<std::string>& headers = {},
        int timeoutSeconds = 20);
    static HttpResponse Post(const std::string& url, const std::string& body,
        const std::vector<std::string>& headers = {}, int timeoutSeconds = 20);
    static std::string UrlEncode(const std::string& text);
    static std::string UserAgent();
};

} // namespace tasamp
