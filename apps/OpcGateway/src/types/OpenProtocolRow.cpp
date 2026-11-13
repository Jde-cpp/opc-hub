#include "OpenProtocolRow.h"
#include <jde/fwk/exceptions/refuse.h>
#include <jde/opc/openProtocol/Mids.h>
#include "ServerCnnctn.h"

#define let const auto

namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Opc };

	//`text`'s digits as a number from `min` to `max`, nullopt for anything else:  a sign, a space, no digit or a value outside.
	Ω digits( sv text, uint min, uint max )ι->optional<uint>{
		uint y{};
		let [end, ec] = std::from_chars( text.data(), text.data()+text.size(), y );
		let read = ec==std::errc{} && end==text.data()+text.size();//into an unsigned type, from_chars takes no sign, space or prefix.
		return read && y>=min && y<=max ? optional<uint>{ y } : nullopt;
	}
	Ω readUrl( str url, OpenProtocolRow& y, SL sl )ε->void{
		let refuse = [&]( sv why ){ REFUSE( "Url '{}' is not an Open Protocol controller's, {}host[:port] with 4545 where the port is left off:  {}.", url, OpenProtocolRow::Scheme, why ); };
		if( !url.starts_with(OpenProtocolRow::Scheme) )
			refuse( "it does not start with the scheme" );
		auto rest = sv{ url }.substr( OpenProtocolRow::Scheme.size() );
		let bracketed = rest.starts_with( '[' );//an IPv6 address, whose colons are not the port's.
		let hostEnd = bracketed ? rest.find( ']' ) : std::min( rest.find(':'), rest.size() );
		if( hostEnd==sv::npos )
			refuse( "its '[' has no ']'" );
		let host = rest.substr( bracketed ? 1 : 0, bracketed ? hostEnd-1 : hostEnd );
		let allowed = [bracketed]( char c ){ return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='.' || c=='-' || c=='_' || (bracketed && (c==':' || c=='%')); };
		if( host.empty() || !std::ranges::all_of(host, allowed) )
			refuse( host.empty() ? "it names no host" : "its host is not a name or an address" );
		y.Host = string{ host };
		rest.remove_prefix( bracketed ? hostEnd+1 : hostEnd );
		if( rest.empty() )
			return;
		let port = rest.starts_with( ':' ) ? digits( rest.substr(1), 1, 65535 ) : nullopt;
		if( !port )
			refuse( "after the host comes a port, 1 to 65535, or nothing" );
		y.Port = (uint16_t)*port;
	}
	Ω readDriver( str driver, OpenProtocolRow& y, SL sl )ε->void{
		boost::system::error_code ec;
		let parsed = boost::json::parse( driver, ec );
		REFUSE_IF( ec || !parsed.is_object(), "An Open Protocol connection's driver is a json object of timeZone and revisions, not '{}'.", driver );
		for( let& member : parsed.get_object() ){
			let& value = member.value();
			if( member.key()=="timeZone" ){
				REFUSE_IF( !value.is_string(), "The driver's timeZone is a time zone's IANA name, not {}.", serialize(value) );
				try{
					y.TimeZone = std::chrono::locate_zone( sv{value.get_string()} );
				}
				catch( const std::runtime_error& e ){
					REFUSE( "The driver's timeZone '{}' is not a time zone the gateway knows:  {}", sv{value.get_string()}, e.what() );
				}
			}
			else if( member.key()=="revisions" ){
				REFUSE_IF( !value.is_object(), "The driver's revisions are an object of MID to its highest revision, as in {{\"0061\":3}}, not {}.", serialize(value) );
				for( let& ceiling : value.get_object() ){
					let mid = ceiling.key().size()==4 ? digits( ceiling.key(), 0, 9999 ) : nullopt;
					REFUSE_IF( !mid, "The driver's revisions name a MID by its four digits, as in \"0061\", not \"{}\".", sv{ceiling.key()} );
					let revision = ceiling.value().try_to_number<uint>();
					REFUSE_IF( !revision || *revision<1 || *revision>999, "The driver's revision for MID {} is a whole number from 1 to 999, not {}.", sv{ceiling.key()}, serialize(ceiling.value()) );
					y.Revisions[(uint16_t)*mid] = (uint16_t)*revision;
				}
			}
			else
				REFUSE( "An Open Protocol connection's driver has no '{}':  it takes timeZone and revisions.", sv{member.key()} );
		}
	}

	OpenProtocolRow::OpenProtocolRow( const ServerCnnctn& row, SL sl )ε{
		readUrl( row.Url, *this, sl );
		if( row.Driver.size() )
			readDriver( row.Driver, *this, sl );
	}
	α OpenProtocolRow::Identity( const OpenProtocol::Mid0002& id )ι->jobject{
		let text = []( str value ){ return value.empty() ? jvalue{} : jvalue{ value }; };
		let number = []( optional<uint32_t> value ){ return value ? jvalue{ *value } : jvalue{}; };
		return {
			{ "controllerName", text(id.ControllerName) },
			{ "supplierCode", text(id.SupplierCode) },
			{ "openProtocolVersion", text(id.OpenProtocolVersion) },
			{ "controllerSoftwareVersion", text(id.ControllerSoftwareVersion) },
			{ "toolSoftwareVersion", text(id.ToolSoftwareVersion) },
			{ "controllerSerialNumber", text(id.ControllerSerialNumber) },
			{ "systemType", number(id.SystemType) },
			{ "systemSubtype", number(id.SystemSubtype) },
			{ "stationId", number(id.StationId) },
			{ "stationName", text(id.StationName) }
		};
	}
}