// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

// MFC 헤더
#include <afxwin.h>         // MFC 핵심 및 표준 구성 요소
#include <afxext.h>         // MFC 확장
#include <afxdisp.h>        // MFC 자동화 클래스
#include <afxdtctl.h>       // MFC의 Internet Explorer 4 공용 컨트롤 지원
#include <afxcmn.h>         // MFC의 Windows 공용 컨트롤 지원
#include <afxcontrolbars.h> // MFC의 리본 및 컨트롤 막대 지원
#include <afxdialogex.h>

// Windows 헤더
#include <windows.h>

// C 런타임 헤더
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

// C++ 표준 라이브러리
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <chrono>

#endif //PCH