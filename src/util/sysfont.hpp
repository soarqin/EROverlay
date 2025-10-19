#pragma once

namespace er::util {

wchar_t *const * systemFontFileList(const wchar_t *fontname);
void freeSystemFontFileList(wchar_t *const *pathList);

}
