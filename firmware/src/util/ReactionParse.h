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
    // The prefix must look like an emoji and nothing else.
    //
    // Testing only the first byte was far too loose: any DM whose last line was 8
    // Crockford-legal characters got swallowed and never stored -- no bubble, no
    // unread badge, no notification, no log. Testing "every byte non-ASCII" fixed
    // the Latin cases but still ate CJK/Cyrillic/Greek messages ("Doe\nmidnight"),
    // and rejected real reactions whose first codepoint IS ASCII (the keycaps
    // 1..9, # and * are ASCII digit + VS16 + U+20E3).
    //
    // So: test positively for emoji codepoints. Everything outside the emoji and
    // modifier ranges below is an ordinary message and must be left alone. This
    // path is not gated by messaging.reactions, so it runs on every device -- a
    // false positive here destroys a message.
    static constexpr uint8_t MAX_REACTION_CODEPOINTS = 8;   // ZWJ families reach 7
    if (emoji.length() == 0) return false;

    uint8_t codepoints = 0;
    bool sawEmojiBase = false;
    for (size_t i = 0; i < emoji.length(); ) {
        uint8_t b = (uint8_t)emoji[i];
        uint32_t cp; size_t adv;
        if      (b < 0x80)          { cp = b;                                   adv = 1; }
        else if ((b & 0xE0) == 0xC0){ cp = b & 0x1F;                            adv = 2; }
        else if ((b & 0xF0) == 0xE0){ cp = b & 0x0F;                            adv = 3; }
        else if ((b & 0xF8) == 0xF0){ cp = b & 0x07;                            adv = 4; }
        else return false;                                   // bad UTF-8 lead byte
        if (i + adv > emoji.length()) return false;           // truncated sequence
        for (size_t k = 1; k < adv; k++) {
            uint8_t cb = (uint8_t)emoji[i + k];
            if ((cb & 0xC0) != 0x80) return false;            // bad continuation
            cp = (cp << 6) | (cb & 0x3F);
        }
        i += adv;
        if (++codepoints > MAX_REACTION_CODEPOINTS) return false;

        const bool isBase =
            (cp >= 0x1F300 && cp <= 0x1FAFF) ||   // pictographs, faces, symbols
            (cp >= 0x2600  && cp <= 0x27BF)  ||   // misc symbols + dingbats
            (cp >= 0x2B00  && cp <= 0x2BFF)  ||   // arrows/stars
            (cp >= 0x1F000 && cp <= 0x1F0FF) ||   // mahjong/cards
            (cp >= 0x1F1E6 && cp <= 0x1F1FF) ||   // regional indicators (flags)
            (cp >= 0x2190  && cp <= 0x21FF)  ||   // arrows
            cp == 0x203C || cp == 0x2049 || cp == 0x2122 || cp == 0x2139 ||
            cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299;
        const bool isModifier =
            cp == 0xFE0F || cp == 0xFE0E ||       // variation selectors
            cp == 0x200D ||                       // ZWJ
            cp == 0x20E3 ||                       // combining keycap
            (cp >= 0x1F3FB && cp <= 0x1F3FF) ||   // skin tones
            (cp >= 0xE0020 && cp <= 0xE007F);     // tag characters (subdivision flags)
        // A keycap is "1" + VS16 + U+20E3: the ASCII digit is only legal when the
        // sequence actually ends in the combining keycap mark.
        const bool isKeycapBase = (cp >= '0' && cp <= '9') || cp == '#' || cp == '*';

        if (isBase) sawEmojiBase = true;
        else if (isKeycapBase)  { if (emoji.indexOf("\xE2\x83\xA3") < 0) return false; sawEmojiBase = true; }
        else if (!isModifier)   return false;     // anything else: ordinary text
    }
    if (!sawEmojiBase) return false;

    out_emoji = emoji;
    out_hash  = normalizeCrockford(hash);
    return true;
}

}  // namespace mclite
