#include "PDFExtractor.h"
#include <process.h>
#include <CharTypes.h>
#include <TextString.h>
#include "xPDFInfo.h"
#include <locale.h>
#include <wchar.h>
#include <strsafe.h>

/**
* @file
* PDF metadata and text extraction class.
* 
* PDF document is opened on a first call of #PDFExtractor::extract or #PDFExtractor::compare functions.
* It stays open while following #PDFExtractor::extract or #PDFExtractor::compare functions calls 
* have the same fileName value. Opening and processing PDF document can take significant
* time, CPU and memory. It is better to keep PDFDoc object active while TC may do
* multiple calls of extract or compare functions in short time.
* When fileName changes, currently open file is closed,
* and new one is open. This works fine until last file in the list/directory. 
* TC doesn't inform plugin that file can be closed. It stays open and cannot be modified,
* moved or deleted. To solve this problem, data extraction runs in another thread.
* If TC doesn't call #PDFExtractor::extract function in 100ms, file is closed.
* 
* @msc
* TC,WDX,PRODUCER,XPDF;
* TC=>WDX [label="ContentGetValueW"];
* WDX=>PRODUCER [label="StartWorkerThread"];
* PRODUCER=>PRODUCER [label="waitForProducer"];
* WDX->PRODUCER [label="PRODUCER EVENT"];
* WDX=>WDX [label="waitForConsumer"];
* PRODUCER=>XPDF [label="doWork"];
* XPDF>>PRODUCER [label="Request"];
* PRODUCER->WDX [label="CONSUMER EVENT"];
* WDX>>TC [label="fieldValue"];
* ...;
* PRODUCER=>PRODUCER [label="close"];
* 
* @endmsc
*
* Similar principle has been used in text extraction. Data offset that TC sends in unitIndex
* cannot be used to jump to a position in PDF. When a block of text is extracted from PDF,
* extraction thread stops and informs TC thread about extracted data. TC compares data
* with search string and informs plugin if document can be closed.
*
* @msc
* TC,WDX,PRODUCER,XPDF,OUTPUT_DEV;
* TC=>WDX [label="ContentGetValueW(unitIndex=0)"];
* WDX=>PRODUCER [label="StartWorkerThread"];
* PRODUCER=>PRODUCER [label="waitForProducer"];
* WDX->PRODUCER [label="PRODUCER EVENT"];
* WDX=>WDX [label="waitForConsumer"];
* PRODUCER=>XPDF [label="doWork"];
* XPDF=>>OUTPUT_DEV [label="outputFunction"];
* OUTPUT_DEV->WDX [label="CONSUMER EVENT"];
* OUTPUT_DEV=>OUTPUT_DEV [label="wait for PRODUCER"];
* WDX>>TC [label="fieldValue"];
* TC=>TC [label="compare"];
* TC=>WDX [label="ContentGetValueW(unitIndex=1)"];
* WDX->OUTPUT_DEV [label="PRODUCER EVENT"];
* OUTPUT_DEV>>XPDF [label="return 0"];
* XPDF=>>OUTPUT_DEV [label="outputFunction"];
* OUTPUT_DEV->WDX [label="CONSUMER EVENT"];
* OUTPUT_DEV=>OUTPUT_DEV [label="wait for PRODUCER"];
* WDX>>TC [label="fieldValue"];
* TC=>TC [label="string found"];
* TC=>WDX [label="ContentGetValueW(unitIndex=-1)"];
* WDX->OUTPUT_DEV [label="cancel, PRODUCER EVENT"];
* WDX=>WDX [label="waitForConsumer"];
* OUTPUT_DEV>>XPDF [label="return 1"];
* XPDF>>PRODUCER [label="Request"];
* PRODUCER->WDX [label="CONSUMER EVENT, close PDF"];
* WDX>>TC [label="result"];
* ...;
* PRODUCER=>PRODUCER [label="waitForProducer"]; 
* @endmsc
*
*/

/**
* The keys required to read the metadata fields. 
*/
static const char* metaDataFields[] =
{
    "Title", "Subject", "Keywords", "Author", "Creator", "Producer"
};

/**
* Constructor.
* Alloc ThreadData object, Critical Section and locale.
*/
PDFExtractor::PDFExtractor()
{
    m_data = new ThreadData();
    InitializeCriticalSection(&m_data->lock);
    m_locale = _create_locale(LC_COLLATE, ".ACP");
}

/**
* Destructor, free allocated resources.
* Don't call abort() function from destructor.
*/
PDFExtractor::~PDFExtractor()
{
    TRACE(L"%hs\n", __FUNCTION__);

    if (m_data)
    {
        if (m_data->handles[CONSUMER_HANDLE])
        {
            CloseHandle(m_data->handles[CONSUMER_HANDLE]);
            m_data->handles[CONSUMER_HANDLE] = nullptr;
        }
        if (m_data->handles[PRODUCER_HANDLE])
        {
            CloseHandle(m_data->handles[PRODUCER_HANDLE]);
            m_data->handles[PRODUCER_HANDLE] = nullptr;
        }
        if (m_data->request.allocated && m_data->request.fieldValue)
        {
            delete[] static_cast<char*>(m_data->request.fieldValue);
            m_data->request.fieldValue = nullptr;
            m_data->request.allocated = false;
        }
        TRACE(L"%hs!CS\n", __FUNCTION__);
        DeleteCriticalSection(&m_data->lock);

        TRACE(L"%hs!data\n", __FUNCTION__);
        delete m_data;
        m_data = nullptr;
    }
    if (m_search)
    {
        TRACE(L"%hs!search\n", __FUNCTION__);
        delete m_search;
        m_search = nullptr;
    }

    if (m_locale)
    {
        TRACE(L"%hs!locale\n", __FUNCTION__);
        _free_locale(m_locale);
        m_locale = nullptr;
    }
}

/**
* Close PdfDoc.
* Set Request::status to closed.
*/
void PDFExtractor::closeDoc()
{
    if (m_doc)
    {
        InterlockedExchange(&m_data->request.status, request_status::closed);
        delete m_doc;
        m_doc = nullptr;
    }
}

/**
* Close PdfDoc and free resources.
*/
void PDFExtractor::close()
{
    if (m_fileName)
    {
        TRACE(L"%hs!%ls\n", __FUNCTION__, m_fileName);
        free(m_fileName);
        m_fileName = nullptr;
    }
    closeDoc();
}

/**
* Open new PDF document if requested file is different than open one.
* Close PDF if requested file name is nullptr.
* Set Request::status to active if new document has been open successfuly.
*
* @return true if PdfDoc is valid
*/
bool PDFExtractor::open()
{
    auto newFile = false;
    EnterCriticalSection(&m_data->lock);
    {
        if (!m_data->request.fileName)
        {
            close();
        }
        else
        {
            if (!m_fileName)
            {
                m_fileName = _wcsdup(m_data->request.fileName);
                newFile = true;
            }
            else if (m_fileName && wcsicmp(m_fileName, m_data->request.fileName))
            {
                close();
                m_fileName = _wcsdup(m_data->request.fileName);
                newFile = true;
            }
        }
    }
    LeaveCriticalSection(&m_data->lock);

    if (newFile)
    {
        closeDoc();
        if (m_fileName)
            m_doc = new PDFDoc(m_fileName, lstrlenW(m_fileName));

        if (m_doc)
        {
            if (m_doc->isOk())
                InterlockedExchange(&m_data->request.status, request_status::active);
            else
            {
                closeDoc();
                EnterCriticalSection(&m_data->lock);
                {
                    m_data->request.result = ft_fileerror;
                }
                LeaveCriticalSection(&m_data->lock);
            }
        }
    }
    return m_doc ? true : false;
}

/**
* Converts string from PDF Unicode to wchar_t.
* 
* @param[out]       dst     converted string
* @param[in,out]    cbDst   size of #dst in bytes
* @param[in]        src     string to convert
* @param[in]        cchSrc  number of unicode characters
* @return number of characters in #dst, 0 if error
*/
ptrdiff_t PDFExtractor::UnicodeToUTF16(wchar_t* dst, int *cbDst, const Unicode* src, int cchSrc)
{
    auto start = dst;
    if (src && dst && cbDst)
    {
        for (int i = 0; (i < cchSrc) && (*cbDst > sizeOfWchar); i++)
        {
            *dst++ = *src++ & 0xFFFF;
            *cbDst -= sizeOfWchar;
        }
        *dst = 0;
        return (dst - start);
    }
    return 0;
}

/**
* Removes characters in #delims from input string.
* 
* @param[in,out]    str     string to be cleaned up
* @param[in]        cchStr  count of characters in string
* @param[in]        delims  characters to clean up from #str
* @return number of characters in #str
*/
size_t PDFExtractor::removeDelimiters(wchar_t* str, size_t cchStr, const wchar_t* delims)
{
    size_t i = 0;
    if (str && (cchStr > 0) && delims)
    {
        size_t n = 0;
        for (; n < cchStr; ++n)
        {
            if (wcschr(delims, str[n]))
                continue;

            if (i != n)
                str[i] = str[n];

            ++i;
        }
        if (i != n)
            str[i] = 0;
    }
    return i;
}

/**
* Converts nibble value to hex character.
*
* @param[in]    nibble   nibble to convert
* @return converted hex character
*/
wchar_t PDFExtractor::nibble2wchar(int nibble)
{
    auto ret = L'x';
    if ((nibble >= 0) && (nibble <= 9))
        ret = nibble + L'0';
    else if ((nibble >= 0x0A) && (nibble <= 0x0F))
        ret = nibble - 0x0A + L'A';

    return ret;
}

/**
* Converts binary value to hex string and appends to destination.
*
* @param[out]   dst     destination string
* @param[in]    cbDst   size of dst in bytes
* @param[in]    value   value to convert to hex string
*/
void PDFExtractor::appendHexValue(wchar_t* dst, int cbDst, int value)
{
    wchar_t tmp[2] = { 0 };

    tmp[0] = nibble2wchar((value >> 4) & 0x0F);
    StringCbCatW(dst, cbDst, tmp);

    tmp[0] = nibble2wchar(value & 0x0F);
    StringCbCatW(dst, cbDst, tmp);
}

/**
* Extract metadata information from PDF and convert to wchar_t.
* Data exchange is guarded in critical section.
* 
* @param[in]    doc     pointer to PDFDoc object
* @param[in]    key     one of values from #metaDataFields
*/
void PDFExtractor::getMetadataString(PDFDoc* doc, const char* key)
{
    Object objDocInfo;
    if (doc->getDocInfo(&objDocInfo)->isDict())
    {
        Object obj;
        auto dict = objDocInfo.getDict();
        if (dict->lookup(key, &obj)->isString())
        {
            TextString ts(obj.getString());
            EnterCriticalSection(&m_data->lock);
            {
                if (UnicodeToUTF16(static_cast<wchar_t*>(m_data->request.fieldValue), &m_data->request.cbfieldValue, ts.getUnicode(), ts.getLength()))
                    m_data->request.result = ft_stringw;
            }
            LeaveCriticalSection(&m_data->lock);
        }
        obj.free();
    }
    objDocInfo.free();
}

/**
* PDF document contains signature fields.
* It is not verified if document is signed or if signature is valid.
*
* @param[in]    doc     pointer to PDFDoc object
* @return true if SigFlags value > 0
*/
BOOL PDFExtractor::hasSignature(PDFDoc* doc)
{
    auto catalog = doc->getCatalog();
    if (catalog)
    {
        auto acroForm = catalog->getAcroForm();
        if (acroForm->isDict())
        {
            Object obj;
            auto dict = acroForm->getDict();
            if (dict->lookup("SigFlags", &obj)->isInt())
            {
                // verify bit positions 1 and 2
                return static_cast<BOOL>(obj.getInt() & 0x03);
            }
            obj.free();
        }
    }
    return FALSE;
}

/**
* Extracts PDF file identifier. 
* This value should be two MD5 strings.
* Data exchange is guarded in critical section.
*
* @param[in]    doc     pointer to PDFDoc object
*/
void PDFExtractor::getDocID(PDFDoc* doc)
{
    Object fileIDObj, fileIDObj1;

    doc->getXRef()->getTrailerDict()->dictLookup("ID", &fileIDObj);
    if (fileIDObj.isArray()) 
    {
        EnterCriticalSection(&m_data->lock);
        {
            auto dst = static_cast<wchar_t*>(m_data->request.fieldValue);
            *dst = 0;
            // convert byte arrays to human readable strings
            for (int i = 0; i < fileIDObj.arrayGetLength(); i++)
            {
                if (fileIDObj.arrayGet(i, &fileIDObj1)->isString())
                {
                    GString* str = fileIDObj1.getString();
                    if (i)
                        StringCbCatW(dst, m_data->request.cbfieldValue, L"-");

                    for (int j = 0; j < str->getLength(); j++)
                    {
                        appendHexValue(dst, m_data->request.cbfieldValue, str->getChar(j));
                    }
                }
                fileIDObj1.free();
            }
            if (*dst)
                m_data->request.result = ft_stringw;
        }
        LeaveCriticalSection(&m_data->lock);
    }
    fileIDObj.free();
}

/**
* PDF document was updated incrementally without rewriting the entire file. 
*
* @param[in]    doc     pointer to PDFDoc object
* @return true if PDF is incremental
*/
BOOL PDFExtractor::isIncremental(PDFDoc* doc)
{
    return (doc->getXRef()->getNumXRefTables() > 1) ? TRUE : FALSE;
}

/**
* From Portable document format - Part 1: PDF 1.7, 
* 14.8 Tagged PDF
* "Tagged PDF (PDF 1.4) is a stylized use of PDF that builds on the logical structure framework described in 14.7, "Logical Structure""
*
* @param[in]    doc     pointer to PDFDoc object
* @return   true if PDF is tagged
*/
BOOL PDFExtractor::isTagged(PDFDoc* doc)
{
    return doc->getStructTreeRoot()->isDict() ? TRUE : FALSE;
}

/**
* "PDF Attribute" field data extraction.
* Data exchange is guarded in critical section.
*
* @param[in]    doc     pointer to PDFDoc object
*/
void PDFExtractor::getMetadataAttrStr(PDFDoc* doc)
{
    EnterCriticalSection(&m_data->lock);
    {
        auto dst = static_cast<wchar_t*>(m_data->request.fieldValue);
        *dst = 0;

        StringCbCatW(dst, m_data->request.cbfieldValue, doc->okToPrint()    ? L"P" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, doc->okToCopy()     ? L"C" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, doc->okToChange()   ? L"M" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, doc->okToAddNotes() ? L"N" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, isIncremental(doc)  ? L"I" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, isTagged(doc)       ? L"T" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, doc->isLinearized() ? L"L" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, doc->isEncrypted()  ? L"E" : L"-");
        StringCbCatW(dst, m_data->request.cbfieldValue, hasSignature(doc)   ? L"S" : L"-");

        if (*dst)
            m_data->request.result = ft_stringw;
    }
    LeaveCriticalSection(&m_data->lock);
}

/**
* "Created" and "Modified" fields data extraction.
* Converts PDF date and time to FILETIME structure.
* Data exchange is guarded in critical section.
*
* @param[in]    doc     pointer to PDFDoc object
* @param[in]    key     "CreationDate" or "ModDate"
*/
void PDFExtractor::getMetadataDate(PDFDoc* doc, const char* key)
{
    Object objDocInfo;
    if (doc->getDocInfo(&objDocInfo)->isDict())
    {
        Object obj;
        auto dict = objDocInfo.getDict();
        if (dict->lookup(key, &obj)->isString())
        {
            const auto acrobatDateTimeString = obj.getString()->getCString();
            if (acrobatDateTimeString && ((strlen(acrobatDateTimeString) == 16) || (strlen(acrobatDateTimeString) == 23)))
            {
                SYSTEMTIME sysTime = { 0 };
                FILETIME fileTime = { 0 };
                char dateTimeString[5] = { 0 };

                // Year
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 2, 4);
                sysTime.wYear = atoi(dateTimeString);
                // Month
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 6, 2);
                sysTime.wMonth = atoi(dateTimeString);
                // Day
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 8, 2);
                sysTime.wDay = atoi(dateTimeString);
                // Hours
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 10, 2);
                sysTime.wHour = atoi(dateTimeString);
                // Minutes
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 12, 2);
                sysTime.wMinute = atoi(dateTimeString);
                // Seconds
                StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 14, 2);
                sysTime.wSecond = atoi(dateTimeString);

                if (SystemTimeToFileTime(&sysTime, &fileTime))
                {
                    // Different timezone given.
                    if (strlen(acrobatDateTimeString) == 23)
                    {
                        StringCchCopyNA(dateTimeString, sizeof(dateTimeString), acrobatDateTimeString + 16, 3);
                        LARGE_INTEGER timeValue;
                        timeValue.HighPart = fileTime.dwHighDateTime;
                        timeValue.LowPart = fileTime.dwLowDateTime;
                        timeValue.QuadPart -= _atoi64(dateTimeString) * 36000000000ULL;
                        fileTime.dwHighDateTime = timeValue.HighPart;
                        fileTime.dwLowDateTime = timeValue.LowPart;
                    }
                    EnterCriticalSection(&m_data->lock);
                    {
                        memcpy(m_data->request.fieldValue, &fileTime, sizeof(FILETIME));
                        m_data->request.result = ft_datetime;
                    }
                    LeaveCriticalSection(&m_data->lock);
                }
            }
        }
        obj.free();
    }
    objDocInfo.free();
}

/** 
* Converts a given point value to the unit given in unitIndex.
*
* @param[in]    pageSizePointsValue   page size in points
*/
void PDFExtractor::getPaperSize(double pageSizePointsValue)
{
    switch (m_data->request.unitIndex)
    {
    case suMilliMeters:
        pageSizePointsValue *= 0.3528;
        break;
    case suCentiMeters:
        pageSizePointsValue *= 0.03528;
        break;
    case suInches:
        pageSizePointsValue *= 0.0139;
        break;
    case suPoints:
        break;
    default:
        pageSizePointsValue = 0.0;
        break;
    }
    getValue(pageSizePointsValue, ft_numeric_floating);
}

/**
* Sets simple result values (BOOL, int and double) to output buffer.
* Data exchange is guarded in critical section.
*
* @tparam       T       typedef of value
* @param        value   value to be set to output buffer
* @param[in]    type    type of result value
*/
template<typename T> 
void PDFExtractor::getValue(T value, int type)
{
    EnterCriticalSection(&m_data->lock);
    {
        *(static_cast<T*>(m_data->request.fieldValue)) = value;
        m_data->request.result = type;
    }
    LeaveCriticalSection(&m_data->lock);
}

/**
* Calls specific extraction functions.
*/
void PDFExtractor::doWork()
{
    switch (m_data->request.fieldIndex)
    {
    case fiTitle:
    case fiSubject:
    case fiKeywords:
    case fiAuthor:
    case fiCreator:
    case fiProducer:
        getMetadataString(m_doc, metaDataFields[m_data->request.fieldIndex]);
        break;
    case fiDocStart:
    case fiFirstRow:
        m_tc.output(m_doc, m_data);
        break;
    case fiNumberOfPages:
        getValue(m_doc->getNumPages(), ft_numeric_32);
        break;
    case fiPDFVersion:
        getValue(m_doc->getPDFVersion(), ft_numeric_floating);
        break;
    case fiPageWidth:
        getPaperSize(m_doc->getPageCropWidth(1));
        break;
    case fiPageHeight:
        getPaperSize(m_doc->getPageCropHeight(1));
        break;
    case fiCopyingAllowed:
        getValue<BOOL>(m_doc->okToCopy(), ft_boolean);
        break;
    case fiPrintingAllowed:
        getValue<BOOL>(m_doc->okToPrint(), ft_boolean);
        break;
    case fiAddCommentsAllowed:
        getValue<BOOL>(m_doc->okToAddNotes(), ft_boolean);
        break;
    case fiChangingAllowed:
        getValue<BOOL>(m_doc->okToChange(), ft_boolean);
        break;
    case fiEncrypted:
        getValue<BOOL>(m_doc->isEncrypted(), ft_boolean);
        break;
    case fiTagged:
        getValue(isTagged(m_doc), ft_boolean);
        break;
    case fiLinearized:
        getValue<BOOL>(m_doc->isLinearized(), ft_boolean);
        break;
    case fiIncremental:
        getValue(isIncremental(m_doc), ft_boolean);
        break;
    case fiSignature:
        getValue(hasSignature(m_doc), ft_boolean);
        break;
    case fiCreationDate:
        getMetadataDate(m_doc, "CreationDate");
        break;
    case fiLastModifiedDate:
        getMetadataDate(m_doc, "ModDate");
        break;
    case fiID:
        getDocID(m_doc);
        break;
    case fiAttributesString:
        getMetadataAttrStr(m_doc);
        break;
    case fiText:
        m_tc.output(m_doc, m_data);
        break;
    default:
        break;
    }
    // change status from active to complete
    InterlockedCompareExchange(&m_data->request.status, request_status::complete, request_status::active);

    TRACE(L"%hs!%d complete\n", __FUNCTION__, m_data->request.fieldIndex);
}

/**
* Extractor thread main function.
* To start extraction, set request params and raise producer event from TC thread.
* When extraction is complete, raises consumer event to wake TC thread up.
* To exit thread, TC must set active to 0 and raise producer event.
*/
void PDFExtractor::waitForProducer()
{
    long status;
    DWORD dwRet;
    InterlockedExchange(&m_data->active, TRUE);
    while (InterlockedOr(&m_data->active, 0))
    {
        // !!! produder idle point !!!
        dwRet = WaitForSingleObject(m_data->handles[PRODUCER_HANDLE], PRODUCER_TIMEOUT);
        if (dwRet == WAIT_OBJECT_0)
        {
            status = InterlockedOr(&m_data->request.status, 0);
            if (status != request_status::canceled)
            {
                if (open())
                    doWork();

                // check status after extraction is complete
                status = InterlockedOr(&m_data->request.status, 0);
            }
            // inform consumer that extraction is complete or cancelled
            SetEvent(m_data->handles[CONSUMER_HANDLE]);
            if (status == request_status::canceled)
                close();
        }
        else if (dwRet == WAIT_TIMEOUT)
        {
            // if there are no new requests, close PDFDoc
            close();
        }
        else
        {
            // set thread exit flag
            InterlockedExchange(&m_data->active, FALSE);
        }
    }
    // thread is about to exit, close PDFDoc
    close();
}

/**
* Extraction thread entry function.
* This is a static function, there is no access to non-static functions.
* Ponter to PDFExtractor object (this) is passed in function parameter.
* 
* @param[in]    param   pointer to PDFExtractor object (this)
* @return 0
*/
unsigned int __stdcall threadFunc(void* param)
{
    if (param)
    {
        auto extractor = static_cast<PDFExtractor*>(param);
        extractor->waitForProducer();
    }
    TRACE(L"%hs!end thread\n", __FUNCTION__);
    _endthreadex(0);
    return 0;
}

/**
* Start extraction thread, if not already started.
* Create unnamed events with automatic reset.
*
* @return thread ID number
*/
unsigned int PDFExtractor::startWorkerThread()
{
    unsigned int threadID = 0;

    if (!m_data->handles[CONSUMER_HANDLE])
        m_data->handles[CONSUMER_HANDLE] = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (!m_data->handles[PRODUCER_HANDLE])
        m_data->handles[PRODUCER_HANDLE] = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (m_data->handles[CONSUMER_HANDLE] && m_data->handles[PRODUCER_HANDLE])
    {
        // if thread is not started..
        if (!m_data->handles[THREAD_HANDLE])
        {
            // start new thread
            m_data->handles[THREAD_HANDLE] = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, threadFunc, this, 0, &threadID));
            if (m_data->handles[THREAD_HANDLE])
            {
                // wait a little bit for thread to start...
                auto dwRet = WaitForSingleObject(m_data->handles[THREAD_HANDLE], 10);
                if (dwRet != WAIT_TIMEOUT)
                    threadID = 0;
            }
        }
        else
        {
            // get running thread ID
            threadID = GetThreadId(m_data->handles[THREAD_HANDLE]);
        }
    }
    return threadID;
}

/**
* Raise producer event to start extraction and wait for consumer event.
* If consumer doesn't respond in #CONSUMER_TIMEOUT, function returns #ft_fieldempty.
* 
* @return result of an extraction
*/
int PDFExtractor::waitForConsumer()
{
    int result = ft_fileerror;
    if (InterlockedOr(&m_data->active, 0) && m_data->handles[PRODUCER_HANDLE] && m_data->handles[CONSUMER_HANDLE])
    {
        auto dwRet = SignalObjectAndWait(m_data->handles[PRODUCER_HANDLE], m_data->handles[CONSUMER_HANDLE], CONSUMER_TIMEOUT, FALSE);
        switch (dwRet)
        {
        case WAIT_OBJECT_0:
            EnterCriticalSection(&m_data->lock);
            {
                result = m_data->request.result;
            }
            LeaveCriticalSection(&m_data->lock);
            break;
        case WAIT_TIMEOUT:
            InterlockedCompareExchange(&m_data->request.status, request_status::canceled, request_status::active);
            result = ft_fieldempty;
            break;
        default:
            InterlockedCompareExchange(&m_data->request.status, request_status::canceled, request_status::active);
            result = ft_fileerror;
            break;
        }

        TRACE(L"%hs!consumer!dw=%lu result=%d\n", __FUNCTION__, dwRet, result);
    }
    return result;
}

/**
* Assign data from TC to internal structure.
* Data exchange is guarded in critical section.
* If TC doesn't provide buffer for output data (compare), a new buffer is created.
*
* @param[in]    fileName        full path to PDF document
* @param[in]    fieldIndex      index of the field
* @param[in]    unitIndex       index of the unit, -1 for fiText field when searched string is found
* @param[in]    fieldValue      buffer for retrieved data
* @param[in]    cbfieldValue    sizeof buffer in bytes
* @param[in]    flags           TC flags
* @param[in]    timeout         producer timeout (in text extraction)
* @return       ft_fieldempty if data cannot be set, ft_setsuccess if successfuly set
*/
int PDFExtractor::initData(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags, DWORD timeout)
{
    auto status = InterlockedOr(&m_data->request.status, 0);

    if (   (status == request_status::canceled)                                                 // extraction is cancelled, but PDFDoc isn't closed yet
        || ((status == request_status::active)   && (unitIndex == 0))                           // previous extraction is still active
        || ((status == request_status::closed)   && (unitIndex > 0) && (fieldIndex == fiText))  // extraction is closed but TC wants next text block
        || ((status == request_status::complete) && (unitIndex > 0) && (fieldIndex == fiText))  // extraction is completed but TC wants next text block
        )
    {
        return ft_fieldempty;
    }

    EnterCriticalSection(&m_data->lock);
    {
        // TC didn't provide output buffer, probably compare function
        if (!fieldValue)
        {
            // output buffer wasn't created yet
            if (!m_data->request.allocated)
            {
                // create one
                m_data->request.fieldValue = new char[DEFAULT_FIELD_CB];
                m_data->request.allocated = true;
            }
            cbfieldValue = DEFAULT_FIELD_CB;
        }
        else
        {
            // TC provided output buffer, but we also created one
            if (m_data->request.allocated)
            {
                // release alocated buffer
                m_data->request.allocated = false;
                delete[] static_cast<char*>(m_data->request.fieldValue);
            }
            // assign request buffer
            m_data->request.fieldValue = fieldValue;
        }
        m_data->request.fileName = fileName;
        m_data->request.fieldIndex = fieldIndex;
        m_data->request.unitIndex = unitIndex;
        m_data->request.ptr = m_data->request.fieldValue;   // set string end to begining of buffer
        m_data->request.cbfieldValue = cbfieldValue;
        m_data->request.flags = flags;
        m_data->request.result = ft_fieldempty;
        m_data->request.timeout = timeout;
    }
    LeaveCriticalSection(&m_data->lock);

    return ft_setsuccess;
}

/**
* Starts data extraction form PDF document.
* Thread state is changed from complete to active to enable new request.
* Producer timeout is set to low value, because producer is TC. It should respond in short time.
*
* @param[in]    fileName        full path to PDF document
* @param[in]    fieldIndex      index of the field
* @param[in]    unitIndex       index of the unit, -1 for fiText field when searched string is found
* @param[out]   fieldValue      buffer for retrieved data
* @param[in]    cbfieldValue    sizeof buffer in bytes
* @param[in]    flags           TC flags
* @return       result of an extraction
*/
int PDFExtractor::extract(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags)
{
    int result = initData(fileName, fieldIndex, unitIndex, fieldValue, cbfieldValue, flags, PRODUCER_TIMEOUT);
    if (result != ft_setsuccess)
        return result;

    InterlockedCompareExchange(&m_data->request.status, request_status::active, request_status::complete);
    if (fieldIndex == fiText)
    {
        if (unitIndex == -1)
        {
            stop();
            result = ft_fieldempty;
        }
        else if (unitIndex == 0)
        {
            if (startWorkerThread())
                result = waitForConsumer();
        }
        else
            result = waitForConsumer();
    }
    else
    {
        if (startWorkerThread())
            result = waitForConsumer();
    }
    return result;
}

/**
* Notifiy text extracting threads that the state of requests is changed.
* Threads should close PdfDocs and exit.
*/
void PDFExtractor::abort()
{
    // if thread is active, mark it as inactice
    if (InterlockedCompareExchange(&m_data->active, FALSE, TRUE))
    {
        // if extraction is active, mark it as cancelled
        InterlockedCompareExchange(&m_data->request.status, request_status::canceled, request_status::active);
        EnterCriticalSection(&m_data->lock);
        {
            m_data->request.fileName = nullptr;
        }
        LeaveCriticalSection(&m_data->lock);
        if (m_data->handles[PRODUCER_HANDLE] && m_data->handles[THREAD_HANDLE])
        {
            TRACE(L"%hs\n", __FUNCTION__);
            // raise producer event to wake thread up, and wait until thread exits
            SignalObjectAndWait(m_data->handles[PRODUCER_HANDLE], m_data->handles[THREAD_HANDLE], PRODUCER_TIMEOUT, FALSE);
        }
    }
    if (m_data->handles[THREAD_HANDLE])
    {
        CloseHandle(m_data->handles[THREAD_HANDLE]);
        m_data->handles[THREAD_HANDLE] = nullptr;
    }

    if (m_search)
        m_search->abort();
}

/**
* Notify text extracting threads that eh state of requests is changed.
* Threads should return back to idle point in waitForProducer and close PdfDocs.
*/
void PDFExtractor::stop()
{
    // if extraction is active, mark it as cancelled
    auto status = InterlockedCompareExchange(&m_data->request.status, request_status::canceled, request_status::active);
    if (status == request_status::active)
    {
        EnterCriticalSection(&m_data->lock);
        {
            m_data->request.fileName = nullptr;
        }
        LeaveCriticalSection(&m_data->lock);
        if (InterlockedOr(&m_data->active, 0) && m_data->handles[PRODUCER_HANDLE] && m_data->handles[CONSUMER_HANDLE])
        {
            TRACE(L"%hs\n", __FUNCTION__);
            SignalObjectAndWait(m_data->handles[PRODUCER_HANDLE], m_data->handles[CONSUMER_HANDLE], CONSUMER_TIMEOUT, FALSE);
        }
    }
    if (m_search)
        m_search->stop();
}

/**
* Notifiy text extracting threads that the state of requests is changed.
* Threads should return back to idle point in #waitForProducer without closing PdfDocs.
*/
void PDFExtractor::done()
{
    auto status = InterlockedCompareExchange(&m_data->request.status, request_status::complete, request_status::active);
    if (status == request_status::active)
    {
        if (InterlockedOr(&m_data->active, 0) && m_data->handles[PRODUCER_HANDLE] && m_data->handles[CONSUMER_HANDLE])
        {
            TRACE(L"%hs\n", __FUNCTION__);
            SignalObjectAndWait(m_data->handles[PRODUCER_HANDLE], m_data->handles[CONSUMER_HANDLE], CONSUMER_TIMEOUT, FALSE);
        }
    }
    if (m_search)
        m_search->done();
}

/**
* Start data extraction and compare extracted data from two PDF documents.
* If extracted data is binary identical, function returns #ft_compare_eq.
* If data are not binary identical, delimiters are removed and text is compared case-insensitive.
* If data are textualy identical, function returns ft_compare_eq_txt.
* If both data fields are empty, function returns #ft_compare_eq.
* 
* @param[in]    progresscallback    pointer to callback function to inform the calling program about the compare progress
* @param[in]    fileName1           first file name to be compared
* @param[in]    fileName2           second file name to be compared
* @param[in]    compareIndex        field data to compare
* @return result of comparision
*/
int PDFExtractor::compare(PROGRESSCALLBACKPROC progresscallback, const wchar_t* fileName1, const wchar_t* fileName2, int compareIndex)
{
    static const wchar_t delims[] = L" \r\n\b\f\t\v\x00a0\x202f\x2007\x2009\x2060";
    auto bytesProcessed = 0;
    auto eq_txt = false;

    // set timeout to long wait, because it waits for another extraction thread
    auto result = initData(fileName1, compareIndex, 0, nullptr, 0, 0, CONSUMER_TIMEOUT);
    if (result != ft_setsuccess)
        return ft_compare_next;

    if (!m_search)
        m_search = new PDFExtractor();

    // set timeout to long wait, because it waits for another extraction thread to complete
    result = m_search->initData(fileName2, compareIndex, 0, nullptr, 0, 0, CONSUMER_TIMEOUT);
    if (result != ft_setsuccess)
        return ft_compare_next;

    // change status from complete to active
    InterlockedCompareExchange(&m_data->request.status, request_status::active, request_status::complete);
    InterlockedCompareExchange(&m_search->m_data->request.status, request_status::active, request_status::complete);

    // start threads
    if (startWorkerThread() && m_search->startWorkerThread())
    {
        // get start time
        auto startCounter = GetTickCount64();
        do
        {
            // wait for consumers to extract data
            result = waitForConsumers();
            // time spent in extraction = now - start
            auto now = GetTickCount64();
            if (result > 0)
            {
                // extraction completed successfuly, mark result as not equal
                result = ft_compare_not_eq;

                // protect data from both threads
                EnterCriticalSection(&m_data->lock);
                {
                    EnterCriticalSection(&m_search->m_data->lock);
                    {
                        // cast from void* to wchar_t*
                        auto start1 = static_cast<wchar_t*>(m_data->request.fieldValue);
                        auto len1 =  lstrlenW(start1);

                        auto start2 = static_cast<wchar_t*>(m_search->m_data->request.fieldValue);
                        auto len2 = lstrlenW(start2);
                        // string len to compare
                        auto min_len = len1 < len2 ? len1 : len2;
                    
                        if (min_len > 0)
                        {
                            // compare binary
                            if (!wmemcmp(start1, start2, min_len))
                            {
                                TRACE(L"%hs!binary!%Iu wchars equal\n", __FUNCTION__, min_len);
                                bytesProcessed += min_len;
                                result = ft_compare_eq;
                            }
                            else
                            {
                                // remove delimiters, spaces
                                auto len1X = removeDelimiters(start1, len1, delims);
                                auto len2X = removeDelimiters(start2, len2, delims);
                                // string len to compare
                                auto min_lenX = len1X < len2X ? len1X : len2X;
                                if (min_lenX > 0)
                                {
                                    // compare as text, case-insensitive, using locale specific information
                                    if (!_wcsnicoll_l(start1, start2, min_lenX, m_locale))
                                    {
                                        TRACE(L"%hs!text!%Iu wchars equal\n", __FUNCTION__, min_lenX);
                                        bytesProcessed += min_lenX;
                                        result = ft_compare_eq;
                                        eq_txt = true;
                                    }
                                    else
                                    {
                                        // text is not equal, abort
                                        TRACE(L"%hs!not equal!'%ls' != '%ls'\n", __FUNCTION__, start1, start2);
                                        LeaveCriticalSection(&m_data->lock);
                                        LeaveCriticalSection(&m_search->m_data->lock);

                                        break;
                                    }
                                }
                                else if (len1X == len2X)
                                {
                                    TRACE(L"%hs!empty text\n", __FUNCTION__);
                                    result = ft_compare_eq;
                                    eq_txt = true;
                                }
                            }
                        }
                        else if (len1 == len2)
                        {
                            TRACE(L"%hs!no data\n", __FUNCTION__);
                            result = ft_compare_eq;
                        }

                        if ((result == ft_compare_eq) && (min_len > 0) && ((len1 > min_len) || (len2 > min_len)))
                        {
                            // discard compared data
                            if (len1 >= min_len)
                                wmemmove(start1, start1 + min_len, len1 - min_len);

                            if (len2 >= min_len)
                                wmemmove(start2, start2 + min_len, len2 - min_len);

                            // part of a string was equal, compare rest
                            result = ft_compare_not_eq;
                        }

                        // adjust string end pointer and remaining buffer size
                        m_data->request.ptr = start1 + len1 - min_len;
                        m_data->request.cbfieldValue += (min_len * sizeOfWchar);

                        m_search->m_data->request.ptr = start2 + len2 - min_len;
                        m_search->m_data->request.cbfieldValue += (min_len * sizeOfWchar);
                    }
                    LeaveCriticalSection(&m_search->m_data->lock);
                }
                LeaveCriticalSection(&m_data->lock);
            }
            else
            {
                // no data extracted in both files
                if (result == ft_fieldempty)
                {
                    TRACE(L"%hs!empty fields\n", __FUNCTION__);
                    // both fields are equaly "empty"
                    result = ft_compare_eq;
                }
                else
                {
                    // error
                    TRACE(L"%hs!error\n", __FUNCTION__);
                }
                break;
            }
            if (progresscallback && (now - startCounter > PRODUCER_TIMEOUT))
            {
                // inform TC about progress
                if (progresscallback(bytesProcessed))
                {
                    // abort by user
                    TRACE(L"%hs!user abort\n", __FUNCTION__);
                    result = ft_compare_abort;
                    break;
                }
                // reset counter
                bytesProcessed = 0;
                startCounter = now;
            }

        } 
        while (   (request_status::active == InterlockedOr(&m_data->request.status, 0)) 
               && (request_status::active == InterlockedOr(&m_search->m_data->request.status, 0))
              );

        // if data was once compared as text, it is not binary equal
        if ((result == ft_compare_eq) && eq_txt)
            result = ft_compare_eq_txt;

        // don't close PDFDocs, they may be used again
        done();
        m_search->done();
    }
    else
    {
        TRACE(L"%hs!unable to start threads\n", __FUNCTION__);
    }
    return result;
}

/**
* Trigger data extraction in open PDF documents.
* Wait until both threads return data.
*
* @return result of extraction
*/
int PDFExtractor::waitForConsumers()
{
    auto result = ft_fileerror;
    auto result1 = ft_fileerror;
    auto result2 = ft_fileerror;

    if (InterlockedOr(&m_data->active, 0) && InterlockedOr(&m_search->m_data->active, 0)
        && m_data->handles[PRODUCER_HANDLE] && m_data->handles[CONSUMER_HANDLE]
        && m_search->m_data->handles[PRODUCER_HANDLE] && m_search->m_data->handles[CONSUMER_HANDLE])
    {
        HANDLE consumers[] = { m_data->handles[CONSUMER_HANDLE] , m_search->m_data->handles[CONSUMER_HANDLE] };

        SetEvent(m_data->handles[PRODUCER_HANDLE]);
        SetEvent(m_search->m_data->handles[PRODUCER_HANDLE]);

        // wait unitl both threads signal that they completed extraction
        auto dwRet = WaitForMultipleObjects(ARRAYSIZE(consumers), consumers, TRUE, CONSUMER_TIMEOUT);
        switch (dwRet)
        {
        case WAIT_OBJECT_0:
            EnterCriticalSection(&m_data->lock);
            {
                result1 = m_data->request.result;
            }
            LeaveCriticalSection(&m_data->lock);
            EnterCriticalSection(&m_search->m_data->lock);
            {
                result2 = m_search->m_data->request.result;
            }
            LeaveCriticalSection(&m_search->m_data->lock);

            // compare results
            result = (result1 == result2) ? result1 : ft_compare_not_eq;
            break;
        case WAIT_TIMEOUT:
            result = ft_compare_abort;
            break;
        default:
            result = ft_compare_abort;
            break;
        }

        TRACE(L"%hs!consumers!dw=%lu result=%d\n", __FUNCTION__, dwRet, result);
    }
    return result;
}
