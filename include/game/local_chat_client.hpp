#pragma once
#include "game/local_chat.hpp"
#include "game/world_packets.hpp"
namespace wowee::game {
inline MessageChatData localChatClientMessage(const LocalChatLine& line) {
    MessageChatData message{};
    message.type=static_cast<ChatType>(line.channel);
    message.language=ChatLanguage::UNIVERSAL;
    message.senderGuid=line.sender;message.senderName=line.senderName;message.receiverName=line.receiverName;
    message.message=localChatDisplayText(line.text);
    return message;
}
}
