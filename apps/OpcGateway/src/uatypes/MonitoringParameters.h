#pragma once
#include <open62541/types.h>
#include "EventFilter.h"

namespace Jde::Opc::Gateway{
	//Part 4's DataChangeFilter (spec *Watch list*):  when an item reports - a status change, a value change, a timestamp change -
	//and the deadband a value change has to clear, absolute in the value's units, or a percentage of the node's EURange, which is
	//the server's to apply:  one that can't refuses the create with the status the ack carries, or the modify with the one the
	//MonitoredItemModified push carries.
	struct DataChangeFilter final{
		UA_DataChangeTrigger Trigger{ UA_DATACHANGETRIGGER_STATUSVALUE };
		UA_DeadbandType DeadbandType{ UA_DEADBANDTYPE_NONE };
		double DeadbandValue{};
		α operator==( const DataChangeFilter& )Ι->bool=default;
	};
	//What a monitored item asks its server for - UA_MonitoringParameters less the client handle.  A screen's items take the
	//defaults; the historian's sample at each node's MinTimeInterval, or at 0, the server's fastest, and queue a publishing
	//interval's samples (spec *Collector contract*);  a watch list's carry what its rows ask.  A listener answers these per node
	//(IDataChange::ItemParameters), so a rebuild re-creates its items as it has them now, and ModifyMonitoredItemsAwait changes
	//them in place.  An event item (spec *Events*) asks these with `Events`, its select clauses, in place of a data-change filter:
	//the EventNotifier attribute, whose sampling interval the server ignores, so 0 here, and a queue of its events.
	struct MonitoringParameters final{
		double SamplingInterval{ 500.0 };//ms;  0 is the server's fastest, negative the publishing interval, which a session gives in ms (GatewaySocketSession::ItemParameters).
		uint32 QueueSize{ 1 };
		bool DiscardOldest{ true };
		optional<DataChangeFilter> Filter;//none:  the server's default, DataChangeFilter{} - a report on a status or value change, no deadband.
		optional<EventFilter> Events;//an event item's clauses:  the item's kind, which a listener of the node has to share.
		α IsEvent()Ι->bool{ return Events.has_value(); }
		Ω ForEvents( EventFilter events, uint32 queueSize=100 )ι->MonitoringParameters{ return { .SamplingInterval=0, .QueueSize=queueSize, .Events=move(events) }; }
		//None and DataChangeFilter{} are one ask, so an item asked either way is no change to modify;  an event filter equals another as a set.
		α operator==( const MonitoringParameters& x )Ι->bool{ return SamplingInterval==x.SamplingInterval && QueueSize==x.QueueSize && DiscardOldest==x.DiscardOldest && Filter.value_or(DataChangeFilter{})==x.Filter.value_or(DataChangeFilter{}) && Events==x.Events; }
		//Into a create's or a modify's requestedParameters - the three fields, and the filter as a UA_DataChangeFilter when there is one,
		//or a UA_EventFilter for an event item.
		α Set( UA_MonitoringParameters& ua )Ι->void;
	};

	Ξ MonitoringParameters::Set( UA_MonitoringParameters& ua )Ι->void{
		ua.samplingInterval = SamplingInterval;
		ua.queueSize = QueueSize;
		ua.discardOldest = DiscardOldest;
		if( Events ){
			Events->Set( ua );
			return;
		}
		if( !Filter )
			return;
		UA_DataChangeFilter filter{ .trigger=Filter->Trigger, .deadbandType=(UA_UInt32)Filter->DeadbandType, .deadbandValue=Filter->DeadbandValue };
		UA_ExtensionObject_setValueCopy( &ua.filter, &filter, &UA_TYPES[UA_TYPES_DATACHANGEFILTER] );//a copy:  the request's clear frees it.
	}
}