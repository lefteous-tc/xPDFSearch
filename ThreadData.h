#ifndef ThreadDataH
#define ThreadDataH

#include <windows.h>

#include "OutputDev.h"
#include "PDFDoc.h"

struct ThreadData
{
	HANDLE consumerSemaphore;
	HANDLE producerSemaphore;
	char* text;
	int max_text_length;
	wchar_t fileName[MAX_PATH];
	BOOL found;
	OutputDev* dev;
	PDFDoc* doc;
	BOOL copyPermissionRequired;
};

#endif