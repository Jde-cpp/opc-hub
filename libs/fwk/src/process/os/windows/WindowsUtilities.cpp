#include "WindowsUtilities.h"
#include <jde/fwk/chrono.h>

#define let const auto
using namespace std::chrono;
namespace Jde{
	α Windows::ToWString( sv value )ι->std::wstring{
		if( value.empty() )
			return {};
		let size = (int)value.size();
		std::wstring y( ::MultiByteToWideChar(CP_UTF8, 0, value.data(), size, nullptr, 0), L'\0' );
		::MultiByteToWideChar( CP_UTF8, 0, value.data(), size, y.data(), (int)y.size() );
		return y;
	}

	α Windows::ToString( std::wstring_view value )ι->string{
		if( value.empty() )
			return {};
		let size = (int)value.size();
		string y( ::WideCharToMultiByte(CP_UTF8, 0, value.data(), size, nullptr, 0, nullptr, nullptr), '\0' );
		::WideCharToMultiByte( CP_UTF8, 0, value.data(), size, y.data(), (int)y.size(), nullptr, nullptr );
		return y;
	}

	α Windows::ToSystemTime( TimePoint tp )ι->SYSTEMTIME{
		let date{ floor<days>(tp) };
		const year_month_day ymd{ date };
		const hh_mm_ss time{ floor<milliseconds>(tp-date) };

		SYSTEMTIME systemTime;
		systemTime.wYear = (WORD)(int)ymd.year();
		systemTime.wMonth = (WORD)(unsigned)ymd.month();
		systemTime.wDayOfWeek = (WORD)weekday{ date }.c_encoding();
		systemTime.wDay = (WORD)(unsigned)ymd.day();
		systemTime.wHour = (WORD)(unsigned)time.hours().count();
		systemTime.wMinute = (WORD)time.minutes().count();
		systemTime.wSecond = (WORD)time.seconds().count();
		systemTime.wMilliseconds = (WORD)time.subseconds().count();
		return systemTime;
	}

	α Windows::ToTimePoint( SYSTEMTIME systemTime )ε->TimePoint{
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