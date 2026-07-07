#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H
// miniz 的 export 標頭正常由 CMake 的 generate_export_header 產生。
// 我們一律靜態連結，符號可見性不需特別處理，給空定義即可。
#define MINIZ_EXPORT
#endif  // MINIZ_EXPORT_H
