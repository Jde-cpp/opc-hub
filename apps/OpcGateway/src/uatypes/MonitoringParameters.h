#pragma once

namespace Jde::Opc::Gateway{
	//What a monitored item asks its server for - UA_MonitoringParameters less the client handle and the filter.  A screen's
	//items take the defaults; the historian's sample at each node's MinTimeInterval, or at 0, the server's fastest, and queue a
	//publishing interval's samples (spec *Collector contract*).  A listener answers these per node (IDataChange::ItemParameters),
	//so a rebuild re-creates its items as it has them now, and ModifyMonitoredItemsAwait changes them in place.
	struct MonitoringParameters final{
		double SamplingInterval{ 500.0 };//ms;  0 is the server's fastest.
		uint32 QueueSize{ 1 };
		bool DiscardOldest{ true };
		α operator==( const MonitoringParameters& )Ι->bool=default;
	};
}