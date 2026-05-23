#pragma once

#include <string>
#include <windows.h>

std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);
std::string GetDlgTextUtf8(HWND hwnd);
void SetDlgTextUtf8(HWND hwnd, const std::string& utf8);
