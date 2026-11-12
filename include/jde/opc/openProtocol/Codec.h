#pragma once
#include <expected>
#include "Mids.h"

//The Open Protocol codec (docs/open-protocol/spec.md *The codec*):  frames to messages and back, with no I/O.  A frame is
//twenty ASCII bytes of header, the data field, and a NUL the length does not count (§2.2).  A peer is untrusted input, so
//nothing here throws:  what the tables do not allow comes back as a Refusal.
namespace Jde::Opc::OpenProtocol{
	constexpr uint16_t HeaderSize{ 20 };
	constexpr uint16_t MaxLength{ 9999 };//the header and the data field (§3.8).

	enum class ERefusal : uint8_t{
		Length,//the length is not four digits, is under the header's twenty, or is not the frame's.
		Header,//the MID, the no-ack flag, the station, the spindle or the sequence number is not its digits.
		Revision,//the revision is not three digits or three spaces.
		Parts,//a part field outside §7.1's table, or the two disagreeing.
		Nul,//no NUL where the length ends.
		UnknownMid,//a MID with no type here.
		UnknownRevision,//a revision past the type's tables.
		Linked,//one part of a linked message, which the codec does not join.
		Field,//a data field that is short, has another parameter's id, is not its digits or its characters, or is out of range.
		Value//encoding:  a value that does not fit its field.
	};
	struct Refusal{
		ERefusal Code;
		string What;
	};
	Τ using Expected = std::expected<T,Refusal>;

	//Table 1's fields but the length.  A digit field of spaces reads as its default, and the encoder writes digits.
	struct Header{
		uint16_t Mid{};
		uint16_t Revision{ 1 };//three spaces and 000 are 1.
		bool NoAck{};
		uint8_t Station{ 1 };//two spaces are 1.
		uint8_t Spindle{ 1 };
		uint8_t Sequence{};//1 to 99 at link level, 0 not used.
		uint8_t Parts{};//a linked message's parts and this one's number, 1 to 99 by §7.1's table, 0 not used.
		uint8_t Part{};
		α operator==( const Header& )Ι->bool = default;
	};
	//A header and the length its frame gives:  the header and the data field, the NUL not counted.
	struct FrameHeader{
		Header Head;
		uint16_t Length{};
	};
	struct Message{
		Header Head;
		Body Data;
		α operator==( const Message& )Ι->bool = default;
	};

	//The bytes the frame starting `buffer` takes on the wire, its length and the NUL after it, or nullopt until the
	//buffer holds the length's four digits:  a receiver finds a frame's end by its length, never by the NUL (§3.8).
	α FrameSize( sv buffer )ι->Expected<optional<uint>>;
	//The header of the frame starting `frame`, from its first twenty bytes.
	α DecodeHeader( sv frame )ι->Expected<FrameHeader>;
	//A frame's data field, without the NUL, by the revision of its own header whatever was asked.
	α DecodeBody( const Header& head, sv data )ι->Expected<Body>;
	//A whole frame with its NUL.
	α Decode( sv frame )ι->Expected<Message>;
	//The frame with its NUL, at the revision the header names;  its MID is the body's, whatever the header holds.
	α Encode( Header head, const Body& body )ι->Expected<string>;
	Ξ Encode( const Message& m )ι->Expected<string>{ return Encode( m.Head, m.Data ); }

	α MidOf( const Body& body )ι->uint16_t;
	α MaxRevision( uint16_t mid )ι->uint16_t;//the last revision of a MID's type, 0 for a MID with none.
	α ErrorName( uint16_t code )ι->sv;//Table 23's description of a MID 0004 code, empty for one it does not list.
	α LinkErrorName( uint16_t code )ι->sv;//Table 9's, of a MID 9998 code.
}