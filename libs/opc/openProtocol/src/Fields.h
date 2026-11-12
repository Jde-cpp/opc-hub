#pragma once
#include <jde/opc/openProtocol/Codec.h>

//The data field's two walkers, with the same members, so a MID's layout is written once (Layouts.h) and read both ways.
//A table's row is one call:  the parameter's id, 0 where the table gives none, the member and the value's width.  Each
//walker keeps its first refusal and does nothing after it.
namespace Jde::Opc::OpenProtocol{
	α Quote( sv bytes )ι->string;//for a refusal's text:  quoted, a byte that is no printable ASCII as \xNN.

	struct Reader{
		Reader( sv data, const Header& head )ι: _data{ data }, _head{ head }{}
		//Digits, zero-padded as Table 3 has them or, as the specification's own MID 0061 and 0065 examples have their
		//tightening ids, left-justified with spaces after.  All spaces is a value not sent.
		template<std::unsigned_integral T> α Num( uint8_t id, optional<T>& v, uint8_t width )ι->void{
			if( auto n = Number(id, width, std::numeric_limits<T>::max()) )
				v = (T)*n;
		}
		α Scaled( uint8_t id, optional<double>& v, uint8_t width )ι->void;//the integer over 100.
		α Text( uint8_t id, string& v, uint16_t width )ι->void;//Latin-1 as UTF-8, without the spaces after it.
		α Time( uint8_t id, optional<LocalTime>& v )ι->void;
		α Rest( string& v, uint16_t max )ι->void;//a text that runs to the data field's end.
		//The refusal.  Bytes after the last field, as a later firmware's parameters would be, are ignored:  logged at Warning
		//the first time the process sees them for a MID and revision, and at Debug after.
		α Finish()ι->optional<Refusal>;
	private:
		α Take( uint8_t id, uint16_t width )ι->optional<sv>;
		α Number( uint8_t id, uint8_t width, uint64_t max )ι->optional<uint64_t>;
		α SetText( uint8_t id, uint start, sv field, string& v )ι->void;
		α Refuse( uint8_t id, uint start, string&& why )ι->void;
		sv _data;
		const Header& _head;
		uint _i{};
		optional<Refusal> _refusal;
	};

	struct Writer{
		Writer( const Header& head )ι: _head{ head }{}
		template<std::unsigned_integral T> α Num( uint8_t id, const optional<T>& v, uint8_t width )ι->void{
			Number( id, v ? optional<uint64_t>{*v} : nullopt, width );
		}
		α Scaled( uint8_t id, const optional<double>& v, uint8_t width )ι->void;//to the nearest hundredth.
		α Text( uint8_t id, sv v, uint16_t width )ι->void;
		α Time( uint8_t id, const optional<LocalTime>& v )ι->void;
		α Rest( sv v, uint16_t max )ι->void;
		string Data;
		optional<Refusal> Refused;
	private:
		α Id( uint8_t id )ι->void;
		α Latin1( uint8_t id, sv v )ι->optional<string>;//`v`'s wire bytes, refused where it is no text.
		α Number( uint8_t id, optional<uint64_t> v, uint8_t width )ι->void;
		α Refuse( uint8_t id, string&& why )ι->void;
		const Header& _head;
	};
}