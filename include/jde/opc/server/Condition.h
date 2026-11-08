#pragma once
#include <open62541/server.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Server{
	struct Conditions;
	//An AlarmConditionType object built by hand, since open62541's own alarms and conditions are off in this build (clients spec *Alarms and
	//conditions*, #425).  The host declares the object, its state variables and the type's methods in its address space - the OpcServer's
	//pumps nodeset does - and Conditions::Add takes it over:  the state written to the instance's variables, so a client reads them and a
	//select clause into the instance, `/AckedState/Id`, resolves through them;  each transition an event of the object's type from its
	//source, through UA_Server_createEventEx, with the ConditionId under the key open62541 prints the standard clause as, since the build
	//answers that clause Bad_NotSupported itself;  Acknowledge, Confirm and AddComment taking the last event's EventId and a comment, a stale
	//one refused Bad_EventIdUnknown (Part 9);  and Refresh replaying the last event, EventId and Time included, while Retain holds.  One
	//branch, the main one, so BranchId stays null;  EnabledState stays Enabled.  Driven on the server's thread, from a method or value
	//callback, where the state needs no lock:  a host raising from elsewhere posts onto it (UA_Server_addTimedCallback).
	struct Condition final : noncopyable{
		α Id()Ι->const NodeId&{ return _id; }
		α Source()Ι->const NodeId&{ return _source; }
		α Input()Ι->const optional<NodeId>&{ return _input; }
		α Limit()Ι->const optional<NodeId>&{ return _limit; }
		α LastEventId()Ι->str{ return _lastEventId; }//the bytes of the last event's, which Acknowledge and Confirm want;  empty before the first.
		//Retain, Part 9's:  while the condition is active, unacknowledged or, with a ConfirmedState, unconfirmed.
		α Retain()Ι->bool{ return _state.Active || !_state.Acked || !_state.Confirmed; }
		α Active()Ι->bool{ return _state.Active; }
		//A transition:  active with the severity and message, which unsets AckedState and ConfirmedState again and clears the comment, or
		//inactive with them.  A severity that differs moves the last one to LastSeverity.  The event's status;  its EventId is LastEventId.
		α Raise( bool active, UA_UInt16 severity, sv message )ι->UA_StatusCode;
		//AcknowledgeableConditionType's Acknowledge and Confirm, ConditionType's AddComment:  Bad_EventIdUnknown for an EventId that isn't the last
		//event's, Bad_ConditionBranchAlreadyAcked and Bad_ConditionBranchAlreadyConfirmed for a state already held, Bad_InvalidState for a Confirm
		//before the Acknowledge, Bad_NotSupported for a Confirm on a condition declared without ConfirmedState;  each a transition event.
		//`comment`'s text and locale are kept, with `user` as ClientUserId and its SourceTimestamp;  a comment with both empty leaves the last.
		α Acknowledge( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode;
		α Confirm( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode;
		α AddComment( const UA_ByteString& eventId, const UA_LocalizedText& comment, sv user )ι->UA_StatusCode;
		//The last event again, to the session's subscription alone (Conditions::Refresh):  nothing while Retain is false.
		α Refresh( const UA_NodeId& session, UA_UInt32 subscription )ι->UA_StatusCode;
		//The input's value, after the server stored it (WatchHigh's callback):  its status goes to Quality and its number is compared with the limit.
		α OnInput( const UA_DataValue& value )ι->void;
		α OnLimit()ι->void;//the limit was written:  the input's current value is compared with it again.
		//The condition's input:  a numeric variable whose every write - a client's, the PubSub reader's - is compared with the value of `limit`, a
		//property the host declares, and raises the condition over it with `severity` and clears it at or under it, when the input crosses the
		//limit and that changes the state.  Through a value callback on `input` and one on `limit`, each of which keeps its value and replaces
		//any notifications it had.  One condition to an input:  a second is a throw.
		α WatchHigh( const NodeId& input, const NodeId& limit, UA_UInt16 severity )ε->void;
	private:
		friend Conditions;
		Condition( UA_Server& ua, NodeId id, NodeId source )ε;
		struct TwoState final{ NodeId Variable; NodeId Id; };
		struct Stamped final{ NodeId Variable; NodeId Time; };//a ConditionVariableType's value and its SourceTimestamp, the time of its last change.
		α Emit( const UA_NodeId* session=nullptr, const UA_UInt32* subscription=nullptr )ι->UA_StatusCode;
		α IsLast( const UA_ByteString& eventId )Ι->bool;
		α Write( const NodeId& node, const void* scalar, uint type )ι->void;
		α Write( const TwoState& state, bool value, sv text )ι->void;
		α WriteText( const NodeId& node, sv text, sv locale={} )ι->void;
		α Stamp( const NodeId& timestamp )ι->void;
		α ApplyComment( const UA_LocalizedText& comment, sv user )ι->void;
		α SetComment( sv text, sv locale, sv user )ι->void;
		α WriteState()ι->void;//ActiveState, AckedState, ConfirmedState, Retain, Severity, Message, Comment, ClientUserId, Quality and LastSeverity from _state.
		struct State final{
			bool Active{}; bool Acked{ true }; bool Confirmed{ true }; UA_UInt16 Severity{}; UA_UInt16 LastSeverity{}; UA_StatusCode Quality{ UA_STATUSCODE_GOOD };
			string Message; string Comment; string CommentLocale; string ClientUserId;
		};
		UA_Server& _ua;
		NodeId _id, _source, _type;
		string _sourceName;//the source's DisplayName as of the last event, the events' SourceName.
		TwoState _enabled, _active, _acked;
		optional<TwoState> _confirmed;//AcknowledgeableConditionType's optional one.
		NodeId _retain, _severity, _message, _branchId, _clientUserId, _suppressedOrShelved;
		Stamped _comment, _quality, _lastSeverity;
		State _state;
		string _lastEventId;
		UA_DateTime _lastTime{};
		optional<NodeId> _input, _limit;
		optional<bool> _inputOver;//the input's last verdict against the limit;  none before its first sample.
		string _inputName;//the input's DisplayName, for the message.
		UA_UInt16 _inputSeverity{};
	};

	//A server's conditions, kept on its UAServer:  Add takes a host's object over and, the first time, binds the type's methods -
	//Acknowledge i=9111, Confirm i=9113, AddComment i=9029, Enable i=9027 and Disable i=9028, which answer Bad_ConditionAlreadyEnabled and
	//Bad_NotSupported since a condition here is never disabled, and ConditionRefresh i=3875 on ConditionType, open to every session
	//(UAAccess), which replays every retained condition to the calling session's subscription its argument names, RefreshStart and
	//RefreshEnd from the Server object around it as open62541's own alarms do, so a subscription's items on the Server object see the
	//bracket, one on a lower notifier the conditions it emits, and another session's subscription none of it;  a subscription that isn't the
	//session's is refused Bad_SubscriptionIdInvalid, read off the SubscriptionDiagnosticsArray.  Found from a callback through the node
	//context of ConditionType and of each method bound (Of).  Add before the server runs, on the host's thread;  the rest on the server's.
	struct Conditions final : noncopyable{
		//`condition`, raised from `source`, the object whose events it is - a pump - with the SubscribeToEvents bit or under one that has it:
		//the type from its HasTypeDefinition, its variables by browse name, each missing one but ConfirmedState a throw.
		α Add( UA_Server& server, const NodeId& condition, const NodeId& source )ε->Condition&;
		α Find( const UA_NodeId& condition )ι->Condition*;
		α FindByInput( const UA_NodeId& input )ι->Condition*;
		α OnLimit( const UA_NodeId& limit )ι->void;//`limit` was written:  each condition held to it (Condition::OnLimit).
		α Refresh( UA_Server& server, const UA_NodeId& session, UA_UInt32 subscription )ι->UA_StatusCode;
		Ω Of( UA_Server& server )ι->Conditions*;//the server's, null before its first Add.
	private:
		α Bind( UA_Server& server )ε->void;
		vector<up<Condition>> _conditions;
		bool _bound{};
	};
}