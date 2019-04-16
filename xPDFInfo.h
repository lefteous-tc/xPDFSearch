#ifndef xPDFInfoH
#define xPDFInfoH

#include <tchar.h>
#include <windows.h>

#include "contentplug.h"

#include "xpdfsearch_base.h"


#include "GString.h"
#include "Object.h"
#include "UnicodeMap.h"


#include "ThreadData.h"
#include "PDFMetadataExtractor.h"


// functions
// ---------

// getText starts the fulltext extraction thread. It returns found text to Total Commander. 
int getText (wchar_t* FileName, int UnitIndex,void* FieldValue,int maxlen,int flags, BOOL copyPermissionRequired);

// getDocStart returns the first ~1000 characters of a pdf file.
int getDocStart (wchar_t* FileName, int UnitIndex,void* FieldValue,int maxlen,int flags, BOOL firstRow);

void fixInvalidCharacters (char* text);

// getTextFunc is the text extraction thread function.
DWORD WINAPI getTextFunc (void* param);

// destroy is used to clean some globally allocated resources allocated in init.
void destroy ();



// threadData is used to share resource between the consumer and the producer thread.
// Access has to be synchronized!
ThreadData threadData;

// threadHandle is the threadHandle for the producer thread.
HANDLE threadHandle = NULL;

// syncObj contains synchronization and thread objects.
HANDLE syncObj [2];

// extract is used to indicate if text is currently extracted from a pdf file.
BOOL extract = FALSE;

PDFMetadataExtractor* pMetadataExtrator;

// variables
// ---------

// The fieldNames array is used to simplify fieldname returning.
char* fieldNames[FIELD_COUNT] = 
{
	"Title", "Subject", "Keywords", "Author", "Application", "PDF Producer", "Number Of Pages", "PDF Version", "Page Width", "Page Height",
	"Document Start", "First Row",
	"Copying Allowed", "Printing Allowed", "Adding Comments Allowed", "Changing Allowed", "Encrypted", "Tagged", "Linearized",
	"Created", "Modified",	
	"Text"
};

// The fieldtypes array is used to simplify fieldtype returning.
int fieldTypes [FIELD_COUNT] = 
{
	ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_numeric_32, ft_numeric_floating, ft_numeric_floating, ft_numeric_floating,
	ft_stringw, ft_stringw,
	ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean,
	ft_datetime, ft_datetime,	
	ft_fulltext
};

// enableDateTimeField is used to indicate if date time fields at the end of the field list are support 
// by currently used Total Commander version.
BOOL enableDateTimeField = FALSE;

#endif