#include "Tightening.h"
#include <jde/fwk/process/process.h>
#include <jde/fwk/utils/mathUtils.h>

#define let const auto
namespace Jde::Opc::Emulator{
	using namespace OpenProtocol;
	constexpr ELogTags _tags{ ELogTags::App };
	constexpr sv _block{ "/emulator/tightening" };

	Ω hundredths( double v )ι->double{ return std::round( v*100 )/100; }//a torque as the wire carries it.
	//The share of a set's window a NOK's torque lands past its limit by, at most.
	Ω past( const Mid0015& pset )ι->double{ return ( *pset.TorqueMax-*pset.TorqueMin )/4; }

	Ω production( const jobject& o, SL sl )ε->TagSpec{
		jobject tag{ {"name", _block}, {"mode", "production"} };//its refusals name the block.
		for( sv key : {"cycle"sv, "jitter"sv, "reject"sv} ){
			if( let value = o.if_contains(key) )
				tag[key] = *value;
		}
		return TagSpec{ tag, sl };
	}
	Ω pset( const jobject& o, SL sl )ε->Mid0015{
		let id = FindKey<uint16_t>( o, "id", Ƒ("{} parameter set", _block), sl ).value_or( 0 );
		THROW_IFSL( id<1 || id>999, "{}: a parameter set's `id` is 1 to 999.", _block );
		let owner = Ƒ( "{} parameter set {}", _block, id );
		let torque = [&]( sv key ){
			let v = FindKey<double>( o, key, owner, sl );
			THROW_IFSL( !v, "{}: it needs a `{}`, a torque in Nm.", owner, key );
			return hundredths( *v );
		};
		Mid0015 y{ .Pset=id, .PsetName=FindKey<string>(o, "name", owner, sl).value_or(string{}), .BatchSize=FindKey<uint8_t>(o, "batchSize", owner, sl).value_or(0),
			.TorqueMin=torque("min"), .TorqueMax=torque("max"), .TorqueFinalTarget=torque("target") };
		THROW_IFSL( *y.TorqueMin<0 || *y.TorqueMin>=*y.TorqueFinalTarget || *y.TorqueFinalTarget>=*y.TorqueMax, "{}: its `target` ({}) is between its `min` ({}), 0 or more, and its `max` ({}).", owner, *y.TorqueFinalTarget, *y.TorqueMin, *y.TorqueMax );
		//The codec's own word:  on the highest torque a NOK can have, and on the name's characters and length, the batch's two
		//digits and each torque's six.
		let result = Encode( {.Revision=Mid0061::MaxRevision}, Mid0061{.Torque=*y.TorqueMax+past(y)+.01} );
		THROW_IFSL( !result, "{}: its `max` leaves a NOK's torque no room above it - {}", owner, result.error().What );
		let selected = Encode( {.Revision=Mid0015::MaxRevision}, y );
		THROW_IFSL( !selected, "{}: {}", owner, selected.error().What );
		return y;
	}

	TighteningSpec::TighteningSpec( const jobject& o, SL sl )ε:
		Rundowns{ production(o, sl) }{
		Port = FindKey<uint16_t>( o, "port", _block, sl ).value_or( Port );
		Address = FindKey<string>( o, "address", _block, sl ).value_or( Address );
		Keep = FindKey<uint>( o, "keep", _block, sl ).value_or( Keep );
		Log = FindKey<bool>( o, "log", _block, sl ).value_or( Log );
		THROW_IFSL( !Keep, "{}: `keep` is the results kept for MID 0064, at least 1.", _block );
		let psets = o.if_contains( "psets" );
		THROW_IFSL( !psets || !psets->is_array() || psets->get_array().empty(), "{}: `psets` is an array of at least one parameter set, {{id, name, target, min, max, batchSize}}.", _block );
		for( let& value : psets->get_array() ){
			auto set = pset( Json::AsObject(value, sl), sl );
			THROW_IFSL( std::ranges::contains(Psets, set.Pset, &Mid0015::Pset), "{}: two parameter sets have the id {}.", _block, *set.Pset );
			Psets.push_back( move(set) );
		}
	}

	Ω script( const TighteningSpec& spec )ι->Controller::Script{
		Controller::Script y{
			//A Power Focus 6000's, system type 3, a normal tightening system:  under the release the codec's tables are and the product's own version.
			.Identity={ .CellId=0, .ChannelId=1, .ControllerName=string{TighteningStation::ControllerName}, .SupplierCode="ACT", .OpenProtocolVersion="2.20.1",
				.ControllerSoftwareVersion=string{Process::ProductVersion}, .SystemType=3, .SystemSubtype=1, .SequenceNumberSupport=1, .LinkingSupport=0,
				.StationId=1, .StationName="Tightening station", .ClientId=1 },
			//MID 0038 with no Source Batch (the PF6000's appendix, §2.7) and MID 0050 with no identifier source (§5.7.1).
			.Refuses={ {Mid0038::Id, 20}, {Mid0050::Id, 8} },
			.KeepResults=spec.Keep,
			.DropSilent=15s,//a controller's (§5.28.1).
			.Log=spec.Log
		};
		for( let& set : spec.Psets )
			y.Psets.emplace( *set.Pset, set );
		return y;
	}
	Ω first( const TighteningSpec& spec )ι->Mid0015{
		auto y = spec.Psets.front();
		y.PsetChangeTime = LocalNow();
		return y;
	}

	TighteningStation::TighteningStation( boost::asio::io_context& context, TighteningSpec spec )ε:
		_production{ MakeProduction(spec.Rundowns) },
		_pset{ first(spec) },
		_controller{ context, spec.Port, script(spec), spec.Address }{
		_controller.Select( _pset );
		INFO( "Tightening station on {}:{}:  a rundown every {}, {} NOK, {} parameter set(s), {} selected.", spec.Address.empty() ? "every interface" : spec.Address, Port(), Chrono::ToString(spec.Rundowns.Cycle),
			Ƒ("{:.0f}%", spec.Rundowns.Reject*100), spec.Psets.size(), *_pset.Pset );
	}

	α TighteningStation::Sample( Duration dt )ι->void{
		if( let enabled = _controller.ToolEnabled(); enabled!=_enabled ){
			_enabled = enabled;
			INFO( "[tightening]tool <- {} (external)", enabled ? "enabled" : "disabled" );
		}
		//None until the controller's strand has run the start's selection, which is _pset's own.
		if( let selected = _controller.Selected(); selected && (selected->Pset!=_pset.Pset || selected->PsetChangeTime!=_pset.PsetChangeTime) ){
			_pset = *selected;
			_batch = 0;
			INFO( "[tightening]parameter set <- {} (external)", *_pset.Pset );
		}
		let made = (uint)_production.Count->Next( dt, _enabled );
		let rejects = (uint)_production.Rejects->Next( dt, _enabled );
		for( ; _made<made; ++_made ){
			let reject = _rejects<rejects;
			if( reject )
				++_rejects;
			_controller.Push( Rundown(reject) );
		}
	}

	α TighteningStation::Rundown( bool reject )ι->Mid0061{
		let min = *_pset.TorqueMin; let max = *_pset.TorqueMax; let target = *_pset.TorqueFinalTarget;
		double torque;
		uint8_t torqueStatus{ 1 };
		if( reject ){
			let by = .01+absl::Uniform<double>( Math::BitGen(), 0, past(_pset) );
			let low = min-by>=0 && absl::Bernoulli( Math::BitGen(), .5 );//a torque is never under 0.
			torque = low ? min-by : max+by;
			torqueStatus = low ? 0 : 2;
		}
		else
			torque = std::clamp( absl::Gaussian<double>(Math::BitGen(), target, std::min(target-min, max-target)/3), min, max );
		let size = _pset.BatchSize.value_or( 0 );
		if( size && !reject )
			++_batch;
		let done = size && _batch==size;
		Mid0061 y{ .CellId=0, .ChannelId=1, .ControllerName=string{ControllerName}, .JobId=0, .Pset=_pset.Pset, .BatchSize=size, .BatchCounter=_batch,
			.Status=reject ? 0 : 1, .BatchStatus=!size ? 2 : done ? 1 : 3, .TorqueStatus=torqueStatus, .TorqueMin=min, .TorqueMax=max, .TorqueFinalTarget=target,
			.Torque=hundredths(torque), .Time=LocalNow(), .PsetChangeTime=_pset.PsetChangeTime, .PsetName=_pset.PsetName, .TorqueUnit=1, .ResultType=1 };
		if( done )
			_batch = 0;
		return y;
	}

	α TighteningStation::ToString()Ι->string{
		return Ƒ( "tightening[results={} nok={} pset={} tool={}]", _made, _rejects, *_pset.Pset, _enabled );
	}
}