#include "FlowServer.h"
#include <jde/fwk/co/BlockAwait.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/opc/server/INodeAccess.h>
#include <jde/flows/Props.h>
#include "../GatewayAppClient.h"
#include "../types/ServerCnnctn.h"
#include "../drivers/DriverServer.h"
#include "../ql/GatewayQL.h"
#include "FlowHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	constexpr ELogTags _tags{ (ELogTags)EOpcLogTags::Server };
	constexpr Duration _closeWait{ 2s };//how long a stop waits for the sessions its clients are closing.
	constexpr sv _flowObject{ "_Flow" };
	constexpr uint MaxName{ 128 };
	namespace Prop = Flows::Prop;
	using Flows::ENodeType;

	//Browse and reads open to every session, #237's posture, and nothing written, edited or called:  a variable is its flow's.
	struct FlowNodeAccess final : Opc::Server::INodeAccess{
		α NodeRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::Read; }
		α EditRights( const NodeId&, UserPK )ι->Access::ERights override{ return Access::ERights::None; }
		α MayBrowse( const NodeId&, UserPK )ι->bool override{ return true; }
		α UserRights( const NodeId&, UserPK )ι->Opc::Server::EAccess override{ return Opc::Server::EAccess::Read; }
	};

	//A built-in type a flow's json is read toward (Value::Set), by its name, whatever its case.
	Ω builtIn( sv name )ι->const UA_DataType*{
		for( uint i=UA_TYPES_BOOLEAN; i<=UA_TYPES_LOCALIZEDTEXT; ++i ){
			if( let& type = UA_TYPES[i]; Str::ToLower(string{type.typeName})==Str::ToLower(string{name}) && Value::Settable(type) )
				return &type;
		}
		return nullptr;
	}
	FlowVariable::FlowVariable( const jobject& props, SL sl )ε:
		Name{ Prop::Text(props, "name") },
		Description{ Prop::Text(props, "description", {}) }
	{
		THROW_IFSL( Name.size()>MaxName, "`name` is {} characters, over the {} a variable's takes.", Name.size(), MaxName );
		THROW_IFSL( Name.contains('/'), "`name` '{}' holds a '/', which a node's path is split by.", Name );
		THROW_IFSL( Name==_flowObject, "`name` '{}' is the folder's own object.", Name );
		THROW_IFSL( std::ranges::any_of(Name, []( char c ){ return (unsigned char)c<0x20; }), "`name` holds a control character." );
		auto type = Prop::Text( props, "dataType" );
		Array = type.ends_with( "[]" );
		if( Array )
			type.resize( type.size()-2 );
		Type = builtIn( type );
		THROW_IFSL( !Type, "`dataType` '{}' is no built-in type a flow writes, as Double, Int32, Boolean, String and DateTime are, or an array of one, `Double[]`.", type );
		if( let units = Prop::Find(props, "engineeringUnits") ){
			if( let text = units->if_string() ){
				THROW_IFSL( text->empty(), "`engineeringUnits` must be the unit's text, or an object of displayName, description, unitId and namespaceUri." );
				Units = EUInformation{ .DisplayName=string{*text} };
			}
			else{
				THROW_IFSL( !units->is_object(), "`engineeringUnits` must be the unit's text, or an object of displayName, description, unitId and namespaceUri." );
				let& o = units->get_object();
				boost::system::error_code ec;
				let id = Prop::Find( o, "unitId" );
				Units = EUInformation{ Prop::Text(o, "namespaceUri", {}), id ? id->to_number<int32_t>(ec) : -1, Prop::Text(o, "displayName", {}), Prop::Text(o, "description", {}) };
				THROW_IFSL( ec, "`engineeringUnits`' unitId must be a whole number." );
			}
		}
		if( let range = Prop::Find(props, "euRange") ){
			THROW_IFSL( !range->is_object(), "`euRange` must be an object of low and high." );
			Range = EURange{ Prop::Number(range->get_object(), "low"), Prop::Number(range->get_object(), "high") };
			THROW_IFSL( !(Range->Low<Range->High), "`euRange`'s low must be under its high." );
		}
	}

	static sp<FlowServer> _instance; static std::mutex _instanceMutex;
	α FlowServer::Instance()ι->sp<FlowServer>{ lg _{ _instanceMutex }; return _instance; }
	α FlowServer::NamespaceUri()ι->string{ return Ƒ( "urn:jde:{}:flows", ServerCnnctn::FlowSlug ); }
	Ω stringId( string&& id )ι->NodeId{ return NodeId{ UA_NODEID_STRING_ALLOC(ServerCnnctn::DriverBrowseNs, id.c_str()) }; }
	α FlowServer::FolderId( sv flow )ι->NodeId{ return stringId( string{flow} ); }
	α FlowServer::VariableId( sv flow, sv name )ι->NodeId{ return stringId( Ƒ("{}/{}", flow, name) ); }
	α FlowServer::FlowId( sv flow, sv name )ι->NodeId{ return stringId( Ƒ("{}/{}/{}", flow, _flowObject, name) ); }
	α FlowServer::NodeText( sv flow, sv name )ι->string{
		return HistNodeText( ExNodeId{jvalue{jobject{{"nsu", NamespaceUri()}, {"s", Ƒ("{}/{}", flow, name)}}}} );
	}
	α FlowServer::Read( const Flows::Graph& graph )ι->std::expected<vector<FlowVariable>,Flows::Fault>{
		vector<FlowVariable> y;
		flat_map<string,string> named;//a name's node.
		for( let& node : graph.Nodes ){
			if( node.Type!=ENodeType::Variable )
				continue;
			try{
				y.emplace_back( node.Props );
			}
			catch( const std::exception& e ){
				return std::unexpected{ Flows::Fault{node.Id, e.what()} };
			}
			if( let [first, added] = named.try_emplace(y.back().Name, node.Id); !added )
				return std::unexpected{ Flows::Fault{node.Id, Ƒ("Variable '{}' is node '{}'s:  a variable has one node, which every wire to it may end at.", y.back().Name, first->second)} };
		}
		return y;
	}

	//The row (spec *Derived variables*), through the QL as the gateway's own, so the save's rules, the provider row and the
	//live side run as for any connection.
	Ω ensureRow()ε->void{
		let url = ServerCnnctn::FlowUrl();
		let rows = BlockAwait<ServerCnnctnAwait,vector<ServerCnnctn>>( ServerCnnctnAwait{DB::Key{string{ServerCnnctn::FlowSlug}}, true} );
		if( rows.empty() ){
			try{
				QL().QuerySync<jvalue>( "mutation createServerConnection( slug:$slug, name:$name, kind:$kind, url:$url, isDefault:false, description:$description ){ id }",
					{{"slug", ServerCnnctn::FlowSlug}, {"name", "Flows"}, {"kind", ServerCnnctn::KindFlow}, {"url", url}, {"description", "The gateway's flow server:  its flows' derived variables."}}, {UserPK::System} );
				INFO( "Connection '{}' made for the flow server, {}.", ServerCnnctn::FlowSlug, url );
			}
			catch( const std::exception& e ){//the server runs on:  no client reaches it until a start makes the row.
				ERR( "Connection '{}' could not be made for the flow server, so no client reaches the flows' variables until the gateway starts again:  {}", ServerCnnctn::FlowSlug, e.what() );
			}
			return;
		}
		let& row = rows.front();
		if( row.Kind!=ServerCnnctn::KindFlow ){//a row from before the kind's rule kept the slug.
			ERR( "Connection '{}' is of kind '{}', not the flow server's:  no client reaches the flows' variables until it is purged and the gateway started again.", row.Slug, row.Kind.empty() ? ServerCnnctn::KindUa : sv{row.Kind} );
			return;
		}
		if( row.Url!=url ){
			try{
				QL().QuerySync<jvalue>( "mutation updateServerConnection( id:$id, url:$url )", {{"id", row.Id}, {"url", url}}, {UserPK::System} );
				INFO( "Connection '{}':  its url is now the flow server's, {}.", row.Slug, url );
			}
			catch( const std::exception& e ){
				ERR( "Connection '{}' keeps url {}, not the flow server's, {}, so no client reaches the flows' variables until the gateway starts again:  {}", row.Slug, row.Url, url, e.what() );
			}
		}
		if( row.Deleted )
			WARN( "Connection '{}' is deleted:  no client reaches the flows' variables until it is restored.", row.Slug );
	}

	FlowServer::FlowServer()ε:
		_port{ ServerCnnctn::FlowPort() }
	{
		const ServerCnnctnNK slug{ ServerCnnctn::FlowSlug };
		_server = mu<Opc::Server::UAServer>( DriverServer::Settings("Flows", slug, _port), AppClient(), ms<FlowNodeAccess>(), nullptr );
		let ns = UA_Server_addNamespace( _server->Ptr(), NamespaceUri().c_str() );
		THROW_IF( ns!=ServerCnnctn::DriverBrowseNs, "its namespace is {}, not {}.", ns, ServerCnnctn::DriverBrowseNs );
	}
	α FlowServer::Wanted()ι->bool{ return Settings::FindObject( "/gateway/flows" )!=nullptr; }
	α FlowServer::Start()ε->void{
		if( !Wanted() )
			return;
		ensureRow();
		auto rows = BlockAwait( SelectFlows({.IncludeDeleted=true}) );
		Listen( move(rows), DS()->Select(FlowSql::CurrentGraphs()), false );
	}
	α FlowServer::Listen( vector<FlowRow> rows, vector<DB::Row> graphs, bool again )ι->bool{
		sp<FlowServer> server;
		bool made{};
		try{
			server = sp<FlowServer>{ new FlowServer{} };
			auto& ua = *server->_server->Ptr();
			uint variables{};
			for( auto& graph : graphs ){
				let id = graph.Get<uint32>( 0 );
				let row = std::ranges::find( rows, id, &FlowRow::Id );
				if( row==rows.end() )
					continue;
				try{
					let read = Flows::Graph::Read( Json::ParseValue(graph.TakeString(1)), true, nullopt );
					auto found = read ? Read( *read ) : std::expected<vector<FlowVariable>,Flows::Fault>{ std::unexpect, read.error() };
					if( found && found->size() ){
						variables += found->size();
						server->Apply( ua, id, row->Slug, row->Version.value_or(0), row->Enabled && !row->Deleted, move(*found) );
					}
					if( !found )
						WARN( "[{}]The flow's variables are not served:  node '{}':  {}", row->Slug, found.error().NodeId, found.error().Message );
				}
				catch( const std::exception& e ){
					WARN( "[{}]The flow's variables are not served:  its graph does not read:  {}", row->Slug, e.what() );
				}
			}
			made = true;
			server->_server->Run();//fails where it cannot listen.
			server->_sessions = server->_server->Sessions();
			INFOT( ELogTags::App, "The flow server listens on 127.0.0.1:{} - {} folder(s), {} variable(s).", server->_port, server->_folders.size(), variables );
		}
		catch( const std::exception& e ){//the first an Error, each try after it a line of the host's retry.
			LOG( again ? ELogLevel::Debug : ELogLevel::Error, _tags, "The flows have no server for their variables{}:  it could not {} 127.0.0.1:{} - /gateway/flows/server/port:  {}", again ? " yet" : ", and it is tried again", made ? "listen on a port something else may hold," : "be made for", ServerCnnctn::FlowPort(), e.what() );
			return false;
		}
		lg _{ _instanceMutex };
		_instance = move( server );
		return true;
	}
	α FlowServer::Shutdown( bool terminate )ι->void{
		sp<FlowServer> server;
		{
			lg _{ _instanceMutex };
			server = move( _instance );
		}
		if( !server )
			return;
		{
			lg _{ server->_mutex };
			server->_stopping = true;
		}
		//The clients were told to go before this:  each closes its session on a live server, as a UA client should.
		if( !terminate ){
			let deadline = steady_clock::now()+_closeWait;
			while( server->Sessions().size() && steady_clock::now()<deadline )
				std::this_thread::sleep_for( 5ms );
			if( let left = server->Sessions().size(); left )
				WARN( "{} session(s) of the gateway's own clients were still open when the flow server stopped.", left );
		}
		server->_server.reset();//what was posted runs first.
		INFO( "The flow server on 127.0.0.1:{} stopped.", server->_port );
	}

	α FlowServer::Post( absl::AnyInvocable<void( UA_Server& )> f )ι->void{
		lg _{ _mutex };
		if( !_stopping )
			Opc::Server::UAServer::Post( *_server->Ptr(), std::move(f) );
	}
	α FlowServer::Deployed( FlowPK id, string slug, uint32 version, bool enabled, vector<FlowVariable> variables )ι->void{
		Post( [self=shared_from_this(), id, slug=move(slug), version, enabled, variables=move(variables)]( UA_Server& ua )mutable{ self->Apply( ua, id, slug, version, enabled, move(variables) ); } );
	}
	α FlowServer::Enabled( string slug, bool enabled )ι->void{
		Post( [self=shared_from_this(), slug=move(slug), enabled]( UA_Server& ua ){
			if( let p = self->_folders.find(slug); p!=self->_folders.end() && p->second.Linked )
				self->SetFlow( ua, slug, nullopt, enabled );
		} );
	}
	α FlowServer::Purged( string slug )ι->void{
		Post( [self=shared_from_this(), slug=move(slug)]( UA_Server& ua ){ self->Apply( ua, 0, slug, 0, false, {} ); } );
	}
	FlowServer::Target::Target( string slug, string name )ι:
		Id{ VariableId(slug, name) }, Slug{ move(slug) }, Name{ move(name) }
	{}
	α FlowServer::Write( sp<const Target> target, Value value )ι->void{
		++_writes;
		Post( [target=move(target), value=move(value)]( UA_Server& ua ){
			let sc = UA_Server_writeDataValue( &ua, target->Id, value );
			if( !sc && target->Failing ){
				INFO( "[{}]Variable '{}' is written again, after {} more write(s) failed.", target->Slug, target->Name, target->Failing-1 );
				target->Failing = 0;
			}
			else if( sc && !target->Failing++ )
				WARN( "[{}]Variable '{}' could not be written:  {}.  A write that fails after it is counted, until one succeeds.", target->Slug, target->Name, UAException::Message(sc) );
		} );
	}

	//Attributes are views into the strings for the add, which copies them.
	Ω text( sv s )ι->UA_LocalizedText{ return UA_LocalizedText{ {}, ToUV(s) }; }
	Ω qualified( sv s, NsIndex ns=ServerCnnctn::DriverBrowseNs )ι->UA_QualifiedName{ return UA_QualifiedName{ ns, ToUV(s) }; }
	Ω organizes()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_ORGANIZES ); }
	Ω hasComponent()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_HASCOMPONENT ); }
	Ω hasProperty()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_HASPROPERTY ); }
	Ω variableType()ι->UA_NodeId{ return UA_NODEID_NUMERIC( 0, UA_NS0ID_BASEDATAVARIABLETYPE ); }
	constexpr std::array<sv,2> _properties{ "EngineeringUnits", "EURange" };
	Ω propertyId( sv flow, sv name, uint property )ι->NodeId{ return stringId( Ƒ("{}/{}/{}", flow, name, _properties[property]) ); }
	//The reference from `parent` to `child` made or taken, both ways:  how a node joins the address space and leaves it.
	Ω link( UA_Server& ua, const NodeId& parent, UA_NodeId type, const NodeId& child, bool on )ε->void{
		const UA_ExpandedNodeId target{ child, UA_STRING_NULL, 0 };//a view.
		UAε( on ? UA_Server_addReference(&ua, parent, type, target, true) : UA_Server_deleteReference(&ua, parent, type, true, target, true) );
	}
	α FlowServer::Zero( const FlowVariable& variable, StatusCode status )ι->Value{
		UA_DataValue zero; UA_DataValue_init( &zero );
		if( variable.Array )
			UA_Variant_setArray( &zero.value, UA_EMPTY_ARRAY_SENTINEL, 0, variable.Type );
		else
			UA_Variant_setScalar( &zero.value, UA_new(variable.Type), variable.Type );
		zero.hasValue = true;
		zero.status = status;
		zero.hasStatus = true;
		return Value{ move(zero) };//which it now owns.
	}
	Ω unset( UA_Server& ua, const NodeId& id, const FlowVariable& variable, StatusCode status )ε->void{
		auto value = FlowServer::Zero( variable, status );
		value.sourceTimestamp = UA_DateTime_now();
		value.hasSourceTimestamp = true;
		UAε( UA_Server_writeDataValue(&ua, id, value) );
	}
	Ω rank( const FlowVariable& variable )ι->UA_Int32{ return variable.Array ? UA_VALUERANK_ONE_OR_MORE_DIMENSIONS : UA_VALUERANK_SCALAR; }
	//`_Flow`'s Messages and Errors, the host's figures as they are asked for:  under the service lock, so a lookup alone.
	Ω readTotal( UA_Server*, const UA_NodeId*, void*, const UA_NodeId* nodeId, void* nodeContext, UA_Boolean sourceTime, const UA_NumericRange*, UA_DataValue* value )ι->UA_StatusCode{
		if( !nodeId || !value || nodeId->identifierType!=UA_NODEIDTYPE_STRING )
			return UA_STATUSCODE_BADINTERNALERROR;
		let totals = FlowHost::Instance().Totals( (FlowPK)reinterpret_cast<uintptr_t>(nodeContext) );
		const UA_UInt64 total = ToSV( nodeId->identifier.string ).ends_with( "/Errors" ) ? totals.Errors : totals.Messages;
		if( let sc = UA_Variant_setScalarCopy(&value->value, &total, &UA_TYPES[UA_TYPES_UINT64]); sc )
			return sc;
		value->hasValue = true;
		if( sourceTime ){
			value->sourceTimestamp = UA_DateTime_now();
			value->hasSourceTimestamp = true;
		}
		return UA_STATUSCODE_GOOD;
	}
	//A node made already, by an add of the folder that failed after it, is taken as made:  the next try finishes the folder.
	Ω made( UA_StatusCode status )ε->void{
		if( status && status!=UA_STATUSCODE_BADNODEIDEXISTS )
			throw UAException{ status };
	}
	α FlowServer::AddFolder( UA_Server& ua, FlowPK id, str slug )ε->void{
		UA_ObjectAttributes folder = UA_ObjectAttributes_default;
		folder.displayName = text( slug );
		made( UA_Server_addObjectNode(&ua, FolderId(slug), NodeId::ObjectsFolder(), organizes(), qualified(slug), UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE), folder, nullptr, nullptr) );
		UA_ObjectAttributes object = UA_ObjectAttributes_default;
		object.displayName = text( _flowObject );
		made( UA_Server_addObjectNode(&ua, VariableId(slug, _flowObject), FolderId(slug), hasComponent(), qualified(_flowObject), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE), object, nullptr, nullptr) );
		let attributes = [&]( sv name, const UA_DataType& type ){
			UA_VariableAttributes y = UA_VariableAttributes_default;
			y.displayName = text( name );
			y.dataType = type.typeId;
			y.valueRank = UA_VALUERANK_SCALAR;
			y.accessLevel = UA_ACCESSLEVELMASK_READ;
			return y;
		};
		for( let& [name, type] : {std::pair{"Version"sv, UA_TYPES_UINT32}, std::pair{"Enabled"sv, UA_TYPES_BOOLEAN}} )
			made( UA_Server_addVariableNode(&ua, FlowId(slug, name), VariableId(slug, _flowObject), hasComponent(), qualified(name), variableType(), attributes(name, UA_TYPES[type]), nullptr, nullptr) );
		for( let name : {"Messages"sv, "Errors"sv} )
			made( UA_Server_addCallbackValueSourceVariableNode(&ua, FlowId(slug, name), VariableId(slug, _flowObject), hasComponent(), qualified(name), variableType(), attributes(name, UA_TYPES[UA_TYPES_UINT64]), UA_CallbackValueSource{.read=readTotal, .write=nullptr}, reinterpret_cast<void*>(uintptr_t{id}), nullptr) );
	}
	//Its type's zero, or an array of none, with Bad_WaitingForInitialData, until its flow writes it.  AccessLevel CurrentRead,
	//and the defaults:  WriteMask 0, MinimumSamplingInterval 0, so an item at 0 hears each write.  An array's ValueRank is
	//OneOrMoreDimensions, which takes no ArrayDimensions, so a type may change from a scalar and back in place (Retype).
	α FlowServer::AddVariable( UA_Server& ua, str slug, const FlowVariable& variable )ε->void{
		let id = VariableId( slug, variable.Name );
		let zero = Zero( variable, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
		UA_VariableAttributes attributes = UA_VariableAttributes_default;
		attributes.displayName = text( variable.Name );
		attributes.description = text( variable.Description );
		attributes.dataType = variable.Type->typeId;
		attributes.valueRank = rank( variable );
		attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
		attributes.value = zero.value;//a view.
		UAε( UA_Server_addVariableNode(&ua, id, FolderId(slug), organizes(), qualified(variable.Name), variableType(), attributes, nullptr, nullptr) );
		unset( ua, id, variable, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
	}
	//The node as another type, in place:  through BaseDataType, which any value fits and which takes none, since open62541
	//holds a DataType and a ValueRank to the value there is and refuses a number's node a value of none.
	α FlowServer::Retype( UA_Server& ua, str slug, const FlowVariable& variable )ε->void{
		let id = VariableId( slug, variable.Name );
		UAε( UA_Server_writeDataType(&ua, id, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE)) );
		UA_DataValue none; UA_DataValue_init( &none );
		none.status = UA_STATUSCODE_BADWAITINGFORINITIALDATA;
		none.hasStatus = true;
		UAε( UA_Server_writeDataValue(&ua, id, none) );
		UAε( UA_Server_writeValueRank(&ua, id, rank(variable)) );
		UAε( UA_Server_writeDataType(&ua, id, variable.Type->typeId) );
		unset( ua, id, variable, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
	}
	α FlowServer::SetProperties( UA_Server& ua, str slug, Held& held, const FlowVariable& variable )ε->void{
		let parent = VariableId( slug, variable.Name );
		let set = [&]( uint property, const UA_DataType& type, const void* value ){
			let id = propertyId( slug, variable.Name, property );
			auto& state = held.Properties[property];
			UA_Variant variant; UA_Variant_setScalar( &variant, const_cast<void*>(value), &type );
			if( !value ){
				if( state==EProperty::Linked ){
					link( ua, parent, hasProperty(), id, false );
					state = EProperty::Unlinked;
				}
			}
			else if( state==EProperty::None ){
				UA_VariableAttributes attributes = UA_VariableAttributes_default;
				attributes.displayName = text( _properties[property] );
				attributes.dataType = type.typeId;
				attributes.valueRank = UA_VALUERANK_SCALAR;
				attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
				attributes.value = variant;
				UAε( UA_Server_addVariableNode(&ua, id, parent, hasProperty(), qualified(_properties[property], 0), UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE), attributes, nullptr, nullptr) );
				state = EProperty::Linked;
			}
			else{
				UAε( UA_Server_writeValue(&ua, id, variant) );
				if( state==EProperty::Unlinked )
					link( ua, parent, hasProperty(), id, true );
				state = EProperty::Linked;
			}
		};
		let& units = variable.Units;
		const UA_EUInformation information{ ToUV(units ? sv{units->NamespaceUri} : sv{}), units ? (UA_Int32)units->UnitId : -1, text(units ? sv{units->DisplayName} : sv{}), text(units ? sv{units->Description} : sv{}) };
		set( 0, UA_TYPES[UA_TYPES_EUINFORMATION], units ? &information : nullptr );
		const UA_Range range{ variable.Range ? variable.Range->Low : 0, variable.Range ? variable.Range->High : 0 };
		set( 1, UA_TYPES[UA_TYPES_RANGE], variable.Range ? &range : nullptr );
	}
	α FlowServer::Drop( UA_Server& ua, str slug, Held& held )ι->void{
		try{
			let id = VariableId( slug, held.Variable.Name );
			held.Linked = false;
			link( ua, FolderId(slug), organizes(), id, false );
			unset( ua, id, held.Variable, UA_STATUSCODE_BADOBJECTDELETED );
		}
		catch( const std::exception& e ){
			WARN( "[{}]Variable '{}' could not be taken from its folder:  {}", slug, held.Variable.Name, e.what() );
		}
	}
	α FlowServer::SetFlow( UA_Server& ua, str slug, optional<uint32> version, bool enabled )ι->void{
		let write = [&]( sv name, const void* scalar, const UA_DataType& type ){
			UA_Variant value; UA_Variant_setScalar( &value, const_cast<void*>(scalar), &type );
			if( let sc = UA_Server_writeValue(&ua, FlowId(slug, name), value); sc )
				WARN( "[{}]Could not write {}.{}:  {}", slug, _flowObject, name, UAException::Message(sc) );
		};
		if( const UA_UInt32 v = version.value_or(0); version )
			write( "Version", &v, UA_TYPES[UA_TYPES_UINT32] );
		const UA_Boolean on{ enabled };
		write( "Enabled", &on, UA_TYPES[UA_TYPES_BOOLEAN] );
	}
	α FlowServer::Apply( UA_Server& ua, FlowPK id, str slug, uint32 version, bool enabled, vector<FlowVariable>&& variables )ι->void{
		auto found = _folders.find( slug );
		try{
			if( variables.empty() ){
				if( found==_folders.end() || !found->second.Linked )
					return;
				auto& folder = found->second;
				folder.Linked = false;
				link( ua, NodeId::ObjectsFolder(), organizes(), FolderId(slug), false );
				for( auto&& [_, held] : folder.Variables ){
					if( held.Linked )
						Drop( ua, slug, held );
				}
				INFO( "[{}]The flow server serves the flow no more:  it has no variable.", slug );
				return;
			}
			if( found==_folders.end() ){
				AddFolder( ua, id, slug );
				found = _folders.try_emplace( slug, Folder{id, true, {}} ).first;
			}
			auto& folder = found->second;
			if( !folder.Linked ){
				link( ua, NodeId::ObjectsFolder(), organizes(), FolderId(slug), true );
				folder.Linked = true;
			}
			if( folder.Id!=id ){//a flow made under a purged one's slug.
				for( let name : {"Messages"sv, "Errors"sv} )
					UAε( UA_Server_setNodeContext(&ua, FlowId(slug, name), reinterpret_cast<void*>(uintptr_t{id})) );
				folder.Id = id;
			}
		}
		catch( const std::exception& e ){
			ERR( "[{}]The flow's folder could not be served:  {}", slug, e.what() );
			return;
		}
		auto& folder = found->second;
		for( auto&& [name, held] : folder.Variables ){
			if( held.Linked && std::ranges::find(variables, name, &FlowVariable::Name)==variables.end() )
				Drop( ua, slug, held );
		}
		for( auto& variable : variables ){//each on its own:  one that fails leaves the rest served.
			try{
				let node = VariableId( slug, variable.Name );
				auto p = folder.Variables.find( variable.Name );
				if( p==folder.Variables.end() ){
					AddVariable( ua, slug, variable );
					p = folder.Variables.try_emplace( variable.Name, Held{variable, true, {}} ).first;
				}
				auto& held = p->second;
				let retyped = held.Variable.Type!=variable.Type || held.Variable.Array!=variable.Array;
				if( retyped )
					Retype( ua, slug, variable );
				if( held.Variable.Description!=variable.Description )
					UAε( UA_Server_writeDescription(&ua, node, text(variable.Description)) );
				SetProperties( ua, slug, held, variable );
				if( !held.Linked ){
					link( ua, FolderId(slug), organizes(), node, true );
					if( !retyped )
						unset( ua, node, variable, UA_STATUSCODE_BADWAITINGFORINITIALDATA );
					held.Linked = true;
				}
				held.Variable = move( variable );
			}
			catch( const std::exception& e ){
				ERR( "[{}]Variable '{}' could not be served:  {}", slug, variable.Name, e.what() );
			}
		}
		SetFlow( ua, slug, version, enabled );
	}
}