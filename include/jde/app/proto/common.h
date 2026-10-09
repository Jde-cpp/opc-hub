#pragma once
#include <jde/db/DBException.h>
#include <jde/app/usings.h>

namespace Jde::App::ProtoUtils{
	//One Transmission holding one Message with request_id set; both sides' builders fill in the rest.
	template<class TTransmission> α SingleMessage( RequestId requestId, auto&& set )ι->TTransmission{
		TTransmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		set( m );
		return t;
	}
	//The one encoder for both builders: a Jde::Exception carries code/status/category, anything else is an unclassified 500.
	α ToException( const runtime_error& e )ι->Jde::Proto::Exception;
	//up<>, not by value: a Jde::Exception return would slice the DB classification back off. The status rides the base, so it survives even when the concrete type (AccessException...) can't cross.
	Ξ ToException( Jde::Proto::Exception&& e, SRCE )ι->up<Jde::Exception>{
		auto y = e.category()==Jde::Proto::DB
			? up<Jde::Exception>{ mu<DB::DBException>( (DB::EDbError)e.category_code(), DB::Sql{}, e.what(), ExceptionArgs{e.code()}, sl ) }
			: mu<Jde::Exception>( e.what(), ExceptionArgs{e.code()}, sl );
		y->SetHttpStatus( (EHttpStatus)e.status_code() );//0 stays 0 - EHttpStatus() resolves unset to 500.
		return y;
	}
	α ToQuery( string&& text, jobject&& variables, bool returnRaw )ι->Jde::Proto::Query;
}
