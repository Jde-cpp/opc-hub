//Historian 2A (#209):  OpcServer collects the variables its nodesets mark Historizing through open62541's setValue and
//serves them to a UA client's HistoryRead - here open62541's synchronous client helpers, over a session of this
//program's own identity.  2B (#210):  it edits them through HistoryUpdate, which needs a grant on an enforced resource,
//and serves the modified values.  2C (#211):  it serves them at requested times and aggregated, Part 13's nine and
//Median, listed in HistoryServerCapabilities' AggregateFunctions folder.
#include <fstream>
#include <numeric>
#include <thread>
#include <absl/cleanup/cleanup.h>
#include <absl/synchronization/notification.h>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/certificategroup_default.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/co/AnyAwait.h>
#include <jde/fwk/crypto/CryptoSettings.h>
#include <jde/fwk/crypto/OpenSsl.h>
#include <jde/fwk/process/execution.h>
#include <jde/db/meta/AppSchema.h>//GetSchema().Authorizer
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/BrowsePath.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/opc/uatypes/Value.h>
#include "../src/access/OpcAuthorize.h"
#include "../src/access/UAAccess.h"
#define let const auto

namespace Jde::Opc::Server::Tests{
	using namespace std::chrono;
	using Pages = vector<vector<Value>>;
	namespace{
		Ω nodeset()ε->fs::path{
			for( let& p : Settings::FindPathArray("/opcServer/configFiles") ){
				if( p.filename()=="pumps.NodeSet2.xml" )
					return p;
			}
			THROW( "pumps.NodeSet2.xml is not in /opcServer/configFiles." );
		}
		Ω doubles( const vector<Value>& values )ι->vector<double>{
			vector<double> y;
			for( let& v : values )
				y.push_back( v.hasValue && UA_Variant_hasScalarType(&v.value, &UA_TYPES[UA_TYPES_DOUBLE]) ? v.Get<UA_Double>(0) : std::numeric_limits<double>::quiet_NaN() );
			return y;
		}
		//Each value's number, whatever numeric type it carries:  an aggregate's Double, or a Count's Int32.
		Ω numbers( const vector<Value>& values )ι->vector<double>{
			vector<double> y;
			for( let& v : values )
				y.push_back( v.hasValue && !v.IsEmpty() && v.IsScalar() && UA_DataType_isNumeric(v.value.type) ? Value{v}.AsNumber<double>() : std::numeric_limits<double>::quiet_NaN() );
			return y;
		}
		Ω sources( const vector<Value>& values )ι->vector<UA_DateTime>{
			vector<UA_DateTime> y;
			for( let& v : values )
				y.push_back( v.sourceTimestamp );
			return y;
		}
		Ω statuses( const vector<Value>& values )ι->vector<UA_StatusCode>{
			vector<UA_StatusCode> y;
			for( let& v : values )
				y.push_back( v.status );
			return y;
		}
		Ω ticks( TimePoint t )ι->UA_DateTime{ return UADateTime{ t }.UA(); }
		//Part 4's info bits on a history value's status:  the InfoType, and what Part 13 sets with it.
		constexpr UA_StatusCode Calculated{ 0x401 }, Interpolated{ 0x402 };
		Ω all( const Pages& pages )ι->vector<Value>{
			vector<Value> y;
			for( let& page : pages ){
				for( let& value : page )
					y.emplace_back( value );
			}
			return y;
		}
		struct Reading final{ UA_StatusCode Status; Pages Values; };
		struct Request final{ optional<TimePoint> Start; optional<TimePoint> End; bool Bounds{}; UA_UInt32 Limit{}; UA_TimestampsToReturn Timestamps{ UA_TIMESTAMPSTORETURN_BOTH }; uint MaxPages{ std::numeric_limits<uint>::max() }; };
		//A modified read's, every page's values and ModificationInfos together.
		struct Info final{ UA_DateTime Time; UA_HistoryUpdateType Type; string User; };
		struct Modified final{ UA_StatusCode Status; vector<Value> Values; vector<Info> Infos; };
		Ω types( const vector<Info>& infos )ι->vector<UA_HistoryUpdateType>{
			vector<UA_HistoryUpdateType> y;
			for( let& i : infos )
				y.push_back( i.Type );
			return y;
		}
	}

	struct HistoryTests : ::testing::Test{
	protected:
		//As opcServerStartup does:  the nodesets, the history they mark, the rights, and the listener.
		Ω Start( const fs::path& pumps )ε->void{
			Server::Initialize( GetSchemaPtr() );
			auto& ua = GetUAServer();
			ua.Load( pumps );
			AddArray( ua );
			AddReadOnly( ua );
			AddType( ua );
			ua.History().Load( ua );
			static_cast<OpcAuthorize&>( *GetSchema().Authorizer ).AssignRights( ua );
			ua.Run();
			_ns = NamespaceIndex( ua, "urn:jde:pumps" );
		}
		//A historizing array, which pumps' nodeset has none of, added as a nodeset adds a variable:  [1,2,3,4].
		Ω Array()ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Array") }; }
		Ω AddArray( UAServer& ua )ε->void{
			UA_VariableAttributes attributes = UA_VariableAttributes_default;
			UA_Double values[]{ 1, 2, 3, 4 };
			UA_Variant_setArray( &attributes.value, values, std::size(values), &UA_TYPES[UA_TYPES_DOUBLE] );
			attributes.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
			attributes.valueRank = UA_VALUERANK_ONE_DIMENSION;
			UA_UInt32 anyLength{};
			attributes.arrayDimensionsSize = 1;
			attributes.arrayDimensions = &anyLength;
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
			attributes.historizing = true;
			UAε( UA_Server_addVariableNode(ua.Ptr(), Array(), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(1, (char*)"HistoryTests.Array"),
				UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		//A type with a member marked Historizing, as companion nodesets mark them, and another inside the type's object
		//`part`, both Mandatory, and an instance of the type, which open62541 gives copies of both, mark and modelling rule
		//included.
		//A historizing Double whose AccessLevel is CurrentRead and HistoryRead, 5, as companion nodesets mark theirs:  history
		//read-only.
		Ω ReadOnly()ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.ReadOnly") }; }
		Ω AddReadOnly( UAServer& ua )ε->void{
			UA_VariableAttributes attributes = UA_VariableAttributes_default;
			UA_Double value{};
			UA_Variant_setScalar( &attributes.value, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			attributes.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
			attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_HISTORYREAD;
			attributes.historizing = true;
			UAε( UA_Server_addVariableNode(ua.Ptr(), ReadOnly(), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(1, (char*)"HistoryTests.ReadOnly"),
				UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
		}
		Ω TypeMember()ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Type.member") }; }
		Ω PartMember()ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Type.part.member") }; }
		Ω Instance()ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Instance") }; }
		Ω AddType( UAServer& ua )ε->void{
			let server = ua.Ptr();
			const NodeId type{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Type") }, part{ UA_NODEID_STRING_ALLOC(1, "HistoryTests.Type.part") };
			let component = UA_NODEID_NUMERIC( 0, UA_NS0ID_HASCOMPONENT );
			let mandatory = [server]( const UA_NodeId& node ){
				UAε( UA_Server_addReference(server, node, UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE), UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY), true) );
			};
			UAε( UA_Server_addObjectTypeNode(server, type, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), UA_QUALIFIEDNAME(1, (char*)"HistoryTests.Type"),
				UA_ObjectTypeAttributes_default, nullptr, nullptr) );
			UAε( UA_Server_addObjectNode(server, part, type, component, UA_QUALIFIEDNAME(1, (char*)"part"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), UA_ObjectAttributes_default, nullptr, nullptr) );
			mandatory( part );
			UA_VariableAttributes attributes = UA_VariableAttributes_default;
			attributes.dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
			attributes.historizing = true;
			for( let& [member, parent] : {std::pair{TypeMember(), type}, std::pair{PartMember(), part}} ){
				UAε( UA_Server_addVariableNode(server, member, parent, component, UA_QUALIFIEDNAME(1, (char*)"member"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr) );
				mandatory( member );
			}
			UAε( UA_Server_addObjectNode(server, Instance(), NodeId::ObjectsFolder(), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES), UA_QUALIFIEDNAME(1, (char*)"HistoryTests.Instance"), type,
				UA_ObjectAttributes_default, nullptr, nullptr) );
		}
		//An edit needs Update, or Delete, granted on a resource that is enforced (spec *Authorization*):  the root nodeIds
		//resource, enforced, with this program's user granted everything on it through a role of its own, find-or-create
		//as AccessTests' are, since the suites share one db in a run.  The grant is made while the resource is unenforced,
		//which is when the delegated admin check passes a user that administers nothing yet.
		Ω Enforce()ε->void{
			auto app = AppClient();
			let schema = "opc."+Settings::FindString( "/opcServer/resource" ).value_or( "test" );
			let nodeSlug = jobject{ {"slug","nodeIds"} };
			//Each change reaches the authorizer as an event:  the delete's is waited for before the restore, or the wait for
			//the restore could end on the state before the delete, and the delete's event reopen the nodes after it.
			auto& authorizer = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
			let reached = [&]( bool active, sv change ){
				for( uint i=0; authorizer.FindActiveResourcePK(schema, "nodeIds", "").has_value()!=active; ++i ){
					THROW_IF( i==200, "The nodeIds resource's {} didn't reach the authorizer.", change );
					std::this_thread::sleep_for( 50ms );
				}
			};
			app->QuerySync<jvalue>( "deleteResource( slug:$slug, criteria:null )", nodeSlug );
			reached( false, "delete" );
			constexpr sv roleSlug{ "HistoryEditor" };
			if( app->QuerySync("role(slug:$slug){id}", {{"slug", roleSlug}}).empty() ){
				let role = app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug", roleSlug}, {"name", "History editor"}} );
				let roleId = Json::AsNumber<Access::RolePK::Type>( role.at("id") );
				app->QuerySync<jvalue>( "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:0, resource:{schemaName:$schema, slug:\"nodeIds\"}} )", {{"roleId", roleId}, {"allowed", underlying(Access::ERights::All)}, {"schema", schema}} );
				app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", app->UserPK().Value}, {"roleId", roleId}} );
			}
			app->QuerySync<jvalue>( "restoreResource( slug:$slug, criteria:null )", nodeSlug );
			reached( true, "restore" );//then map the nodes under the resource.
			authorizer.AssignRights( GetUAServer() );
			THROW_IF( empty(authorizer.EditRights(Node(Rpm1), app->UserPK()) & Access::ERights::Update), "This program's user can't edit history." );
		}
		Ω SetUpTestCase()ε->void{
			Server::Initialize( GetSchemaPtr() );//lets go of the path, and takes it again:  what an earlier run left goes, but for the lock.
			let path = Settings::FindPath( "/opcServer/hist/path" );
			THROW_IF( !path, "The tests' config has no /opcServer/hist/path." );
			for( let& entry : fs::directory_iterator{*path} ){
				if( entry.path().filename()!="historian.lock" )
					fs::remove_all( entry.path() );
			}
			_base = floor<seconds>( Clock::now() )-1min;
			Start( nodeset() );
			Enforce();
			Connect();
		}
		Ω TearDownTestCase()ι->void{ Disconnect(); }

		//A session of this program's own user:  its session with the AppServer as an issued token, which the server takes
		//under an encrypting token policy, so the client carries the policies, with the server's own certificate for one.
		Ω Connect()ε->void{
			UA_ClientConfig config{};
			const Crypto::CryptoSettings ssl{ *Settings::FindObject("/opcServer/ssl") };
			auto certificate = ToUAByteString( Crypto::ReadCertificate(ssl.Certificate.Path) );
			auto pem = ToUAByteString( Crypto::ReadPrivateKey(ssl.PrivateKey) );
			UA_ByteString key = UA_BYTESTRING_NULL;
			UAε( UA_CertificateUtils_decryptPrivateKey(*pem, UA_BYTESTRING_NULL, &key) );
			let configured = UA_ClientConfig_setDefaultEncryption( &config, *certificate, key, nullptr, 0, nullptr, 0 );
			UA_ByteString_clear( &key );
			UAε( configured );
			config.certificateVerification.clear( &config.certificateVerification );
			UA_CertificateGroup_AcceptAll( &config.certificateVerification );
			config.securityMode = UA_MESSAGESECURITYMODE_NONE;
			UA_String_clear( &config.securityPolicyUri );
			UA_String_copy( &UA_SECURITY_POLICY_NONE_URI, &config.securityPolicyUri );
			UA_String_clear( &config.clientDescription.applicationUri );
			config.clientDescription.applicationUri = UA_STRING_ALLOC( ssl.Certificate.SanUri().c_str() );
			auto token = UA_IssuedIdentityToken_new();
			token->tokenData = UA_BYTESTRING_ALLOC( Ƒ("{:x}", AppClient()->SessionId()).c_str() );
			UA_ExtensionObject_clear( &config.userIdentityToken );
			UA_ExtensionObject_setValue( &config.userIdentityToken, token, &UA_TYPES[UA_TYPES_ISSUEDIDENTITYTOKEN] );
			_client = UA_Client_newWithConfig( &config );
			THROW_IF( !_client, "UA_Client_newWithConfig failed." );
			UAε( UA_Client_connect(_client, Ƒ("opc.tcp://127.0.0.1:{}", Settings::FindNumber<PortType>("/opcServer/port").value_or(4840)).c_str()) );
		}
		Ω Disconnect()ι->void{
			if( auto client = std::exchange(_client, nullptr) ){
				UA_Client_disconnect( client );
				UA_Client_delete( client );
			}
		}

		Ω Node( UA_UInt32 id )ι->NodeId{ return NodeId{ _ns, id }; }
		Ω History()ι->UAHistory&{ return GetUAServer().History(); }
		Ω Index( UA_UInt32 id )ι->optional<Hist::NodeIndex>{ return History().Find( Node(id) ); }
		Ω Thresholds( UA_UInt32 id )ε->Hist::Thresholds{
			let index = Index( id );
			THROW_IF( !index, "{} is not historized.", id );
			return *History().Group()->FindThresholds( *index );
		}
		//A write as a client's or the PubSub reader's reaches the server, with the time the source gave it.
		Ω Write( UA_UInt32 id, const void* scalar, uint type, TimePoint source )ε->void{
			let node = Node( id );
			UA_WriteValue write; UA_WriteValue_init( &write );
			write.nodeId = node;
			write.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_Variant_setScalar( &write.value.value, const_cast<void*>(scalar), &UA_TYPES[type] );
			write.value.hasValue = true;
			write.value.sourceTimestamp = UADateTime{ source }.UA();
			write.value.hasSourceTimestamp = true;
			UAε( UA_Server_write(GetUAServer().Ptr(), &write) );
		}
		Ω Write( UA_UInt32 id, double value, TimePoint source )ε->void{ Write( id, &value, UA_TYPES_DOUBLE, source ); }
		Ω WriteBoolean( UA_UInt32 id, bool value, TimePoint source )ε->void{ const UA_Boolean b{ value }; Write( id, &b, UA_TYPES_BOOLEAN, source ); }
		//UA_Client_HistoryRead_raw, each page it fetched:  it follows the continuation points until none is left, or
		//MaxPages are in, which releases the one it holds.
		Ω Read( const NodeId& node, Request request )ι->Reading{
			struct Context final{ Pages Values; uint MaxPages; };
			Context context{ {}, request.MaxPages };
			let onPage = []( UA_Client*, const UA_NodeId*, UA_Boolean /*more*/, const UA_ExtensionObject* data, void* context )->UA_Boolean {
				auto& y = *static_cast<Context*>( context );
				auto& page = y.Values.emplace_back();
				if( data->encoding==UA_EXTENSIONOBJECT_DECODED && data->content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
					let& history = *static_cast<const UA_HistoryData*>( data->content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						page.emplace_back( history.dataValues[i] );
				}
				return y.Values.size()<y.MaxPages;
			};
			let ticks = []( optional<TimePoint> t ){ return t ? UADateTime{ *t }.UA() : UA_DateTime{}; };
			let sc = UA_Client_HistoryRead_raw( _client, &node, onPage, ticks(request.Start), ticks(request.End), UA_STRING_NULL, request.Bounds, request.Limit, request.Timestamps, &context );
			return { sc, move(context.Values) };
		}
		Ω Read( UA_UInt32 id, Request request )ι->Reading{ return Read( Node(id), move(request) ); }
		//UA_Client_HistoryRead_modified, every page's values and ModificationInfos.
		Ω ReadModified( UA_UInt32 id, Request request )ι->Modified{
			let node = Node( id );
			Modified y{};
			let onPage = []( UA_Client*, const UA_NodeId*, UA_Boolean, const UA_ExtensionObject* data, void* context )->UA_Boolean {
				auto& y = *static_cast<Modified*>( context );
				if( data->encoding==UA_EXTENSIONOBJECT_DECODED && data->content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYMODIFIEDDATA] ){
					let& history = *static_cast<const UA_HistoryModifiedData*>( data->content.decoded.data );
					for( uint i=0; i<history.dataValuesSize; ++i )
						y.Values.emplace_back( history.dataValues[i] );
					for( uint i=0; i<history.modificationInfosSize; ++i )
						y.Infos.push_back( {history.modificationInfos[i].modificationTime, history.modificationInfos[i].updateType, ToString(history.modificationInfos[i].userName)} );
				}
				return true;
			};
			let ticks = []( optional<TimePoint> t ){ return t ? UADateTime{ *t }.UA() : UA_DateTime{}; };
			y.Status = UA_Client_HistoryRead_modified( _client, &node, onPage, ticks(request.Start), ticks(request.End), UA_STRING_NULL, request.Bounds, request.Limit, request.Timestamps, &y );
			return y;
		}
		//A value as a HistoryUpdate carries it, keyed by its SourceTimestamp.
		Ω Sample( double value, TimePoint source )ι->Value{
			UA_Variant v; UA_Variant_init( &v );
			UA_Variant_setScalarCopy( &v, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			Value y{ move(v) };
			y.sourceTimestamp = UADateTime{ source }.UA();
			y.hasSourceTimestamp = true;
			return y;
		}
		//open62541's client helpers:  the one value's result, or the entry's status when that isn't Good - and Bad_UnexpectedError
		//for an entry refused whole, which carries no value results, so those go through HistoryUpdate below.
		Ω Insert( UA_UInt32 id, double value, TimePoint source )ι->UA_StatusCode{ auto v = Sample( value, source ); let node = Node( id ); return UA_Client_HistoryUpdate_insert( _client, &node, &v ); }
		Ω Replace( UA_UInt32 id, double value, TimePoint source )ι->UA_StatusCode{ auto v = Sample( value, source ); let node = Node( id ); return UA_Client_HistoryUpdate_replace( _client, &node, &v ); }
		Ω Update( UA_UInt32 id, double value, TimePoint source )ι->UA_StatusCode{ auto v = Sample( value, source ); let node = Node( id ); return UA_Client_HistoryUpdate_update( _client, &node, &v ); }
		Ω DeleteRaw( UA_UInt32 id, TimePoint start, TimePoint end )ι->UA_StatusCode{ let node = Node( id ); return UA_Client_HistoryUpdate_deleteRaw( _client, &node, UADateTime{start}.UA(), UADateTime{end}.UA() ); }
		//A HistoryUpdate of one entry the helpers don't send:  its result's status, and each value's.
		struct Updated final{ UA_StatusCode Status; vector<UA_StatusCode> Results; };
		Ω HistoryUpdate( void* details, uint type )ι->Updated{
			UA_HistoryUpdateRequest request; UA_HistoryUpdateRequest_init( &request );
			UA_ExtensionObject entry; UA_ExtensionObject_init( &entry );
			entry.encoding = UA_EXTENSIONOBJECT_DECODED;
			entry.content.decoded.type = &UA_TYPES[type];
			entry.content.decoded.data = details;
			request.historyUpdateDetailsSize = 1;
			request.historyUpdateDetails = &entry;
			auto response = UA_Client_Service_historyUpdate( _client, request );
			Updated y{ response.responseHeader.serviceResult, {} };
			if( !y.Status && response.resultsSize==1 ){
				y.Status = response.results[0].statusCode;
				y.Results.assign( response.results[0].operationResults, response.results[0].operationResults+response.results[0].operationResultsSize );
			}
			UA_HistoryUpdateResponse_clear( &response );
			return y;
		}
		//One value's UpdateData as the service answers it:  the entry's status, and the value's when the entry is Good.
		Ω InsertEntry( UA_UInt32 id, Value& value )ι->Updated{
			let node = Node( id );
			UA_UpdateDataDetails details; UA_UpdateDataDetails_init( &details );
			details.nodeId = node;
			details.performInsertReplace = UA_PERFORMUPDATETYPE_INSERT;
			details.updateValuesSize = 1;
			details.updateValues = &value;
			return HistoryUpdate( &details, UA_TYPES_UPDATEDATADETAILS );
		}
		//The backend's edit callbacks themselves, for a session of user's:  an UpdateData's status and its value's result, as
		//HistoryUpdate has them, since a value the grant refuses leaves the status Good, and a delete's status.
		Ω UpdateCallback( UA_UInt32 id, UAAccess::SessionContext* session, double value, TimePoint source )ι->Updated{
			let node = Node( id );
			auto v = Sample( value, source );
			UA_UpdateDataDetails details; UA_UpdateDataDetails_init( &details );
			details.nodeId = node;
			details.performInsertReplace = UA_PERFORMUPDATETYPE_INSERT;
			details.updateValuesSize = 1;
			details.updateValues = &v;
			UA_HistoryUpdateResult result; UA_HistoryUpdateResult_init( &result );
			History().UpdateData( GetUAServer(), nullptr, session, details, result );
			Updated y{ result.statusCode, {result.operationResults, result.operationResults+result.operationResultsSize} };
			UA_HistoryUpdateResult_clear( &result );
			return y;
		}
		Ω DeleteCallback( UA_UInt32 id, UAAccess::SessionContext* session, TimePoint start, TimePoint end )ι->UA_StatusCode{
			let node = Node( id );
			UA_DeleteRawModifiedDetails details; UA_DeleteRawModifiedDetails_init( &details );
			details.nodeId = node;
			details.startTime = UADateTime{ start }.UA();
			details.endTime = UADateTime{ end }.UA();
			UA_HistoryUpdateResult result; UA_HistoryUpdateResult_init( &result );
			History().DeleteRawModified( GetUAServer(), nullptr, session, details, result );
			let status = result.statusCode;
			UA_HistoryUpdateResult_clear( &result );
			return status;
		}
		//The backend's callbacks themselves, for a session of user's:  the result's status of a raw read, and of an at-time read.
		Ω Callback( UA_UInt32 id, absl::FunctionRef<void( UAHistory&, const UA_HistoryReadValueId&, UA_HistoryReadResult&, UA_HistoryData* const* )> read )ι->UA_StatusCode{
			let node = Node( id );
			UA_HistoryReadValueId value; UA_HistoryReadValueId_init( &value );
			value.nodeId = node;
			UA_HistoryReadResult result; UA_HistoryReadResult_init( &result );
			UA_HistoryData data; UA_HistoryData_init( &data );
			UA_HistoryData* p{ &data };
			read( History(), value, result, &p );
			let status = result.statusCode;
			UA_HistoryData_clear( &data );
			UA_HistoryReadResult_clear( &result );
			return status;
		}
		Ω Callback( UA_UInt32 id, UAAccess::SessionContext* session, TimePoint start, TimePoint end )ι->UA_StatusCode{
			UA_ReadRawModifiedDetails details; UA_ReadRawModifiedDetails_init( &details );
			details.startTime = ticks( start );
			details.endTime = ticks( end );
			return Callback( id, [&]( UAHistory& history, const UA_HistoryReadValueId& value, UA_HistoryReadResult& result, UA_HistoryData* const* data ){
				history.ReadRaw( GetUAServer(), nullptr, session, details, UA_TIMESTAMPSTORETURN_BOTH, false, {&value, 1}, &result, data );
			});
		}
		Ω AtTimeCallback( UA_UInt32 id, UAAccess::SessionContext* session, TimePoint time )ι->UA_StatusCode{
			UA_DateTime t{ ticks(time) };
			UA_ReadAtTimeDetails details; UA_ReadAtTimeDetails_init( &details );
			details.reqTimesSize = 1;
			details.reqTimes = &t;
			return Callback( id, [&]( UAHistory& history, const UA_HistoryReadValueId& value, UA_HistoryReadResult& result, UA_HistoryData* const* data ){
				history.ReadAtTime( GetUAServer(), nullptr, session, details, UA_TIMESTAMPSTORETURN_BOTH, false, {&value, 1}, &result, data );
			});
		}
		//A HistoryRead the helpers don't send, at-time or processed:  each node's status and pages, the continuation
		//points followed, each node's own, until none is left.
		Ω HistoryRead( void* details, uint type, const vector<NodeId>& nodes, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ε->vector<Reading>{
			vector<Reading> y( nodes.size(), Reading{UA_STATUSCODE_GOOD, {}} );
			vector<uint> pending( nodes.size() );//each node still to read, by its position in nodes.
			std::iota( pending.begin(), pending.end(), 0u );
			vector<UA_HistoryReadValueId> ids( nodes.size() );
			for( uint i=0; i<nodes.size(); ++i ){
				UA_HistoryReadValueId_init( &ids[i] );
				ids[i].nodeId = nodes[i];
			}
			absl::Cleanup freed = [&]{ for( auto& id : ids ) UA_ByteString_clear( &id.continuationPoint ); };
			for( uint pages{}; pending.size(); ++pages ){
				THROW_IF( pages==100, "A read that never ends." );
				vector<UA_HistoryReadValueId> read;
				for( let i : pending )
					read.push_back( ids[i] );//shallow:  freed through ids.
				UA_HistoryReadRequest request; UA_HistoryReadRequest_init( &request );
				request.historyReadDetails.encoding = UA_EXTENSIONOBJECT_DECODED;
				request.historyReadDetails.content.decoded.type = &UA_TYPES[type];
				request.historyReadDetails.content.decoded.data = details;
				request.timestampsToReturn = timestamps;
				request.nodesToReadSize = read.size();
				request.nodesToRead = read.data();
				auto response = UA_Client_Service_historyRead( _client, request );
				absl::Cleanup cleared = [&]{ UA_HistoryReadResponse_clear( &response ); };
				if( response.responseHeader.serviceResult || response.resultsSize!=pending.size() ){
					for( let i : pending )
						y[i].Status = response.responseHeader.serviceResult ? response.responseHeader.serviceResult : UA_STATUSCODE_BADUNEXPECTEDERROR;
					break;
				}
				vector<uint> next;
				for( uint r=0; r<pending.size(); ++r ){
					let i = pending[r];
					let& result = response.results[r];
					y[i].Status = result.statusCode;
					auto& page = y[i].Values.emplace_back();
					let& data = result.historyData;
					if( data.encoding==UA_EXTENSIONOBJECT_DECODED && data.content.decoded.type==&UA_TYPES[UA_TYPES_HISTORYDATA] ){
						let& history = *static_cast<const UA_HistoryData*>( data.content.decoded.data );
						for( uint v=0; v<history.dataValuesSize; ++v )
							page.emplace_back( history.dataValues[v] );
					}
					UA_ByteString_clear( &ids[i].continuationPoint );
					if( result.continuationPoint.length ){
						UA_ByteString_copy( &result.continuationPoint, &ids[i].continuationPoint );
						next.push_back( i );
					}
				}
				pending = move( next );
			}
			return y;
		}
		Ω AtTime( const NodeId& node, std::initializer_list<TimePoint> times, bool simpleBounds=false, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ε->Reading{
			vector<UA_DateTime> at;
			for( let t : times )
				at.push_back( ticks(t) );
			UA_ReadAtTimeDetails details; UA_ReadAtTimeDetails_init( &details );
			details.reqTimesSize = at.size();
			details.reqTimes = at.data();
			details.useSimpleBounds = simpleBounds;
			return HistoryRead( &details, UA_TYPES_READATTIMEDETAILS, {node}, timestamps ).at( 0 );
		}
		//A processed read of nodes, each by the function in its place, under the node's AggregateConfiguration unless one is given.
		Ω Processed( const vector<NodeId>& nodes, const vector<NodeId>& functions, UA_DateTime start, UA_DateTime end, double intervalMs, optional<UA_AggregateConfiguration> configuration={}, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ε->vector<Reading>{
			vector<UA_NodeId> types;//shallow:  each function's own, as the request's array.
			for( let& function : functions )
				types.push_back( function );
			UA_ReadProcessedDetails details; UA_ReadProcessedDetails_init( &details );
			details.startTime = start;
			details.endTime = end;
			details.processingInterval = intervalMs;
			details.aggregateTypeSize = functions.size();
			details.aggregateType = types.data();
			details.aggregateConfiguration = configuration.value_or( UA_AggregateConfiguration{.useServerCapabilitiesDefaults=true} );
			return HistoryRead( &details, UA_TYPES_READPROCESSEDDETAILS, nodes, timestamps );
		}
		Ω Aggregate( UA_UInt32 id, const NodeId& function, TimePoint start, TimePoint end, Duration interval, optional<UA_AggregateConfiguration> configuration={}, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ε->Reading{
			return Processed( {Node(id)}, {function}, ticks(start), ticks(end), duration<double,std::milli>{interval}.count(), configuration, timestamps ).at( 0 );
		}
		Ω Aggregate( UA_UInt32 id, UA_UInt32 function, TimePoint start, TimePoint end, Duration interval, optional<UA_AggregateConfiguration> configuration={}, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_BOTH )ε->Reading{
			return Aggregate( id, NodeId{0, function}, start, end, interval, configuration, timestamps );
		}
		Ω Property( const NodeId& parent, std::initializer_list<sv> path, UA_UInt16 ns=0 )ε->NodeId{
			vector<UA_QualifiedName> names;
			for( let name : path )
				names.push_back( {ns, {name.size(), (UA_Byte*)name.data()}} );
			auto found = UA_Server_browseSimplifiedBrowsePath( GetUAServer().Ptr(), parent, names.size(), names.data() );
			let good = found.statusCode==UA_STATUSCODE_GOOD && found.targetsSize;
			NodeId y{ good ? found.targets[0].targetId.nodeId : UA_NODEID_NULL };
			UA_BrowsePathResult_clear( &found );
			THROW_IF( !good, "'{}' has no '{}'.", parent.ToString(), Str::Join(path, "/") );
			return y;
		}
		Ω Variant( const NodeId& node )ι->Value{
			UA_Variant v; UA_Variant_init( &v );
			if( let sc = UA_Server_readValue(GetUAServer().Ptr(), node, &v) )
				return Value{ (StatusCode)sc };
			return Value{ move(v) };
		}
		template<class T> Ω Setting( UA_UInt32 variable, sv name )ε->T{ return Variant( Property(Node(variable), {"HA Configuration", name}) ).template AsNumber<T>(); }
		//A property of the HA Configuration's AggregateConfiguration, a Boolean as 0 or 1:  none when it holds no value.
		Ω Aggregate( UA_UInt32 variable, sv name )ε->optional<double>{
			auto value = Variant( Property(Node(variable), {"HA Configuration", "AggregateConfiguration", name}) );
			return value.hasValue && !value.IsEmpty() ? value.AsNumber<double>() : optional<double>{};
		}

		//Seconds into the minute before the server started:  before the value each node took at its start, and, unless the
		//run crosses midnight, on its day.
		Ω At( uint second )ι->TimePoint{ return _base+seconds{ second }; }
		static inline UA_Client* _client{};
		static inline NsIndex _ns{};
		static inline TimePoint _base{};
		constexpr static UA_UInt32 Status1{ 6011 }, Rpm1{ 6012 }, Status2{ 6021 }, Rpm2{ 6022 }, Rpm3{ 6032 }, Rpm4{ 6042 }, RpmManual{ 6054 };
	};

	//What the nodeset marks is what is historized, each node under its HA Configuration's thresholds:  every deviation
	//format, a MinTimeInterval and a heartbeat.
	TEST_F( HistoryTests, HistorizesWhatTheNodesetMarks ){
		ASSERT_TRUE( History().Enabled() );
		for( let id : {Status1, Rpm1, Rpm2, Rpm3, Rpm4, RpmManual} )
			EXPECT_TRUE( Index(id) ) << id;
		EXPECT_FALSE( Index(Status2) );
		EXPECT_FALSE( Index(1003) );//the type's own motorRpm.

		let rpm1 = Thresholds( Rpm1 );
		EXPECT_EQ( rpm1.ExceptionDeviation, 5 );
		EXPECT_EQ( rpm1.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_ABSOLUTEVALUE );
		EXPECT_EQ( rpm1.MinTimeInterval, 1s );
		EXPECT_EQ( rpm1.MaxTimeInterval, Duration::zero() );
		EXPECT_FALSE( rpm1.Stepped );
		let rpm2 = Thresholds( Rpm2 );
		EXPECT_EQ( rpm2.ExceptionDeviation, 1 );
		EXPECT_EQ( rpm2.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFVALUE );
		EXPECT_EQ( rpm2.MinTimeInterval, Duration::zero() );
		EXPECT_EQ( rpm2.MaxTimeInterval, 1min );
		let rpm3 = Thresholds( Rpm3 );
		EXPECT_EQ( rpm3.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFRANGE );
		ASSERT_TRUE( rpm3.Range );
		EXPECT_EQ( rpm3.Range->High, 1500 );//its InstrumentRange, not its EURange.
		EXPECT_TRUE( rpm3.Stepped );
		let rpm4 = Thresholds( Rpm4 );
		EXPECT_EQ( rpm4.ExceptionDeviation, 0.5 );
		EXPECT_EQ( rpm4.DeviationFormat, UA_EXCEPTIONDEVIATIONFORMAT_PERCENTOFEURANGE );
		ASSERT_TRUE( rpm4.Range );
		EXPECT_EQ( rpm4.Range->Low, 0 );
		EXPECT_EQ( rpm4.Range->High, 3000 );
		EXPECT_EQ( Thresholds(RpmManual).ExceptionDeviation, 10 );
		let status = Thresholds( Status1 );
		EXPECT_FALSE( status.ExceptionDeviation );//every change.
		EXPECT_TRUE( status.Stepped );
	}

	//What a client needs to find a node's history and the nodeset left out:  the history bit, and the HA Configuration
	//with its defaults.
	TEST_F( HistoryTests, PublishesWhatTheNodesetLeavesOut ){
		auto ua = GetUAServer().Ptr();
		for( let id : {Status1, Rpm1, Rpm2, Rpm3, Rpm4, RpmManual} ){
			UA_Byte level{};
			ASSERT_EQ( UA_Server_readAccessLevel(ua, Node(id), &level), UA_STATUSCODE_GOOD ) << id;
			EXPECT_EQ( level, UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE | UA_ACCESSLEVELMASK_HISTORYREAD | UA_ACCESSLEVELMASK_HISTORYWRITE ) << id;
			UA_Boolean historizing{};
			EXPECT_EQ( UA_Server_readHistorizing(ua, Node(id), &historizing), UA_STATUSCODE_GOOD ) << id;
			EXPECT_TRUE( historizing ) << id;//the loader keeps the attribute.
			EXPECT_TRUE( Setting<bool>(id, "ServerTimestampSupported") ) << id;
			let today = UADateTime{ floor<days>(Clock::now()) }.UA();//no file yet, and nothing older than today.
			EXPECT_EQ( Variant(Property(Node(id), {"HA Configuration", "StartOfArchive"})).Get<UA_DateTime>(0), today ) << id;
			EXPECT_EQ( Variant(Property(Node(id), {"HA Configuration", "StartOfOnlineArchive"})).Get<UA_DateTime>(0), today ) << id;
			EXPECT_EQ( Aggregate(id, "TreatUncertainAsBad"), 1 ) << id;
			EXPECT_EQ( Aggregate(id, "PercentDataBad"), 100 ) << id;
			EXPECT_EQ( Aggregate(id, "PercentDataGood"), 100 ) << id;
			EXPECT_EQ( Aggregate(id, "UseSlopedExtrapolation"), 0 ) << id;
		}
		UA_Byte level{};
		ASSERT_EQ( UA_Server_readAccessLevel(ua, Node(Status2), &level), UA_STATUSCODE_GOOD );
		EXPECT_EQ( level, UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE );

		//pump1.status has no HA Configuration in the nodeset.
		EXPECT_TRUE( Setting<bool>(Status1, "Stepped") );
		EXPECT_EQ( Setting<double>(Status1, "MinTimeInterval"), 0 );
		EXPECT_EQ( Setting<double>(Status1, "MaxTimeInterval"), 0 );
		EXPECT_THROW( Property(Node(Status1), {"HA Configuration", "ExceptionDeviation"}), Exception );//none stores every change.
		//The nodeset's are kept, and what it leaves out of one is added.
		EXPECT_FALSE( Setting<bool>(Rpm1, "Stepped") );
		EXPECT_EQ( Setting<double>(Rpm1, "MinTimeInterval"), 1000 );
		EXPECT_EQ( Setting<double>(Rpm1, "MaxTimeInterval"), 0 );
		EXPECT_TRUE( Setting<bool>(Rpm3, "Stepped") );
		EXPECT_EQ( Variant(Property(Node(RpmManual), {"HA Configuration", "ExceptionDeviationFormat"})).Get<UA_Int32>(0), UA_EXCEPTIONDEVIATIONFORMAT_ABSOLUTEVALUE );
	}

	//A type's members are templates, left alone though marked:  no index, no history bit and no HA Configuration.  The
	//instance's copies are its live variables, historized.
	TEST_F( HistoryTests, LeavesATypesMembersOut ){
		auto ua = GetUAServer().Ptr();
		for( let& member : {TypeMember(), PartMember()} ){
			EXPECT_FALSE( History().Find(member) ) << member.ToString();
			UA_Byte accessLevel{};
			UAε( UA_Server_readAccessLevel(ua, member, &accessLevel) );
			EXPECT_FALSE( accessLevel & UA_ACCESSLEVELMASK_HISTORYREAD ) << member.ToString();
			EXPECT_THROW( Property(member, {"HA Configuration"}), Exception ) << member.ToString();
		}
		for( let& path : {std::initializer_list<sv>{"member"}, std::initializer_list<sv>{"part", "member"}} ){
			let member = Property( Instance(), path, 1 );
			EXPECT_TRUE( History().Find(member) ) << member.ToString();
			EXPECT_NO_THROW( Property(member, {"HA Configuration"}) ) << member.ToString();
		}
	}

	//Only what is served is claimed:  raw and modified reads, at most readLimit values a call, and the four edits the
	//plugin has a callback for.
	TEST_F( HistoryTests, PublishesItsCapabilities ){
		let capability = []( UA_UInt32 id ){ return Variant( NodeId{0, id} ); };
		EXPECT_TRUE( capability(UA_NS0ID_HISTORYSERVERCAPABILITIES_ACCESSHISTORYDATACAPABILITY).Get<UA_Boolean>(0) );
		EXPECT_EQ( capability(UA_NS0ID_HISTORYSERVERCAPABILITIES_MAXRETURNDATAVALUES).Get<UA_UInt32>(0), History().ReadLimit() );
		EXPECT_EQ( History().ReadLimit(), 10'000u );
		for( let id : {UA_NS0ID_HISTORYSERVERCAPABILITIES_INSERTDATACAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_REPLACEDATACAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_UPDATEDATACAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_DELETERAWCAPABILITY} )
			EXPECT_TRUE( capability(id).Get<UA_Boolean>(0) ) << id;
		for( let id : {UA_NS0ID_HISTORYSERVERCAPABILITIES_ACCESSHISTORYEVENTSCAPABILITY, UA_NS0ID_HISTORYSERVERCAPABILITIES_DELETEATTIMECAPABILITY} )
			EXPECT_FALSE( capability(id).Get<UA_Boolean>(0) ) << id;
	}

	//The aggregates served, listed in HistoryServerCapabilities' AggregateFunctions folder:  Part 13's nine objects of
	//namespace 0, and Median, an AggregateFunctionType object of the server's own namespace, each the aggregate it is
	//requested as.  What isn't served isn't listed, and isn't an aggregate.
	TEST_F( HistoryTests, PublishesItsAggregates ){
		UA_BrowseDescription browse; UA_BrowseDescription_init( &browse );
		browse.nodeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HISTORYSERVERCAPABILITIES_AGGREGATEFUNCTIONS );
		browse.browseDirection = UA_BROWSEDIRECTION_FORWARD;
		browse.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_ORGANIZES );
		browse.includeSubtypes = true;
		browse.resultMask = UA_BROWSERESULTMASK_BROWSENAME | UA_BROWSERESULTMASK_TYPEDEFINITION;
		auto found = UA_Server_browse( GetUAServer().Ptr(), 0, &browse );
		EXPECT_EQ( found.statusCode, UA_STATUSCODE_GOOD );
		flat_set<NodeId> listed;
		let median = UAHistory::Median();
		for( uint i=0; i<found.referencesSize; ++i ){
			let& reference = found.references[i];
			listed.emplace( reference.nodeId.nodeId );
			EXPECT_EQ( reference.typeDefinition.nodeId.identifier.numeric, UA_NS0ID_AGGREGATEFUNCTIONTYPE ) << i;
			if( UA_NodeId_equal(&reference.nodeId.nodeId, &median) ){
				EXPECT_EQ( reference.browseName.namespaceIndex, 1 );
				EXPECT_EQ( ToString(reference.browseName.name), "Median" );
			}
		}
		UA_BrowseResult_clear( &found );
		const flat_map<UA_UInt32,Hist::EAggregate> part13{ {UA_NS0ID_AGGREGATEFUNCTION_INTERPOLATIVE, Hist::EAggregate::Interpolative}, {UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, Hist::EAggregate::Average},
			{UA_NS0ID_AGGREGATEFUNCTION_TIMEAVERAGE, Hist::EAggregate::TimeAverage}, {UA_NS0ID_AGGREGATEFUNCTION_COUNT, Hist::EAggregate::Count}, {UA_NS0ID_AGGREGATEFUNCTION_MINIMUM, Hist::EAggregate::Minimum},
			{UA_NS0ID_AGGREGATEFUNCTION_MAXIMUM, Hist::EAggregate::Maximum}, {UA_NS0ID_AGGREGATEFUNCTION_START, Hist::EAggregate::Start}, {UA_NS0ID_AGGREGATEFUNCTION_END, Hist::EAggregate::End},
			{UA_NS0ID_AGGREGATEFUNCTION_STANDARDDEVIATIONSAMPLE, Hist::EAggregate::StandardDeviationSample} };
		EXPECT_EQ( listed.size(), part13.size()+1 );
		for( let& [function, aggregate] : part13 ){
			EXPECT_TRUE( listed.contains(NodeId{0, function}) ) << function;
			EXPECT_EQ( UAHistory::Aggregate(NodeId{0, function}), aggregate ) << function;
		}
		EXPECT_TRUE( listed.contains(median) );
		EXPECT_EQ( UAHistory::Aggregate(median), Hist::EAggregate::Median );
		EXPECT_FALSE( listed.contains(NodeId{0, UA_NS0ID_AGGREGATEFUNCTION_RANGE}) );
		EXPECT_FALSE( UAHistory::Aggregate(NodeId{0, UA_NS0ID_AGGREGATEFUNCTION_RANGE}) );
		EXPECT_FALSE( UAHistory::Aggregate(NodeId{UA_NODEID_STRING_ALLOC(2, "Median")}) );
	}

	//setValue to HistoryRead:  what passes the node's band is stored with both timestamps, and read back over Part 11's
	//range, which holds its start and not its end, from the buffer and then from the day's file.
	TEST_F( HistoryTests, ReadsRawHistory ){
		Write( Rpm4, 100, At(0) );
		Write( Rpm4, 200, At(2) );
		Write( Rpm4, 300, At(4) );
		Write( Rpm4, 310, At(6) );//inside 0.5% of the EURange, 15 rpm, of the last stored.
		Write( Rpm4, 400, At(8) );
		for( let flushed : {false, true} ){
			SCOPED_TRACE( flushed ? "from the file" : "from the buffer" );
			if( flushed )
				ASSERT_TRUE( BlockAny(History().Group()->Flush()) );
			let forward = Read( Rpm4, {.Start=At(0), .End=At(10)} );
			EXPECT_TRUE( UA_StatusCode_isGood(forward.Status) ) << UA_StatusCode_name( forward.Status );
			let values = all( forward.Values );
			ASSERT_EQ( doubles(values), (vector<double>{100, 200, 300, 400}) );
			EXPECT_EQ( values[1].sourceTimestamp, UADateTime{At(2)}.UA() );
			EXPECT_TRUE( values[1].hasServerTimestamp );//stamped as the write arrived:  the server is this one.
			EXPECT_NEAR( (double)values[1].serverTimestamp, (double)UA_DateTime_now(), (double)UA_DATETIME_SEC*120 );
			EXPECT_FALSE( values[1].hasStatus );

			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(0), .End=At(8)}).Values)), (vector<double>{100, 200, 300}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(8), .End=At(0)}).Values)), (vector<double>{400, 300, 200}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(1), .End=At(7), .Bounds=true}).Values)), (vector<double>{100, 200, 300, 400}) );
			EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(2), .End=At(2)}).Values)), (vector<double>{200}) );

			let paged = Read( Rpm4, {.Start=At(0), .End=At(10), .Limit=3} );
			EXPECT_EQ( doubles(all(paged.Values)), (vector<double>{100, 200, 300, 400}) );
			ASSERT_GE( paged.Values.size(), 2u );
			EXPECT_EQ( paged.Values[0].size(), 3u );
			//The last two, with the continuation point released, not followed.
			let last = Read( Rpm4, {.End=At(10), .Limit=2, .MaxPages=1} );
			EXPECT_TRUE( UA_StatusCode_isGood(last.Status) ) << UA_StatusCode_name( last.Status );
			EXPECT_EQ( doubles(all(last.Values)), (vector<double>{400, 300}) );

			let sourced = all( Read(Rpm4, {.Start=At(0), .End=At(10), .Timestamps=UA_TIMESTAMPSTORETURN_SOURCE}).Values );
			ASSERT_EQ( sourced.size(), 4u );
			EXPECT_TRUE( sourced[0].hasSourceTimestamp );
			EXPECT_FALSE( sourced[0].hasServerTimestamp );
		}
		let nothing = Read( Rpm4, {.Start=At(100), .End=At(200)} );
		EXPECT_EQ( nothing.Status, UA_STATUSCODE_GOODNODATA );
		EXPECT_TRUE( all(nothing.Values).empty() );
		EXPECT_GT( History().Reads().Count, 0u );
		EXPECT_GT( History().Collections().Count, 0u );
	}

	//Each call reads one day's file, so a read over two days comes in two pages, though neither is full.  Today's holds
	//the 100 and, after it, the 0 the node took at the start.
	TEST_F( HistoryTests, ReadsADayACall ){
		let tomorrow = floor<days>( Clock::now() )+days{ 1 };
		Write( Rpm3, 100, At(20) );
		Write( Rpm3, 200, tomorrow+10s );
		Write( Rpm3, 300, tomorrow+20s );
		let forward = Read( Rpm3, {.Start=At(20), .End=tomorrow+1min} );
		EXPECT_TRUE( UA_StatusCode_isGood(forward.Status) ) << UA_StatusCode_name( forward.Status );
		ASSERT_EQ( forward.Values.size(), 2u );
		EXPECT_EQ( doubles(forward.Values[0]), (vector<double>{100, 0}) );
		EXPECT_EQ( doubles(forward.Values[1]), (vector<double>{200, 300}) );
		let back = Read( Rpm3, {.Start=tomorrow+1min, .End=At(19)} );
		EXPECT_TRUE( UA_StatusCode_isGood(back.Status) ) << UA_StatusCode_name( back.Status );
		ASSERT_EQ( back.Values.size(), 2u );
		EXPECT_EQ( doubles(back.Values[0]), (vector<double>{300, 200}) );
		EXPECT_EQ( doubles(back.Values[1]), (vector<double>{0, 100}) );

		//Rpm1 has nothing after its start value on either day, each a file once flushed.  The helper hands over the first
		//call's empty page, and stops at the second's Good_NoData.
		ASSERT_TRUE( BlockAny(History().Group()->Flush()) );
		for( let& request : {Request{.Start=At(100), .End=tomorrow+1min}, Request{.Start=tomorrow+1min, .End=At(100)}} ){
			let nothing = Read( Rpm1, request );
			EXPECT_EQ( nothing.Status, UA_STATUSCODE_GOODNODATA ) << UA_StatusCode_name( nothing.Status );
			ASSERT_EQ( nothing.Values.size(), 1u );
			EXPECT_TRUE( nothing.Values[0].empty() );
		}
	}

	//A write with an IndexRange changes part of an array, and open62541 passes setValue only that part:  the history keeps
	//the array the write made.  A scalar with a range writes one element.
	TEST_F( HistoryTests, KeepsTheArrayAPartialWriteMakes ){
		let node = Array();
		let write = [&]( const UA_Variant& part, sv range, TimePoint source ){
			UA_WriteValue w; UA_WriteValue_init( &w );
			w.nodeId = node;
			w.attributeId = UA_ATTRIBUTEID_VALUE;
			w.indexRange = UA_String{ range.size(), (UA_Byte*)range.data() };
			w.value.value = part;
			w.value.hasValue = true;
			w.value.sourceTimestamp = UADateTime{ source }.UA();
			w.value.hasSourceTimestamp = true;
			UAε( UA_Server_write(GetUAServer().Ptr(), &w) );
		};
		UA_Double nine{ 9 }, seven{ 7 };
		UA_Variant part;
		UA_Variant_setArray( &part, &nine, 1, &UA_TYPES[UA_TYPES_DOUBLE] );
		write( part, "2", At(30) );
		UA_Variant_setScalar( &part, &seven, &UA_TYPES[UA_TYPES_DOUBLE] );
		write( part, "0", At(31) );
		let array = []( const UA_Variant& v ){
			return v.type==&UA_TYPES[UA_TYPES_DOUBLE] && !UA_Variant_isScalar(&v) ? vector<double>{ (UA_Double*)v.data, (UA_Double*)v.data+v.arrayLength } : vector<double>{};
		};
		ASSERT_EQ( array(Variant(node).value), (vector<double>{7, 2, 9, 4}) );

		let history = Read( node, {.Start=At(30), .End=At(32)} );
		EXPECT_TRUE( UA_StatusCode_isGood(history.Status) ) << UA_StatusCode_name( history.Status );
		let values = all( history.Values );
		ASSERT_EQ( values.size(), 2u );
		EXPECT_EQ( array(values[0].value), (vector<double>{1, 2, 9, 4}) );
		EXPECT_EQ( array(values[1].value), (vector<double>{7, 2, 9, 4}) );
	}

	TEST_F( HistoryTests, RefusesWhatItCannotServe ){
		EXPECT_EQ( Read(Status2, {.Start=At(0), .End=At(10)}).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );//not historized.
		EXPECT_EQ( Read(Node(999'999), {.Start=At(0), .End=At(10)}).Status, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( Read(Rpm4, {.Start=At(0)}).Status, UA_STATUSCODE_BADHISTORYOPERATIONINVALID );//two of start, end and a count bound a read.
		EXPECT_EQ( Read(Rpm4, {.Start=At(0), .End=At(10), .Timestamps=UA_TIMESTAMPSTORETURN_NEITHER}).Status, UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT );
		EXPECT_EQ( ReadModified(Rpm4, {.Start=At(0), .End=At(10), .Bounds=true}).Status, UA_STATUSCODE_BADINVALIDARGUMENT );//the modified values have no bounds.

		//At-time and processed reads:  one aggregate per node, an aggregate that isn't served, a time or an interval not
		//given, a start that is its end, server timestamps alone, and a node that isn't historized, or isn't there.
		const NodeId average{ 0, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE };
		let mismatch = Processed( {Node(Rpm4), Node(Rpm2)}, {average}, ticks(At(0)), ticks(At(10)), 5000 );
		ASSERT_EQ( mismatch.size(), 2u );
		EXPECT_EQ( mismatch[0].Status, UA_STATUSCODE_BADAGGREGATELISTMISMATCH );
		EXPECT_EQ( mismatch[1].Status, UA_STATUSCODE_BADAGGREGATELISTMISMATCH );
		EXPECT_EQ( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_RANGE, At(0), At(10), 5s).Status, UA_STATUSCODE_BADAGGREGATENOTSUPPORTED );
		EXPECT_EQ( Processed({Node(Rpm4)}, {average}, 0, ticks(At(10)), 5000).at(0).Status, UA_STATUSCODE_BADHISTORYOPERATIONINVALID );
		EXPECT_EQ( Processed({Node(Rpm4)}, {average}, ticks(At(0)), 0, 5000).at(0).Status, UA_STATUSCODE_BADHISTORYOPERATIONINVALID );
		EXPECT_EQ( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(0), At(10), -5s).Status, UA_STATUSCODE_BADINVALIDARGUMENT );
		EXPECT_EQ( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(5), At(5), 5s).Status, UA_STATUSCODE_BADINVALIDARGUMENT );
		EXPECT_EQ( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(0), At(10), 5s, {}, UA_TIMESTAMPSTORETURN_SERVER).Status, UA_STATUSCODE_BADTIMESTAMPSTORETURNINVALID );
		EXPECT_EQ( Aggregate(Status2, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(0), At(10), 5s).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( AtTime(Node(Rpm4), {}).Status, UA_STATUSCODE_BADHISTORYOPERATIONINVALID );
		EXPECT_EQ( AtTime(Node(Status2), {At(0)}).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( AtTime(Node(999'999), {At(0)}).Status, UA_STATUSCODE_BADNODEIDUNKNOWN );

		//The edits:  a node that isn't historized, one that isn't there, a value with no SourceTimestamp, a delete without
		//both times, and what isn't served.
		auto one = Sample( 1, At(0) );
		EXPECT_EQ( InsertEntry(Status2, one).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( DeleteRaw(Status2, At(0), At(10)), UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( InsertEntry(999'999, one).Status, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( Insert(Status2, 1, At(0)), UA_STATUSCODE_BADUNEXPECTEDERROR );//the helper's answer to an entry refused whole.
		{
			UA_Variant v; UA_Variant_init( &v );
			const double one{ 1 };
			UA_Variant_setScalarCopy( &v, &one, &UA_TYPES[UA_TYPES_DOUBLE] );
			Value unstamped{ move(v) };
			let node = Node( Rpm4 );
			EXPECT_EQ( UA_Client_HistoryUpdate_insert(_client, &node, &unstamped), UA_STATUSCODE_BADINVALIDTIMESTAMPARGUMENT );
		}
		let node = Node( Rpm4 );
		EXPECT_EQ( UA_Client_HistoryUpdate_deleteRaw(_client, &node, 0, UADateTime{At(10)}.UA()), UA_STATUSCODE_BADHISTORYOPERATIONINVALID );//not from 1601.
		EXPECT_EQ( UA_Client_HistoryUpdate_deleteRaw(_client, &node, UADateTime{At(0)}.UA(), 0), UA_STATUSCODE_BADHISTORYOPERATIONINVALID );
		UA_DeleteAtTimeDetails atTime; UA_DeleteAtTimeDetails_init( &atTime );
		atTime.nodeId = node;
		UA_DateTime time{ UADateTime{At(0)}.UA() };
		atTime.reqTimesSize = 1;
		atTime.reqTimes = &time;
		EXPECT_EQ( HistoryUpdate(&atTime, UA_TYPES_DELETEATTIMEDETAILS).Status, UA_STATUSCODE_BADNOTSUPPORTED );//open62541 has no callback for it.
		UA_DeleteRawModifiedDetails modified; UA_DeleteRawModifiedDetails_init( &modified );
		modified.nodeId = node;
		modified.isDeleteModified = true;
		modified.startTime = time;
		modified.endTime = UADateTime{ At(10) }.UA();
		EXPECT_EQ( HistoryUpdate(&modified, UA_TYPES_DELETERAWMODIFIEDDETAILS).Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );//the audit trail.
		auto v = Sample( 1, At(0) );
		UA_UpdateDataDetails remove; UA_UpdateDataDetails_init( &remove );
		remove.nodeId = node;
		remove.performInsertReplace = UA_PERFORMUPDATETYPE_REMOVE;//annotations'.
		remove.updateValuesSize = 1;
		remove.updateValues = &v;
		let removed = HistoryUpdate( &remove, UA_TYPES_UPDATEDATADETAILS );
		EXPECT_EQ( removed.Status, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( removed.Results, vector<UA_StatusCode>{UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED} );//refused whole, so its value is too.
	}

	//A value a Write of the variable would refuse answers Bad_TypeMismatch in its place, and the rest are written:  a
	//String, an Int64 or an array for Rpm4's scalar Double, and a scalar for the array's.
	TEST_F( HistoryTests, RefusesAValueOfTheWrongType ){
		static_assert( sizeof(Value)==sizeof(UA_DataValue) );//an array of them is the request's.
		let stamped = []( UA_Variant v, TimePoint source ){
			Value y{ move(v) };
			y.sourceTimestamp = UADateTime{ source }.UA();
			y.hasSourceTimestamp = true;
			return y;
		};
		let scalar = [&]( const void* p, uint type, TimePoint source ){
			UA_Variant v; UA_Variant_init( &v );
			UA_Variant_setScalarCopy( &v, p, &UA_TYPES[type] );
			return stamped( v, source );
		};
		let array = [&]( std::initializer_list<UA_Double> values, TimePoint source ){
			UA_Variant v; UA_Variant_init( &v );
			UA_Variant_setArrayCopy( &v, values.begin(), values.size(), &UA_TYPES[UA_TYPES_DOUBLE] );
			return stamped( v, source );
		};
		let update = []( const UA_NodeId& node, vector<Value>& values ){
			UA_UpdateDataDetails details; UA_UpdateDataDetails_init( &details );
			details.nodeId = node;
			details.performInsertReplace = UA_PERFORMUPDATETYPE_INSERT;
			details.updateValuesSize = values.size();
			details.updateValues = values.data();
			return HistoryUpdate( &details, UA_TYPES_UPDATEDATADETAILS );
		};
		const UA_String text{ UA_STRING((char*)"five") };
		const UA_Int64 wide{ 5 };
		vector<Value> rpm4{ Sample(5, At(57)), scalar(&text, UA_TYPES_STRING, At(58)), scalar(&wide, UA_TYPES_INT64, At(58)), array({5, 6}, At(58)) };
		let updated = update( Node(Rpm4), rpm4 );
		EXPECT_EQ( updated.Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( updated.Results, (vector<UA_StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADTYPEMISMATCH, UA_STATUSCODE_BADTYPEMISMATCH, UA_STATUSCODE_BADTYPEMISMATCH}) );
		EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(57), .End=At(59)}).Values)), vector<double>{5} );

		const UA_Double one{ 1 };
		vector<Value> arrays{ array({1, 2}, At(58)), scalar(&one, UA_TYPES_DOUBLE, At(59)) };
		EXPECT_EQ( update(Array(), arrays).Results, (vector<UA_StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADTYPEMISMATCH}) );
	}

	//ReadProcessed:  each aggregate served, over Rpm4's records, three an interval, through a UA client by the
	//AggregateFunction node that names it, as the library computes them (its tests replay Part 13's examples).  A
	//computed value is stamped at its interval's start, with no server timestamp; Start, End and an Interpolative hit
	//are the record as it is.  One interval over the range; reversed, each interval holds its later end and is stamped
	//at it; the request's configuration over the node's; readLimit values a page; and a number of what isn't one refused.
	TEST_F( HistoryTests, ReadsAggregates ){
		for( uint i=0; i<7; ++i )
			Write( Rpm4, 100.0*(i+1), At(11+2*i) );//100 at 11 through 700 at 23, each past the band.
		struct Case final{ UA_UInt32 Function; vector<double> Values; };
		const Case cases[]{ {UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, {200, 500}}, {UA_NS0ID_AGGREGATEFUNCTION_TIMEAVERAGE, {250, 550}}, {UA_NS0ID_AGGREGATEFUNCTION_COUNT, {3, 3}},
			{UA_NS0ID_AGGREGATEFUNCTION_MINIMUM, {100, 400}}, {UA_NS0ID_AGGREGATEFUNCTION_MAXIMUM, {300, 600}}, {UA_NS0ID_AGGREGATEFUNCTION_START, {100, 400}}, {UA_NS0ID_AGGREGATEFUNCTION_END, {300, 600}},
			{UA_NS0ID_AGGREGATEFUNCTION_STANDARDDEVIATIONSAMPLE, {100, 100}}, {UA_NS0ID_AGGREGATEFUNCTION_INTERPOLATIVE, {100, 400}} };
		for( let& c : cases ){
			SCOPED_TRACE( c.Function );
			let read = Aggregate( Rpm4, c.Function, At(11), At(23), 6s );
			EXPECT_TRUE( UA_StatusCode_isGood(read.Status) ) << UA_StatusCode_name( read.Status );
			let values = all( read.Values );
			EXPECT_EQ( numbers(values), c.Values );
			let stamps = c.Function==UA_NS0ID_AGGREGATEFUNCTION_END ? vector<UA_DateTime>{ticks(At(15)), ticks(At(21))} : vector<UA_DateTime>{ticks(At(11)), ticks(At(17))};//End's records are the intervals' last.
			EXPECT_EQ( sources(values), stamps );
		}
		let median = Processed( {Node(Rpm4)}, {UAHistory::Median()}, ticks(At(11)), ticks(At(23)), 6000 ).at( 0 );
		EXPECT_TRUE( UA_StatusCode_isGood(median.Status) ) << UA_StatusCode_name( median.Status );
		EXPECT_EQ( numbers(all(median.Values)), (vector<double>{200, 500}) );

		let average = all( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(11), At(23), 6s).Values );
		ASSERT_EQ( average.size(), 2u );
		EXPECT_EQ( average[0].status, Calculated );
		EXPECT_TRUE( average[0].hasSourceTimestamp );
		EXPECT_FALSE( average[0].hasServerTimestamp );
		let end = all( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_END, At(11), At(23), 6s).Values );
		ASSERT_EQ( end.size(), 2u );
		EXPECT_FALSE( end[0].hasStatus );
		EXPECT_TRUE( end[0].hasServerTimestamp );

		//One interval over the range, 100 through 600 and not the 700 at its end; reversed, (17,23] then (11,17].
		EXPECT_EQ( numbers(all(Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(11), At(23), 0s).Values)), vector<double>{350} );
		let reversed = all( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(23), At(11), 6s).Values );
		EXPECT_EQ( numbers(reversed), (vector<double>{600, 300}) );
		EXPECT_EQ( sources(reversed), (vector<UA_DateTime>{ticks(At(23)), ticks(At(17))}) );
		//The request's configuration, or the node's with useServerCapabilitiesDefaults:  a pair of percentages Part 13
		//doesn't allow refuses the read.
		let own = Aggregate( Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(11), At(23), 6s, UA_AggregateConfiguration{.treatUncertainAsBad=true, .percentDataBad=100, .percentDataGood=100} );
		EXPECT_EQ( numbers(all(own.Values)), (vector<double>{200, 500}) );
		EXPECT_EQ( Aggregate(Rpm4, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(11), At(23), 6s, UA_AggregateConfiguration{.percentDataBad=50, .percentDataGood=20}).Status, UA_STATUSCODE_BADAGGREGATEINVALIDINPUTS );
		//readLimit values a page:  1 ms intervals over the 12 s are 12,000 counts, most of nothing, in two pages.
		let paged = Aggregate( Rpm4, UA_NS0ID_AGGREGATEFUNCTION_COUNT, At(11), At(23), 1ms );
		EXPECT_TRUE( UA_StatusCode_isGood(paged.Status) ) << UA_StatusCode_name( paged.Status );
		ASSERT_EQ( paged.Values.size(), 2u );
		EXPECT_EQ( paged.Values[0].size(), History().ReadLimit() );
		EXPECT_EQ( all(paged.Values).size(), 12'000u );
		//A numeric aggregate of a Boolean is Bad_AggregateInvalidInputs, value by value.
		WriteBoolean( Status1, true, At(11) );
		let booleans = Aggregate( Status1, UA_NS0ID_AGGREGATEFUNCTION_AVERAGE, At(11), At(17), 6s );
		EXPECT_TRUE( UA_StatusCode_isGood(booleans.Status) ) << UA_StatusCode_name( booleans.Status );
		EXPECT_EQ( statuses(all(booleans.Values)), vector<UA_StatusCode>{UA_STATUSCODE_BADAGGREGATEINVALIDINPUTS} );
	}

	//ReadAtTime:  a record at the time as it is, and otherwise the value interpolated from the node's bounding records,
	//stepped on Rpm4 and sloped on Rpm2, whose Stepped is false, each marked Interpolated and stamped at the time with no
	//server timestamp, in the request's order.  Simple bounds take the nearest records whatever their status:  the same
	//here, where none is Bad.  A read for server timestamps alone is refused, and source alone gets them.
	TEST_F( HistoryTests, ReadsAtTime ){
		Write( Rpm4, 800, At(25) );
		Write( Rpm4, 900, At(27) );
		Write( Rpm2, 100, At(11) );
		Write( Rpm2, 300, At(15) );
		for( let simple : {false, true} ){
			SCOPED_TRACE( simple ? "simple bounds" : "interpolated bounds" );
			let read = AtTime( Node(Rpm4), {At(26), At(25), At(27)}, simple );
			EXPECT_TRUE( UA_StatusCode_isGood(read.Status) ) << UA_StatusCode_name( read.Status );
			let values = all( read.Values );
			EXPECT_EQ( doubles(values), (vector<double>{800, 800, 900}) );
			EXPECT_EQ( sources(values), (vector<UA_DateTime>{ticks(At(26)), ticks(At(25)), ticks(At(27))}) );
			ASSERT_EQ( values.size(), 3u );
			EXPECT_EQ( values[0].status, Interpolated );
			EXPECT_FALSE( values[0].hasServerTimestamp );
			EXPECT_FALSE( values[1].hasStatus );
			EXPECT_TRUE( values[1].hasServerTimestamp );
			let sloped = all( AtTime(Node(Rpm2), {At(13)}, simple).Values );
			EXPECT_EQ( doubles(sloped), vector<double>{200} );
			EXPECT_EQ( statuses(sloped), vector<UA_StatusCode>{Interpolated} );
		}
		let sourced = all( AtTime(Node(Rpm4), {At(25)}, false, UA_TIMESTAMPSTORETURN_SOURCE).Values );
		ASSERT_EQ( sourced.size(), 1u );
		EXPECT_TRUE( sourced[0].hasSourceTimestamp );
		EXPECT_FALSE( sourced[0].hasServerTimestamp );
		EXPECT_EQ( AtTime(Node(Rpm4), {At(25)}, false, UA_TIMESTAMPSTORETURN_SERVER).Status, UA_STATUSCODE_BADTIMESTAMPSTORETURNINVALID );
	}

	//HistoryUpdate through open62541's client helpers, each value's result the library's:  a raw read then sees the
	//series as edited, and a modified read the values the edits changed, by the time they target and in the order made,
	//each with who made it and when.  A delete's range holds its start and not its end, as a read's does, and
	//startTime equal to endTime is that instant.  Rpm4 stores each write here:  a node with a MinTimeInterval holds a
	//write stamped before its last stored value as its pending value, which the next replaces.
	TEST_F( HistoryTests, EditsHistory ){
		Write( Rpm4, 100, At(40) );
		Write( Rpm4, 200, At(42) );
		Write( Rpm4, 300, At(44) );
		EXPECT_EQ( Insert(Rpm4, 150, At(41)), UA_STATUSCODE_GOODENTRYINSERTED );//flushes the buffer first, so the 100 is on disk to refuse the next.
		EXPECT_EQ( Insert(Rpm4, 1, At(40)), UA_STATUSCODE_BADENTRYEXISTS );
		EXPECT_EQ( Replace(Rpm4, 250, At(42)), UA_STATUSCODE_GOODENTRYREPLACED );
		EXPECT_EQ( Replace(Rpm4, 9, At(43)), UA_STATUSCODE_BADNOENTRYEXISTS );
		EXPECT_EQ( Update(Rpm4, 350, At(44)), UA_STATUSCODE_GOODENTRYREPLACED );
		EXPECT_EQ( Update(Rpm4, 400, At(46)), UA_STATUSCODE_GOODENTRYINSERTED );
		EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(40), .End=At(47)}).Values)), (vector<double>{100, 150, 250, 350, 400}) );
		EXPECT_EQ( DeleteRaw(Rpm4, At(41), At(44)), UA_STATUSCODE_GOOD );
		EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(40), .End=At(47)}).Values)), (vector<double>{100, 350, 400}) );
		EXPECT_EQ( DeleteRaw(Rpm4, At(41), At(44)), UA_STATUSCODE_BADNODATA );
		EXPECT_EQ( DeleteRaw(Rpm4, At(46), At(46)), UA_STATUSCODE_GOOD );
		EXPECT_EQ( DeleteRaw(Rpm4, At(44), At(41)), UA_STATUSCODE_BADINVALIDARGUMENT );
		EXPECT_EQ( doubles(all(Read(Rpm4, {.Start=At(40), .End=At(47)}).Values)), (vector<double>{100, 350}) );

		let before = UA_DateTime_now()-UA_DATETIME_SEC*60;
		let modified = ReadModified( Rpm4, {.Start=At(40), .End=At(47)} );
		EXPECT_TRUE( UA_StatusCode_isGood(modified.Status) ) << UA_StatusCode_name( modified.Status );
		EXPECT_EQ( doubles(modified.Values), (vector<double>{150, 150, 200, 250, 300, 400, 400}) );//an INSERT's is the value inserted, the others' the one replaced.
		EXPECT_EQ( types(modified.Infos), (vector<UA_HistoryUpdateType>{UA_HISTORYUPDATETYPE_INSERT, UA_HISTORYUPDATETYPE_DELETE, UA_HISTORYUPDATETYPE_REPLACE, UA_HISTORYUPDATETYPE_DELETE, UA_HISTORYUPDATETYPE_UPDATE, UA_HISTORYUPDATETYPE_UPDATE, UA_HISTORYUPDATETYPE_DELETE}) );
		ASSERT_EQ( modified.Values.size(), 7u );
		EXPECT_EQ( modified.Values[2].sourceTimestamp, UADateTime{At(42)}.UA() );
		EXPECT_TRUE( modified.Values[2].hasServerTimestamp );//the original, as collected.
		let user = GetSchema().Authorizer->UserName( AppClient()->UserPK() );
		for( let& info : modified.Infos ){
			EXPECT_EQ( info.User, user );
			EXPECT_GE( info.Time, before );
			EXPECT_LE( info.Time, UA_DateTime_now() );
		}
		EXPECT_EQ( ReadModified(Rpm4, {.Start=At(45), .End=At(46)}).Status, UA_STATUSCODE_GOODNODATA );
		let paged = ReadModified( Rpm4, {.Start=At(47), .End=At(40), .Limit=3} );//in reverse, three a page.
		EXPECT_EQ( doubles(paged.Values), (vector<double>{400, 400, 300, 250, 200, 150, 150}) );
		EXPECT_GT( History().Edits().Count, 0u );
	}

	//An edit needs Update, a delete Delete, granted on a resource that is enforced:  a stranger is refused whichever way
	//the node answers reads, and on an open server, where reads answer All, the edits answer None and UserAccessLevel
	//leaves HistoryWrite out, so a client sees the history as read-only before it tries (spec *Authorization*, #237).
	TEST_F( HistoryTests, EditsNeedAnEnforcedGrant ){
		auto& authorizer = static_cast<OpcAuthorize&>( *GetSchema().Authorizer );
		let node = Node( Rpm1 );
		const UserPK stranger{ 0x7FFF'FFF0 };
		UAAccess::SessionContext session{ "", TimePoint::max(), 0, stranger };
		EXPECT_EQ( UpdateCallback(Rpm1, &session, 1, At(50)).Status, UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_EQ( DeleteCallback(Rpm1, &session, At(40), At(50)), UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_EQ( UpdateCallback(Rpm1, nullptr, 1, At(50)).Status, UA_STATUSCODE_BADUSERACCESSDENIED );//no session.
		EXPECT_FALSE( UAAccess::GetUserAccessLevel(GetUAServer().Ptr(), nullptr, nullptr, &session, &node, nullptr) & UA_ACCESSLEVELMASK_HISTORYWRITE );
		UAAccess::SessionContext own{ "", TimePoint::max(), 0, AppClient()->UserPK() };
		EXPECT_TRUE( UAAccess::GetUserAccessLevel(GetUAServer().Ptr(), nullptr, nullptr, &own, &node, nullptr) & UA_ACCESSLEVELMASK_HISTORYWRITE );
		let inserted = UpdateCallback( Rpm1, &own, 1, At(50) );
		EXPECT_EQ( inserted.Status, UA_STATUSCODE_GOOD );
		EXPECT_EQ( inserted.Results, vector<UA_StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED} );//the value went through, not only the entry.
		EXPECT_EQ( DeleteCallback(Rpm1, &own, At(50), At(51)), UA_STATUSCODE_GOOD );

		//A server nothing enforces:  every node open to reads, and no edit.
		OpcAuthorize open{ "opc.open" };
		EXPECT_EQ( open.NodeRights(node, stranger), Access::ERights::All );
		EXPECT_EQ( open.EditRights(node, stranger), Access::ERights::None );
		EXPECT_EQ( underlying(open.UserRights(node, stranger)), underlying(EAccess::All) & ~underlying(EAccess::HistoryWrite) );
		EXPECT_EQ( underlying(authorizer.UserRights(node, AppClient()->UserPK())), underlying(EAccess::All) );
	}

	//A nodeset that gives a historized variable HistoryRead without HistoryWrite made its history read-only:  load keeps
	//it so, a client sees it so, and an edit of it is refused whatever the user holds.  Its history still reads.
	TEST_F( HistoryTests, KeepsANodesetsReadOnlyHistory ){
		let node = ReadOnly();
		ASSERT_TRUE( History().Find(node) );
		constexpr UA_Byte readOnly{ UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_HISTORYREAD };
		UA_Byte level{};
		ASSERT_EQ( UA_Server_readAccessLevel(GetUAServer().Ptr(), node, &level), UA_STATUSCODE_GOOD );
		EXPECT_EQ( level, readOnly );
		UA_Byte userLevel{};
		ASSERT_EQ( UA_Client_readUserAccessLevelAttribute(_client, node, &userLevel), UA_STATUSCODE_GOOD );
		EXPECT_EQ( userLevel, readOnly );//this program's user holds All on the enforced root.
		auto v = Sample( 1, At(30) );
		UA_UpdateDataDetails insert; UA_UpdateDataDetails_init( &insert );
		insert.nodeId = node;
		insert.performInsertReplace = UA_PERFORMUPDATETYPE_INSERT;
		insert.updateValuesSize = 1;
		insert.updateValues = &v;
		EXPECT_EQ( HistoryUpdate(&insert, UA_TYPES_UPDATEDATADETAILS).Status, UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_EQ( UA_Client_HistoryUpdate_deleteRaw(_client, &node, UADateTime{At(0)}.UA(), UADateTime{At(60)}.UA()), UA_STATUSCODE_BADUSERACCESSDENIED );
		let read = Read( node, {.Start=At(0), .End=Clock::now()} ).Status;
		EXPECT_TRUE( UA_StatusCode_isGood(read) ) << UA_StatusCode_name( read );
	}

	//An edit's flush resumes on the executor, where work waiting on the service lock the edit holds can take every
	//thread:  the edit answers Bad_Timeout at hist.editTimeout rather than hold the lock for good, and is written once a
	//thread frees.
	TEST_F( HistoryTests, EditGivesUpOnAStarvedExecutor ){
		let timeout = Settings::FindDuration( "/opcServer/hist/editTimeout" );
		ASSERT_TRUE( timeout );
		let threads = Settings::FindNumber<uint>( "/workers/executor/threads" ).value_or( std::thread::hardware_concurrency() );
		UAAccess::SessionContext own{ "", TimePoint::max(), 0, AppClient()->UserPK() };
		Updated updated;
		steady_clock::duration waited;
		{
			auto release = ms<absl::Notification>();
			absl::Cleanup freed = [release]{ release->Notify(); };
			auto taken = ms<std::atomic<uint>>( 0 );
			for( uint i=0; i<threads; ++i )
				Post( [release, taken]{ ++*taken; release->WaitForNotification(); } );
			for( uint i=0; *taken<threads; ++i ){
				ASSERT_LT( i, 500u ) << "The executor's threads weren't all taken.";
				std::this_thread::sleep_for( 10ms );
			}
			let start = steady_clock::now();
			updated = UpdateCallback( Rpm4, &own, 555, At(55) );
			waited = steady_clock::now()-start;
		}
		EXPECT_EQ( updated.Status, UA_STATUSCODE_BADTIMEOUT ) << UA_StatusCode_name( updated.Status );
		EXPECT_EQ( updated.Results, vector<UA_StatusCode>{UA_STATUSCODE_BADTIMEOUT} );//an entry refused whole, its value too (#3).
		EXPECT_GE( waited, *timeout );
		vector<double> written;
		for( uint i=0; i<500 && written.empty(); ++i ){
			written = doubles( all(Read(Rpm4, {.Start=At(55), .End=At(56)}).Values) );
			if( written.empty() )
				std::this_thread::sleep_for( 10ms );
		}
		EXPECT_EQ( written, vector<double>{555} );
	}

	//Read on the node is the right, asked of the session's user for each node in every mode:  never the collector's.
	TEST_F( HistoryTests, ReadNeedsReadOnTheNode ){
		let status = []( UserPK user, bool atTime ){
			UAAccess::SessionContext session{ "", TimePoint::max(), 0, user };
			return atTime ? AtTimeCallback( Rpm4, &session, At(0) ) : Callback( Rpm4, &session, At(0), At(10) );
		};
		const UserPK stranger{ 0x7FFF'FFF0 };
		for( let atTime : {false, true} ){
			EXPECT_TRUE( UA_StatusCode_isGood(status(AppClient()->UserPK(), atTime)) ) << atTime;
			EXPECT_EQ( status(stranger, atTime), UA_STATUSCODE_BADUSERACCESSDENIED ) << atTime;//on the root the fixture enforces.
		}
		EXPECT_EQ( Callback(Rpm4, nullptr, At(0), At(10)), UA_STATUSCODE_BADUSERACCESSDENIED );//no session.
		EXPECT_EQ( AtTimeCallback(Rpm4, nullptr, At(0)), UA_STATUSCODE_BADUSERACCESSDENIED );
	}

	//A start after a stop:  the files give each node its index back, a node the nodesets no longer historize is removed,
	//and each node's value now is its first after the break.  A suite of its own, with its own server and files, since
	//it replaces the server and drops the client.
	struct HistoryRestartTests : HistoryTests{};
	TEST_F( HistoryRestartTests, KeepsIndexesAndRemovesWhatIsNoLongerHistorized ){
		Disconnect();
		let rpm4 = Index( Rpm4 ), manual = Index( RpmManual );
		ASSERT_TRUE( rpm4 && manual );
		let wrote = floor<microseconds>( Clock::now() );
		Write( Rpm4, 500, wrote );
		Write( RpmManual, 77, wrote );

		std::ifstream in{ nodeset(), std::ios::binary };
		string xml{ std::istreambuf_iterator<char>{in}, {} };
		let marked = "ParentNodeId=\"ns=1;i=5005\" DataType=\"Double\" AccessLevel=\"3\" Historizing=\"true\""sv;
		let at = xml.find( marked );
		ASSERT_NE( at, string::npos );
		xml.erase( at+marked.size()-sizeof("Historizing=\"true\""), sizeof("Historizing=\"true\"") );
		let edited = fs::temp_directory_path()/"pumps.unhistorized.NodeSet2.xml";
		std::ofstream{ edited, std::ios::binary | std::ios::trunc } << xml;
		Start( edited );
		fs::remove( edited );

		EXPECT_EQ( Index(Rpm4), rpm4 );
		EXPECT_FALSE( Index(RpmManual) );
		bool removed{};
		for( let& record : History().Group()->Buffer() ){
			if( let p = get_if<Hist::NodeRemoved>(&record) )
				removed = removed || p->Index==*manual;
		}
		EXPECT_TRUE( removed );
		//What the stop wrote, then the gap it left, then the nodeset's value, which the server starts from.
		let page = History().Group()->Read( {.Nodes={*rpm4}, .Start=UADateTime{wrote}.UA(), .End=UA_DateTime_now()} );
		ASSERT_GE( page.Values.size(), 3u );
		EXPECT_EQ( page.Values[0].Value.value().double_value(), 500 );
		EXPECT_EQ( page.Values[1].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_EQ( page.Values[2].Value.value().double_value(), 0 );
		EXPECT_EQ( Variant(Property(Node(Rpm4), {"HA Configuration", "StartOfArchive"})).Get<UA_DateTime>(0), UADateTime{floor<days>(Clock::now())}.UA() );
	}
}