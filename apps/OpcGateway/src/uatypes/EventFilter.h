#pragma once
#include <open62541/types.h>
#include <jde/opc/uatypes/BrowseName.h>

namespace Jde::Opc::Gateway{
	//One select clause of an EventFilter (spec *Events*):  a SimpleAttributeOperand on BaseEventType for the Value of the node a browse
	//path below the event names - "Message", "2:Rpm", "EnabledState/Id" - each segment's namespace index before its name, `2:` or
	//`2~`, and 0 without one.  Key is what open62541 prints the operand as (UA_SimpleAttributeOperand_print):  "/Message", "/2:Rpm",
	//"/EnabledState/Id" - the key the client delivers each field under (ua_client_subscriptions.c prepareEventFieldsMap) and the key
	//the OpcServer resolves a field it was given by (ua_subscription_event.c resolveSAO), so one clause equals another by it.
	struct EventClause final{
		Ω Parse( sv path, SRCE )ε->EventClause;//throws BadRequest for an empty path or segment, or a namespace that is no UInt16.
		α Operand()Ι->UA_SimpleAttributeOperand;//a deep copy, which the caller clears.
		α operator==( const EventClause& x )Ι->bool{ return Key==x.Key; }
		vector<BrowseName> Path;
		string Key;
	};
	//An event item's EventFilter (spec *Events*):  the eight base fields first, EventId through Severity as BaseEventType orders
	//them, then what a listener selected, and no where clause.  A listener's own list, whose order the fields are pushed in;  an
	//item's is the union of its listeners' (UAMonitoringNodes' merge), which equals another as a set, so a remerge in another
	//order asks no modify.
	struct EventFilter final{
		Ω Base()ι->const vector<EventClause>&;
		Ω Of( const vector<string>& select, SRCE )ε->EventFilter;//Base, then each of `select` parsed, a repeat dropped.
		α Add( const EventClause& clause )ι->bool;//appended unless held.
		α Add( const EventFilter& x )ι->void;//each of x's unless held:  the union.
		α Keys()Ι->flat_set<string>;
		α operator==( const EventFilter& x )Ι->bool{ return Keys()==x.Keys(); }
		α Set( UA_EventFilter& filter )Ι->void;//the clauses as deep copies the filter's clear frees - a create's, a modify's or a ReadEventDetails'.
		α Set( UA_MonitoringParameters& ua )Ι->void;//into a create's or a modify's filter.
		vector<EventClause> Select;
	};
}