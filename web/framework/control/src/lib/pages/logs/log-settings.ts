import * as Log from 'jde-proto/Log';
import { Guid } from '../../model/guid';

export class LogSettings{
	constructor( params:LogSettings|undefined=undefined ){
		if( !params )
			return;
		if( params.level!=undefined )//Trace is 0 - a truthy test dropped it and silently restored the default
			this.level = params.level;
		if( params.hiddenMessages )
			this.hiddenMessages = params.hiddenMessages.map( g=>Guid.fromJson(g) ).filter( g=>g!=undefined );
	}

	//Trace, ie show everything: the level is only applied as a filter now, and nothing filtered the first page before.
	level:Log.ELogLevel=Log.ELogLevel.Trace;
	hiddenMessages:Guid[]=[];
}