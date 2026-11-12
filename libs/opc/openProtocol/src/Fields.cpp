#include "Fields.h"
#include <cmath>
#include <jde/opc/openProtocol/logTags.h>

#define let const auto

namespace Jde::Opc{
	using namespace std::chrono;
namespace OpenProtocol{
	constexpr array<uint64_t,11> Pow10{ 1, 10, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000, 100'000'000, 1'000'000'000, 10'000'000'000 };
	constexpr uint8_t TimeWidth{ 19 };
	constexpr LocalTime MinTime{ local_days{year{0}/1/1} };
	constexpr LocalTime MaxTime{ local_days{year{9999}/12/31}+23h+59min+59s };

	//The row a refusal is about, its byte counted from the frame's first as the tables count.
	Ω where( const Header& head, uint8_t id, uint start )ι->string{
		let byte = HeaderSize+1+start;
		return id
			? Ƒ( "MID {:04} revision {}, parameter {:02} at byte {}", head.Mid, head.Revision, id, byte )
			: Ƒ( "MID {:04} revision {}, byte {}", head.Mid, head.Revision, byte );
	}
	//A text's bytes past ASCII are Latin-1, one character each, so none is lost whatever code page the controller writes.
	Ω isControl( char c )ι->bool{ return (uint8_t)c<0x20; }
	Ω toUtf8( sv latin1 )ι->string{
		string y;
		y.reserve( latin1.size() );
		for( let c : latin1 ){
			if( let b = (uint8_t)c; b<0x80 )
				y += c;
			else{
				y += (char)( 0xC0 | b>>6 );
				y += (char)( 0x80 | (b & 0x3F) );
			}
		}
		return y;
	}
	//nullopt where `utf8` is not UTF-8, or has a character past U+00FF or below U+0020.
	Ω toLatin1( sv utf8 )ι->optional<string>{
		string y;
		y.reserve( utf8.size() );
		for( uint i=0; i<utf8.size(); ++i ){
			let b = (uint8_t)utf8[i];
			if( b>=0x20 && b<0x80 )
				y += utf8[i];
			else if( (b==0xC2 || b==0xC3) && i+1<utf8.size() && ((uint8_t)utf8[i+1] & 0xC0)==0x80 )
				y += (char)( (b & 0x03)<<6 | ((uint8_t)utf8[++i] & 0x3F) );
			else
				return nullopt;
		}
		return y;
	}
	Ω withoutPadding( sv field )ι->sv{ return field.substr( 0, field.find_last_not_of(' ')+1 ); }

	α Reader::Refuse( uint8_t id, uint start, string&& why )ι->void{
		_refusal = Refusal{ ERefusal::Field, Ƒ("{}:  {}.", where(_head, id, start), why) };
	}
	α Reader::Take( uint8_t id, uint16_t width )ι->optional<sv>{
		if( _refusal )
			return nullopt;
		let start = _i;
		if( id ){
			let found = _data.substr( _i, 2 );
			if( found.size()<2 || found[0]!='0'+id/10 || found[1]!='0'+id%10 ){
				Refuse( id, start, found.size()<2 ? "the data field ends before it" : Ƒ("{} is not the parameter's id", Quote(found)) );
				return nullopt;
			}
			_i += 2;
		}
		if( _data.size()-_i<width ){
			Refuse( id, start, Ƒ("the value is {} bytes and the data field ends after {}", width, _data.size()-_i) );
			return nullopt;
		}
		let value = _data.substr( _i, width );
		_i += width;
		return value;
	}
	α Reader::Number( uint8_t id, uint8_t width, uint64_t max )ι->optional<uint64_t>{
		let start = _i;
		let field = Take( id, width );
		if( !field )
			return nullopt;
		let digits = withoutPadding( *field );
		if( digits.empty() )
			return nullopt;
		uint64_t n{};
		for( let c : digits ){
			if( c<'0' || c>'9' ){
				Refuse( id, start, Ƒ("{} is not digits", Quote(*field)) );
				return nullopt;
			}
			n = n*10+( c-'0' );
		}
		if( n>max ){
			Refuse( id, start, Ƒ("{} is past {}", n, max) );
			return nullopt;
		}
		return n;
	}
	α Reader::Scaled( uint8_t id, optional<double>& v, uint8_t width )ι->void{
		if( let n = Number(id, width, std::numeric_limits<uint64_t>::max()) )
			v = *n/100.0;
	}
	α Reader::SetText( uint8_t id, uint start, sv field, string& v )ι->void{
		if( std::ranges::any_of(field, isControl) )
			Refuse( id, start, Ƒ("{} has a control byte, below 0x20", Quote(field)) );
		else
			v = toUtf8( withoutPadding(field) );
	}
	α Reader::Text( uint8_t id, string& v, uint16_t width )ι->void{
		let start = _i;
		if( let field = Take(id, width) )
			SetText( id, start, *field, v );
	}
	α Reader::Time( uint8_t id, optional<LocalTime>& v )ι->void{
		let start = _i;
		let field = Take( id, TimeWidth );
		if( !field || withoutPadding(*field).empty() )
			return;
		constexpr sv shape{ "dddd-dd-dd:dd:dd:dd" };
		for( uint i=0; i<shape.size(); ++i ){
			if( shape[i]=='d' ? (*field)[i]<'0' || (*field)[i]>'9' : (*field)[i]!=shape[i] )
				return Refuse( id, start, Ƒ("{} is not a YYYY-MM-DD:HH:MM:SS", Quote(*field)) );
		}
		let part = [&]( uint at, uint digits )ι{
			uint y{};
			for( uint i=at; i<at+digits; ++i )
				y = y*10+( (*field)[i]-'0' );
			return y;
		};
		let ymd = year{ (int)part(0, 4) }/month{ (unsigned)part(5, 2) }/day{ (unsigned)part(8, 2) };
		let h = part( 11, 2 ), m = part( 14, 2 ), s = part( 17, 2 );
		//Digits that name no date, 0000-00-00:00:00:00 among them, are a controller's placeholder:  the time is not set.
		if( ymd.ok() && h<24 && m<60 && s<60 )
			v = local_days{ ymd }+hours{ h }+minutes{ m }+seconds{ s };
	}
	α Reader::Rest( string& v, uint16_t max )ι->void{
		if( _refusal )
			return;
		let start = _i;
		let field = _data.substr( _i );
		if( field.size()>max )
			return Refuse( 0, start, Ƒ("{} bytes is past the field's {}", field.size(), max) );
		_i = _data.size();
		SetText( 0, start, field, v );
	}
	α Reader::Finish()ι->optional<Refusal>{
		if( !_refusal && _i<_data.size() ){
			let past = _data.substr( _i );
			let first = Logging::ShouldLog( ELogLevel::Warning, LogTag )
				&& Logging::MarkLogged( Crypto::Md5(Ƒ("openProtocol MID {} revision {} past its layout", _head.Mid, _head.Revision)) );
			LOG( first ? ELogLevel::Warning : ELogLevel::Debug, LogTag, "MID {:04} revision {}:  ignored {}, after byte {}, the revision's last.", _head.Mid, _head.Revision, Quote(past), HeaderSize+_i );
		}
		return move( _refusal );
	}

	α Writer::Refuse( uint8_t id, string&& why )ι->void{
		Refused = Refusal{ ERefusal::Value, Ƒ("{}:  {}.", where(_head, id, Data.size()), why) };
	}
	α Writer::Id( uint8_t id )ι->void{
		if( id )
			fmt::format_to( std::back_inserter(Data), "{:02}", id );
	}
	α Writer::Number( uint8_t id, optional<uint64_t> v, uint8_t width )ι->void{
		if( Refused )
			return;
		if( v && *v>=Pow10[width] )
			return Refuse( id, Ƒ("{} does not fit {} digits", *v, width) );
		Id( id );
		if( v )
			fmt::format_to( std::back_inserter(Data), "{:0{}}", *v, width );
		else
			Data.append( width, ' ' );
	}
	α Writer::Scaled( uint8_t id, const optional<double>& v, uint8_t width )ι->void{
		if( !v )
			return Number( id, nullopt, width );
		if( Refused )
			return;
		if( !(*v>=0) || *v*100>=(double)Pow10[width] )
			return Refuse( id, Ƒ("{} times 100 does not fit {} digits", *v, width) );
		Number( id, (uint64_t)std::llround(*v*100), width );
	}
	α Writer::Latin1( uint8_t id, sv v )ι->optional<string>{
		auto y = toLatin1( v );
		if( !y )
			Refuse( id, Ƒ("{} is not UTF-8 of U+0020 to U+00FF, a text's characters", Quote(v)) );
		return y;
	}
	α Writer::Text( uint8_t id, sv v, uint16_t width )ι->void{
		if( Refused )
			return;
		let bytes = Latin1( id, v );
		if( !bytes )
			return;
		if( bytes->size()>width )
			return Refuse( id, Ƒ("{} is {} characters and the field {}", Quote(v), bytes->size(), width) );
		Id( id );
		Data += *bytes;
		Data.append( width-bytes->size(), ' ' );
	}
	α Writer::Time( uint8_t id, const optional<LocalTime>& v )ι->void{
		if( Refused )
			return;
		if( v && (*v<MinTime || *v>MaxTime) )
			return Refuse( id, "the time is outside the years 0000 to 9999" );
		Id( id );
		if( !v ){
			Data.append( TimeWidth, ' ' );
			return;
		}
		let day = floor<days>( *v );
		const year_month_day ymd{ day };
		const hh_mm_ss time{ *v-day };
		fmt::format_to( std::back_inserter(Data), "{:04}-{:02}-{:02}:{:02}:{:02}:{:02}", (int)ymd.year(), (unsigned)ymd.month(), (unsigned)ymd.day(), time.hours().count(), time.minutes().count(), time.seconds().count() );
	}
	α Writer::Rest( sv v, uint16_t max )ι->void{
		if( Refused )
			return;
		let bytes = Latin1( 0, v );
		if( !bytes )
			return;
		if( bytes->size()>max )
			return Refuse( 0, Ƒ("{} is {} characters and the field {} at most", Quote(v), bytes->size(), max) );
		Data += *bytes;
	}
}
	α OpenProtocol::Quote( sv bytes )ι->string{
		string y{ '\'' };
		for( let c : bytes ){
			if( (uint8_t)c>=0x20 && (uint8_t)c<0x7F )
				y += c;
			else
				fmt::format_to( std::back_inserter(y), "\\x{:02x}", (uint8_t)c );
		}
		return y+'\'';
	}
}