// stb_vorbis 單檔實作，提供 Ogg/Vorbis 解碼（miniaudio 未內建）。
// 只用到 pulldata + stb_vorbis_decode_filename，關掉 pushdata API 以省體積。
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
