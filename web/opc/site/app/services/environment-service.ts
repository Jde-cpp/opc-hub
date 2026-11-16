import { Injectable } from '@angular/core';
import{ IEnvironment } from 'jde-spa'
import {environment} from '../../environments/environment'

//config.json's keys (docs/demo/spec.md *The public origin*), each taken as its own type alone.
const keys:Record<string,( x:any )=>boolean> = {
	applicationServer: x=>typeof x=="object" && !!x && ( x.port===null || Number.isInteger(x.port) && x.port>0 ),//port null:  the page's own port
	home: x=>typeof x=="string" && /^\/[^/]/.test( x ),//a route the empty path redirects to in place of the cards, never itself
	banner: x=>typeof x=="string",//a line above every page
	login: x=>typeof x=="boolean"//false hides the sign-in
};

@Injectable()
export class EnvironmentService implements IEnvironment{
	get<T>( key:string ): T{
		return this.#values[key] as T;
	}

	//Layers config.json, a file beside index.html, over the build's environment, before any service starts (app.config.ts).  No
	//build ships one:  a front serves it for a page the build's values do not fit, a hub behind another port.  It is taken only
	//from a 200 with a JSON content type and a body that parses:  the hub answers a missing one 404, and a front's fallback
	//or ng serve answers the page in its place, which is no config either.  Absent, or not answered within timeoutMs, since
	//the site waits on it to start, the build's environment is all there is.
	async load():Promise<void>{
		let file:any;
		try{
			const response = await fetch( new URL("config.json", document.baseURI), {cache: "no-cache", signal: AbortSignal.timeout(EnvironmentService.timeoutMs)} );
			const type = response.headers.get( "content-type" ) ?? "";
			if( response.status!=200 || !/^application\/json\b/i.test(type) )
				throw new Error( `${response.status} ${type}` );
			file = await response.json();
			if( typeof file!="object" || !file || Array.isArray(file) )
				throw new Error( "not an object" );
		}
		catch( e ){
			console.info( `No config.json beside the page (${e instanceof Error ? e.message : e}) - the build's environment applies.` );
			return;
		}
		const values:Record<string,unknown> = { ...environment };
		for( const [key, valid] of Object.entries(keys) ){
			if( !(key in file) )
				continue;
			if( !valid(file[key]) )
				console.warn( `config.json's '${key}' is not a value the site reads - ignored.`, file[key] );
			else if( key=="applicationServer" )//port null:  the build's is the hub's listener, the front's own (AppServerEnvironment)
				values[key] = { ...environment.applicationServer, port: file[key].port, ...( file[key].port===null ? {listenerPort: environment.applicationServer.port} : {} ) };
			else
				values[key] = file[key];
		}
		if( values["login"]===false && /^\/login(?:[/?#]|$)/.test(String(values["home"])) ){//the login guard would send it back to the empty path
			console.warn( "config.json's 'home' is the login page, which its 'login' false closes - ignored.", values["home"] );
			delete values["home"];
		}
		this.#values = values;
	}
	static timeoutMs = 5_000;
	#values:Record<string,unknown> = environment;
}