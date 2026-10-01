using namespace std::chrono;
#include <codecvt>
#include "WindowsUtilities.h"
#include <jde/fwk/chrono.h>

#define let const auto
#pragma warning( disable : 4996 )
namespace Jde{
	α Windows::ToWString( const string& value)ι->std::wstring{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		return converter.from_bytes( value );
	}

	α Windows::ToString( const std::wstring& value)ι->string{
		std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
		return converter.to_bytes( value );
	}

	α Windows::ToSystemTime( TimePoint tp )ι->SYSTEMTIME{
		let date{ floor<days>(tp) };
		const year_month_day ymd{ date };
		const hh_mm_ss time{ floor<milliseconds>(tp-date) };

		SYSTEMTIME systemTime;
		systemTime.wYear = (WORD)(int)ymd.year();
		systemTime.wMonth = (WORD)(unsigned)ymd.month();
		systemTime.wDay = (WORD)(unsigned)ymd.day();
		systemTime.wHour = (WORD)(unsigned)time.hours().count();
		systemTime.wMinute = (WORD)time.minutes().count();
		systemTime.wSecond = (WORD)time.seconds().count();
		systemTime.wMilliseconds = (WORD)time.subseconds().count();
		return systemTime;
	}

	α Windows::ToTimePoint( SYSTEMTIME systemTime )ι->TimePoint{
		return Chrono::ToTimePoint(
			systemTime.wYear,
			static_cast<uint8>(systemTime.wMonth),
			static_cast<uint8>(systemTime.wDay),
			static_cast<uint8>(systemTime.wHour),
			static_cast<uint8>(systemTime.wMinute),
			static_cast<uint8>(systemTime.wSecond),
			milliseconds( systemTime.wMilliseconds )
		);
	}
}