#include "TcOutputDev.h"
#include "contentplug.h"
#include "xPDFInfo.h"

/**
* @file
* PDF text extraction class and callback functions.
*/
/**
* Converts input string to UTF-16 used for wchar_t, changes byte endianess.
* It filters out \\f and \\b delimiters.
* 
* @param[in]        src     string to be converted
* @param[in]        cbSrc   number of chars in src
* @param[out]       dst     converted string
* @param[in,out]    cbDst   size of des in bytes!!!
* @return number of wchars_ put to dst
*/
static ptrdiff_t convertToUTF16(const char* src, int cbSrc, wchar_t* dst, int *cbDst)
{
    auto start = dst;
    for (int i = 0; (i < cbSrc) && (*cbDst > sizeOfWchar); i += sizeOfWchar)
    {
        // swap bytes
        *dst = (*(src + i + 1) & 0xFF) | ((*(src + i) << 8) & 0xFF00);
        // filter \b and \f
        if ((*dst != L'\f') && (*dst != L'\b'))
        {
            ++dst;
            *cbDst -= sizeOfWchar;  // decrease available buffer size in bytes
        }
    }
    *dst = 0;   // put NUL character at the end of the string

    return (dst - start);
}
/**
* Callback function used in PdfDoc::displayPage to abort text extraction.
* If ThreadData::request::status is not request_status::active, extraction should abort.
* 
* @param[in] stream     pointer to ThreadData structure
* @return gTrue if extraction should abort
*/
static GBool abortExtraction(void* stream)
{
    if (stream)
    {
        auto data = static_cast<ThreadData*>(stream);
        return (request_status::active == InterlockedOr(&data->request.status, 0)) ? gFalse : gTrue;
    }
    return gTrue;
}

/**
* Callback function used in PdfDoc::displayPage used to copy extracted text to request structure.
* For "First Row" field, text is extracted up to first EOL.
* For "Document Start" field, request::cbfieldValue bytes is extracted.
* For "Text" field, data is extracted until TC responds that search string is found.
* To be able to continue to extract text, threading has been used. When block of text has been extracted,
* calling thread is woken up to send data to TC. This thread goes to sleep. TC compares data and sends back result.
* This thread wakes up and continues text extraction or canceles if string has been found.
* TcOutputDev.c has been modified to speedup extraction cancelation when string is found.
* Extracted text is converted to UTF-16 and stored to ThreadData::Request::fieldValue buffer.
* This callback function may be called multiple times before ThreadData::Request::fieldValue is filled up, or line ending has been found.
* 
* @param[in,out]    stream      pointer to ThreadData structure
* @param[in]        text        extracted text
* @param[in]        len         length of extracted text
* @return   0 - extraction shuld continue, 1 - extraction should abort
*/
static int outputFunction(void *stream, const char *text, int len)
{
    auto data = static_cast<ThreadData*>(stream);
    if (data && (request_status::active == InterlockedOr(&data->request.status, 0)) && text && (len > 0))
    {
        int remaining, index;
        DWORD timeout;
        EnterCriticalSection(&data->lock);
        {
            // get data from request structure for later use outside of CriticalSection
            timeout = data->request.timeout;
            remaining = data->request.cbfieldValue;
            index = data->request.fieldIndex;

            // get end of current string
            auto dst = static_cast<wchar_t*>(data->request.ptr);
            // convert data from TextOutputDev to wchar_t
            auto dstLen = convertToUTF16(text, len, dst, &data->request.cbfieldValue);
            if (dstLen)
            {
                if (index == fiFirstRow)
                {
                    data->request.result = ft_stringw;
                    // search for EOL
                    auto pos = wcspbrk(dst, L"\r\n");
                    if (pos)
                    {
                        // EOL found!
                        *pos = 0;       // remove EOL
                        remaining = 0;  // flag to exit extraction
                    }
                }
                else if (index == fiDocStart)
                    data->request.result = ft_stringw;
                else
                    data->request.result = ft_fulltextw;

                dst += dstLen;
                // update end of string pointer
                data->request.ptr = dst;
            }
        }
        LeaveCriticalSection(&data->lock);

        // if no bytes left in dest buffer
        if (remaining <= 2)
        {
            if (index == fiText)
            {
                if (data->handles[CONSUMER_HANDLE] && data->handles[PRODUCER_HANDLE])
                {
                    // signal to TC that data is ready and wait for TC to respond
                    auto dwRet = SignalObjectAndWait(data->handles[CONSUMER_HANDLE], data->handles[PRODUCER_HANDLE], timeout, FALSE);
                    if (dwRet != WAIT_OBJECT_0)
                    {
                        InterlockedCompareExchange(&data->request.status, request_status::canceled, request_status::active);
                        TRACE(L"%hs!dw=%lu!TC not responding\n", __FUNCTION__, dwRet);
                        return 1;
                    }
                }
            }
            else
            {
                // extraction is complete
                InterlockedCompareExchange(&data->request.status, request_status::complete, request_status::active);
                return 1;
            }
        }
    }
    return 0;
}
/**
* TcOutputDev constructor.
* Sets values for TextOutputControl structure used in text extraction.
*/
TcOutputDev::TcOutputDev()
{
    toc.mode = textOutReadingOrder;
    toc.fixedPitch = 0;
    toc.fixedLineSpacing = 0;
    toc.html = gFalse;
    toc.clipText = gFalse;
    toc.discardDiagonalText = gTrue;
    toc.discardInvisibleText = gTrue;
    toc.discardClippedText = gTrue;
    toc.insertBOM = gFalse;
}

/**
* TcOutputDev denstructor.
* Releases TextOutputDev instance.
*/
TcOutputDev::~TcOutputDev()
{
    if (m_dev)
        delete m_dev;
}

/**
* Starts text extraction.
* Extraction goes through all document pages until search string is found.
*
* @param[in]        doc     pointer to xPDF PdcDoc instance
* @param[in,out]    data    pointer to request data
*/
void TcOutputDev::output(PDFDoc* doc, ThreadData* data)
{
    if (data && doc && doc->isOk())
    {
        if (!m_dev)
        {
            // register <b>outputFunction<b> as a callback function for text extraction
            m_dev = new TextOutputDev(&outputFunction, data, &toc);
        }

        if (m_dev && m_dev->isOk())
        {
            // for each page
            for (int page = 1; page <= doc->getNumPages(); ++page) {
                // extract text from page
                doc->displayPage(m_dev, page, 72, 72, 0, gFalse, gTrue, gFalse, abortExtraction, data);
                // release page resources
                doc->getCatalog()->doneWithPage(page);
                // check if extraction is active
                if (request_status::active != InterlockedOr(&data->request.status, 0))
                    break;
            }
        }

        EnterCriticalSection(&data->lock);
        {
            // no text extracted
            if (data->request.fieldValue == data->request.ptr)
            {
                // put NULL character into empty string
                *(static_cast<wchar_t*>(data->request.fieldValue)) = 0;
                data->request.result = ft_fieldempty;
            }
        }
        LeaveCriticalSection(&data->lock);
    }
}
