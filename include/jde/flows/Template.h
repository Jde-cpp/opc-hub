#pragma once
#include "Context.h"
#include "Message.h"

//The template node's mustache (docs/flows/spec.md *Nodes*), for a host's node that writes text from a message too.
namespace Jde::Opc::Flows{
	//Mustache's variables alone:  `{{name}}` a property of the message by its path, `{{flow.key}}` and `{{global.key}}` a
	//context's, escaped for HTML as mustache.js escapes, `{{{name}}}` and `{{&name}}` as they are, and `{{!…}}` a comment.
	//A value that is absent renders as nothing, an object or an array as its json.
	struct Mustache final{
		explicit Mustache( sv text )ε;//refuses a tag that does not close, and a section, a partial or a delimiter.
		//`escape` false:  every variable as it is, for text that is not HTML's, a log line.
		α Render( const Message& m, const Store& flow, const GlobalStore& global, bool escape=true )Ι->string;
	private:
		enum class EFrom : uint8{ Msg, Flow, Global };
		struct Part final{ string Text; optional<Path> Property; EFrom From{}; bool Escape{}; };
		vector<Part> _parts;
	};
}