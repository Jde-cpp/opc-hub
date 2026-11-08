#include <jde/opc/server/Condition.h>
#include <cstring>
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/server/UAAccess.h>

#define let const auto
namespace Jde::Opc::Server{
	constexpr ELogTags _tags = ( ELogTags )EOpcLogTags::Opc;
	constexpr UA_UInt16 RefreshSeverity{ 100 };//open62541's own for its RefreshStart and RefreshEnd.
	namespace{
		//The first target of `path` under `parent`, each name in namespace 0 as the type declares them, or none.
		Ω child( UA_Server& ua, const UA_NodeId& parent, std::initializer_list<const char*> path )ι->optional<NodeId>{
			vector<UA_QualifiedName> names;
			for( let name : path )
				names.push_back( UA_QUALIFIEDNAME(0, (char*)name) );
			auto result = UA_Server_browseSimplifiedBrowsePath( &ua, parent, names.size(), names.data() );
			optional<NodeId> y;
			if( result.statusCode==UA_STATUSCODE_GOOD && result.targetsSize )
				y.emplace( result.targets[0].targetId.nodeId );
			UA_BrowsePathResult_clear( &result );
			return y;
		}
		Ω join( std::initializer_list<const char*> path )ι->string{
			string y;
			for( let name : path )
				y += ( y.empty() ? "" : "/" )+string{ name };
			return y;
		}
		//The object's type, its HasTypeDefinition target:  AlarmConditionType for one that has none to browse.
		Ω typeOf( UA_Server& ua, const UA_NodeId& object )ι->NodeId{
			UA_BrowseDescription description; UA_BrowseDescription_init( &description );
			description.nodeId = object;//a view:  not cleared.
			description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
			description.referenceTypeId = UA_NODEID_NUMERIC( 0, UA_NS0ID_HASTYPEDEFINITION );
			description.nodeClassMask = UA_NODECLASS_OBJECTTYPE;
			description.resultMask = UA_BROWSERESULTMASK_NONE;
			auto result = UA_Server_browse( &ua, 1, &description );
			NodeId y = result.statusCode==UA_STATUSCODE_GOOD && result.referencesSize ? NodeId{ result.references[0].nodeId.nodeId } : NodeId{ UA_NS0ID_ALARMCONDITIONTYPE };
			UA_BrowseResult_clear( &result );
			return y;
		}
		Ω number( const UA_Variant& v )ι->optional<double>{
			if( !UA_Variant_isScalar(&v) || !v.type || !v.data )
				return {};
			return VisitNumeric( *v.type, [&]<class N>()->optional<double>{ return (double)*static_cast<const N*>( v.data ); }, []()->optional<double>{ return {}; } );
		}

		//The condition a call is on:  the method's context is the server's Conditions (Conditions::Bind), the object the instance.
		Ω conditionOf( void* methodContext, const UA_NodeId* objectId )ι->Condition*{
			auto conditions = static_cast<Conditions*>( methodContext );
			return conditions && objectId ? conditions->Find( *objectId ) : nullptr;
		}
		//Who a method's caller is, for ClientUserId, whose logic Part 9 leaves to the server:  the login the session's token names, open62541's
		//`clientUserId`;  an issued token names none, so the user it resolved to (UAAccess), and an anonymous session is nobody.
		Ω clientUser( UA_Server& ua, const UA_NodeId& session, const void* context )ι->string{
			UA_Variant v; UA_Variant_init( &v );//a view of the session's:  not cleared.
			if( UA_Server_getSessionAttribute(&ua, &session, UA_QUALIFIEDNAME(0, (char*)"clientUserId"), &v)==UA_STATUSCODE_GOOD && UA_Variant_hasScalarType(&v, &UA_TYPES[UA_TYPES_STRING]) && static_cast<const UA_String*>(v.data)->length )
				return ToString( *static_cast<const UA_String*>(v.data) );
			let ctx = static_cast<const UAAccess::SessionContext*>( context );
			return ctx && ctx->UserPK.Value ? std::to_string( ctx->UserPK.Value ) : string{};
		}
		//Acknowledge, Confirm and AddComment take the same two arguments, the EventId and a Comment, a LocalizedText.  The server has
		//type-checked them by here, so the shape check is a guard.  Nothing in these throws:  an exception would unwind through the server's C frames.
		template<auto Method>
		Ω commentMethod( UA_Server* ua, const UA_NodeId* sessionId, void* sessionContext, const UA_NodeId*, void* methodContext, const UA_NodeId* objectId, void*, uint inputSize, const UA_Variant* input, uint, UA_Variant* )ι->UA_StatusCode{
			auto condition = conditionOf( methodContext, objectId );
			if( !condition || !ua || !sessionId )
				return UA_STATUSCODE_BADNODEIDUNKNOWN;
			if( inputSize!=2 || !UA_Variant_hasScalarType(&input[0], &UA_TYPES[UA_TYPES_BYTESTRING]) || !UA_Variant_hasScalarType(&input[1], &UA_TYPES[UA_TYPES_LOCALIZEDTEXT]) )
				return UA_STATUSCODE_BADINVALIDARGUMENT;
			return (condition->*Method)( *static_cast<const UA_ByteString*>(input[0].data), *static_cast<const UA_LocalizedText*>(input[1].data), clientUser(*ua, *sessionId, sessionContext) );
		}
		Ω enable( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* methodContext, const UA_NodeId* objectId, void*, uint, const UA_Variant*, uint, UA_Variant* )ι->UA_StatusCode{
			return conditionOf( methodContext, objectId ) ? UA_STATUSCODE_BADCONDITIONALREADYENABLED : UA_STATUSCODE_BADNODEIDUNKNOWN;
		}
		Ω disable( UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* methodContext, const UA_NodeId* objectId, void*, uint, const UA_Variant*, uint, UA_Variant* )ι->UA_StatusCode{
			return conditionOf( methodContext, objectId ) ? UA_STATUSCODE_BADNOTSUPPORTED : UA_STATUSCODE_BADNODEIDUNKNOWN;
		}
		//Whether `subscription` is `session`'s, off the Server's SubscriptionDiagnosticsArray, which open62541 fills from its sessions whatever the
		//EnabledFlag (clients spec *Diagnostics*):  the public API has no other way to a subscription from a method callback.
		Ω owns( UA_Server& ua, const UA_NodeId& session, UA_UInt32 subscription )ι->bool{
			UA_Variant v; UA_Variant_init( &v );
			if( UA_Server_readValue(&ua, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERDIAGNOSTICS_SUBSCRIPTIONDIAGNOSTICSARRAY), &v) )
				return false;
			bool y{};
			if( UA_Variant_hasArrayType(&v, &UA_TYPES[UA_TYPES_SUBSCRIPTIONDIAGNOSTICSDATATYPE]) ){
				let diagnostics = static_cast<const UA_SubscriptionDiagnosticsDataType*>( v.data );
				for( uint i=0; i<v.arrayLength && !y; ++i )
					y = diagnostics[i].subscriptionId==subscription && UA_NodeId_equal( &diagnostics[i].sessionId, &session );
			}
			UA_Variant_clear( &v );
			return y;
		}
		//ConditionRefresh( SubscriptionId ):  the argument is an IntegerId, which the server may hand over as its own type or UInt32, one kind.
		Ω conditionRefresh( UA_Server* ua, const UA_NodeId* sessionId, void*, const UA_NodeId*, void* methodContext, const UA_NodeId*, void*, uint inputSize, const UA_Variant* input, uint outputSize, UA_Variant* )ι->UA_StatusCode{
			auto conditions = static_cast<Conditions*>( methodContext );
			if( !conditions || !ua || !sessionId )
				return UA_STATUSCODE_BADINTERNALERROR;
			if( inputSize!=1 || outputSize!=0 || !UA_Variant_isScalar(&input[0]) || !input[0].type || input[0].type->typeKind!=UA_DATATYPEKIND_UINT32 )
				return UA_STATUSCODE_BADINVALIDARGUMENT;
			let subscription = *static_cast<const UA_UInt32*>( input[0].data );
			if( !owns(*ua, *sessionId, subscription) )
				return UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID;
			return conditions->Refresh( *ua, *sessionId, subscription );
		}
		//A write to a watched input, after the server stored it:  the condition it drives, found by the node, compares it with its limit.
		Ω onInputWrite( UA_Server* ua, const UA_NodeId*, void*, const UA_NodeId* nodeId, void*, const UA_NumericRange*, const UA_DataValue* value )ι->void{
			if( !ua || !nodeId || !value || !value->hasValue )
				return;
			auto conditions = Conditions::Of( *ua );
			if( auto condition = conditions ? conditions->FindByInput( *nodeId ) : nullptr; condition )
				condition->OnInput( *value );
		}
		//A write to a watched limit, after the server stored it:  every condition held to it compares its input again.
		Ω onLimitWrite( UA_Server* ua, const UA_NodeId*, void*, const UA_NodeId* nodeId, void*, const UA_NumericRange*, const UA_DataValue* value )ι->void{
			if( !ua || !nodeId || !value || !value->hasValue )
				return;
			if( auto conditions = Conditions::Of( *ua ); conditions )
				conditions->OnLimit( *nodeId );
		}
	}

	Condition::Condition( UA_Server& ua, NodeId id, NodeId source )ε:
		_ua{ ua }, _id{ move(id) }, _source{ move(source) }, _type{ typeOf(ua, _id) }{
		UA_LocalizedText sourceName; UA_LocalizedText_init( &sourceName );
		UAε( UA_Server_readDisplayName(&ua, _source, &sourceName) );
		_sourceName = ToString( sourceName.text );
		UA_LocalizedText_clear( &sourceName );
		let variable = [&]( std::initializer_list<const char*> path )ε->NodeId{
			auto y = child( ua, _id, path );
			THROW_IF( !y, "The condition {} has no {}.", _id.ToString(), join(path) );
			return move( *y );
		};
		let twoState = [&]( const char* name )ε->TwoState{ return { variable({name}), variable({name, "Id"}) }; };
		let stamped = [&]( const char* name )ε->Stamped{ return { variable({name}), variable({name, "SourceTimestamp"}) }; };
		_enabled = twoState( "EnabledState" );
		_active = twoState( "ActiveState" );
		_acked = twoState( "AckedState" );
		if( child(ua, _id, {"ConfirmedState"}) )
			_confirmed = twoState( "ConfirmedState" );
		_retain = variable( {"Retain"} );
		_severity = variable( {"Severity"} );
		_message = variable( {"Message"} );
		_branchId = variable( {"BranchId"} );
		_clientUserId = variable( {"ClientUserId"} );
		_suppressedOrShelved = variable( {"SuppressedOrShelved"} );
		_comment = stamped( "Comment" );
		_quality = stamped( "Quality" );
		_lastSeverity = stamped( "LastSeverity" );
		Write( _enabled, true, "Enabled" );
		const UA_NodeId null = UA_NODEID_NULL;
		Write( _branchId, &null, UA_TYPES_NODEID );
		const UA_Boolean suppressed{ false };
		Write( _suppressedOrShelved, &suppressed, UA_TYPES_BOOLEAN );
		WriteState();
		for( let stamp : {&_comment, &_quality, &_lastSeverity} )
			Stamp( stamp->Time );
	}
	α Condition::Write( const NodeId& node, const void* scalar, uint type )ι->void{
		UA_Variant v; UA_Variant_init( &v );
		UA_Variant_setScalar( &v, const_cast<void*>(scalar), &UA_TYPES[type] );//borrowed:  the write copies it.
		if( let sc = UA_Server_writeValue(&_ua, node, v); sc )
			WARN( "Could not write {} of condition {}:  {}", node.ToString(), _id.ToString(), UA_StatusCode_name(sc) );
	}
	α Condition::WriteText( const NodeId& node, sv text, sv locale )ι->void{
		const UA_LocalizedText t{ locale.empty() ? UA_STRING_NULL : ToUV(locale), ToUV(text) };
		Write( node, &t, UA_TYPES_LOCALIZEDTEXT );
	}
	α Condition::Stamp( const NodeId& timestamp )ι->void{
		let now = UA_DateTime_now();
		Write( timestamp, &now, UA_TYPES_DATETIME );
	}
	α Condition::Write( const TwoState& state, bool value, sv text )ι->void{
		WriteText( state.Variable, text );
		const UA_Boolean b{ value };
		Write( state.Id, &b, UA_TYPES_BOOLEAN );
	}
	α Condition::WriteState()ι->void{
		Write( _active, _state.Active, _state.Active ? "Active" : "Inactive" );
		Write( _acked, _state.Acked, _state.Acked ? "Acknowledged" : "Unacknowledged" );
		if( _confirmed )
			Write( *_confirmed, _state.Confirmed, _state.Confirmed ? "Confirmed" : "Unconfirmed" );
		const UA_Boolean retain{ Retain() };
		Write( _retain, &retain, UA_TYPES_BOOLEAN );
		const UA_UInt16 severity{ _state.Severity };
		Write( _severity, &severity, UA_TYPES_UINT16 );
		WriteText( _message, _state.Message );
		WriteText( _comment.Variable, _state.Comment, _state.CommentLocale );
		const UA_String user = ToUV( _state.ClientUserId );
		Write( _clientUserId, &user, UA_TYPES_STRING );
		Write( _quality.Variable, &_state.Quality, UA_TYPES_STATUSCODE );
		Write( _lastSeverity.Variable, &_state.LastSeverity, UA_TYPES_UINT16 );
	}
	//The event:  the object's type from its source, the instance resolving the condition fields it holds, and in the map the rest, which the
	//server looks up first - the ConditionId under the key the standard clause prints as, `i=2782#NodeId`, which the build otherwise answers
	//Bad_NotSupported;  EventType, SourceNode and SourceName, since the type's mandatory properties of those names, instantiated on the object
	//with no value, would otherwise answer each clause an empty field in place of the defaults;  Time, kept, so a replay carries the
	//transition's;  ReceiveTime;  and on a replay the EventId kept too, which the server then takes as the event's (cacheEventId) in place of
	//a new one.  A live event's EventId is the server's, copied back.  The map's values are borrowed.
	α Condition::Emit( const UA_NodeId* session, const UA_UInt32* subscription )ι->UA_StatusCode{
		let replay = session!=nullptr;
		let now = UA_DateTime_now();
		if( !replay )
			_lastTime = now;
		UA_LocalizedText name; UA_LocalizedText_init( &name );
		if( UA_Server_readDisplayName(&_ua, _source, &name)==UA_STATUSCODE_GOOD )//the name now:  a client may have renamed the source.
			_sourceName = ToString( name.text );
		UA_LocalizedText_clear( &name );
		UA_KeyValuePair fields[7]; uint count{};
		let add = [&]( const char* key, const void* scalar, uint type )ι{
			fields[count].key = UA_QUALIFIEDNAME( 0, (char*)key );
			UA_Variant_init( &fields[count].value );
			UA_Variant_setScalar( &fields[count].value, const_cast<void*>(scalar), &UA_TYPES[type] );
			++count;
		};
		add( "i=2782#NodeId", &_id, UA_TYPES_NODEID );
		add( "/EventType", &_type, UA_TYPES_NODEID );
		add( "/SourceNode", &_source, UA_TYPES_NODEID );
		const UA_String sourceName = ToUV( _sourceName );
		add( "/SourceName", &sourceName, UA_TYPES_STRING );
		add( "/Time", &_lastTime, UA_TYPES_DATETIME );
		add( "/ReceiveTime", &now, UA_TYPES_DATETIME );
		const UA_ByteString eventId{ _lastEventId.size(), (UA_Byte*)_lastEventId.data() };
		if( replay )
			add( "/EventId", &eventId, UA_TYPES_BYTESTRING );
		const UA_KeyValueMap map{ count, fields };
		const UA_EventDescription ed{
			.sourceNode=_source, .eventType=_type, .severity=_state.Severity, .message={ UA_STRING_NULL, ToUV(_state.Message) },
			.eventFields=&map, .eventInstance=&_id, .sessionId=session, .subscriptionId=subscription
		};
		UA_ByteString created; UA_ByteString_init( &created );
		let sc = UA_Server_createEventEx( &_ua, &ed, replay ? nullptr : &created );
		if( sc )
			WARN( "Condition {} could not {} its event:  {}", _id.ToString(), replay ? "replay" : "raise", UA_StatusCode_name(sc) );
		if( !sc && !replay )//not `else`:  the log macros expand to their own `if`.
			_lastEventId.assign( (const char*)created.data, created.length );
		UA_ByteString_clear( &created );
		return sc;
	}
	α Condition::IsLast( const UA_ByteString& eventId )Ι->bool{
		return _lastEventId.size() && eventId.length==_lastEventId.size() && std::memcmp( eventId.data, _lastEventId.data(), eventId.length )==0;
	}
	α Condition::Raise( bool active, UA_UInt16 severity, sv message )ι->UA_StatusCode{
		_state.Active = active;
		if( severity!=_state.Severity ){
			_state.LastSeverity = _state.Severity;
			_state.Severity = severity;
			Stamp( _lastSeverity.Time );
		}
		_state.Message = message;
		if( active ){//a new alarm wants its own acknowledgement:  what was said of the last one goes with it.
			_state.Acked = false;
			_state.Confirmed = !_confirmed;
			if( _state.Comment.size() || _state.CommentLocale.size() )
				SetComment( {}, {}, {} );
		}
		WriteState();
		return Emit();
	}
	α Condition::SetComment( sv text, sv locale, sv user )ι->void{
		_state.Comment = text;
		_state.CommentLocale = locale;
		_state.ClientUserId = user;
		Stamp( _comment.Time );
	}
	//Part 9:  a comment with text and locale both empty is none, and the last stands;  an empty text with a locale clears it.
	α Condition::ApplyComment( const UA_LocalizedText& comment, sv user )ι->void{
		if( comment.text.length || comment.locale.length )
			SetComment( ToSV(comment.text), ToSV(comment.locale), user );
	}
	α Condition::Acknowledge( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode{
		if( !IsLast(eventId) )
			return UA_STATUSCODE_BADEVENTIDUNKNOWN;
		if( _state.Acked )
			return UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED;
		_state.Acked = true;
		ApplyComment( comment, user );
		WriteState();
		return Emit();
	}
	α Condition::Confirm( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode{
		if( !_confirmed )
			return UA_STATUSCODE_BADNOTSUPPORTED;
		if( !IsLast(eventId) )
			return UA_STATUSCODE_BADEVENTIDUNKNOWN;
		if( !_state.Acked )
			return UA_STATUSCODE_BADINVALIDSTATE;
		if( _state.Confirmed )
			return UA_STATUSCODE_BADCONDITIONBRANCHALREADYCONFIRMED;
		_state.Confirmed = true;
		ApplyComment( comment, user );
		WriteState();
		return Emit();
	}
	α Condition::AddComment( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode{
		if( !IsLast(eventId) )
			return UA_STATUSCODE_BADEVENTIDUNKNOWN;
		ApplyComment( comment, user );
		WriteState();
		return Emit();
	}
	α Condition::Refresh( const UA_NodeId& session, UA_UInt32 subscription )ι->UA_StatusCode{
		return Retain() && _lastEventId.size() ? Emit( &session, &subscription ) : UA_STATUSCODE_GOOD;
	}
	α Condition::WatchHigh( const NodeId& input, const NodeId& limit, UA_UInt16 severity )ε->void{
		THROW_IF( _input, "The condition {} already watches {}.", _id.ToString(), _input->ToString() );
		let conditions = Conditions::Of( _ua );//one value callback a node, and it drives the first condition on the input.
		THROW_IF( conditions && conditions->FindByInput(input), "{} already drives a condition.", input.ToString() );
		UA_LocalizedText name; UA_LocalizedText_init( &name );
		UAε( UA_Server_readDisplayName(&_ua, input, &name) );
		_inputName = ToString( name.text );
		UA_LocalizedText_clear( &name );
		_inputSeverity = severity;
		_limit = limit;
		const UA_ValueCallback limitCallback{ .onRead=nullptr, .onWrite=&onLimitWrite };
		UAε( UA_Server_setVariableNode_valueCallback(&_ua, limit, limitCallback) );
		const UA_ValueCallback callback{ .onRead=nullptr, .onWrite=&onInputWrite };
		UAε( UA_Server_setVariableNode_valueCallback(&_ua, input, callback) );
		_input = input;//last:  the callback finds this condition by it, so a failed install watches nothing.
	}
	//Over the limit raises, at or under it clears, each once:  only a change in the input's own verdict drives it, so a Raise by hand holds
	//until the input next crosses, and even then a state the condition already has raises nothing.  A Bad sample, a value that isn't a finite
	//number or a limit that isn't says nothing about the process, so the state stands;  the status goes to Quality.
	α Condition::OnInput( const UA_DataValue& value )ι->void{
		if( !_limit )
			return;
		const UA_StatusCode quality = value.hasStatus ? value.status : UA_STATUSCODE_GOOD;
		if( quality!=_state.Quality ){
			_state.Quality = quality;
			Write( _quality.Variable, &_state.Quality, UA_TYPES_STATUSCODE );
			Stamp( _quality.Time );
		}
		if( UA_StatusCode_isBad(quality) )
			return;
		let x = number( value.value );
		const Value limit = ReadValue( _ua, *_limit );
		let max = x && limit.hasValue ? number( limit.value ) : optional<double>{};
		if( !max || !std::isfinite(*x) || !std::isfinite(*max) )
			return;
		let over = *x>*max;
		let was = _inputOver.value_or( _state.Active );
		_inputOver = over;
		if( over==was || over==_state.Active )
			return;
		Raise( over, _inputSeverity, Ƒ("{} at {} is {} its limit of {}.", _inputName, *x, over ? "over" : "within", *max) );
	}
	α Condition::OnLimit()ι->void{
		if( _input )
			OnInput( ReadValue(_ua, *_input) );
	}

	α Conditions::Add( UA_Server& server, const NodeId& condition, const NodeId& source )ε->Condition&{
		THROW_IF( Find(condition), "The condition {} is already added.", condition.ToString() );
		Bind( server );
		_conditions.emplace_back( new Condition{server, condition, source} );//not mu:  the constructor is this type's alone.
		INFO( "Condition {} added, raised from {}.", condition.ToString(), source.ToString() );
		return *_conditions.back();
	}
	α Conditions::Find( const UA_NodeId& condition )ι->Condition*{
		for( auto& c : _conditions ){
			if( UA_NodeId_equal(&c->Id(), &condition) )
				return c.get();
		}
		return nullptr;
	}
	α Conditions::FindByInput( const UA_NodeId& input )ι->Condition*{
		for( auto& c : _conditions ){
			if( c->Input() && UA_NodeId_equal(&*c->Input(), &input) )
				return c.get();
		}
		return nullptr;
	}
	α Conditions::OnLimit( const UA_NodeId& limit )ι->void{
		for( auto& c : _conditions ){
			if( c->Limit() && UA_NodeId_equal(&*c->Limit(), &limit) )
				c->OnLimit();
		}
	}
	α Conditions::Refresh( UA_Server& server, const UA_NodeId& session, UA_UInt32 subscription )ι->UA_StatusCode{
		let bracket = [&]( UA_UInt32 type )ι->UA_StatusCode{
			const UA_EventDescription ed{
				.sourceNode=UA_NODEID_NUMERIC( 0, UA_NS0ID_SERVER ), .eventType=UA_NODEID_NUMERIC( 0, type ), .severity=RefreshSeverity,
				.message={ UA_STRING_NULL, UA_STRING_NULL }, .sessionId=&session, .subscriptionId=&subscription
			};
			return UA_Server_createEventEx( &server, &ed, nullptr );
		};
		if( let sc = bracket(UA_NS0ID_REFRESHSTARTEVENTTYPE); sc )
			return sc;
		UA_StatusCode y{ UA_STATUSCODE_GOOD };
		for( auto& condition : _conditions ){
			if( let sc = condition->Refresh(session, subscription); sc && !y )
				y = sc;
		}
		if( let sc = bracket(UA_NS0ID_REFRESHENDEVENTTYPE); sc && !y )
			y = sc;
		return y;
	}
	α Conditions::Of( UA_Server& server )ι->Conditions*{
		void* context{};
		return UA_Server_getNodeContext( &server, UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE), &context )==UA_STATUSCODE_GOOD ? static_cast<Conditions*>( context ) : nullptr;
	}
	//The type's methods, the ns0 nodes every instance references, each with this as its context, and ConditionType's for Of.
	α Conditions::Bind( UA_Server& server )ε->void{
		if( _bound )
			return;
		let bind = [&]( UA_UInt32 method, UA_MethodCallback callback )ε{
			const UA_NodeId id = UA_NODEID_NUMERIC( 0, method );
			UAε( UA_Server_setMethodNodeCallback(&server, id, callback) );
			UAε( UA_Server_setNodeContext(&server, id, this) );
		};
		bind( UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE, &commentMethod<&Condition::Acknowledge> );
		bind( UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_CONFIRM, &commentMethod<&Condition::Confirm> );
		bind( UA_NS0ID_CONDITIONTYPE_ADDCOMMENT, &commentMethod<&Condition::AddComment> );
		bind( UA_NS0ID_CONDITIONTYPE_ENABLE, &enable );
		bind( UA_NS0ID_CONDITIONTYPE_DISABLE, &disable );
		bind( UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH, &conditionRefresh );
		UAε( UA_Server_setNodeContext(&server, UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE), this) );
		_bound = true;
	}
}