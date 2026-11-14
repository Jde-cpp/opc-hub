#pragma once
#include <fstream>
#include <jde/opc/openProtocol/Codec.h>
#define let const auto

//Frames written by hand from the specification's tables (Atlas Copco 9836 4415 01, Release 2.20.1), never by the encoder
//they check.
namespace Jde::Opc::OpenProtocol::Tests{
	using namespace std::chrono;

	Ξ At( int y, unsigned m, unsigned d, int h, int min, int s )ι->LocalTime{ return local_days{ year{y}/month{m}/day{d} }+hours{h}+minutes{min}+seconds{s}; }

	//A frame with the header the encoder writes, Table 1's fields as digits:  the length, the MID, the revision, the no-ack
	//flag, the station, the spindle, the sequence number and the two part fields.
	Ξ Frame( const Header& h, sv data )ι->string{
		return Ƒ( "{:04}{:04}{:03}{}{:02}{:02}{:02}{}{}", HeaderSize+data.size(), h.Mid, h.Revision, h.NoAck ? 1 : 0, h.Station, h.Spindle, h.Sequence, h.Parts, h.Part )+string{ data }+'\0';
	}
	//A frame as the specification's examples have their headers, the nine bytes after the revision blank, or eight after a
	//no-ack flag.
	Ξ ExampleFrame( sv lengthMidRevision, sv data )ι->string{
		return string{ lengthMidRevision }+string( HeaderSize-lengthMidRevision.size(), ' ' )+string{ data }+'\0';
	}
	Ξ Padded( sv text, uint width )ι->string{ return string{ text }+string( width-text.size(), ' ' ); }

	//A frame and the message it is.  `File` is its name in the fuzz corpus.
	struct Case{
		string Name;
		string File;
		string Frame;
		Message Expected;
	};
	//A MID's data field written a table row at a time, beside the message the rows are.  Each row gives its Byte column,
	//which must be where the bytes written so far end, so a width that drifts from the table fails the case.
	Τ struct Table{
		Table( uint16_t revision )ι: Revision{ revision }{}
		//A row with a parameter id, `byte` where the id starts.  A text is padded to `width`.
		α Row( uint byte, uint8_t id, sv value, uint width=0 )ι->void{
			Starts( byte );
			Data += Ƒ( "{:02}", id );
			Data += Padded( value, std::max<uint>(width, value.size()) );
		}
		//A row with no id, `byte` where its value starts.
		α Value( uint byte, sv value, uint width=0 )ι->void{
			Starts( byte );
			Data += Padded( value, std::max<uint>(width, value.size()) );
		}
		//The Byte column's last, for the revision.
		α Ends( uint byte )ι->void{ EXPECT_EQ( HeaderSize+Data.size(), byte ) << Name() << " ends"; }
		α Name()Ι->string{ return Ƒ( "MID {:04} revision {}", T::Id, Revision ); }
		α ToCase()Ι->Case{
			const Header head{ .Mid=T::Id, .Revision=Revision };
			return { Name(), Ƒ("mid{:04}-r{}", T::Id, Revision), Frame(head, Data), Message{head, M} };
		}
		uint16_t Revision;
		string Data;
		T M;
	private:
		α Starts( uint byte )ι->void{ EXPECT_EQ( HeaderSize+Data.size()+1, byte ) << Name() << " byte " << byte; }
	};

	α Cases()ι->vector<Case>;//every MID at every revision.
	α Examples()ι->vector<Case>;//the specification's example frames.
	α Simulator()ι->vector<std::pair<string,string>>;//frames of Jarrekstar's simulator as it wrote them, by their files in the fuzz corpus (SimulatorTests.cpp).

	template<class... T> α Revisions( std::type_identity<variant<T...>> )ι->vector<std::pair<uint16_t,uint16_t>>{ return { {T::Id, T::MaxRevision}... }; }
	//Each MID with a type and its last revision.
	Ξ Revisions()ι->vector<std::pair<uint16_t,uint16_t>>{ return Revisions( std::type_identity<Body>{} ); }

	//A file's bytes as they are:  a frame of the fuzz corpus's or of fixtures/.
	Ξ ReadFile( const fs::path& file )ι->string{
		std::ifstream is{ file, std::ios::binary };
		return { std::istreambuf_iterator<char>{is}, {} };
	}
	//What refused `frame`, or nullopt where it decoded.
	Ξ Refused( sv frame )ι->optional<ERefusal>{
		let y = Decode( frame );
		return y ? nullopt : optional{ y.error().Code };
	}
}