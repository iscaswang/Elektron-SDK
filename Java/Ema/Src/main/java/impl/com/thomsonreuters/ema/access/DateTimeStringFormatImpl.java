///*|-----------------------------------------------------------------------------
// *|            This source code is provided under the Apache 2.0 license      --
// *|  and is provided AS IS with no warranty or guarantee of fit for purpose.  --
// *|                See the project's LICENSE.md for details.                  --
// *|           Copyright Thomson Reuters 2017. All rights reserved.            --
///*|-----------------------------------------------------------------------------

package com.thomsonreuters.ema.access;

import com.thomsonreuters.ema.access.Data.DataCode;

class DateTimeStringFormatImpl  implements  DateTimeStringFormat
{
	private int _format = DateTimeStringFormatTypes.STR_DATETIME_RSSL;
	private String _dtString = "";
	private int _originalFormat = 0;
	
	public int format(int format)
	{
		if(format >= DateTimeStringFormatTypes.STR_DATETIME_ISO8601 && format <= DateTimeStringFormatTypes.STR_DATETIME_RSSL)
			_format = format;
		else
		{
			_dtString = "Invalid DateTimeStringFormatType " + format;
			OmmInvalidUsageExceptionImpl ommIUExcept = new OmmInvalidUsageExceptionImpl();
			throw ommIUExcept.message(_dtString);
		}

		return _format;
	}
	
	public int format()
	{
		return _format;
	}

	public String dateAsString(OmmDate date)
	{ 
		OmmDateImpl ommDtImpl = (OmmDateImpl) date;		
		
		
		if (DataCode.BLANK == ommDtImpl.code())
			return OmmDateImpl.BLANK_STRING;
		else
		{
			_originalFormat = ommDtImpl._rsslDate.format();
			ommDtImpl._rsslDate.format(_format);
			_dtString = ommDtImpl._rsslDate.toString();
			ommDtImpl._rsslDate.format(_originalFormat);
			return _dtString;
		}		
	}
	
	public String timeAsString(OmmTime time)
	{
		OmmTimeImpl ommTimpl = (OmmTimeImpl) time;	
		
		if (DataCode.BLANK == ommTimpl.code())
			return OmmDateImpl.BLANK_STRING;
		else
		{
			_originalFormat = ommTimpl._rsslTime.format();
			ommTimpl._rsslTime.format(_format);
			_dtString = ommTimpl._rsslTime.toString();
			ommTimpl._rsslTime.format(_originalFormat);
			return _dtString;
		}				
	}
	
	public String dateTimeAsString(OmmDateTime dateTime)
	{
		OmmDateTimeImpl ommDTimpl = (OmmDateTimeImpl) dateTime;	
		
		if (DataCode.BLANK == ommDTimpl.code())
			return OmmDateImpl.BLANK_STRING;
		else
		{
			_originalFormat = ommDTimpl._rsslDateTime.format();
			ommDTimpl._rsslDateTime.format(_format);
			_dtString = ommDTimpl._rsslDateTime.toString();
			ommDTimpl._rsslDateTime.format(_originalFormat);
			return _dtString;
		}		
		
	}
}
