#include "net/net_utils.h"

String net_to_valid_utf8(const String& str) {
    bool validUtf8 = true;
    size_t i = 0;
    size_t len = str.length();
    while (i < len) {
        uint8_t c = (uint8_t)str[i];
        if (c < 0x80) {
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80 && ((uint8_t)str[i + 2] & 0xC0) == 0x80) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < len && ((uint8_t)str[i + 1] & 0xC0) == 0x80 && ((uint8_t)str[i + 2] & 0xC0) == 0x80 && ((uint8_t)str[i + 3] & 0xC0) == 0x80) {
            i += 4;
        } else {
            validUtf8 = false;
            break;
        }
    }
    if (validUtf8) return str;

    // String has isolated Latin-1 / ISO-8859-1 bytes; transcode each non-ASCII byte to UTF-8
    String out = "";
    out.reserve(len * 2);
    for (size_t j = 0; j < len; ++j) {
        uint8_t b = (uint8_t)str[j];
        if (b < 0x80) {
            out += (char)b;
        } else {
            out += (char)(0xC0 | (b >> 6));
            out += (char)(0x80 | (b & 0x3F));
        }
    }
    return out;
}

String net_decode_hex_string(const char* hex) {
    if (!hex || strlen(hex) == 0) return "";
    size_t len = strlen(hex);
    // Check if it looks like a valid hex string (even length, hex chars)
    bool isHex = (len % 2 == 0);
    for (size_t i = 0; i < len && isHex; ++i) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            isHex = false;
        }
    }
    if (!isHex || len < 2) {
        return net_to_valid_utf8(String(hex)); // Not hex encoded, return as valid UTF-8 string
    }

    String decoded = "";
    decoded.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        char byteChars[3] = { hex[i], hex[i + 1], '\0' };
        char byteVal = (char)strtol(byteChars, nullptr, 16);
        if (byteVal != 0) {
            decoded += byteVal;
        }
    }
    return net_to_valid_utf8(decoded);
}

String net_url_encode(const char* str) {
    if (!str) return "";
    String encoded = "";
    size_t len = strlen(str);
    for (size_t i = 0; i < len; ++i) {
        char c = str[i];
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else if (c == ' ') {
            encoded += "+";
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
            encoded += buf;
        }
    }
    return encoded;
}

String net_extract_xml_tag(const String& xml, const String& tag) {
    String openTag = "<" + tag + ">";
    String closeTag = "</" + tag + ">";
    int startIdx = xml.indexOf(openTag);
    if (startIdx < 0) return "";
    startIdx += openTag.length();
    int endIdx = xml.indexOf(closeTag, startIdx);
    if (endIdx < 0) return "";
    return xml.substring(startIdx, endIdx);
}

String net_strip_lrc_timestamps(const char* lrc) {
    if (!lrc) return "";
    String out = "";
    out.reserve(strlen(lrc));
    const char* p = lrc;
    while (*p) {
        if (*p == '[') {
            const char* closeBracket = strchr(p, ']');
            if (closeBracket && (closeBracket - p) <= 12) {
                p = closeBracket + 1;
                while (*p == ' ' || *p == '\t') p++;
                continue;
            }
        }
        out += *p++;
    }
    out.trim();
    return out;
}
