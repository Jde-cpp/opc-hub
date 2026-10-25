#pragma once
#include <open62541/server.h>
#include <jde/access/Authorize.h>

namespace Jde::App::Client{ struct IAppClient; }
namespace Jde::Opc::Server{ struct INodeAccess; struct UAConfig; struct UASettings; }
namespace Jde::Opc::Server::UAAccess{
	//Halfway from now to an expiry:  when to re-ask the authority about a snapshot that is still good.  See renewAhead.
	Ξ HalfLife( TimePoint expiration )ι->TimePoint{ const auto now = Clock::now(); return expiration>now ? now+( expiration-now )/2 : expiration; }
	struct SessionContext final{
		SessionContext& operator=( const SessionContext& ) = delete;
		string Endpoint;
		TimePoint Expiration;
		SessionPK SessionId;
		UserPK UserPK;
		TimePoint LastRenewal{};//Throttles the renewal, so a session the authority says is genuinely gone costs one round trip per interval, not one per node access.
		bool Answered{};//the authority has since answered for this Expiration - so it is its verdict, not a stale snapshot, and gets no grace.
		TimePoint RenewAt{ SessionId ? HalfLife(Expiration) : TimePoint::max() };//when to ask ahead of the lapse;  never, with no session to ask about.
		bool Denied{};//the lapse has been logged - the callbacks ask twice a second per monitored item, the log says it once.
		//A context the token itself vouches for - an anonymous, username or certificate login:  no hub session, no expiry.
		Ω Unexpiring( Jde::UserPK user )ι->up<SessionContext>{ return mu<SessionContext>( string{}, TimePoint::max(), SessionPK{}, user ); }
	};

	//A session the server holds, ActivateSession to CloseSession, as a host lists them (the gateway's endpoint{ sessions }):  its UA
	//session id, the user its token resolved to, the token type by its policy's label, the channel's policy and mode, and when it
	//first activated - a re-activation keeps Since and takes the user, which a username login resolves late (ResolveUser).
	struct Session final{ string Id; Jde::UserPK User; sv Type; string PolicyUri; UA_MessageSecurityMode Mode; TimePoint Since; };
	//The sessions a server holds, kept by the plugin as they activate and close, under a lock of its own:  shared with the host,
	//which reads it off the server's thread, and left empty when the server goes.
	struct SessionList final{
		α Activated( Session session )ι->void;
		α Closed( str id )ι->void;
		α Clear()ι->void;
		α List()Ι->vector<Session>;
	private:
		mutable std::mutex _mutex;
		flat_map<string,Session> _sessions;
	};
	α Sessions( UA_Server& server )ι->sp<SessionList>;//the server's, from its plugin's context.

	//The plugin's state, at ac->context:  the token types and logins the host read, its appClient, the session authority, and
	//its nodeAccess, the node rights.  A callback passed no ac, as UAHistory's and the tests' are, reads the server's.
	α Init( UAConfig& config, const UASettings& settings, sp<App::Client::IAppClient> appClient, sp<INodeAccess> nodeAccess )ε->void;
	α ResolveUser( App::Client::IAppClient& appClient, sv loginName )ι->UserPK;//the hub's user for a username login the list matched, UserPK{} when there is none yet - see the definition.
	α UserName( UA_Server& server, UserPK user )ι->string;//the user's name, as the host's acl holds it:  what a history edit records.

	α ActivateSession( UA_Server *server, UA_AccessControl *ac, const UA_EndpointDescription *endpointDescription, const UA_ByteString *secureChannelRemoteCertificate, const UA_NodeId *sessionId, const UA_ExtensionObject *userIdentityToken, void **sessionContext )ι->UA_StatusCode;
	α CloseSession(UA_Server *server, UA_AccessControl *ac,const UA_NodeId *sessionId, void *sessionContext)ι->void;
	α GetUserRightsMask(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext)ι->UA_UInt32;
	α GetUserAccessLevel(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext)ι->UA_Byte;
	α GetUserExecutable(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *methodId, void *methodContext)ι->UA_Boolean;
	α GetUserExecutableOnObject(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *methodId, void *methodContext, const UA_NodeId *objectId, void *objectContext)ι->UA_Boolean;
	α AllowAddNode(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_AddNodesItem *item)ι->UA_Boolean;
	α AllowAddReference(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_AddReferencesItem *item)ι->UA_Boolean;
	α AllowDeleteNode(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteNodesItem *item)ι->UA_Boolean;
	α AllowDeleteReference(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_DeleteReferencesItem *item)ι->UA_Boolean;
	α AllowBrowseNode(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, void *nodeContext)ι->UA_Boolean;
	α AllowTransferSubscription(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *oldSessionId, void *oldSessionContext, const UA_NodeId *newSessionId, void *newSessionContext)ι->UA_Boolean;
	α AllowHistoryUpdateUpdateData(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_PerformUpdateType performInsertReplace, const UA_DataValue *value)ι->UA_Boolean;
	α AllowHistoryUpdateDeleteRawModified(UA_Server *server, UA_AccessControl *ac, const UA_NodeId *sessionId, void *sessionContext, const UA_NodeId *nodeId, UA_DateTime startTimestamp, UA_DateTime endTimestamp, bool isDeleteModified)ι->UA_Boolean;
};