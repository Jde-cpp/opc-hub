#pragma once
#include <span>
#include <jde/opc/usings.h>

struct UA_Client;
struct UA_Server;
namespace Jde::Opc{
	//UA_Client_delete and UA_Server_delete, called from UAPtr.cpp, so a header that holds either needs neither client.h nor server.h.
	struct ClientDelete final{ α operator()( UA_Client* p )Ι->void; };
	struct ServerDelete final{ α operator()( UA_Server* p )Ι->void; };
	using ClientPtr = up<UA_Client,ClientDelete>;
	using ServerPtr = up<UA_Server,ServerDelete>;
	//UA_delete by the type the value was made with:  one whose type is known only at runtime, UA_new's.
	struct UADelete final{
		α operator()( void* p )Ι->void{ UA_delete( p, Type ); }
		const UA_DataType* Type{};
	};
	template<class T=void> using UAPtr = up<T,UADelete>;
	//`type`'s value, zeroed, or null where it would not allocate.
	template<class T=void> α NewUA( const UA_DataType& type )ι->UAPtr<T>{ return UAPtr<T>{ static_cast<T*>(UA_new(&type)), UADelete{&type} }; }
	//An array of `type`, a service's answer or UA_Array_new's, freed with its scope by the same type.  T is the element's C type, or
	//UA_Byte for one known only at runtime, its elements memSize apart.
	template<class T>
	struct UAArray final : noncopyable{
		UAArray( const UA_DataType& type )ι:_type{ type }{}
		UAArray( uint size, const UA_DataType& type )ι:Data{ static_cast<T*>(UA_Array_new(size, &type)) }, Size{ Data ? size : 0 }, _type{ type }{}//zeroed, so a slot not yet filled frees as a no-op;  null where it would not allocate.
		~UAArray(){ UA_Array_delete( Data, Size, &_type ); }
		α Span()Ι->std::span<const T>{ return { Data, Size }; }
		α Release()ι->T*{ Size = 0; return std::exchange( Data, nullptr ); }//to a variant or a request, which frees it from here.
		T* Data{}; uint Size{};
	private:
		const UA_DataType& _type;
	};
}