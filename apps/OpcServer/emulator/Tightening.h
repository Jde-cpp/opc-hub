#pragma once
#include <jde/opc/openProtocol/Controller.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	//The /emulator/tightening block (docs/open-protocol/spec.md *Emulator*).  A key left out takes the member's default.
	struct TighteningSpec{
		TighteningSpec( const jobject& o, SRCE )ε;//throws on a key there but of the wrong kind (FindKey), and on what the members refuse.
		uint16_t Port{ 4545 };//the protocol's (§2.1.1);  0 takes a free one.
		string Address{ "127.0.0.1" };//the interface it listens on, every one where empty.
		//`cycle`, `jitter` and `reject` as production has them (Signals.h):  a rundown every cycle, give or take the jitter, while
		//the tool is enabled, and that share of them NOK.
		TagSpec Rundowns;
		//`psets`, the first selected at the start:  each an `id`, 1 to 999, a `name` of 25 characters at most, a `target` torque
		//inside its `min` and `max`, in Nm to two decimals, and a `batchSize` to 99, 0 no batch.  Refused with none, with two of
		//one id, and with a `max` that leaves a NOK's torque no room under the field's 9999.99.
		vector<OpenProtocol::Mid0015> Psets;
		uint Keep{ 10'000 };//the results kept for MID 0064, the oldest let go past it;  at least 1.
		bool Log{};//every frame in and out at Trace under the openProtocol tag.
	};

	//The emulator's tightening station (docs/open-protocol/plan.md *Phase 11*):  the library's controller as a Power Focus
	//6000 named `Jde PLC emulator`, which numbers its messages, on a port of its own.  A device beside the pumps, advanced
	//by the same clock:  a rundown for each unit production counts while the tool is enabled, its torque about the selected
	//set's target and inside its limits, a reject's outside them and NOK, the batch counting the OK ones to its size and
	//starting over, the tightening ids the controller's own run, each result pushed to its subscribers and kept for MID
	//0064.  MID 0018 selects a set of the table and restarts its batch, MID 0042 and 0043 stop and start the rundowns, and
	//MID 0038 and 0050 are refused as a PF6000 with no Source Batch and no identifier source refuses them, codes 20 and 8.
	struct TighteningStation final{
		static constexpr sv ControllerName{ "Jde PLC emulator" };
		TighteningStation( boost::asio::io_context& context, TighteningSpec spec )ε;//throws where it cannot listen.
		α Sample( Duration dt )ι->void;//a period of the device clock, on the thread that owns it.
		α Port()Ι->uint16_t{ return _controller.Port(); }
		α ToString()Ι->string;//the status line's:  tightening[results=12 nok=1 pset=1 tool=true]
	private:
		α Rundown( bool reject )ι->OpenProtocol::Mid0061;
		Production _production;
		OpenProtocol::Mid0015 _pset;//the set the last period saw selected:  the first, until the controller says another.
		uint _made{}, _rejects{};
		uint16_t _batch{};//the OK rundowns of the batch under way.
		bool _enabled{ true };
		OpenProtocol::Controller _controller;//last:  its strand is running when its constructor returns.
	};
}