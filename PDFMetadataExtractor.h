#ifndef PDFMetadataExtractorH
#define PDFMetadataExtractorH

#include <tchar.h>
#include <windows.h>

#include "GString.h"
#include "Object.h"
#include "Dict.h"
#include "PDFDoc.h"
#include "GlobalParams.h"
#include "UnicodeMap.h"


class PDFMetadataExtractor
{
public:
	PDFMetadataExtractor(int* fieldTypes);	
	~PDFMetadataExtractor();

	// Extracts metadata.
	// POST: fieldValue contains metadata.
	int extract (const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen);

private:
	// getMetadata delegates various metadata inquiries of diffrent types to other functions or gets the information itself.	

	// prepareMetadataExtraction create a new pdf doc object if a new file name has been set.
	// POST: m_oldFileName contains the new file name.
	// POST: m_invalidate indicates if a new file name has been set for the current extraction operation.
	// POST: m_pMetadataDoc contains a PDFDoc object.

	// getDictMetadata gets various metadata information from getInfoString and formatAcrobatDateTime.
	int getDictMetadata (int fieldIndex, int fieldType, PDFDoc& doc, void* fieldValue);

	// getInfoString is used by getMetaData to extract various string metadata from PDF files.
	void getInfoString(Dict *infoDict, char *key, void* fieldValue);
	// Wide char version of getInfoString.
	void getInfoStringW(Dict *infoDict, char *key, void* fieldValue);

	// Converts a date string extracted from PDF file into a valid FILETIME value.
	int formatAcrobatDateTime (char* acrobatDateTimeString, FILETIME* dateTime);

	// Converts a given point value to the unit given in unitIndex.
	double getPaperSize (double pageSizePointsValue, int unitIndex);

	// uMap is used by various functions to set mapping for the output device.
	UnicodeMap *uMap;

	// Field types provided by this plug-in.
	int* m_pFieldTypes;

	// Indicates if a new file name has been set for the current extraction operation.
	BOOL m_invalidate;	

	// The keys required to read the metadata fields.
	static char* metaDataFields [6];
};

#endif