#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "DiscordClient.h"

#include "JsonUtil.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <regex>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr wchar_t kDiscordHost[] = L"discord.com";
constexpr wchar_t kApiPrefix[] = L"/api/v9";

struct HttpResponse {
    DWORD status = 0;
    std::string body;
    std::string error;
};

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], size);
    return wide;
}

HttpResponse HttpRequest(
    const wchar_t* method,
    const std::wstring& path,
    const std::string& token,
    const std::string* jsonBody = nullptr) {

    HttpResponse result;

    HINTERNET session = WinHttpOpen(
        L"DiscordSpammer/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        result.error = "WinHttpOpen failed";
        return result;
    }

    HINTERNET connect = WinHttpConnect(session, kDiscordHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        result.error = "WinHttpConnect failed";
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connect, method, path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        result.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    std::wstring headers =
        L"Authorization: " + Utf8ToWide(token) + L"\r\n"
        L"User-Agent: DiscordBot (ConsoleApplication1, 1.0)\r\n";
    if (jsonBody) {
        headers += L"Content-Type: application/json\r\n";
    }

    WinHttpAddRequestHeaders(
        request, headers.c_str(), static_cast<DWORD>(-1),
        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    const void* bodyPtr = nullptr;
    DWORD bodyLen = 0;
    if (jsonBody) {
        bodyPtr = jsonBody->data();
        bodyLen = static_cast<DWORD>(jsonBody->size());
    }

    if (!WinHttpSendRequest(
            request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            const_cast<void*>(bodyPtr), bodyLen, bodyLen, 0)) {
        result.error = "WinHttpSendRequest failed";
    }
    else if (!WinHttpReceiveResponse(request, nullptr)) {
        result.error = "WinHttpReceiveResponse failed";
    }
    else {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
        result.status = statusCode;

        DWORD available = 0;
        do {
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, &chunk[0], available, &read)) {
                break;
            }
            chunk.resize(read);
            result.body += chunk;
        } while (available > 0);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

bool IsExcludedChannelName(const std::string& name) {
    static const std::regex pattern(R"(.*-[0-9]+$)");
    return std::regex_match(name, pattern);
}

bool IsTextChannel(int type) {
    return type == 0 || type == 5 || type == 15;
}

double ParseRetryAfter(const std::string& body) {
    if (auto val = JsonGetDouble(body, "retry_after")) {
        return *val;
    }
    return 2.0;
}

std::string TrimCopy(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool StartsWithBotPrefix(const std::string& token) {
    return token.size() >= 4
        && (token[0] == 'B' || token[0] == 'b')
        && (token[1] == 'O' || token[1] == 'o')
        && (token[2] == 'T' || token[2] == 't')
        && token[3] == ' ';
}

std::string StripBotPrefix(const std::string& token) {
    if (!StartsWithBotPrefix(token)) {
        return token;
    }
    const size_t space = token.find(' ');
    return TrimCopy(token.substr(space + 1));
}

std::string NormalizeAuthorization(std::string token, TokenKind kind) {
    token = TrimCopy(std::move(token));
    if (token.empty()) {
        return token;
    }

    switch (kind) {
    case TokenKind::Bot:
        // Всегда добавляем "Bot " (если уже был - нормализуем).
        return "Bot " + StripBotPrefix(token);

    case TokenKind::User:
        // Пользовательский токен - без префикса. На всякий случай уберём,
        // если пользователь случайно скопировал "Bot ...".
        return StripBotPrefix(token);

    case TokenKind::Auto:
    default:
        // Если уже есть "Bot " - нормализуем; иначе оставляем как есть,
        // дальше ValidateToken попробует оба варианта.
        if (StartsWithBotPrefix(token)) {
            return "Bot " + StripBotPrefix(token);
        }
        return token;
    }
}

std::string FormatAuthError(const HttpResponse& resp) {
    std::string msg = "HTTP " + std::to_string(resp.status);
    if (resp.status == 401 || resp.status == 403) {
        msg = "Discord rejected the token (401). Open Developer Portal -> Bot -> Reset Token, copy the NEW token only.";
    }
    if (!resp.body.empty()) {
        msg += " | " + resp.body.substr(0, 120);
    }
    if (!resp.error.empty()) {
        msg += " | " + resp.error;
    }
    return msg;
}

} // namespace

DiscordClient::DiscordClient(std::string token, TokenKind kind)
    : token_(NormalizeAuthorization(std::move(token), kind)),
      kind_(kind) {
}

bool DiscordClient::ValidateToken(std::string& error) {
    const auto check = [](const std::string& auth) {
        return HttpRequest(L"GET", std::wstring(kApiPrefix) + L"/users/@me", auth, nullptr);
    };

    // 1) Пробуем как есть (user-токен или уже с префиксом "Bot ").
    HttpResponse resp = check(token_);
    if (resp.status >= 200 && resp.status < 300) {
        return true;
    }

    // 2) Сетевая ошибка - нет смысла пробовать ещё раз.
    if (resp.status == 0) {
        error = resp.error.empty() ? "Network error" : resp.error;
        return false;
    }

    // 3) Только в режиме Auto имеет смысл подбирать префикс.
    //    Для Bot/User пользователь явно указал тип - не угадываем.
    if (kind_ == TokenKind::Auto
        && (resp.status == 401 || resp.status == 403)
        && !StartsWithBotPrefix(token_)) {
        const std::string withBot = "Bot " + token_;
        HttpResponse resp2 = check(withBot);
        if (resp2.status >= 200 && resp2.status < 300) {
            token_ = withBot;
            return true;
        }
        // Если и с "Bot " тоже 401/403 - токен действительно неверный.
        error = FormatAuthError(resp);
        return false;
    }

    error = FormatAuthError(resp);
    return false;
}

std::vector<GuildInfo> DiscordClient::FetchGuilds() const {
    std::vector<GuildInfo> guilds;
    const auto resp = HttpRequest(L"GET", std::wstring(kApiPrefix) + L"/users/@me/guilds", token_, nullptr);
    if (resp.status < 200 || resp.status >= 300) {
        return guilds;
    }
    for (const auto& obj : JsonSplitTopObjects(resp.body)) {
        GuildInfo g;
        if (auto id = JsonGetString(obj, "id")) {
            g.id = *id;
        }
        if (auto name = JsonGetString(obj, "name")) {
            g.name = *name;
        }
        if (!g.id.empty()) {
            guilds.push_back(std::move(g));
        }
    }
    return guilds;
}

std::vector<ChannelInfo> DiscordClient::FetchGuildTextChannels(const std::string& guildId) const {
    std::vector<ChannelInfo> channels;
    const auto path = std::wstring(kApiPrefix) + Utf8ToWide("/guilds/" + guildId + "/channels");
    const auto resp = HttpRequest(L"GET", path, token_, nullptr);
    if (resp.status < 200 || resp.status >= 300) {
        return channels;
    }

    for (const auto& obj : JsonSplitTopObjects(resp.body)) {
        ChannelInfo ch;
        if (auto id = JsonGetString(obj, "id")) {
            ch.id = *id;
        }
        if (auto name = JsonGetString(obj, "name")) {
            ch.name = *name;
        }
        if (auto type = JsonGetInt(obj, "type")) {
            ch.type = static_cast<int>(*type);
        }
        if (ch.id.empty() || !IsTextChannel(ch.type) || IsExcludedChannelName(ch.name)) {
            continue;
        }
        ch.label = "#" + ch.name;
        channels.push_back(std::move(ch));
    }

    std::sort(channels.begin(), channels.end(),
        [](const ChannelInfo& a, const ChannelInfo& b) { return a.name < b.name; });
    return channels;
}

std::vector<ChannelInfo> DiscordClient::FetchDmChannels() const {
    std::vector<ChannelInfo> channels;
    const auto resp = HttpRequest(L"GET", std::wstring(kApiPrefix) + L"/users/@me/channels", token_, nullptr);
    if (resp.status < 200 || resp.status >= 300) {
        return channels;
    }

    for (const auto& obj : JsonSplitTopObjects(resp.body)) {
        ChannelInfo ch;
        if (auto id = JsonGetString(obj, "id")) {
            ch.id = *id;
        }
        if (auto type = JsonGetInt(obj, "type")) {
            ch.type = static_cast<int>(*type);
        }
        ch.name = "dm-" + ch.id;

        size_t searchFrom = 0;
        std::vector<std::string> names;
        while (searchFrom < obj.size()) {
            const size_t recPos = obj.find("\"recipients\"", searchFrom);
            if (recPos == std::string::npos) {
                break;
            }
            const size_t recObj = obj.find('{', recPos);
            if (recObj == std::string::npos) {
                break;
            }
            const size_t recEnd = obj.find('}', recObj);
            if (recEnd == std::string::npos) {
                break;
            }
            const std::string recJson = obj.substr(recObj, recEnd - recObj + 1);
            if (auto username = JsonGetString(recJson, "username")) {
                names.push_back(*username);
            }
            else if (auto globalName = JsonGetString(recJson, "global_name")) {
                names.push_back(*globalName);
            }
            searchFrom = recEnd + 1;
        }

        if (!names.empty()) {
            ch.label = names[0];
            for (size_t i = 1; i < names.size(); ++i) {
                ch.label += ", " + names[i];
            }
        }
        else {
            ch.label = "DM (id: " + ch.id + ")";
        }
        channels.push_back(std::move(ch));
    }
    return channels;
}

bool DiscordClient::PostChannelMessage(
    const std::string& channelId,
    const std::string& content,
    std::string& error,
    double& retryAfterSec) const {

    retryAfterSec = 0.0;
    const std::string body = R"({"content":")" + JsonEscape(content) + R"("})";
    const auto path = std::wstring(kApiPrefix) + Utf8ToWide("/channels/" + channelId + "/messages");
    const auto resp = HttpRequest(L"POST", path, token_, &body);

    if (resp.status >= 200 && resp.status < 300) {
        return true;
    }

    if (resp.status == 429) {
        retryAfterSec = ParseRetryAfter(resp.body);
        error = "Rate limit (429)";
        return false;
    }

    error = "HTTP " + std::to_string(resp.status);
    if (!resp.body.empty()) {
        error += ": " + resp.body.substr(0, 200);
    }
    return false;
}

DiscordClient::SpamResult DiscordClient::RunSpamLoop(
    const std::string& channelId,
    const std::string& message,
    double intervalSeconds,
    int maxMessages,
    bool isTurbo,
    std::atomic<bool>& stopFlag) const {

    SpamResult stats;

    const double delay = isTurbo ? 0.35 : intervalSeconds;

    while (!stopFlag.load()) {
        if (maxMessages > 0 && stats.sent >= maxMessages) {
            break;
        }

        std::string error;
        double retryAfter = 0.0;
        if (PostChannelMessage(channelId, message, error, retryAfter)) {
            ++stats.sent;
        }
        else {
            ++stats.failed;
            if (retryAfter > 0.0) {
                const auto until = std::chrono::steady_clock::now()
                    + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(retryAfter));
                while (!stopFlag.load() && std::chrono::steady_clock::now() < until) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                continue;
            }
        }

        if (delay > 0.0) {
            const auto until = std::chrono::steady_clock::now()
                + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(delay));
            while (!stopFlag.load() && std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
    }

    return stats;
}
