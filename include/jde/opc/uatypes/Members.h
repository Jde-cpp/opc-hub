#pragma once
#include "Variant.h"

//A structure's members as open62541 lays them out (ua_types.c copyStructure and copyUnion), read and written in one place (clients
//spec *Structures*):  each member after its padding;  an array as its uint count then its pointer, an optional one absent when the
//pointer is null (an empty one present is the sentinel);  an optional scalar by pointer, null when absent;  a union's selected member
//alone, at its padding from the start, after the UInt32 switch field.
namespace Jde::Opc{
	//Each member of the structure `p` of `type` to `f( member, data, count )`:  for a scalar member its element and 1, for an array member
	//its elements and their count - the data null or UA_EMPTY_ARRAY_SENTINEL for an empty one - and for an absent optional member null and
	//0;  a union's selected member alone, none when none is selected.  `f` answers false to stop, and so does the walk when it did.  The
	//walk throws what `f` throws:  noexcept only for an `f` that is, so a member a serializer refuses reaches its caller's catch.
	α ForEachMember( const void* p, const UA_DataType& type, const auto& f )noexcept( noexcept(f(*type.members, p, uint{})) )->bool{
		auto ptr = reinterpret_cast<uintptr_t>( p );
		const auto array = [&]( const UA_DataTypeMember& m, uintptr_t at )->bool{
			const auto count = *reinterpret_cast<const uint*>( at );
			const auto data = *reinterpret_cast<const void* const*>( at+sizeof(uint) );
			return f( m, data, data && data!=UA_EMPTY_ARRAY_SENTINEL ? count : uint{} );
		};
		if( type.typeKind==UA_DATATYPEKIND_UNION ){
			const auto selection = *reinterpret_cast<const UA_UInt32*>( ptr );
			if( !selection || selection>type.membersSize )
				return true;
			const auto& m = type.members[selection-1];
			return m.isArray ? array( m, ptr+m.padding ) : f( m, reinterpret_cast<const void*>(ptr+m.padding), 1 );
		}
		for( uint i=0; i<type.membersSize; ++i ){
			const auto& m = type.members[i];
			ptr += m.padding;
			if( m.isArray ){
				if( !array(m, ptr) )
					return false;
				ptr += sizeof(uint)+sizeof(void*);
			}
			else if( m.isOptional ){
				const auto member = *reinterpret_cast<const void* const*>( ptr );
				if( !f(m, member, member ? 1 : 0) )
					return false;
				ptr += sizeof(void*);
			}
			else{
				if( !f(m, reinterpret_cast<const void*>(ptr), 1) )
					return false;
				ptr += m.memberType->memSize;
			}
		}
		return true;
	}

	//A value of `have` fits a member of `want`, its bytes moved into the slot as they are:  the type itself, one of its kind - Duration's
	//Double, UtcTime's DateTime - or an enum's Int32;  for a structure the type its id names, a copy of it another registry array may hold.
	//A kind of the same size never fits another:  a Guid's bytes in a String slot are a length and a pointer.
	Ξ MemberFits( const UA_DataType& have, const UA_DataType& want )ι->bool{
		if( &have==&want )
			return true;
		if( have.memSize!=want.memSize )
			return false;
		if( IsStructure(have) || IsStructure(want) )
			return IsStructure( have ) && IsStructure( want ) && UA_NodeId_equal( &have.typeId, &want.typeId );
		return have.typeKind==want.typeKind || ( want.typeKind==UA_DATATYPEKIND_ENUM && have.typeKind==UA_DATATYPEKIND_INT32 );
	}

	//The structure `p` of `type`, zeroed by UA_new, filled member by member from `member( m )`:  a Variant holding the member's value - a
	//scalar, or the array an array member takes, null for a null one, of a type that fits the member's (MemberFits) - whose box the fill
	//takes, or nullopt for one absent.  A required member absent, a value that doesn't fit, a scalar given an array or an array a scalar,
	//a union given more than one of its members:  refused, naming the member, and what a throw leaves half filled clears cleanly.
	//Value::Set's structures and ProtoUtils::ToVariant's fill by it.
	α FillMembers( void* p, const UA_DataType& type, const auto& member, SL sl )ε->void{
		auto ptr = reinterpret_cast<uintptr_t>( p );
		const auto check = [&]( const UA_DataTypeMember& m, const Variant& v )ε->void{
			THROW_IFSL( !v.type, "'{}'.{} has no value.", type.typeName, m.memberName );
			THROW_IFSL( !MemberFits(*v.type, *m.memberType), "'{}'.{} is a '{}', not a '{}'.", type.typeName, m.memberName, v.type->typeName, m.memberType->typeName );
		};
		const auto take = [&]( const UA_DataTypeMember& m, Variant& v )ε->std::pair<void*,uint>{
			check( m, v );
			THROW_IFSL( !v.data, "'{}'.{} has no value.", type.typeName, m.memberName );
			std::pair<void*,uint> y{ v.data, v.arrayLength };
			v.data = nullptr; v.arrayLength = 0;//the elements are the slot's now;  the box's dimensions, if any, go with the Variant.
			return y;
		};
		const auto scalar = [&]( const UA_DataTypeMember& m, Variant& v, uintptr_t at )ε->void{
			THROW_IFSL( !v.IsScalar(), "'{}'.{} is a scalar.", type.typeName, m.memberName );
			const auto [data, _] = take( m, v );
			::memcpy( reinterpret_cast<void*>(at), data, m.memberType->memSize );
			UA_free( data );//the box, not what it points at - that moved into the slot.
		};
		const auto array = [&]( const UA_DataTypeMember& m, Variant& v, uintptr_t at )ε->void{
			THROW_IFSL( v.IsScalar(), "'{}'.{} is an array.", type.typeName, m.memberName );
			if( !v.data ){//a null array, length -1 on the wire, as a server sends one it never set:  the zeroed slot is one.
				check( m, v );
				return;
			}
			const auto [data, count] = take( m, v );
			*reinterpret_cast<uint*>( at ) = count;
			*reinterpret_cast<void**>( at+sizeof(uint) ) = data;
		};
		if( type.typeKind==UA_DATATYPEKIND_UNION ){
			for( UA_UInt32 i=0; i<type.membersSize; ++i ){
				const auto& m = type.members[i];
				auto v = member( m );
				if( !v )
					continue;
				THROW_IFSL( *reinterpret_cast<const UA_UInt32*>(ptr), "A '{}' is one of its members, not {} and {}.", type.typeName, type.members[*reinterpret_cast<const UA_UInt32*>(ptr)-1].memberName, m.memberName );
				*reinterpret_cast<UA_UInt32*>( ptr ) = i+1;
				if( m.isArray )
					array( m, *v, ptr+m.padding );
				else
					scalar( m, *v, ptr+m.padding );
			}
			return;
		}
		for( uint i=0; i<type.membersSize; ++i ){
			const auto& m = type.members[i];
			ptr += m.padding;
			auto v = member( m );
			THROW_IFSL( !v && !m.isOptional, "'{}'.{} is missing.", type.typeName, m.memberName );
			if( m.isArray ){
				if( v )
					array( m, *v, ptr );
				ptr += sizeof(uint)+sizeof(void*);
			}
			else if( m.isOptional ){
				if( v ){
					THROW_IFSL( !v->IsScalar(), "'{}'.{} is a scalar.", type.typeName, m.memberName );
					*reinterpret_cast<void**>( ptr ) = take( m, *v ).first;
				}
				ptr += sizeof(void*);
			}
			else{
				scalar( m, *v, ptr );
				ptr += m.memberType->memSize;
			}
		}
	}
}