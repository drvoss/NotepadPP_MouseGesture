#pragma once
#include <windows.h>

// Minimal Notepad++ messages and view constants.
#define NPPMSG (WM_USER + 1000)

#define NPPM_GETCURRENTSCINTILLA (NPPMSG + 4)
#define NPPM_GETNBOPENFILES (NPPMSG + 7)
#define NPPM_GETCURRENTDOCINDEX (NPPMSG + 23)
#define NPPM_ACTIVATEDOC (NPPMSG + 28)

#define MAIN_VIEW 0
#define SUB_VIEW 1
#define PRIMARY_VIEW 1
#define SECOND_VIEW 2
