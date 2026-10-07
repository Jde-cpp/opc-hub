#pragma once
#include <absl/functional/function_ref.h>
#include <jde/historian/Group.h>
#include <jde/opc/uatypes/NodeId.h>

namespace Jde::Opc::Server{
	//A variable a nodeset marks Historizing, as the address space holds it once the nodesets are loaded (historian spec,
	//*OpcServer* and *UA backend*).
	struct Historized final{
		NodeId Id;//by the server's own namespace index.
		Hist::Member Member;//by namespace URI, with the thresholds of its HA Configuration.
		NodeId StartOfArchive, StartOfOnlineArchive;//its HA Configuration's, which the archive's first day is written to.

		//Every such variable but a type's members, in NodeId order.  Each gets what a client needs to find its history and
		//its nodeset left out:  HistoryRead in its AccessLevel, an HA Configuration object, and in that the thresholds'
		//defaults, Stepped true and both intervals 0, ServerTimestampSupported, AggregateConfiguration at Part 13's
		//defaults, and the two archive starts.  No ExceptionDeviation is added:  none stores every change.  An enumeration
		//takes none, and a percent-of-range format takes the variable's InstrumentRange or EURange.  Throws for a setting
		//that isn't of its kind, naming the node.
		//
		//typeStepped says whether an HA Configuration's Stepped is the type's:  open62541 gives an object whose nodeset
		//declares none the type's mandatory one, which holds false, so its presence doesn't say the nodeset set it.
		Ω Load( UA_Server& ua, absl::FunctionRef<bool( const UA_NodeId& )> typeStepped )ε->vector<Historized>;
		//The node's Value attribute, with the timestamps asked for.
		Ω Read( UA_Server& ua, const UA_NodeId& node, UA_TimestampsToReturn timestamps=UA_TIMESTAMPSTORETURN_NEITHER )ι->Value;
	};
}