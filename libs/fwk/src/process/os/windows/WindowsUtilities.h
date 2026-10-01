namespace Jde::Windows
{
	#define Φ Γ α
	Φ ToWString( const string& value)ι->std::wstring;
	Φ ToString( const std::wstring& value)ι->string;
	Φ ToSystemTime( TimePoint time )ι->SYSTEMTIME;
	Φ ToTimePoint( SYSTEMTIME systemTime )ι->TimePoint;
#undef Φ
}