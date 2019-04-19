#include "xPDFInfo.h"
#include <wchar.h>
#include "PDFExtractor.h"
#include <GlobalParams.h>
#include <strsafe.h>

/**
* @file 
* TotalCommander context plugin (wdx, wdx64) for PDF data extraction and comparision.
* Based on xPDF v4.01 from Glyph & Cog, LLC.
*/

/** enableDateTimeField is used to indicate if date time fields are supported by currently used Total Commander version. */
static auto enableDateTimeField = false;
/** enableCompareFields is used to indicate if compare fields are supported by currently used Total Commander version. */
static auto enableCompareFields = false;

/**
* Names of fields returned to TC.
* Names are grouped by field types.
*/
static const char* fieldNames[FIELD_COUNT] =
{
    "Title", "Subject", "Keywords", "Author", "Application", "PDF Producer", "Document Start", "First Row",
    "Number Of Pages",
    "PDF Version", "Page Width", "Page Height",
    "Copying Allowed", "Printing Allowed", "Adding Comments Allowed", "Changing Allowed", "Encrypted", "Tagged", "Linearized", "Incremental", "Signature Field",
    "Created", "Modified",
    "ID", "PDF Attributes",
    "Text"
};

/** Array used to simplify fieldType returning. */
const int fieldTypes[FIELD_COUNT] =
{
    ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw, ft_stringw,
    ft_numeric_32,
    ft_numeric_floating, ft_numeric_floating, ft_numeric_floating,
    ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean, ft_boolean,
    ft_datetime, ft_datetime,
    ft_stringw, ft_stringw,
    ft_fulltext
};
/** Supported field flags, special value for attributes */
const int fieldFlags[FIELD_COUNT] =
{
    0, 0, 0, 0, 0, 0, 0, 0, 
    0,
    0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    0, contflags_substattributestr,
    0
};

/**< Only one instance of PDFExtractor per thread. */
#if defined(_MSC_VER) && (_MSC_VER < 1900)
__declspec(thread)
#else
thread_local
#endif
static PDFExtractor* g_extractor = nullptr;

#ifdef _DEBUG
/** Writes debug trace.
* Please note that output trace is limited to 1024 characters!
*
* @param[in] format format options
* @param[in] ...     optional arguments
*
*/
bool __cdecl _trace(const wchar_t *format, ...)
{
    wchar_t buffer[1024];
    wchar_t* end = nullptr;
    size_t remaining = 0;
    SYSTEMTIME now;

    GetLocalTime(&now);
    StringCchPrintfExW( buffer, ARRAYSIZE(buffer)
                      , &end, &remaining
                      , STRSAFE_NULL_ON_FAILURE
                      , L"%.2u%.2u%.2u.%.3u!%.5lu!"
                      , now.wHour, now.wMinute, now.wSecond, now.wMilliseconds
                      , GetCurrentThreadId()
                      );

    va_list argptr;
    va_start(argptr, format);
    StringCchVPrintfW(end, remaining, format, argptr);
    va_end(argptr);

    OutputDebugStringW(buffer);

    return true;
}
#endif

/**
* Destroys PDFExtractor instance.
* Before destruction, abort() is called to exit threads.
* It may take some time to exit thread if text extraction is in progress.
*/
static void destroy()
{
    if (g_extractor)
    {
        TRACE(L"%hs\n", __FUNCTION__);
        g_extractor->abort();
        delete g_extractor;
        g_extractor = nullptr;
    }
}
 /**
 * DLL (wdx) entry point.
 * When TC needs service from this plugin for the first time,
 * DllMain is called with DLL_PROCESS_ATTACH value. 
 * It may be called from TC's main GUI thread, or from woker threads.
 * xPDF globalParam structure is initialized with default values.
 * xPDF settings can be changed by putting <b>xpdfrc</b> file to the directory where wdx is located.
 *
 * When plugin is unloaded or TC is closing, DLL_PROCESS_DETACH is called.
 */
BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID)
{
    TRACE(L"%hs!%lu\n", __FUNCTION__, reason);
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        // Initialize globally used resources.
        globalParams = new GlobalParams(nullptr);
        globalParams->setTextEncoding("UCS-2");         // extracted text encoding (not for metadata)
        globalParams->setTextPageBreaks(gFalse);        // don't add \f for page breaks
        globalParams->setTextEOL("unix");               // extracted text line endings
        break;
    case DLL_PROCESS_DETACH:
        destroy();              // Release PDFExtractor instance, if any
        TRACE(L"%hs!globalParams\n", __FUNCTION__);
        delete globalParams;    // Clean up
        globalParams = nullptr;
        break;
    case DLL_THREAD_DETACH:
        destroy();              // Release PDFExtractor instance, if any. Don't clean up globalParams
        break;
    }
    return TRUE;
}
/**
* Returns PDF detection string.
* @param[out]    DetectString    detection buffer
* @param[in]     maxlen          detection buffer size in chars
* @return 0     unused
*/
int __stdcall ContentGetDetectString(char* DetectString, int maxlen)
{
    // PDF files are all we can handle.
    StringCchCopyA(DetectString, maxlen, "EXT=\"PDF\"");
    return 0;
}

/**
* See "Content Plugin Interface" document
* xpdfsearch supports indexes in ranges 0 to 25 and 10000 to 10025.
* Range above 10000 is reserved for directory synchronization functions
* @param[in]    FieldIndex     index
* @param[out]   FieldName      name of requested FieldIndex
* @param[in]    Units          "mm|cm|in|pt" for FieldIndex=fiPageWidth and fiPageHeight
* @param[in]    maxlen         size of Units and FieldName in chars
* @return type of requested field
*/
int __stdcall ContentGetSupportedField(int FieldIndex, char* FieldName, char* Units, int maxlen)
{
    TRACE(L"%hs!index=%d\n", __FUNCTION__, FieldIndex);

    // clear units
    Units[0] = 0;

    // set field names for compare indexes
    if ((FieldIndex >= ft_comparebaseindex) && (FieldIndex < ft_comparebaseindex + FIELD_COUNT))
    {
        if (enableCompareFields)
        {
            StringCchPrintfA(FieldName, maxlen, "Compare %s", fieldNames[FieldIndex - ft_comparebaseindex]);
            return ft_comparecontent;
        }
        return ft_nomorefields;
    }

    // Exclude date time fields in older TC versions.
    if ((FieldIndex < 0) || (FieldIndex >= FIELD_COUNT) ||
        (!enableDateTimeField && ((FieldIndex == fiCreationDate) || (FieldIndex == fiLastModifiedDate))))
    {
        return ft_nomorefields;
    }

    // set field names
    StringCchCopyA(FieldName, maxlen, fieldNames[FieldIndex]);

    //set units names
    if ((FieldIndex == fiPageWidth) || (FieldIndex == fiPageHeight))
        StringCchCopyA(Units, maxlen, "mm|cm|in|pt");

    return fieldTypes[FieldIndex];
}
/**
* Plugin state change.
* See "Content Plugin Interface" document.
* When TC reads new directory or re-reads current one, close open PDF.
* @param[in]    state   state value.
* @param[in]    path    current path
*/
void __stdcall ContentSendStateInformationW(int state, const wchar_t* path)
{
    TRACE(L"%hs!%d\n", __FUNCTION__, state);
   switch (state)
   {
   case contst_readnewdir:
       if (g_extractor)
           g_extractor->stop();
       break;
   default:
       break;
   }
}
/**
* ANSI version of ContentGetValue is not supported
*/
int __stdcall ContentGetValue(const char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags)
{
    TRACE(L"%hs\n", __FUNCTION__);
    return ft_notsupported;
}

/**
* Retrieves the value of a specific field for a given PDF document.
* See "Content Plugin Interface" document.
* Creates PDFExtractor object, if not already created, calls extraction function.
* If fieldIndex is out of bounds, current PDF document is closed.
*
* @param[in]    fileName        full path to PDF document
* @param[in]    fieldIndex      index of the field
* @param[in]    unitIndex       index of the unit,  -1 for ft_fulltext type fields when searched string is found
* @param[out]   fieldValue      buffer for retrieved data
* @param[in]    cbfieldValue    sizeof buffer in bytes
* @param[in]    flags           TC flags
* @return       result of an extraction
*/
int __stdcall ContentGetValueW(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags)
{
    TRACE(L"%hs!%ls!%d %d\n", __FUNCTION__, fileName, fieldIndex, unitIndex);

    if ((fieldIndex >= fiTitle) && (fieldIndex <= fiText))
    {
        if (CONTENT_DELAYIFSLOW & flags)
            return ft_delayed;

        if (!g_extractor)
            g_extractor = new PDFExtractor();

        if (g_extractor)
            return g_extractor->extract(fileName, fieldIndex, unitIndex, fieldValue, cbfieldValue, flags);
        
        return ft_fileerror;
    }
    else if (g_extractor)
        g_extractor->stop();

    return ft_nomorefields;
}
/**
* Checks for version of currently used Total Commander / version of plugin interface.
* If plugin interface is lower than 1.2, PDF date and time fields are not supported.
* If plugin interface is lower than 2.1, compare by content fields are not supported.
*
* @param[in]    dps see ContentDefaultParamStruct in "Content Plugin Interface" document.
*/
void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps)
{
    TRACE(L"%hs\n", __FUNCTION__);
    // Check content plugin interface version to enable fields of type datetime.
    enableDateTimeField = ((dps->PluginInterfaceVersionHi == 1) && (dps->PluginInterfaceVersionLow >= 2)) || (dps->PluginInterfaceVersionHi > 1);
    enableCompareFields = ((dps->PluginInterfaceVersionHi == 2) && (dps->PluginInterfaceVersionLow >= 10)) || (dps->PluginInterfaceVersionHi > 2);
}

/**
* Plugin is beeing unloaded. Close extraction thread.
* This function is called only form main GUI thread.
* Don't free globalParams form here, because other worker threads
* may be using it.
*/
void __stdcall ContentPluginUnloading()
{
    TRACE(L"%hs\n", __FUNCTION__);
    if (g_extractor)
        g_extractor->abort();
}

/**
* Directory change has occurred, stop extraction.
* See "Content Plugin Interface" document.
* @param[in]  fileName      not used
*/
void __stdcall ContentStopGetValueW(const wchar_t* fileName)
{
    TRACE(L"%hs\n", __FUNCTION__);
    if (g_extractor)
        g_extractor->stop();
}
/**
* ContentGetSupportedFieldFlags is called to get various information about a plugin variable.
* See "Content Plugin Interface" document.
* Only "PDF Attributes" fields has non-default flag.
* 
* @param[in]    FieldIndex  index of the field for which flags should be returned
* @return flags for selected field
*/
int __stdcall ContentGetSupportedFieldFlags(int FieldIndex)
{
    if (FieldIndex == -1)
        return contflags_substmask;

    if ((FieldIndex >= fiTitle) && (FieldIndex <= fiText))
        return fieldFlags[FieldIndex];

    return 0;
}
/**
* ContentCompareFiles is called in Synchronize dirs to compare two files by content.
* xPDFsearch provides option to compare content of all exposed fields.
* Content of field for each file is extracted in it's own thread.
* 
* @param[in] progresscallback   callback function to inform the calling program about the compare progress
* @param[in] compareindex       field to compare, staring with 10000
* @param[in] filename1          first name to be compared
* @param[in] filename2          second name to be compated
* @param[in] filedetails        not used
* @return result of comparision. For values see "Content Plugin Interface" document.
*/
int __stdcall ContentCompareFilesW(PROGRESSCALLBACKPROC progresscallback, int compareindex, WCHAR* filename1, WCHAR* filename2, FileDetailsStruct* filedetails)
{
    if ((compareindex < ft_comparebaseindex) || (compareindex >= ft_comparebaseindex + FIELD_COUNT))
        return ft_compare_next;

    if (!g_extractor)
        g_extractor = new PDFExtractor();

    if (g_extractor)
        return g_extractor->compare(progresscallback, filename1, filename2, compareindex - ft_comparebaseindex);

    return ft_compare_next;
}
