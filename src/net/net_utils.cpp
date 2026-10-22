#include "net/net_utils.h"

static bool is_strict_utf8_printable(const char* buf, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t c = (uint8_t)buf[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return false; // Control character
        }
        if (c < 0x80) {
            i++;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < len && ((uint8_t)buf[i + 1] & 0xC0) == 0x80) {
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < len && ((uint8_t)buf[i + 1] & 0xC0) == 0x80 && ((uint8_t)buf[i + 2] & 0xC0) == 0x80) {
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4 && i + 3 < len && ((uint8_t)buf[i + 1] & 0xC0) == 0x80 && ((uint8_t)buf[i + 2] & 0xC0) == 0x80 && ((uint8_t)buf[i + 3] & 0xC0) == 0x80) {
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

String net_to_valid_utf8(const String& str) {
    if (is_strict_utf8_printable(str.c_str(), str.length())) {
        return str;
    }

    // String has isolated Latin-1 / ISO-8859-1 bytes; transcode each non-ASCII byte to UTF-8
    size_t len = str.length();
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
    bool isHex = (len >= 2 && (len % 2 == 0));
    bool allDigits = true;
    for (size_t i = 0; i < len && isHex; ++i) {
        char c = hex[i];
        if (c >= '0' && c <= '9') {
            continue;
        } else if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            allDigits = false;
        } else {
            isHex = false;
        }
    }

    // Short numeric strings like "1984", "2024", "99" are plain numbers, not hex
    if (!isHex || (allDigits && len <= 6)) {
        return net_to_valid_utf8(String(hex));
    }

    String decoded = "";
    decoded.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        char byteChars[3] = { hex[i], hex[i + 1], '\0' };
        uint8_t byteVal = (uint8_t)strtoul(byteChars, nullptr, 16);
        if (byteVal == 0) {
            return net_to_valid_utf8(String(hex));
        }
        decoded += (char)byteVal;
    }

    // Guard against plain ASCII words made of hex letters (e.g. "ABBA", "ACDC", "CAFE", "FACE"):
    // If the decoded bytes contain control chars or invalid UTF-8, treat the input as plain text.
    if (!is_strict_utf8_printable(decoded.c_str(), decoded.length())) {
        return net_to_valid_utf8(String(hex));
    }

    return decoded;
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
        if (*p == '[' && isdigit((unsigned char)p[1])) {
            const char* closeBracket = strchr(p, ']');
            const char* colonPtr = strchr(p, ':');
            if (closeBracket && colonPtr && colonPtr < closeBracket && (closeBracket - p) <= 12) {
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
