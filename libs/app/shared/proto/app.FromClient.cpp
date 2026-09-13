#include <jde/app/proto/app.FromClient.h>
#include <boost/uuid/uuid_io.hpp>
#include <jde/fwk/chrono.h>
#include <jde/fwk/io/protobuf.h>
#include <jde/db/DBException.h>
#include <jde/fwk/settings.h>
#include <jde/web/Jwt.h>
#include <jde/app/proto/LogProto.h>
#include "Log.pb.h"

using Jde::Protobuf::ToBytes;
namespace Jde::App::FromClient{
	Ω setMessage( RequestId requestId, function<void(PFromClient::Message&)> f )ι->PFromClient::Transmission{
		PFromClient::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		f( m );
		return t;
	}
	Ω transString( RequestId requestId, function<void(PFromClient::Message&)> f )ι->string{
		auto t = setMessage( requestId, f );
		return Protobuf::ToString( t );
	}
}
namespace Jde::App{
	α FromClient::AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, RequestId requestId )ι->StringTrans{
		return transString( requestId, [&](auto& m){
			auto& s = *m.mutable_add_session();
			s.set_domain( domain );
			s.set_login_name( loginName );
			s.set_provider_pk( providerPK );
			s.set_user_endpoint( userEndPoint );
			s.set_is_socket( isSocket );
		} );
	}

	α FromClient::Exception( runtime_error&& e, RequestId requestId )ι->PFromClient::Transmission{
		return setMessage( requestId, [&](auto& m){
			auto& request = *m.mutable_exception();
			request.set_what( e.what() );
			if( auto p = dynamic_cast<Jde::Exception*>(&e); p ){
				request.set_code( (uint32)p->Code() );
				request.set_status_code( p->HttpStatus() );//status & classification ride the base virtuals - the type can't cross the wire.
				request.set_category( (Jde::Proto::ECategory)p->Category() );
				request.set_category_code( p->CategoryCode() );
			}
			else
				request.set_status_code( 500 );//plain std::exception - unclassified server fault.
		} );
	}
	α FromClient::Exception( string&& e, RequestId requestId )ι->PFromClient::Transmission{
		return setMessage( requestId, [&](auto& m){
			auto& request = *m.mutable_exception();
			request.set_what( move(e) );
		} );
	}

	α FromClient::Instance( str application, str instanceName, SessionPK sessionId, RequestId requestId, str authResource )ι->PFromClient::Transmission{
		PFromClient::Transmission t;
		auto& m = *t.add_messages();
		m.set_request_id( requestId );
		auto& i = *m.mutable_instance();
		i.set_application( application );
		i.set_instance_name( instanceName );
		i.set_session_id( sessionId );
		i.set_host( Settings::FindString("/http/host").value_or(Process::HostName()) );//advertised web host - browsers reach the web endpoint by this name, and sameHost CORS requires it to match the page's host.  A loopback name is rewritten by the page to its own host (web: resolveInstanceHost), so the configs' "localhost" serves every browser that reaches the page; the machine name is kept as sent.
		i.set_pid( Process::ProcessId() );
		*i.mutable_start_time() = Jde::Protobuf::ToTimestamp( Process::StartTime() );
		i.set_web_port( Settings::FindNumber<PortType>("/http/port").value_or(0) );
		i.set_auth_resource( authResource );//M10: field 10, never set until now, so the AppServer's delegated-admin registration never ran.

		return t;
	}
	α FromClient::Query( string query, jobject variables, RequestId requestId, bool returnRaw )ι->string{
		return transString( requestId, [&](auto& m){
			auto& request = *m.mutable_query();
			request.set_text( query );
			request.set_return_raw( returnRaw );
			*request.mutable_variables() = serialize( move(variables) );
		} );
	}

	α FromClient::QueryResult( string&& result, RequestId requestId )ι->PFromClient::Transmission{
		return setMessage( requestId, [&](auto& m){
			*m.mutable_query_result() = move( result );
		} );
	}

	α FromClient::Jwt( RequestId requestId )ι->StringTrans{
		return transString( requestId, [&](auto& m){m.set_request_type(Proto::FromClient::ERequestType::Jwt);} );
	}

	α FromClient::Login( Web::Jwt&& jwt, RequestId requestId )ι->StringTrans{
		return transString( requestId, [&](auto& m){
			*m.mutable_jwt() = move(jwt.Payload());
		} );
	}
/*
	α FromClient::ToStatus( flat_map<string,string>&& values )ι->PFromClient::Status{
		PFromClient::Status y;
		*y.mutable_start_time() = Jde::Protobuf::ToTimestamp( Process::StartTime() );
		y.set_memory( Process::MemorySize() );
		for_each( values, [&y](auto&& value){(*y.mutable_values())[move(value.first)] = move(value.second); } );
		return y;
	}

	α FromClient::Status( flat_map<string,string>&& values )ι->PFromClient::Transmission{
		PFromClient::Transmission t;
		auto& status = *t.add_messages()->mutable_status() = ToStatus( move(values) );
		*status.mutable_start_time() = Jde::Protobuf::ToTimestamp( Process::StartTime() );
		status.set_memory( Process::MemorySize() );
		//for_each( values, [&status](auto&& value){(*status.mutable_values())[move(value.first)] = move(value.second); } );
		return t;
	}
*/
	α FromClient::Session( SessionPK sessionId, RequestId requestId )ι->StringTrans{
		return transString( requestId, [&](auto& m){
			m.set_session_info( sessionId );
		});
	}

	α FromClient::Subscription( string&& query, jobject variables, RequestId requestId )ι->string{
		return transString( requestId, [&](auto& m){
			auto& sub = *m.mutable_subscription();
			sub.set_text( move(query) );
			*sub.mutable_variables() = serialize( move(variables) );
		});
	}
	α FromClient::Unsubscription( const vector<QL::SubscriptionId>& ids, RequestId requestId )ι->PFromClient::Transmission{
		return setMessage( requestId, [&](auto& m){
			auto& u = *m.mutable_unsubscription();
			for( const auto id : ids )
				u.add_request_ids( id );
		});
	}
	α FromClient::LogEntries( vector<Logging::Entry>&& entries )ι->PFromClient::Transmission{
		PFromClient::Transmission t;
		for( auto&& entry : entries ){
			*t.add_messages()->mutable_log_entry() = LogProto::LogEntryClient( move(entry) );
		}
		return t;
	}
}