#pragma once
#include <jde/app/usings.h>
#include <jde/access/usings.h>
#include <jde/ql/usings.h>
#include "App.FromClient.pb.h"

namespace Jde::Web{ struct Jwt; }
namespace Jde::App::FromClient{
	namespace PFromClient = Jde::App::Proto::FromClient;
	α AddSession( str domain, str loginName, Access::ProviderPK providerPK, str userEndPoint, bool isSocket, RequestId requestId )ι->PFromClient::Transmission;
	α Exception( runtime_error&& e, RequestId requestId=0 )ι->PFromClient::Transmission;
	α Exception( string&& e, RequestId requestId )ι->PFromClient::Transmission;
	α Jwt( RequestId requestId )ι->PFromClient::Transmission;
	α Login( Web::Jwt&& jwt, RequestId requestId )ι->PFromClient::Transmission;
	α Query( string query, jobject variables, RequestId requestId, bool returnRaw=true )ι->PFromClient::Transmission;
	α QueryResult( string&& result, RequestId requestId )ι->PFromClient::Transmission;
	//M10: authResource is the schema this instance asks to be the admin authorizer for - the AppServer's kInstance arm registers it
	//against IAdminAcl, and that block was dead because nothing ever set the field.  Empty for an app that authorizes nothing.
	α Instance( str application, str instanceName, SessionPK sessionId, RequestId requestId, str authResource )ι->PFromClient::Transmission;
	α Session( SessionPK sessionId, RequestId requestId )ι->PFromClient::Transmission;
	α Subscription( string&& query, jobject variables, RequestId requestId )ι->PFromClient::Transmission;
	α Unsubscription( const vector<QL::SubscriptionId>& ids, RequestId requestId )ι->PFromClient::Transmission;
	α LogEntries( vector<Logging::Entry>&& entries )ι->PFromClient::Transmission;
}