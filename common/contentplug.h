/**
* @file
* Contents of file contplug.h version 2.11
*/
#pragma once
#include <Windows.h>

/**
* @defgroup ft_types ContentGetSupportedField return values
* @{ */
#define ft_nomorefields     0   /**< The fieldIndex is beyond the last available field */
#define ft_numeric_32       1   /**< 32-bit signed number */
#define ft_numeric_64       2   /**< 64-bit signed number, e.g. for file sizes */
#define ft_numeric_floating 3   /**< double precision floating point number */
#define ft_date             4   /**< date value (year, month, day) */
#define ft_time             5   /**< time value (hour, minute, second); date and time are in local time */
#define ft_boolean          6   /**< true/false value */
#define ft_multiplechoice   7   /**< value allowing a limited number of choices; use the Units field to return all possible values */
#define ft_string           8   /**< text string; values returned by ContentGetValue(W) may be of type ft_stringw or ft_string */
#define ft_fulltext         9   /**< full text (multiple text strings), only used for searching */
#define ft_datetime         10  /**< full text with UTF-16 encoding (multiple text strings), only used for searching */
#define ft_stringw          11  /**< timestamp of type FILETIME, as returned e.g. by FindFirstFile() */
#define ft_fulltextw        12  /**< text string with UTF-16 encoding */
#define ft_comparecontent   100 /**< used in "Synchronize dirs" only  */
/** @} */

#define ft_comparebaseindex 10000   /**< starting index value for fields used in ContentCompareFiles() */
/**
* @defgroup ft_compare ContentCompareFiles return values
* @{ */
#define ft_compare_eq_txt   2       /**< two files are equal, show equal sign with 'TXT' below it in list */
#define ft_compare_eq       1       /**< two files are equal, show equal sign in list */
#define ft_compare_not_eq   0       /**< two files are different */
#define ft_compare_err      -1      /**< error, could not open at least one of the files */
#define ft_compare_abort    -2      /**< compare aborted */
#define ft_compare_next     -3      /**< file cannot be compared with this function, please continue with the next plugin */
/** @} */

/**
* @defgroup ft_content ContentGetValue return values
* @{ */
#define ft_delayed          0       /**< field takes a long time to extract -> try again in background  */
#define ft_nosuchfield      -1      /**< error, invalid field number given  */
#define ft_fileerror        -2      /**< file i/o error */
#define ft_fieldempty       -3      /**< field valid, but empty */
#define ft_ondemand         -4      /**< field will be retrieved only when user presses \<SPACEBAR\>  */
#define ft_notsupported     -5      /**< function not supported   */
#define ft_setcancel        -6      /**< user clicked cancel in field editor    */
/** @} */

/**
* @defgroup ft_ok ContentSetValue return values
* @{ */
#define ft_setsuccess 0     /**< setting of the attribute succeeded	*/
/** @} */

/**
* @defgroup contflags ContentGetSupportedFieldFlags return values
* @{ */
#define contflags_edit                      1   /**< plugin allows to edit (modify) this field via Files - Change attributes */
#define contflags_substsize                 2
#define contflags_substdatetime             4
#define contflags_substdate                 6
#define contflags_substtime                 8
#define contflags_substattributes           10
#define contflags_substattributestr         12
#define contflags_passthrough_size_float    14
#define contflags_substmask                 14
#define contflags_fieldedit                 16
/** @} */

/**
* @defgroup contst ContentSendStateInformation return values
* @{ */
#define contst_readnewdir       1   /**<  TC reads one of the file lists */
#define contst_refreshpressed   2   /**< user has pressed F2 or Ctrl+R to force a reload */
#define contst_showhint         4   /**< tooltip/hint window is shown for the current file */
/** @} */

#define setflags_first_attribute 1  /**< First attribute of this file    */
#define setflags_last_attribute  2  /**< Last attribute of this file     */
#define setflags_only_date       4  /**< Only set the date of the datetime value!    */

#define editflags_initialize     1  /**< The data passed to the plugin may be used to  initialize the edit dialog*/

#define CONTENT_DELAYIFSLOW 1       /**< ContentGetValue called in foreground    */
/**
* If requested via contflags_passthrough_size_float: The size
* is passed in as floating value, TC expects correct value
* from the given units value, and optionally a text string
*/
#define CONTENT_PASSTHROUGH 2

/**
* Used in ContentSetDefaultParams to inform the plugin about the current plugin interface version and ini file location.
*/
typedef struct {
    int size;                           /**< size of the structure, in bytes */
    DWORD PluginInterfaceVersionLow;    /**< Low value of plugin interface version. This is the value after the comma, multiplied by 100! Example. For plugin interface version 1.30, the low DWORD is 30 and the high DWORD is 1. */
    DWORD PluginInterfaceVersionHi;     /**< High value of plugin interface version. */
    char DefaultIniName[MAX_PATH];      /**< Suggested location+name of the ini file where the plugin could store its data. */
} ContentDefaultParamStruct;

/**
* PDF date structure
*/
typedef struct {
    WORD wYear;     /**< year */
    WORD wMonth;    /**< month */
    WORD wDay;      /**< day */
} tdateformat, *pdateformat;

/**
* PDF Time structure
*/
typedef struct {
    WORD wHour;     /**< hour */
    WORD wMinute;   /**< minute */
    WORD wSecond;   /**< second */
} ttimeformat, *ptimeformat;

/**
* Used in ContentCompareFiles to inform the plugin about the file details of the left and right file.
*/
typedef struct {
    __int64 filesize1;  /**< size of the first file. */
    __int64 filesize2;  /**< size of the second file. */
    FILETIME filetime1; /**< last modification time stamp of the first file, in standard Windows format */
    FILETIME filetime2; /**< last modification time stamp of the second file, in standard Windows format */
    DWORD attr1;        /**< attributes of the first file */
    DWORD attr2;        /**< attributes of the second file */
} FileDetailsStruct;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
    /**
    * Callback function which your plugin needs to call during a call to ContentCompareFiles to inform the host program about the progress of the comparison.
    * @param[in] nextblockdata The number of bytes compared since the last call of PROGRESSCALLBACKPROC.
    * @return 0 OK, continue, else, user pressed the Cancel/Abort button
    */
    typedef int(__stdcall *PROGRESSCALLBACKPROC)(int nextblockdata);

    __declspec(dllexport) int __stdcall ContentGetDetectString(char* DetectString, int maxlen);
    __declspec(dllexport) int __stdcall ContentGetSupportedField(int FieldIndex, char* FieldName, char* Units, int maxlen);
    __declspec(dllexport) int __stdcall ContentGetValue(const char* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags);
    __declspec(dllexport) int __stdcall ContentGetValueW(const wchar_t* fileName, int fieldIndex, int unitIndex, void* fieldValue, int cbfieldValue, int flags);

    __declspec(dllexport) void __stdcall ContentSetDefaultParams(ContentDefaultParamStruct* dps);
    __declspec(dllexport) void __stdcall ContentPluginUnloading();
    __declspec(dllexport) void __stdcall ContentStopGetValue(const char* FileName);
    __declspec(dllexport) void __stdcall ContentStopGetValueW(const wchar_t* FileName);
    __declspec(dllexport) int __stdcall ContentGetDefaultSortOrder(int FieldIndex);
    __declspec(dllexport) int __stdcall ContentGetSupportedFieldFlags(int FieldIndex);
    __declspec(dllexport) int __stdcall ContentSetValue(const char* FfileName, int fieldIndex, int unitIndex, int fieldType, void* fieldValue, int flags);
    __declspec(dllexport) int __stdcall ContentSetValueW(const wchar_t* fileName, int fieldIndex, int unitIndex, int fieldType, void* fieldValue, int flags);

    __declspec(dllexport) int __stdcall ContentEditValue(HWND ParentWin, int FieldIndex, int UnitIndex, int FieldType, void* FieldValue, int maxlen, int flags, char* langidentifier);
    __declspec(dllexport) void __stdcall ContentSendStateInformation(int state, char* path);
    __declspec(dllexport) void __stdcall ContentSendStateInformationW(int state, const wchar_t* path);

    __declspec(dllexport) int __stdcall ContentCompareFiles(PROGRESSCALLBACKPROC progresscallback, int compareindex, char* filename1, char* filename2, FileDetailsStruct* filedetails);
    __declspec(dllexport) int __stdcall ContentCompareFilesW(PROGRESSCALLBACKPROC progresscallback, int compareindex, WCHAR* filename1, WCHAR* filename2, FileDetailsStruct* filedetails);
#ifdef __cplusplus
}
#endif /* __cplusplus */
