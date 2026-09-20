import { nameKey, statusText } from "./status-code";
import { StatusCode } from "./types";

export class OpcError implements Error{
	constructor( public sc:StatusCode, public name:string, public stack:string, public cause:string|undefined ){
		if( !OpcError.messages.has(nameKey(sc)) )
			OpcError.messages.set( nameKey(sc), null );//null = "pending fetch" sentinel that emptyMessages() collects; undefined would be skipped and never fetched from /ErrorCodes
	}
	get message(){ return OpcError.messages.get( nameKey(this.sc) ) ?? `sc=${this.sc}`; }
	toString(){ return `[${this.sc.toString(16)}] - ${OpcError.text(this.sc)}`; }//text(), not the map:  an unfetched name rendered "- null"
	static emptyMessages(){
		let empty = [];
		for( const [sc, message] of OpcError.messages ) {
			if( message===null )
				empty.push( sc );
		};
		return empty;
	}
	//the server's name for the code, less its flags - the map is keyed by nameKey, so every InfoBits variant of a code shares one fetch
	static statusCodeText( sc:StatusCode ):string|undefined{
		const y = OpcError.messages.get( nameKey(sc) );
		if( !y )
			OpcError.messages.set( nameKey(sc), null );
		return y ?? undefined;
	}
	//what the screen shows for a reading's quality:  the name with its flags ("UncertainEngineeringUnitsExceeded+High").
	//Asking registers the name as pending, which is what gets it fetched.
	static text( sc:StatusCode ):string{ return statusText( sc, nameKey(sc) ? OpcError.statusCodeText(sc) : undefined ); }//Good needs no name, so no fetch
	static setMessages( x:{sc:StatusCode, message:string}[] ){ x.forEach( e=>
		OpcError.messages.set(nameKey(e.sc), e.message) );
	}

	private static messages:Map<StatusCode,string|null> = new Map<StatusCode,string|null>();
}