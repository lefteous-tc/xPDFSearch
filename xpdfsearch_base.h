#ifndef xpdfsearch_baseH
#define xpdfsearch_baseH

#include <windows.h>

// FIELD_COUNT is used to globally set the number of supported fields.
const int FIELD_COUNT = 22;

// The fieldIndexes enumeration is used simplify access to fields.
enum fieldIndexes 
{
	fiTitle, fiSubject, fiKeywords, fiAuthor, fiCreator, fiProducer, fiNumberOfPages, fiPDFVersion, fiPageWidth, fiPageHeight,
	fiDocStart, fiFirstRow,
	fiCopyingAllowed, fiPrintingAllowed, fiAddCommentsAllowed, fiChangingAllowed, fiEncrypted, fiTagged, fiLinearized,
	fiCreationDate, fiLastModifiedDate,	
	fiText
};

enum SizeUnits
{
	suMilliMeters, suCentiMeters, suInches, suPoints
};


#endif