#pragma once
#include "ThreadData.h"

/**
* @file
* TcOutputDev class declaration.
*/

/**
* Class for text extraction from PDF to TC.
*/
class TcOutputDev
{
public:
    explicit TcOutputDev();
    ~TcOutputDev();

    void output(PDFDoc* doc, ThreadData* data);
private:
    TextOutputDev*      m_dev{ nullptr };   /**< text extractor */
    TextOutputControl   toc;                /**< settings for TextOutputDev */
};
