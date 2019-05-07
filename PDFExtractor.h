#pragma once

#include "contentplug.h"
#include <tchar.h>

#include <Object.h>
#include "TcOutputDev.h"

/**
* @file 
* PDFExtractor header file.
*/

/**
* Extract various data from PDF document.
* Compare data from two PDF documents.
*/
class PDFExtractor
{
public:
    explicit PDFExtractor();
    PDFExtractor(const PDFExtractor&) = delete;
    PDFExtractor& operator=(const PDFExtractor&) = delete;
    ~PDFExtractor();
    int extract(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags);
    int compare(PROGRESSCALLBACKPROC progresscallback, const wchar_t* fileName1, const wchar_t* fileName2, int compareIndex);
    void abort();
    void stop();
    void waitForProducer();

private:
    void getMetadataString(PDFDoc* doc, const char* key);
    void getMetadataDate(PDFDoc* doc, const char* key);
    void getMetadataAttrStr(PDFDoc* doc);
    void getDocID(PDFDoc* doc);
    static BOOL isIncremental(PDFDoc* doc);
    static BOOL isTagged(PDFDoc* doc);
    static BOOL hasSignature(PDFDoc* doc);

    void getPaperSize(double pageSizePointsValue);
    template<typename T> void getValue(T value, int type);

    static ptrdiff_t UnicodeToUTF16(wchar_t* dst, int *cbDst, const Unicode* src, int cchSrc);
    static size_t removeDelimiters(wchar_t* str, size_t cchStr, const wchar_t* delims);
    static void appendHexValue(wchar_t* dst, int cbDst, int value);
    static wchar_t nibble2wchar(int value);
    int initData(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags, DWORD timeout);

    unsigned int startWorkerThread();
    int waitForConsumer();
    int waitForConsumers();
    bool open();
    void close();
    void closeDoc();
    void doWork();
    void done();

    ThreadData*     m_data{ nullptr };      /**< pointer to thread data, request    */
    wchar_t*        m_fileName{nullptr};    /**< full patht to PDF document, used to compare open with new one  */
    PDFDoc*         m_doc{nullptr};         /**< pointer to PDFDoc object   */
    PDFExtractor*   m_search{ nullptr };    /**< pointer to second instance of PDFExtractor, used to extract data from second file when comparing data */
    _locale_t       m_locale{ nullptr };    /**< locale-specific value, used for compare as text */
    TcOutputDev     m_tc;                   /**< text extraction object */
};
