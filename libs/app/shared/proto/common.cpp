#include <jde/app/proto/common.h>

namespace Jde::App{
	static_assert( (int)Jde::Proto::Jde==(int)Jde::Exception::ECategory::Jde && (int)Jde::Proto::DB==(int)Jde::Exception::ECategory::DB );//Common.proto ECategory mirrors the fwk enum.

	α ProtoUtils::ToException( const runtime_error& e )ι->Jde::Proto::Exception{
		Jde::Proto::Exception proto;
		proto.set_what( e.what() );
		if( auto p = dynamic_cast<const Jde::Exception*>(&e); p ){
			proto.set_code( p->Code() );
			proto.set_status_code( p->HttpStatus() );//status & classification ride the base virtuals - the type can't cross the wire.
			proto.set_category( (Jde::Proto::ECategory)p->Category() );
			proto.set_category_code( p->CategoryCode() );
		}
		else
			proto.set_status_code( 500 );//plain std::exception - unclassified server fault.
		return proto;
	}
	α ProtoUtils::ToQuery( string&& text, jobject&& variables, bool returnRaw )ι->Jde::Proto::Query{
		Jde::Proto::Query query;
		query.set_text( move(text) );
		query.set_variables( serialize(variables) );
		query.set_return_raw( returnRaw );
		return query;
	}
}