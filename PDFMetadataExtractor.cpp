#include "PDFMetadataExtractor.h"
#include <strsafe.h>
#include "chartypes.h"
#include "xpdfsearch_base.h"
#include "contentplug.h"

// The keys required to read the metadata fields.	
char* PDFMetadataExtractor::metaDataFields [6] =
{
	"Title", "Subject", "Keywords", "Author", "Creator", "Producer"
};

PDFMetadataExtractor::PDFMetadataExtractor(int* fieldTypes):
uMap(NULL),
m_pFieldTypes(fieldTypes)
{	
	// Initialize globally used resources.
	globalParams = new GlobalParams("");	
	// get mapping to output encoding.
	uMap = globalParams->getTextEncoding();
}

PDFMetadataExtractor::~PDFMetadataExtractor()
{	
	uMap->decRefCnt();
	delete globalParams;
	globalParams = NULL;
}

// Extracts metadata.
// POST: fieldValue contains metadata.

// prepareMetadataExtraction create a new pdf doc object if a new file name has been set.
// POST: m_oldFileName contains the new file name.
// POST: m_invalidate indicates if a new file name has been set for the current extraction operation.
// POST: m_pMetadataDoc contains a PDFDoc object.

// getMetaData delegates various metadata inquiries of diffrent types to other functions or gets the information itself.
int PDFMetadataExtractor::extract (const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int maxlen)
{
	int fieldType = m_pFieldTypes[fieldIndex];

	// open PDF file
	PDFDoc doc ((wchar_t*)fileName, wcslen(fileName));

	if (!doc.isOk ())
	{
		return ft_fileerror;
	}

	switch (fieldIndex)
	{
	case fiTitle:
	case fiSubject:
	case fiKeywords:
	case fiAuthor:
	case fiCreator:
	case fiProducer:
	case fiCreationDate:
	case fiLastModifiedDate:
		fieldType = getDictMetadata (fieldIndex, fieldType, doc, fieldValue);		
		break;
	case fiNumberOfPages:			
		*(int*)fieldValue = doc.getNumPages();			
		break;
	case fiPDFVersion:
		*(double*)fieldValue = doc.getPDFVersion();
		break;
	case fiPageWidth:
		*(double*)fieldValue = getPaperSize (doc.getPageCropWidth(1), unitIndex);
		break;
	case fiPageHeight:
		*(double*)fieldValue = getPaperSize (doc.getPageCropHeight(1), unitIndex);
		break;
	case fiCopyingAllowed:
		*(BOOL*)fieldValue = doc.okToCopy();
		break;
	case fiPrintingAllowed:
		*(BOOL*)fieldValue = doc.okToPrint();
		break;
	case fiAddCommentsAllowed:
		*(BOOL*)fieldValue = doc.okToAddNotes();
		break;
	case fiChangingAllowed:
		*(BOOL*)fieldValue = doc.okToChange();
		break;
	case fiEncrypted:
		*(BOOL*)fieldValue = doc.isEncrypted();
		break;
	case fiTagged:
		*(BOOL*)fieldValue = doc.getStructTreeRoot()->isDict();
		break;
	case fiLinearized:
		*(BOOL*)fieldValue = doc.isLinearized();
		break;
	default:
		fieldType = ft_fieldempty;			
	}		
	return fieldType;
}

// getDictMetadata gets various metadata information from getInfoString and formatAcrobatDateTime.
int PDFMetadataExtractor::getDictMetadata (int fieldIndex, int fieldType, PDFDoc& doc, void* fieldValue)
{
	Object docInfo;		
	doc.getDocInfo(&docInfo);

	// Test if retrieved information is valid.
	if (!docInfo.isDict())
	{
		docInfo.free ();
		return ft_fieldempty;
	}	

	Dict* dict = docInfo.getDict ();


	// Get requested information from dictionary.
	switch (fieldIndex)
	{
	case fiTitle:
	case fiSubject:
	case fiKeywords:
	case fiAuthor:
	case fiCreator:
	case fiProducer:
		getInfoStringW (dict, metaDataFields[fieldIndex], (void*)fieldValue);
		break;
	case fiCreationDate:
		{				
			Object obj;
			if (dict->lookup("CreationDate", &obj)->isString())
			{
				fieldType = formatAcrobatDateTime (obj.getString()->getCString(), (FILETIME*)fieldValue);
			}
			else
			{
				fieldType = ft_fieldempty;
			}
		}
		break;
	case fiLastModifiedDate:
		{					
			Object obj;
			if (dict->lookup("ModDate", &obj)->isString())
			{						
				fieldType = formatAcrobatDateTime (obj.getString()->getCString(), (FILETIME*)fieldValue);
			}				
			else
			{				
				fieldType = ft_fieldempty;
			}
		}
		break;
	default:
		fieldType = ft_fieldempty;			
	}	
	return fieldType;
}

// getInfoString is used by getMetaData to extract various string metadata from PDF files.
void PDFMetadataExtractor::getInfoString(Dict *infoDict, char *key, void* fieldValue)
{
	Object obj;
	GString *s1;
	Unicode u;
	char buf[8] = {0};
	char out [0x1000] = {0};
	int i = 0;
	int n = 0;
	int c = 0;

	if (infoDict->lookup(key, &obj)->isString()) 
	{
		s1 = obj.getString();
		int s1Length = s1->getLength();
		if ((s1->getChar(0) & 0xff) == 0xfe && (s1->getChar(1) & 0xff) == 0xff) 
		{			
			i = 2;
			while (i < s1Length) 
			{
				u = ((s1->getChar(i) & 0xff) << 8) | (s1->getChar(i+1) & 0xff);
				i += 2;				
				n = uMap->mapUnicode(u, buf, sizeof(buf));
				out [c] = buf[0];
				c++;
			}
		} 
		else 
		{			
			i = 0;			
			while (i < s1Length)
			{
				u = s1->getChar(i) & 0xff;
				++i;
				n = uMap->mapUnicode(u, buf, sizeof(buf));
				out [c] = buf[0];
				c++;
			}
		}	
	}
	StringCchCopy ((char*)fieldValue, 0x1000, out);
	obj.free();
}

// Wide char version of getInfoString.
void PDFMetadataExtractor::getInfoStringW(Dict *infoDict, char *key, void* fieldValue)
{
	Object obj;
	GString *s1;
	Unicode u;	
	wchar_t out [0x1000] = {0};
	int i = 0;
	int n = 0;
	int c = 0;

	if (infoDict->lookup(key, &obj)->isString()) 
	{
		s1 = obj.getString();
		int s1Length = s1->getLength();
		if ((s1->getChar(0) & 0xff) == 0xfe && (s1->getChar(1) & 0xff) == 0xff) 
		{			
			i = 2;
			while (i < s1Length) 
			{
				u = ((s1->getChar(i) & 0xff) << 8) | (s1->getChar(i+1) & 0xff);
				out[c] = u;
				i += 2;				
				c++;
			}
		} 
		else 
		{			
			i = 0;			
			while (i < s1Length)
			{
				u = s1->getChar(i) & 0xff;
				++i;
				out[c] = u;
				c++;
			}
		}	
	}
	StringCchCopyW ((wchar_t*)fieldValue, 0x1000, out);
	obj.free();
}

int PDFMetadataExtractor::formatAcrobatDateTime (char* acrobatDateTimeString, FILETIME* dateTime)
{		
	if (!(strlen(acrobatDateTimeString) == 16 || strlen(acrobatDateTimeString) == 23))
	{		
		return ft_fieldempty;
	}
	SYSTEMTIME sysTime;	
	ZeroMemory (&sysTime, sizeof(SYSTEMTIME));	

	char dateTimeString [5] = {0};
	// Year
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 2, 4);
	sysTime.wYear = atoi (dateTimeString);	
	// Month
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 6, 2);
	sysTime.wMonth = atoi (dateTimeString);
	// Day
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 8, 2);
	sysTime.wDay = atoi (dateTimeString);
	// Hours
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 10, 2);
	sysTime.wHour = atoi (dateTimeString);
	// Minutes
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 12, 2);
	sysTime.wMinute = atoi (dateTimeString);
	// Seconds
	StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 14, 2);
	sysTime.wSecond = atoi (dateTimeString);

	SystemTimeToFileTime (&sysTime, dateTime);

	// Different timezone given.
	if (strlen(acrobatDateTimeString) == 23)
	{
		StringCchCopyN (dateTimeString, MAX_PATH, acrobatDateTimeString + 16, 3);		
		LARGE_INTEGER timeValue = {dateTime->dwLowDateTime, dateTime->dwHighDateTime};
		timeValue.QuadPart -= _atoi64 (dateTimeString) * 36000000000;		
		dateTime->dwHighDateTime = timeValue.HighPart;
		dateTime->dwLowDateTime = timeValue.LowPart;		
	}
	return ft_datetime;
}

// Converts a give n point value to the unit given in unitIndex.
double PDFMetadataExtractor::getPaperSize (double pageSizePointsValue, int unitIndex)
{	
	switch (unitIndex)
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
	}
	return pageSizePointsValue;
}