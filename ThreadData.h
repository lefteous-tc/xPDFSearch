#pragma once

#include <Windows.h>
#include <TextOutputDev.h>
#include <PDFDoc.h>

/**
* @file
* Declarations of constants and structures used in threads 
*/

/**
* @defgroup handles ThreadData::handles indexes
* @{ */
constexpr auto THREAD_HANDLE   = 0; /**< thread handle index in ThreadData::handles[] */
constexpr auto CONSUMER_HANDLE = 1; /**< consumer event handle index in ThreadData::handles[] */ 
constexpr auto PRODUCER_HANDLE = 2; /**< producer event handle index in ThreadData::handles[] */
/** @} */

#if 0
constexpr auto CONSUMER_TIMEOUT = INFINITE;
constexpr auto PRODUCER_TIMEOUT = 100U;
#else
/**
* wait for 10 s for producer to produce data (form PDF)
* wait for 100 ms for consumer (TC) to consume data and ask for more,
* on timeout close PDF file
* it is a bad idea to wait for infinite
*/

constexpr auto CONSUMER_TIMEOUT = 10000U; /**< time for one data extraction */
constexpr auto PRODUCER_TIMEOUT = 100U;/**< extractor waits for next request from TC, or closes PDF document */
#endif

constexpr auto DEFAULT_FIELD_CB = 4096U;/**< size of Request.fieldValue, if not provided form TC */

constexpr auto sizeOfWchar = sizeof(wchar_t);/**< sizeof wchar_t */

/** 
* Request status enumeration 
*/
enum request_status
{
    closed,     /**< PDF document is closed */
    active,     /**< data extraction form PDF document in progress */
    complete,   /**< data extraction form PDF document complete */
    canceled    /**< data extraction is cancelled, waiting to close document */
};

/**
* PDF extraction request related data 
*/
struct Request
{
    int fieldIndex;             /**< field index to extract */
    int unitIndex;              /**< unit index */
    int cbfieldValue;           /**< size of fieldValue in BYTES!!! */
    int flags;                  /**< flags from TC */
    int result;                 /**< result of an extraction */
    bool allocated;             /**< true=fieldValue is allocated in this class */
    DWORD timeout;              /**< time to wait in text extraction procedure */
    volatile LONG status;       /**< request status, @see request_status */
    void* fieldValue;           /**< extracted data buffer */
    void* ptr;                  /**< pointer to end of extracted data, offset pointer to fieldValue */
    const wchar_t* fileName;    /**< name of PDF document */
};

/**
* Extraction thread related data 
*/
struct ThreadData
{
    volatile LONG active;   /**< thread status, 1 when active */
    CRITICAL_SECTION lock;  /**< lock to protect Request while exchanging data */
    HANDLE handles[3U];     /**< thread, producer and consumer event handles */
    Request request;        /**< extraction request */
};
