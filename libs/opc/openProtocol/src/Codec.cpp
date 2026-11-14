#include <jde/opc/openProtocol/Codec.h>
#include "Layouts.h"

#define let const auto

namespace Jde::Opc{
namespace OpenProtocol{
	template<class... Args> Ω refuse( ERefusal code, fmt::format_string<Args...> what, Args&&... args )ι->std::unexpected<Refusal>{
		return std::unexpected{ Refusal{code, Ƒ(what, FWD(args)...)} };
	}
	//A header field's number:  its digits, or nullopt where it is all spaces.  false for anything else.
	Ω headerNumber( sv field, optional<uint16_t>& y )ι->bool{
		y.reset();
		if( field.find_first_not_of(' ')==sv::npos )
			return true;
		uint16_t n{};
		for( let c : field ){
			if( c<'0' || c>'9' )
				return false;
			n = n*10+( c-'0' );
		}
		y = n;
		return true;
	}
	//§7.1's table, a part count or number from its one character:  0 for the space and the 0 that are not used, nullopt
	//outside the table.  85 to 91 have two characters each, the second in code page 437's 0xE0 to 0xEE with 92 to 99.
	Ω partNumber( char ch )ι->optional<uint8_t>{
		let c = (uint8_t)ch;
		if( c==' ' || c=='0' )
			return 0;
		if( c>='1' && c<='9' )
			return c-'0';
		if( c>='A' && c<='^' )
			return 10+( c-'A' );
		if( c>='a' && c<='~' )
			return 40+( c-'a' );
		if( c>='!' && c<='/' )
			return 70+( c-'!' );
		if( c>=':' && c<='@' )
			return 85+( c-':' );
		if( c>=0xE0 && c<=0xEE )
			return 85+( c-0xE0 );
		return nullopt;
	}
	Ω frameLength( sv digits )ι->Expected<uint16_t>{
		optional<uint16_t> y;
		if( !headerNumber(digits, y) || !y )
			return refuse( ERefusal::Length, "The length {} is not four digits.", Quote(digits) );
		if( *y<HeaderSize )
			return refuse( ERefusal::Length, "The length {} is under the header's {}.", *y, HeaderSize );
		return *y;
	}

	Τ Ω decodeAs( const Header& head, sv data )ι->Expected<Body>{
		if( !head.Revision || head.Revision>T::MaxRevision )
			return refuse( ERefusal::UnknownRevision, "MID {:04} revision {} is not 1 to {}, the revisions the codec reads.", head.Mid, head.Revision, T::MaxRevision );
		if( head.Parts>1 )
			return refuse( ERefusal::Linked, "MID {:04} is part {} of {}, and the codec joins no linked message.", head.Mid, head.Part, head.Parts );
		T m;
		Reader reader{ data, head };
		Walk( m, reader, head.Revision );
		if( auto refusal = reader.Finish() )
			return std::unexpected{ move(*refusal) };
		return Body{ move(m) };
	}
	template<class... T> Ω decodeOneOf( std::type_identity<variant<T...>>, const Header& head, sv data )ι->Expected<Body>{
		optional<Expected<Body>> y;
		(void)( (head.Mid==T::Id && (y = decodeAs<T>(head, data), true)) || ... );
		if( !y )
			return refuse( ERefusal::UnknownMid, "MID {:04} is not one the codec reads.", head.Mid );
		return move( *y );
	}
	template<class... T> Ω maxRevisionOf( std::type_identity<variant<T...>>, uint16_t mid )ι->uint16_t{
		uint16_t y{};
		(void)( (mid==T::Id && (y = T::MaxRevision, true)) || ... );
		return y;
	}
}
	α OpenProtocol::FrameSize( sv buffer )ι->Expected<optional<uint>>{
		if( buffer.size()<4 )
			return nullopt;
		return frameLength( buffer.substr(0, 4) ).transform( []( uint16_t y )ι->optional<uint>{ return y+1; } );
	}

	α OpenProtocol::DecodeHeader( sv frame )ι->Expected<FrameHeader>{
		if( frame.size()<HeaderSize )
			return refuse( ERefusal::Length, "{} bytes is short of the header's {}.", frame.size(), HeaderSize );
		FrameHeader y;
		if( let n = frameLength(frame.substr(0, 4)) )
			y.Length = *n;
		else
			return std::unexpected{ n.error() };
		auto& head = y.Head;
		optional<uint16_t> n;
		if( !headerNumber(frame.substr(4, 4), n) || !n )
			return refuse( ERefusal::Header, "The MID {} is not four digits.", Quote(frame.substr(4, 4)) );
		head.Mid = *n;
		if( !headerNumber(frame.substr(8, 3), n) )
			return refuse( ERefusal::Revision, "MID {:04}:  the revision {} is not three digits or three spaces.", head.Mid, Quote(frame.substr(8, 3)) );
		head.Revision = n.value_or( 0 ) ? *n : 1;
		if( let flag = frame[11]; flag=='1' )
			head.NoAck = true;
		else if( flag!='0' && flag!=' ' )
			return refuse( ERefusal::Header, "MID {:04}:  the no-ack flag {} is not 0, 1 or a space.", head.Mid, Quote(frame.substr(11, 1)) );
		struct Field{ uint8_t At; sv Name; uint8_t Default; uint8_t& Value; };
		for( let& f : {Field{12, "station", 1, head.Station}, Field{14, "spindle", 1, head.Spindle}, Field{16, "sequence number", 0, head.Sequence}} ){
			if( !headerNumber(frame.substr(f.At, 2), n) )
				return refuse( ERefusal::Header, "MID {:04}:  the {} {} is not two digits or two spaces.", head.Mid, f.Name, Quote(frame.substr(f.At, 2)) );
			f.Value = (uint8_t)n.value_or( f.Default );
		}
		let parts = partNumber( frame[18] ), part = partNumber( frame[19] );
		if( !parts || !part )
			return refuse( ERefusal::Parts, "MID {:04}:  the part fields {} have a character outside the part table.", head.Mid, Quote(frame.substr(18, 2)) );
		if( *parts ? !*part || *part>*parts : *part!=0 )
			return refuse( ERefusal::Parts, "MID {:04}:  part {} of {} is no part.", head.Mid, *part, *parts );
		head.Parts = *parts;
		head.Part = *part;
		return y;
	}

	α OpenProtocol::DecodeBody( const Header& head, sv data )ι->Expected<Body>{
		return decodeOneOf( std::type_identity<Body>{}, head, data );
	}

	α OpenProtocol::Decode( sv frame )ι->Expected<Message>{
		let head = DecodeHeader( frame );
		if( !head )
			return std::unexpected{ head.error() };
		let length = head->Length;
		if( frame.size()<length || frame.size()>length+1u )
			return refuse( ERefusal::Length, "MID {:04}:  the length {} and its NUL make {} bytes, and the frame is {}.", head->Head.Mid, length, length+1, frame.size() );
		if( frame.size()==length || frame[length]!='\0' )
			return refuse( ERefusal::Nul, "MID {:04}:  no NUL follows the length's {} bytes.", head->Head.Mid, length );
		auto h = head->Head;
		//No request names an acknowledgement's revision, and a controller may write there the revision of the request it answers:
		//MID 0005 is Table 24's one layout, and MID 0004's code is Table 21's two digits where its frame holds no third.
		if( h.Mid==Mid0005::Id )
			h.Revision = 1;
		else if( h.Mid==Mid0004::Id && h.Revision>1 )
			h.Revision = length<HeaderSize+7 ? 1 : 2;
		return DecodeBody( h, frame.substr(HeaderSize, length-HeaderSize) ).transform( [&]( Body&& body ){ return Message{h, move(body)}; } );
	}

	α OpenProtocol::Encode( Header head, const Body& body )ι->Expected<string>{
		return std::visit( [&]<class T>( const T& m )ι->Expected<string>{
			head.Mid = T::Id;
			if( !head.Revision || head.Revision>T::MaxRevision )
				return refuse( ERefusal::UnknownRevision, "MID {:04} revision {} is not 1 to {}, the revisions the codec writes.", head.Mid, head.Revision, T::MaxRevision );
			if( head.Station>99 || head.Spindle>99 || head.Sequence>99 )
				return refuse( ERefusal::Value, "MID {:04}:  station {}, spindle {} and sequence number {} are two digits each.", head.Mid, head.Station, head.Spindle, head.Sequence );
			if( head.Parts>1 || head.Part!=head.Parts )
				return refuse( ERefusal::Linked, "MID {:04}:  part {} of {} is not a whole message, and the codec splits none.", head.Mid, head.Part, head.Parts );
			Writer writer{ head };
			Walk( m, writer, head.Revision );
			if( writer.Refused )
				return std::unexpected{ move(*writer.Refused) };
			let length = HeaderSize+writer.Data.size();
			if( length>MaxLength )
				return refuse( ERefusal::Length, "MID {:04} would be {} bytes, past {}.", head.Mid, length, MaxLength );
			auto frame = Ƒ( "{:04}{:04}{:03}{}{:02}{:02}{:02}{}{}", length, head.Mid, head.Revision, head.NoAck ? 1 : 0, head.Station, head.Spindle, head.Sequence, head.Parts, head.Part );
			frame += writer.Data;
			frame += '\0';
			return frame;
		}, body );
	}

	α OpenProtocol::MidOf( const Body& body )ι->uint16_t{
		return std::visit( []<class T>( const T& )ι{ return T::Id; }, body );
	}
	α OpenProtocol::MaxRevision( uint16_t mid )ι->uint16_t{
		return maxRevisionOf( std::type_identity<Body>{}, mid );
	}
}