#pragma once
#include <jde/web/server/IHttpRequestAwait.h>
#include <jde/opc/uatypes/Value.h>
#include "async/ReadAwait.h"

namespace Jde::Opc{ struct NodeId; }
namespace Jde::Opc::Gateway{
	struct UAClient;
	using namespace Jde::Web::Server;
	//Not final: a host routing both apps from one listener (OpcHub) derives from it for the OPC routes and await_resume's
	//UAClientException arm, and adds the AppServer's beside them.
	struct HttpRequestAwait : IHttpRequestAwait{
		using base = IHttpRequestAwait;
		HttpRequestAwait( HttpRequest&& req, SRCE )ι;
		α await_ready()ι->bool override;
		α Suspend()ι->void override;
		α await_resume()ε->HttpTaskResult override;
	protected:
		α Login( str endpoint )ι->TAwait<optional<Web::FromServer::SessionInfo>>::Task;//POST /login {opc,user,password} - the OPC server's user/password.
		α Logout()ι->TAwait<jvalue>::Task;
		α CoHandleRequest( ServerCnnctnNK&& opcId )ι->void;
	private:
		α ParseNodes()ε->tuple<flat_set<NodeId>,jarray>;
		α ResumeSnapshots( flat_map<NodeId, Value>&& results, jarray&& j )ι->void;
		α SnapshotWrite( flat_set<NodeId>&& nodes, flat_map<NodeId, Value> values, jarray jNodes )ι->TAwait<ReadResponse>::Task;
		α SnapshotWrite( flat_map<NodeId, Value>&& values )ι->TAwait<flat_map<NodeId,UA_WriteResponse>>::Task;
		α SnapshotRead( bool write={} )ι->TAwait<flat_map<NodeId, Value>>::Task;
		sp<UAClient> _client;
	};
}