#include "Fields.h"
#include <cmath>

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
	Ω isText( sv v )ι->bool{
		return std::ranges::all_of( v, []( char c ){ return (uint8_t)c>=0x20 && (uint8_t)c<=0x7F; } );
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
		if( isText(field) )
			v = withoutPadding( field );
		else
			Refuse( id, start, Ƒ("{} has a byte outside a text's 0x20 to 0x7F", Quote(field)) );
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
		bool shaped{ true };
		for( uint i=0; i<shape.size() && shaped; ++i )
			shaped = shape[i]=='d' ? (*field)[i]>='0' && (*field)[i]<='9' : (*field)[i]==shape[i];
		let part = [&]( uint at, uint digits )ι{
			uint y{};
			for( uint i=at; i<at+digits; ++i )
				y = y*10+( (*field)[i]-'0' );
			return y;
		};
		if( shaped ){
			let ymd = year{ (int)part(0, 4) }/month{ (unsigned)part(5, 2) }/day{ (unsigned)part(8, 2) };
			let h = part( 11, 2 ), m = part( 14, 2 ), s = part( 17, 2 );
			if( ymd.ok() && h<24 && m<60 && s<60 ){
				v = local_days{ ymd }+hours{ h }+minutes{ m }+seconds{ s };
				return;
			}
		}
		Refuse( id, start, Ƒ("{} is not a YYYY-MM-DD:HH:MM:SS", Quote(*field)) );
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
		if( !_refusal && _i<_data.size() )
			_refusal = Refusal{ ERefusal::Field, Ƒ("MID {:04} revision {}:  the data field runs {} past byte {}, the revision's last.", _head.Mid, _head.Revision, _data.size()-_i, HeaderSize+_i) };
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
	α Writer::Text( uint8_t id, sv v, uint16_t width )ι->void{
		if( Refused )
			return;
		if( v.size()>width )
			return Refuse( id, Ƒ("{} is {} characters and the field {}", Quote(v), v.size(), width) );
		if( !isText(v) )
			return Refuse( id, Ƒ("{} has a byte outside a text's 0x20 to 0x7F", Quote(v)) );
		Id( id );
		Data += v;
		Data.append( width-v.size(), ' ' );
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
		if( v.size()>max )
			return Refuse( 0, Ƒ("{} is {} characters and the field {} at most", Quote(v), v.size(), max) );
		if( !isText(v) )
			return Refuse( 0, Ƒ("{} has a byte outside a text's 0x20 to 0x7F", Quote(v)) );
		Data += v;
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