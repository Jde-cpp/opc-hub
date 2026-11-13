#pragma once

//Context (docs/flows/spec.md *Context*):  json maps in memory, each change told to the host, whose it is to debounce and write.
namespace Jde::Opc::Flows{
	//A node's store or a flow's:  on the flow's strand alone.
	struct Store final{
		using Changed = function<void( sv key, const jvalue* value )>;//value null:  the key went.
		Store( jobject values={}, Changed changed={} )ι:_values{ move(values) }, _changed{ move(changed) }{}
		α Get( sv key )Ι->const jvalue*;//null for a key it lacks.
		α Set( sv key, jvalue value )ι->void;//a null value takes the key away.
		α Keys()Ι->vector<string>;
	private:
		jobject _values;
		Changed _changed;
	};

	//The gateway's store, every flow's on parallel strands and the Context tab's:  under a mutex, `Get` a copy and `Set` an
	//exchange, so the calls stay synchronous.  A read-modify-write across flows is not atomic.
	struct GlobalStore final{
		using Changed = Store::Changed;//called under the store's lock, in the order the values were set, so it only queues.
		GlobalStore( jobject values={}, Changed changed={} )ι:_values{ move(values) }, _changed{ move(changed) }{}
		α Get( sv key )Ι->jvalue;//null for a key it lacks.
		α Set( sv key, jvalue value )ι->jvalue;//the value before;  a null value takes the key away.
		α Keys()Ι->vector<string>;
	private:
		mutable mutex _mutex;
		jobject _values;
		Changed _changed;
	};
}