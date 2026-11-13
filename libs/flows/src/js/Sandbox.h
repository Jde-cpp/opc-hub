#pragma once
#include <quickjs.h>
#include <jde/flows/Flow.h>

//The Function node's sandbox (docs/flows/spec.md *The Function node*):  quickjs-ng, one JSRuntime a flow and one JSContext
//a Function node, with no module loader, so `import` resolves nothing, and no `std` or `os` module, so a body has no
//file, socket, process or clock but what the node gives it.
namespace Jde::Opc::Flows::Js{
	//What one entry into the runtime came to.
	struct Ran final{
		bool Interrupted{};//it ran past the timeout, or the flow stopped under it:  no job ran after.
		bool Halted{};//interrupted by the flow's stop.
		bool InDrain{};//interrupted among the jobs, what it entered for having returned.
		bool Killed{};//the interrupt ended code that was running;  false for one that fell between two jobs.
		JSContext* Culprit{};//whose code the interrupt ended, or whose job was next.
		string Why;//the interrupt, as an error's text.
		vector<JSContext*> Touched;//each context a job ran in.
		vector<std::pair<JSContext*,string>> Failures;//a job that ended on what no promise takes, out of memory.
	};
	//A promise rejected with no handler, which the tracker reports and a handler added later takes back.
	struct Rejection final{ JSContext* Ctx; JSValue Promise; JSValue Reason; };

	//A flow's runtime, on its strand alone but for Halt:  the memory limit, the stack limit, a GC threshold at a share of
	//the memory, the interrupt handler and the rejection tracker.  No module loader and no dynamic-import hook is set.
	struct Runtime final{
		Runtime( const FunctionLimits& limits, string flow, jobject env )ε;
		~Runtime();
		Runtime( const Runtime& )=delete;
		α operator=( const Runtime& )->Runtime& =delete;

		//One entry into the runtime, and the only way any of its code runs:  a body's call, a timer's callback, On Start, On
		//Stop, each with the jobs it leaves, and the reading of what they returned, which runs a body's getters.  It records
		//the stack's top, since the overflow check is against the stack the runtime last saw and a strand runs on any of the
		//executor's threads, and it is the interrupt handler's clock:  past `Timeout`, or once the flow stops, the handler
		//answers non-zero from then on, the memory limit raised by a reserve first, so there is room for the interrupt's own
		//error where the heap is full, and the stack limit put where no call fits, so what the interrupt left running cannot
		//start more.  Both limits are put back as the entry ends.  One at a time.
		struct Entry final{
			Entry( Runtime& rt, bool stopping=false )ι;//`stopping`:  On Stop's, which the flow's stop does not interrupt.
			~Entry();
			Entry( const Entry& )=delete;
			α operator=( const Entry& )->Entry& =delete;
			//The pending jobs, every context's, until none is left, one is interrupted or the timeout passes between two:
			//the rest wait for the runtime's next entry.
			α Drain()ι->void;
			α Interrupted()Ι->bool{ return _ran.Interrupted; }
			α Why()Ι->string;
			α Result()ι->Ran;//once, as the entry ends.
		private:
			α Expired( JSContext* ctx, bool running )ι->bool;
			Runtime& _rt;
			const bool _stopping;
			const steady_clock::time_point _deadline;
			Ran _ran;
			friend struct Runtime;
		};
		α Halt()ι->void{ _halted.store( true, std::memory_order_relaxed ); }//any thread.
		//The pending exception as an error's text, and no longer pending:  the interrupt's when the entry was interrupted,
		//whatever the body made of it.
		α TakeError( JSContext* ctx )ι->string;
		α Handled( JSValueConst promise )ι->void;//a rejected promise its node has read:  not the tracker's to report.
		α TakeRejected()ι->vector<Rejection>;//those still unhandled, the caller's to free.
		α Forget( JSContext* ctx )ι->void;//a context is going.

		JSRuntime* const Rt;
		const FunctionLimits Limits;
		const string Flow;//its name, for the log.
		const jobject Env;
		bool Continuing{};//an entry is posted for the jobs a cut drain left.
	private:
		Ω Interrupt( JSContext* ctx, void* opaque )ι->int;
		Ω Rejected( JSContext* ctx, JSValueConst promise, JSValueConst reason, bool handled, void* opaque )ι->void;
		α GcShare()Ι->size_t{ return Limits.Memory/4*3; }
		atomic<bool> _halted{};
		Entry* _entry{};
		vector<Rejection> _rejected;
	};

	//An error as its text, `ReferenceError: x is not defined (code:3:11)`:  null is out of memory, what quickjs-ng throws
	//when it has no room to build its own error, and what a body's own `throw null` reads as.  In an entry:  it runs the
	//error's `toString`.
	α ErrorText( JSContext* ctx, JSValueConst error )ι->string;
	constexpr sv OutOfMemory{ "InternalError: out of memory" };//quickjs-ng's own text for it, where it had room to build its error.
}