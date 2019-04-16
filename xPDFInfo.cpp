#include "xPDFInfo.h"
#include <strsafe.h>

#include "PDFDoc.h"
#include "TextOutputDev.h"

BOOL APIENTRY DllMain(HANDLE hDLL, DWORD reason, LPVOID)
{	
    return TRUE;
}

int __stdcall ContentGetDetectString(char* DetectString,int maxlen)
{
	// PDF files are all we can handle.
	StringCchCopy (DetectString, maxlen, "EXT=\"PDF\"");	
	return 0;
}

int __stdcall ContentGetSupportedField(int FieldIndex,char* FieldName,char* Units,int maxlen)
{
	// Exclude date time fields in older TC versions.
	if (FieldIndex < 0 || FieldIndex >= FIELD_COUNT || (!enableDateTimeField && (FieldIndex == fiCreationDate || FieldIndex == fiLastModifiedDate)))
	{		
		return ft_nomorefields;
	}
	StringCchCopy (FieldName, maxlen, fieldNames[FieldIndex]);		
	if (FieldIndex == fiPageWidth || FieldIndex == fiPageHeight)
	{
		StringCchCopy (Units, maxlen, "mm|cm|in|pt");
	}
	else 
	{
		Units[0] = 0;
	}
	return fieldTypes[FieldIndex];
}

int __stdcall ContentGetValue(char* FileName,int FieldIndex,int UnitIndex,void* FieldValue,int maxlen,int flags)
{
	return ft_notsupported;
}

int __stdcall ContentGetValueW(wchar_t* fileName,int FieldIndex,int UnitIndex,void* FieldValue,int maxlen,int flags)
{
	int result = ft_fileerror;
	switch (FieldIndex)
	{
	// full text search.
	case fiText:		
		result = getText (fileName, UnitIndex, FieldValue, maxlen, flags, FALSE);
		break;
	case fiDocStart:
		if (flags & CONTENT_DELAYIFSLOW)
		{
			result = ft_delayed;
		}
		else
		{
			result = getDocStart (fileName, UnitIndex, FieldValue, maxlen, flags, FALSE);
		}
		break;
	case fiFirstRow:
		if (flags & CONTENT_DELAYIFSLOW)
		{
			result = ft_delayed;
		}
		else
		{
			result = getDocStart (fileName, UnitIndex, FieldValue, maxlen, flags, TRUE);
		}
		break;
	// metadata.
	case fiTitle:	
	case fiSubject:
	case fiKeywords:
	case fiAuthor:
	case fiCreator:
	case fiProducer:
	case fiNumberOfPages:
	case fiPDFVersion:
	case fiPageWidth:
	case fiPageHeight:
	case fiCopyingAllowed:
	case fiPrintingAllowed:
	case fiAddCommentsAllowed:
	case fiChangingAllowed:
	case fiEncrypted:
	case fiTagged:
	case fiLinearized:
	case fiCreationDate:
	case fiLastModifiedDate:	
		// All metadata fields are delayed fields.
		if (flags & CONTENT_DELAYIFSLOW)
		{
			result = ft_delayed;
		}
		else
		{			
			result = pMetadataExtrator->extract (fileName, FieldIndex, UnitIndex, FieldValue, maxlen);
		}
		break;
	default:
		result = ft_nosuchfield;		
	}
	return result;
}

void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps)
{
	// Check content plugin interface version to enable fields of type datetime.
	enableDateTimeField = (dps->PluginInterfaceVersionHi == 1 && dps->PluginInterfaceVersionLow >= 5) || 
		dps->PluginInterfaceVersionHi > 1;
	pMetadataExtrator = new PDFMetadataExtractor (fieldTypes);	
}

// Used to clean some globally allocated resources allocated in init.
void __stdcall ContentPluginUnloading(void)
{
	delete pMetadataExtrator;
	pMetadataExtrator = NULL;
}

// getText starts the fulltext extraction thread. It returns found text to Total Commander.
int getText (wchar_t* FileName, int UnitIndex,void* FieldValue,int maxlen,int flags, BOOL copyPermissionRequired)
{	
	int result = ft_fieldempty;
	switch (UnitIndex)
	{	
	case -1:		
		// String found --> clean up.
		threadData.found = TRUE;				
		ReleaseSemaphore (threadData.producerSemaphore, 1, NULL);		
		WaitForSingleObject (threadHandle, INFINITE);
		break;
	default:
		// Extraction thread has not been started yet. Let's go!
		if (!extract)
		{				
			threadData.consumerSemaphore = CreateSemaphore (NULL, 0, 1, NULL);
			threadData.producerSemaphore = CreateSemaphore (NULL, 0, 1, NULL);
			threadData.copyPermissionRequired = copyPermissionRequired;
			syncObj[0] = threadData.consumerSemaphore;	
			DWORD threadID = 0;
			threadHandle = CreateThread (NULL, 0, getTextFunc, (void*)&threadData, CREATE_SUSPENDED, &threadID);
			syncObj[1] = threadHandle;
			extract = TRUE;
			StringCchCopyW (threadData.fileName, MAX_PATH, FileName);
			threadData.text = new char [maxlen];
			threadData.max_text_length = maxlen;
			threadData.found = FALSE;
			ResumeThread (threadHandle);
		}
		
		// release producer thread.
		ReleaseSemaphore (threadData.producerSemaphore, 1, NULL);
		
		// Wait for producer thread to finish text extraction.
		// Alternatively wait for producer thread to finish.
		DWORD r = WaitForMultipleObjects (2, syncObj, FALSE, INFINITE);

		// 0 = Consumer semaphore signaled --> Text extracted, return it to Total Commander.
		if (r == 0)
		{
			StringCchCopy ((char*)FieldValue, MAX_PATH, threadData.text);
			result = ft_fulltext;
		}
		break;
	}
	// Clean up resources after finding the string (UnitIndex == 1) or nothing found (WaitForMultipleObjects returned 1)
	if (result == ft_fieldempty)
	{
		delete [] threadData.text;
		CloseHandle (threadData.consumerSemaphore);
		CloseHandle (threadData.producerSemaphore);	
		CloseHandle (threadHandle);
		extract = FALSE;
	}
	return result;
}

int getDocStart (wchar_t* fileName, int UnitIndex,void* FieldValue,int maxlen,int flags, BOOL firstRow)
{		
	char* newText = new char [maxlen];
	// Read ahead first text fragment.
	int exitCode = getText (fileName, UnitIndex, newText, maxlen, flags, TRUE);
	unsigned short textLength = 0;
	while (exitCode == ft_fulltext && textLength < maxlen && !firstRow)
	{		
		fixInvalidCharacters (newText);
		StringCchCat ((char*)FieldValue, maxlen, newText);
		textLength += strlen (newText);
		// Read next text fragment.		
		exitCode = getText (fileName, UnitIndex, newText, maxlen, flags, TRUE);
	}
	// End of document not reached --> cleanup explicitely.
	if (exitCode == ft_fulltext)
	{
		getText (NULL, -1, NULL, -1, -1, FALSE);
	}	
	// Only one fragment extracted.	
	if (!textLength)
	{
		if (newText[0])
		{
			StringCchCopy ((char*)FieldValue, maxlen, newText);
			// If text has been extracted return string as field type.
			exitCode = ft_string;
		}
		else
		{
			// Nothing found.
			exitCode = ft_fieldempty;
		}
	}
	else
	{		
		// If text has been extracted return string as field type.
		exitCode = ft_string;
	}
	delete [] newText;
	return exitCode;
}

// getTextFunc is the text extraction thread function.
DWORD WINAPI getTextFunc (void* param)
{		
	// Allocate new GString object. Deallocated by PDFDoc instance (doc)!
	//GString* fileNameString = new GString(threadData.fileName);

	// open PDF file
	PDFDoc* doc = new PDFDoc(threadData.fileName, wcslen(threadData.fileName));

	if (!doc->isOk() || (!doc->okToCopy() && threadData.copyPermissionRequired)) 
	{			
		delete doc;
		ExitThread (ft_fileerror);		
	}
  

	TextOutputDev* textOut = new TextOutputDev(&threadData);	

	if (textOut->isOk())
	{
		threadData.dev = textOut;
		threadData.doc = doc;		
		doc->displayPages(textOut, 1, doc->getNumPages(), 72, 72, 0, gFalse, gTrue, gFalse);						
	}	
	delete doc;
	delete textOut;


	ExitThread (ft_fieldempty);
}

void fixInvalidCharacters (char* text)
{
	for (unsigned short i = 0; i < strlen(text); i++)
	{
		if (text[i] == '\f')
		{
			text[i] = ' ';
		}
	}
}