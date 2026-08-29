#pragma once
#include <Arduino.h>
#include "MsgHash.h"

namespace mclite {

// Parse a MeshCore-format reaction from incoming message text.
// Channel/Room format: {emoji}@[{targetSenderName}]\n{8-char-crockford}
// DM format:           {emoji}\n{8-char-crockford}
// Returns true and fills out_emoji / out_hash if it matches. The emoji prefix must
// be entirely non-ASCII and at most a few codepoints, so an ordinary message is
// never misidentified as a reaction and silently swallowed.
inline bool parseIncomingReaction(const String& text, bool isChannel,
                                   String& out_emoji, String& out_hash) {
    int nl = text.lastIndexOf('\n');
    if (nl < 0) return false;
    if ((int)text.length() != nl + 1 + 8) return false;

    String hash = text.substring(nl + 1);
    if (!isCrockfordB32(hash)) return false;

    String prefix = text.substring(0, nl);
    String emoji;
    if (isChannel) {
        int atBracket = prefix.indexOf("@[");
        if (atBracket < 0 || !prefix.endsWith("]")) return false;
        emoji = prefix.substring(0, atBracket);
    } else {
        emoji = prefix;
    }
    // The prefix must look like an emoji and nothing else. Testing only the FIRST
    // byte was far too loose: any DM whose last line happened to be 8 Crockford-
    // legal characters got swallowed here and never stored -- no bubble, no unread
    // badge, no notification, no log. "\xF0\x9F\x98\x80 party at\nmidnight" is enough
    // ("midnight" is legal Crockford: i->1, o->0, no u), and so is any German or
    // French message that opens with an accented character and ends in an 8-letter
    // word. This path is not gated by messaging.reactions, so it reached every
    // device. Requiring EVERY byte to be non-ASCII rejects those (the space and the
    // ASCII letters fail) while still accepting real emoji, including ZWJ
    // sequences, skin-tone modifiers and variation selectors. The codepoint cap
    // also bounds what a peer can push into the stored reaction list.
    static constexpr uint8_t MAX_REACTION_CODEPOINTS = 4;
    if (emoji.length() == 0) return false;
    uint8_t codepoints = 0;
    for (size_t i = 0; i < emoji.length(); i++) {
        uint8_t b = (uint8_t)emoji[i];
        if (b < 0x80) return false;               // any ASCII byte -> not an emoji
        if ((b & 0xC0) != 0x80) codepoints++;     // count UTF-8 lead bytes
    }
    if (codepoints == 0 || codepoints > MAX_REACTION_CODEPOINTS) return false;

    out_emoji = emoji;
    out_hash  = normalizeCrockford(hash);
    return true;
}

}  // namespace mclite
