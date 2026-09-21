#pragma once
#include <jde/access/Authorize.h>

namespace Jde::Opc::Server{ struct UAConfig; }
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
	};

	α Init( UAConfig& config )ε->void;
	α ResolveUser( sv loginName )ι->UserPK;//the hub's user for a username login the list matched, UserPK{} when there is none yet - see the definition.

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

	enum class EOpcAccessLevel : uint{
		Read = UA_ACCESSLEVELMASK_READ,
		Write = UA_ACCESSLEVELMASK_WRITE,
		HistoryRead = UA_ACCESSLEVELMASK_HISTORYREAD,
		HistoryWrite = UA_ACCESSLEVELMASK_HISTORYWRITE,
		SemanticChange = UA_ACCESSLEVELMASK_SEMANTICCHANGE,
		StatusWrite = UA_ACCESSLEVELMASK_STATUSWRITE,
		TimestampWrite = UA_ACCESSLEVELMASK_TIMESTAMPWRITE,
		AllAccess = Read | Write | HistoryRead | HistoryWrite | StatusWrite | TimestampWrite | SemanticChange,
		AccessLevel = uint{UA_WRITEMASK_ACCESSLEVEL} << 32,
		ArrayDimensions = uint{UA_WRITEMASK_ARRRAYDIMENSIONS} << 32,
		BrowseName = uint{UA_WRITEMASK_BROWSENAME} << 32,
		ContainsNoLoops = uint{UA_WRITEMASK_CONTAINSNOLOOPS} << 32,
		DataType = uint{UA_WRITEMASK_DATATYPE} << 32,
		Description = uint{UA_WRITEMASK_DESCRIPTION} << 32,
		DisplayName = uint{UA_WRITEMASK_DISPLAYNAME} << 32,
		EventNotifier = uint{UA_WRITEMASK_EVENTNOTIFIER} << 32,
		Executable = uint{UA_WRITEMASK_EXECUTABLE} << 32,
		Historizing = uint{UA_WRITEMASK_HISTORIZING} << 32,
		InverseName = uint{UA_WRITEMASK_INVERSENAME} << 32,
		IsAbstract = uint{UA_WRITEMASK_ISABSTRACT} << 32,
		MinimumSamplingInterval = uint{UA_WRITEMASK_MINIMUMSAMPLINGINTERVAL} << 32,
		NodeClass = uint{UA_WRITEMASK_NODECLASS} << 32,
		NodeId = uint{UA_WRITEMASK_NODEID} << 32,
		Symmetric = uint{UA_WRITEMASK_SYMMETRIC} << 32,
		UserAccessLevel = uint{UA_WRITEMASK_USERACCESSLEVEL} << 32,
		UserExecutable = uint{UA_WRITEMASK_USEREXECUTABLE} << 32,
		UserWriteMask = uint{UA_WRITEMASK_USERWRITEMASK} << 32,
		ValueRank = uint{UA_WRITEMASK_VALUERANK} << 32,
		WriteMask = uint{UA_WRITEMASK_WRITEMASK} << 32,
		ValueForVariableType = uint{UA_WRITEMASK_VALUEFORVARIABLETYPE} << 32,
		DataTypeDefinition = uint{UA_WRITEMASK_DATATYPEDEFINITION} << 32,
		RolePermissions = uint{UA_WRITEMASK_ROLEPERMISSIONS} << 32,
		AccessRestrictions = uint{UA_WRITEMASK_ACCESSRESTRICTIONS} << 32,
		AccessLevelEx = uint{UA_WRITEMASK_ACCESSLEVELEX} << 32,
		AllWrite = AccessLevel | ArrayDimensions | BrowseName | ContainsNoLoops | DataType | Description | DisplayName | EventNotifier | Executable | Historizing | InverseName | IsAbstract | MinimumSamplingInterval | NodeClass | NodeId | Symmetric | UserAccessLevel | UserExecutable | UserWriteMask | ValueRank | WriteMask | ValueForVariableType | DataTypeDefinition | RolePermissions | AccessRestrictions | AccessLevelEx,
		All = AllAccess | AllWrite
	};
};