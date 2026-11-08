namespace Jde::Windows
{
	#define Φ Γ α
	Φ ToWString( sv value )ι->std::wstring;
	Φ ToString( std::wstring_view value )ι->string;
	Φ ToSystemTime( TimePoint time )ι->SYSTEMTIME;
	Φ ToTimePoint( SYSTEMTIME systemTime )ε->TimePoint;
#undef Φ
}