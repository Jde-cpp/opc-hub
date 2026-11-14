#include <jde/flows/Template.h>
#include <jde/fwk/str.h>
#include <jde/flows/Props.h>
#include "Nodes.h"
#include "Operand.h"

#define let const auto

namespace Jde::Opc::Flows{
	namespace{
		constexpr ELogTags _tags{ ELogTags::App };
		constexpr std::array<sv,2> _outputs{ "text", "json" };

		//`template` rendered into `property`, `payload` by default, as text or with `output` "json" as the json it reads as.
		struct Template final : INode{
			Template( const jobject& props )ε:
				_property{ Prop::Text(props, "property", "payload") },
				_json{ Prop::Choice(props, "output", _outputs, 0)==1 },
				_template{ Text(props) }
			{}
			α Receive( Message& m )ε->void override{
				let text = _template.Render( m, FlowContext(), Global() );
				_property.Set( m.Json, _json ? parse(text) : jvalue{text} );
				Send( move(m) );
			}
		private:
			Ω Text( const jobject& props )ε->sv{
				let v = Prop::Find( props, "template" );
				THROW_IF( !v || !v->is_string(), "`template` must be text." );
				return v->get_string();
			}
			Path _property;
			bool _json;
			Mustache _template;
		};

		constexpr std::array<sv,5> _levels{ "Trace", "Debug", "Information", "Warning", "Error" };
		//A point whose messages the debug panel shows:  the trace has them, as it has every hop.  With `level` each goes to
		//the log too, unless `active` is false, and in a replay is its capture instead.
		struct Debug final : INode{
			Debug( const jobject& props )ε:
				_active{ Prop::Flag(props, "active", true) }{
				if( Prop::Find(props, "level") )
					_level = (ELogLevel)Prop::Choice( props, "level", _levels );
			}
			α Receive( Message& m )ε->void override{
				if( !_level || !_active )
					return;
				if( Replaying() )
					Capture( m.Id(), m.Json );
				else
					LOG( *_level, _tags, "[{}]{}", Def().Label(), serialize(m.Json) );
			}
		private:
			bool _active;
			optional<ELogLevel> _level;
		};

		struct Link final : INode{
			α Receive( Message& m )ε->void override{ Send( move(m) ); }
		};
	}
}
namespace Jde::Opc::Flows{
	Mustache::Mustache( sv text )ε{
		while( !text.empty() ){
			let open = text.find( "{{" );
			if( open!=0 )
				_parts.emplace_back( string{text.substr(0, open)} );
			if( open==sv::npos )
				break;
			let raw = text.substr( open ).starts_with( "{{{" );
			let close = text.find( raw ? "}}}" : "}}", open );
			THROW_IF( close==sv::npos, "the template has a tag that does not close." );
			auto name = Str::Trim( text.substr(open+(raw ? 3 : 2), close-open-(raw ? 3 : 2)) );
			text.remove_prefix( close+(raw ? 3 : 2) );
			if( name.starts_with('!') )
				continue;
			THROW_IF( !name.empty() && sv{"#^/>="}.contains(name.front()), "'{}':  the template's mustache is its variables alone, with no sections, partials or delimiters.", name );
			let ampersand = name.starts_with( '&' );
			if( ampersand )
				name = Str::Trim( name.substr(1) );
			auto& part = _parts.emplace_back();
			part.Escape = !raw && !ampersand;
			let dot = name.find( '.' );
			let scope = name.substr( 0, dot );
			if( dot!=sv::npos && (scope=="flow" || scope=="global") ){
				part.From = scope=="flow" ? EFrom::Flow : EFrom::Global;
				part.Property.emplace( name.substr(dot+1) );
			}
			else
				part.Property.emplace( name );
		}
	}
	Ω escaped( sv text )ι->string{//mustache.js's entities.
		string y;
		y.reserve( text.size() );
		for( let ch : text ){
			switch( ch ){
			case '&': y += "&amp;"; break;
			case '<': y += "&lt;"; break;
			case '>': y += "&gt;"; break;
			case '"': y += "&quot;"; break;
			case '\'': y += "&#39;"; break;
			case '/': y += "&#x2F;"; break;
			case '`': y += "&#x60;"; break;
			case '=': y += "&#x3D;"; break;
			default: y += ch;
			}
		}
		return y;
	}
	α Mustache::Render( const Message& m, const Store& flow, const GlobalStore& global, bool escape )Ι->string{
		string text;
		for( let& part : _parts ){
			if( !part.Property ){
				text += part.Text;
				continue;
			}
			jobject context;
			if( part.From!=EFrom::Msg ){//a context's key is the path's first name.
				let& key = part.Property->Root();
				if( part.From==EFrom::Global )
					context[key] = global.Get( key );
				else if( let v = flow.Get(key) )
					context[key] = *v;
			}
			let v = part.Property->Find( part.From==EFrom::Msg ? m.Json : context );
			if( !v || v->is_null() )
				continue;
			let rendered = ToText( *v );
			text += part.Escape && escape ? escaped( rendered ) : rendered;
		}
		return text;
	}
}
namespace Jde::Opc{
	α Flows::MakeTemplate( const jobject& props )ε->up<INode>{ return mu<Template>( props ); }
	α Flows::MakeDebug( const jobject& props )ε->up<INode>{ return mu<Debug>( props ); }
	α Flows::MakeLink( const jobject& props )ε->up<INode>{
		Prop::Text( props, "name" );
		return mu<Link>();
	}
}