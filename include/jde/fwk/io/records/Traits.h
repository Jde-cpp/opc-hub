#pragma once
#include <concepts>
#include "Framing.h"

namespace Jde::IO::Records{
	//What the layer asks of an owner's record type, a protobuf message with a oneof:  its specialization for the type.
	template<class T> struct Traits;
	template<class T> concept Record = requires( T& r, const T& c, State& s, const Proto::FileStart& f ){
		//The oneof members holding the layer's FileStart and Checkpoint, 15 at most so their tags are one byte.
		{ Traits<T>::StartField }->std::convertible_to<uint32_t>;
		{ Traits<T>::CheckpointField }->std::convertible_to<uint32_t>;
		//Whether the field is a oneof member this build has:  a whole frame of one it hasn't is Unknown.
		{ Traits<T>::Known( uint32_t{} ) }->std::same_as<bool>;
		//The record's ordering key:  none for a FileStart, a Checkpoint, or a record with none, which the merge files under
		//the time before it.
		{ Traits<T>::Time( c ) }->std::same_as<optional<Time>>;
		//The owner's encoding of its times against s, in file order - the historian's deltas, which a FileStart resets.
		//No-ops for an owner with none.
		{ Traits<T>::ToDisk( r, s ) };
		{ Traits<T>::ToMemory( r, s ) };
		//r becomes the FileStart record.
		{ Traits<T>::SetStart( r, f ) };
	} && Traits<T>::StartField<=15 && Traits<T>::CheckpointField<=15;
}