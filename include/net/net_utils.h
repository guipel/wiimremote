#pragma once

#include <Arduino.h>

// Validate and transcode Latin-1 / ISO-8859-1 strings to UTF-8
String net_to_valid_utf8(const String& str);

// LinkPlay hex-encoded metadata string decoder with UTF-8 fallback
String net_decode_hex_string(const char* hex);

// URL parameter encoder for external query strings
String net_url_encode(const char* str);

// Lightweight XML tag value extractor for UPnP description.xml and SOAP
String net_extract_xml_tag(const String& xml, const String& tag);

// Strips [mm:ss.xx] timing tags from synced lyrics
String net_strip_lrc_timestamps(const char* lrc);
