//#include <string>
#include <windows.h>
#include <vector>
#include <assert.h>

#include "utf8.h"

using namespace std;

std::string cp1251_to_utf8(const char *str)
{
	string res;	
	int result_u, result_c;


	result_u = MultiByteToWideChar(1251,
		0,
		str,
		-1,
		0,
		0);
	
	if (!result_u)
		return 0;

	wchar_t *ures = new wchar_t[result_u];

	if(!MultiByteToWideChar(1251,
		0,
		str,
		-1,
		ures,
		result_u))
	{
		delete[] ures;
		return 0;
	}


	result_c = WideCharToMultiByte(
		CP_UTF8,
		0,
		ures,
		-1,
		0,
		0,
		0, 0);

	if(!result_c)
	{
		delete [] ures;
		return 0;
	}

	char *cres = new char[result_c];

	if(!WideCharToMultiByte(
		CP_UTF8,
		0,
		ures,
		-1,
		cres,
		result_c,
		0, 0))
	{
		delete[] cres;
		return 0;
	}
	delete[] ures;
	res.append(cres);
	delete[] cres;
	return res;
}

std::string utf8_to_cp1251(const char *str)
{
	string res;	
	int result_u, result_c;


	result_u = MultiByteToWideChar(CP_UTF8,
		0,
		str,
		-1,
		0,
		0);
	
	if (!result_u)
		return 0;

	wchar_t *ures = new wchar_t[result_u];

	if(!MultiByteToWideChar(CP_UTF8,
		0,
		str,
		-1,
		ures,
		result_u))
	{
		delete[] ures;
		return 0;
	}


	result_c = WideCharToMultiByte(
		1251,
		0,
		ures,
		-1,
		0,
		0,
		0, 0);

	if(!result_c)
	{
		delete [] ures;
		return 0;
	}

	char *cres = new char[result_c];

	if(!WideCharToMultiByte(
		1251,
		0,
		ures,
		-1,
		cres,
		result_c,
		0, 0))
	{
		delete[] cres;
		delete[] ures;
		return 0;
	}
	delete[] ures;
	res.append(cres);
	delete[] cres;
	return res;
}

string char2hex( char dec );
string urlencode(const string &c)
{
     
     string escaped="";
     int max = c.length();
     for(int i=0; i<max; i++)
     {
         if ( (48 <= c[i] && c[i] <= 57) ||//0-9
              (65 <= c[i] && c[i] <= 90) ||//abc...xyz
              (97 <= c[i] && c[i] <= 122) || //ABC...XYZ
              (c[i]=='~' || c[i]=='!' || c[i]=='*' || c[i]=='(' || c[i]==')' || c[i]=='\'' || c[i]=='/')
         )
         {
             escaped.append( &c[i], 1);
         }
         else
         {
             escaped.append("%");
             escaped.append( _strupr((char *) char2hex(c[i]).c_str()) );//converts char 255 to string "ff" / FF !
         }
     }
     return escaped;
}

 string char2hex( char dec )
{
     char dig1 = (dec&0xF0)>>4;
     char dig2 = (dec&0x0F);
     if ( 0<= dig1 && dig1<= 9) dig1+=48;    //0,48inascii
     if (10<= dig1 && dig1<=15) dig1+=97-10; //a,97inascii
     if ( 0<= dig2 && dig2<= 9) dig2+=48;
     if (10<= dig2 && dig2<=15) dig2+=97-10;

     string r;
     r.append( &dig1, 1);
     r.append( &dig2, 1);
     return r;
}


 string urldecode(string ins)
	 {
	 char *pszDecodedOut = (char*) malloc (ins.length() + 1);
	 char *pszEncodedIn = (char*) malloc (ins.length() + 1);

	 memset(pszDecodedOut, 0, ins.length() + 1);
	 strcpy(pszEncodedIn, ins.c_str());

	 enum DecodeState_e
		 {
		 STATE_SEARCH = 0, ///< searching for an ampersand to convert
		 STATE_CONVERTING, ///< convert the two proceeding characters from hex
		 };

	 DecodeState_e state = STATE_SEARCH;

	 for(unsigned int i = 0; i < strlen(pszEncodedIn); ++i)
		 {
		 switch(state)
			 {
		 case STATE_SEARCH:
			 {
			 if(pszEncodedIn[i] != '%')
				 {
				 strncat(pszDecodedOut, &pszEncodedIn[i], 1);
				 assert(strlen(pszDecodedOut) < nBufferSize);
				 break;
				 }

			 // We are now converting
			 state = STATE_CONVERTING;
			 }
			 break;

		 case STATE_CONVERTING:
			 {
			 // Conversion complete (i.e. don't convert again next iter)
			 state = STATE_SEARCH;

			 // Create a buffer to hold the hex. For example, if %20, this
			 // buffer would hold 20 (in ASCII)
			 char pszTempNumBuf[3] = {0};
			 strncpy(pszTempNumBuf, &pszEncodedIn[i], 2);

			 // Ensure both characters are hexadecimal
			 bool bBothDigits = true;

			 for(int j = 0; j < 2; ++j)
				 {
				 if(!isxdigit(pszTempNumBuf[j]))
					 bBothDigits = false;
				 }

			 if(!bBothDigits)
				 break;

			 // Convert two hexadecimal characters into one character
			 int nAsciiCharacter;
			 sscanf(pszTempNumBuf, "%x", &nAsciiCharacter);

			 // Ensure we aren't going to overflow
			 assert(strlen(pszDecodedOut) < nBufferSize);

			 // Concatenate this character onto the output
			 strncat(pszDecodedOut, (char*)&nAsciiCharacter, 1);

			 // Skip the next character
			 i++;
			 }
			 break;
			 }
		 }
	 string outs = pszDecodedOut;
	 free(pszDecodedOut);
	 free(pszEncodedIn);
	 return outs;

	 }
