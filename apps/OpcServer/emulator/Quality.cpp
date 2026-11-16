#include "Quality.h"
#include <charconv>
#include <ranges>
#include <jde/fwk/str.h>
#include "Devices.h"

#define let const auto
namespace Jde::Opc::Emulator{
	constexpr UA_StatusCode _structureChanged{ 1u<<15 }, _semanticsChanged{ 1u<<14 }, _infoTypeMask{ 3u<<10 }, _infoTypeDataValue{ 1u<<10 }, _limitMask{ 3u<<8 }, _overflow{ 1u<<7 };
	constexpr array<sv,4> _limitNames{ "none", "low", "high", "constant" };
	constexpr array<std::pair<sv,UA_StatusCode>,21> _statusNames{{
		{"Good", UA_STATUSCODE_GOOD}, {"GoodLocalOverride", UA_STATUSCODE_GOODLOCALOVERRIDE}, {"GoodClamped", UA_STATUSCODE_GOODCLAMPED},
		{"Uncertain", UA_STATUSCODE_UNCERTAIN}, {"UncertainNoCommunicationLastUsableValue", UA_STATUSCODE_UNCERTAINNOCOMMUNICATIONLASTUSABLEVALUE},
		{"UncertainLastUsableValue", UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE}, {"UncertainSubstituteValue", UA_STATUSCODE_UNCERTAINSUBSTITUTEVALUE},
		{"UncertainInitialValue", UA_STATUSCODE_UNCERTAININITIALVALUE}, {"UncertainSensorNotAccurate", UA_STATUSCODE_UNCERTAINSENSORNOTACCURATE},
		{"UncertainEngineeringUnitsExceeded", UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED}, {"UncertainSubNormal", UA_STATUSCODE_UNCERTAINSUBNORMAL},
		{"Bad", UA_STATUSCODE_BAD}, {"BadConfigurationError", UA_STATUSCODE_BADCONFIGURATIONERROR}, {"BadNotConnected", UA_STATUSCODE_BADNOTCONNECTED},
		{"BadDeviceFailure", UA_STATUSCODE_BADDEVICEFAILURE}, {"BadSensorFailure", UA_STATUSCODE_BADSENSORFAILURE}, {"BadOutOfService", UA_STATUSCODE_BADOUTOFSERVICE},
		{"BadNoCommunication", UA_STATUSCODE_BADNOCOMMUNICATION}, {"BadCommunicationError", UA_STATUSCODE_BADCOMMUNICATIONERROR},
		{"BadWaitingForInitialData", UA_STATUSCODE_BADWAITINGFORINITIALDATA}, {"BadOutOfRange", UA_STATUSCODE_BADOUTOFRANGE} }};

	α ParseLimit( sv s, SL sl )ε->ELimit{
		for( uint i=0; i<_limitNames.size(); ++i ){
			if( _limitNames[i]==s )
				return (ELimit)i;
		}
		THROWSL( "Unknown limit '{}' - one of {}.", s, Str::Join(_limitNames, "|") );
	}
	α StatusNames()ι->std::span<const std::pair<sv,UA_StatusCode>>{ return _statusNames; }

	α Compose( UA_StatusCode code, StatusFlags flags )ι->UA_StatusCode{
		if( flags.StructureChanged )
			code |= _structureChanged;
		if( flags.SemanticsChanged )
			code |= _semanticsChanged;
		if( flags.Limit!=ELimit::None )
			code = ( code & ~_limitMask ) | ( (UA_StatusCode)flags.Limit<<8 );
		if( flags.Overflow )
			code |= _overflow;
		if( code & (_limitMask | _overflow) )
			code = ( code & ~_infoTypeMask ) | _infoTypeDataValue;
		return code;
	}

	α ParseStatus( sv s, SL sl )ε->UA_StatusCode{
		let named = find_if( _statusNames, [s](let& x){ return x.first==s; } );
		if( named!=_statusNames.end() )
			return named->second;
		let hex = s.starts_with( "0x" ) || s.starts_with( "0X" );
		let digits = hex ? s.substr( 2 ) : s;
		UA_StatusCode y{};
		let [end, ec] = std::from_chars( digits.data(), digits.data()+digits.size(), y, hex ? 16 : 10 );
		THROW_IFSL( digits.empty() || ec!=std::errc{} || end!=digits.data()+digits.size(), "Unknown status '{}' - a code by number (0x808C0000), or one of {}.", s, Str::Join(_statusNames | std::views::keys, "|") );
		THROW_IFSL( (y>>30)==3, "Status '{}': severity 11 is reserved (OPC 10000-4 7.38).", s );
		return y;
	}

	α ToString( UA_StatusCode sc )ι->string{
		string y{ UA_StatusCode_name(sc) };//matches on the top 16 bits, so the flags below do not hide the name.
		if( let limit = (sc & _limitMask)>>8; limit )
			y += Ƒ( "+{}{}", (char)std::toupper(_limitNames[limit][0]), _limitNames[limit].substr(1) );
		if( sc & _overflow )
			y += "+Overflow";
		if( sc & _structureChanged )
			y += "+StructureChanged";
		if( sc & _semanticsChanged )
			y += "+SemanticsChanged";
		return y;
	}

	Ω isLastUsable( UA_StatusCode sc )ι->bool{
		return UA_StatusCode_isEqualTop( sc, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE ) || UA_StatusCode_isEqualTop( sc, UA_STATUSCODE_UNCERTAINNOCOMMUNICATIONLASTUSABLEVALUE );
	}
	//The window's status, as the config spells it, names the window in its refusals - a tag has several.
	Ω windowOwner( const jobject& o, sv tagName )ι->string{
		let status = o.if_contains( "status" );
		return status ? Ƒ( "Tag '{}' quality {}", tagName, serialize(*status) ) : Ƒ( "Tag '{}' quality", tagName );
	}
	QualityWindow::QualityWindow( const jobject& o, sv tagName, SL sl )ε:
		Recurrence{ o, windowOwner(o, tagName), {"status", "limit", "hold", "overflow", "structureChanged", "semanticsChanged"}, sl }{
		let owner = windowOwner( o, tagName );
		let& status = Json::AsValue( o, "status", sl );//a name, a "0x…" string, or the number itself.
		let code = status.is_string() ? ParseStatus( status.get_string(), sl ) : ParseStatus( std::to_string(Json::AsNumber<UA_StatusCode>(status, sl)), sl );
		let limit = FindKey<string>( o, "limit", owner, sl );
		HasLimit = limit.has_value();
		Status = Compose( code, {limit ? ParseLimit(*limit, sl) : ELimit::None, FindKey<bool>(o, "overflow", owner, sl).value_or(false), FindKey<bool>(o, "structureChanged", owner, sl).value_or(false), FindKey<bool>(o, "semanticsChanged", owner, sl).value_or(false)} );
		Hold = FindKey<bool>( o, "hold", owner, sl ).value_or( UA_StatusCode_isBad(Status) || isLastUsable(Status) );
	}

	TagQuality::TagQuality( vector<QualityWindow> windows, optional<double> sensorMin, optional<double> sensorMax, uint64_t seed, uint32_t place )ι:
		_windows{ move(windows) }, _sensorMin{ sensorMin }, _sensorMax{ sensorMax }{
		std::seed_seq sequence{ (uint32_t)seed, (uint32_t)(seed>>32), place };
		_engine.seed( sequence );
	}
	α TagQuality::Advance( const Device& device )ι->void{
		for( auto& window : _windows ){
			while( device.Clock>=window.End )//a period may jump a window, or several:  each is passed, never published.
				window.Advance( device, _engine );
		}
	}
	α TagQuality::Anchor( Duration drift, const Device& device )ι->void{
		for( auto& window : _windows )
			window.Anchor( drift, device, _engine );
	}
	α TagQuality::Apply( const Device& device, double raw, double& value )ι->UA_StatusCode{
		Advance( device );
		let window = find_if( _windows, [&](let& w){ return w.IsActive(device.Clock); } );
		let active = window!=_windows.end();
		if( !active || !window->Hold ){
			_clamp = _sensorMin && raw<*_sensorMin ? ELimit::Low : _sensorMax && raw>*_sensorMax ? ELimit::High : ELimit::None;
			value = _clamp==ELimit::Low ? *_sensorMin : _clamp==ELimit::High ? *_sensorMax : raw;
		}
		if( active )
			return window->HasLimit ? window->Status : Compose( window->Status, {_clamp} );
		return _clamp==ELimit::None ? UA_STATUSCODE_GOOD : Compose( UA_STATUSCODE_UNCERTAINENGINEERINGUNITSEXCEEDED, {_clamp} );
	}
}