// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include "framework.h"

// MFC ���
#include <afxwin.h>         // MFC �ٽ� �� ǥ�� ���� ���
#include <afxext.h>         // MFC Ȯ��
#include <afxdisp.h>        // MFC �ڵ�ȭ Ŭ����
#include <afxdtctl.h>       // MFC�� Internet Explorer 4 ���� ��Ʈ�� ����
#include <afxcmn.h>         // MFC�� Windows ���� ��Ʈ�� ����
#include <afxcontrolbars.h> // MFC�� ���� �� ��Ʈ�� ���� ����
#include <afxdialogex.h>

// Windows ���
#include <windows.h>

// C ��Ÿ�� ���
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

// C++ ǥ�� ���̺귯��
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