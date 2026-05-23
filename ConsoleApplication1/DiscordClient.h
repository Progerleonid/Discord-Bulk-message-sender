#pragma once

#include <atomic>
#include <string>
#include <vector>

struct GuildInfo {
    std::string id;
    std::string name;
};

struct ChannelInfo {
    std::string id;
    std::string name;
    int type = -1;
    std::string label;
};

// Auto - попробовать как есть, при 401 повторить с префиксом "Bot ".
// Bot  - всегда добавлять префикс "Bot " (бот-токен из Developer Portal).
// User - использовать как есть, без префикса (user-токен / selfbot).
enum class TokenKind { Auto, Bot, User };

class DiscordClient {
public:
    explicit DiscordClient(std::string token, TokenKind kind = TokenKind::Auto);

    bool ValidateToken(std::string& error);
    std::vector<GuildInfo> FetchGuilds() const;
    std::vector<ChannelInfo> FetchGuildTextChannels(const std::string& guildId) const;
    std::vector<ChannelInfo> FetchDmChannels() const;

    bool PostChannelMessage(
        const std::string& channelId,
        const std::string& content,
        std::string& error,
        double& retryAfterSec) const;

    struct SpamResult {
        int sent = 0;
        int failed = 0;
    };

    SpamResult RunSpamLoop(
        const std::string& channelId,
        const std::string& message,
        double intervalSeconds,
        int maxMessages,
        bool isTurbo,
        std::atomic<bool>& stopFlag) const;

private:
    std::string token_;
    TokenKind kind_ = TokenKind::Auto;
};
