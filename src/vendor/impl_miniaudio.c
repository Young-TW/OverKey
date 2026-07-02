// miniaudio 單檔實作（音訊解碼/播放，純 CPU，不需 OpenGL/X11）。
// 不需要錄音與編碼功能，關掉以縮短編譯與體積。
#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
