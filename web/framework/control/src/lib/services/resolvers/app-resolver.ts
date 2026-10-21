import { inject, Injectable } from '@angular/core';
import {ActivatedRouteSnapshot, Resolve, Router, RouterStateSnapshot} from '@angular/router';
import { matchConfig, plural, RouteItem, RouteStore, uncapitalize } from 'jde-spa';
import { APP_SERVICE } from '../app/app-service';
import { resolveInstanceHost } from '../app/app-service-types';
import { TableSettings } from '../ql-list-resolver';
import { SnackbarService } from '../../shared/snackbar/snackbar-service';


export type Connection = { id: number, instanceId: number, programName: string, displayName: string, instanceName: string, hostName: string, created: Date, status: { memory: number, values: any[] }, urlSegments:string[], linked:boolean };//linked:  the site has a page for it (the PLC emulator has none)

export class AppInstanceRoute extends RouteItem{
	constructor( programName:string, instanceName:string, tableSettings:TableSettings ){
		super();
		this.path = `${programName}/${instanceName}`;
		this.title = `${programName}/${instanceName}`;
		this.tableSettings = tableSettings;
	}
	tableSettings:TableSettings;
}
//The sidenav item for an instance page that has no resolver of its own (app-server, opc-server).  AppResolver keeps the instance
//names under apps/<program>, and the sibling links are rendered as parent.path + '/' + sibling.path - so the parent must be set
//or they resolve against the route's own url instead.
export function instanceRouteItem( programSegment:string, instanceName:string, routeStore:RouteStore ):RouteItem{
	const y = new RouteItem( {path: `${programSegment}/${instanceName}`, title: `${programSegment}/${instanceName}`} );
	y.parent = new RouteItem( {path: "/apps", title: "Applications"} );
	y.siblings = AppResolver.siblings( programSegment, routeStore );
	return y;
}

@Injectable()
export class AppResolver implements Resolve<Connection[]> {
	#appService = inject( APP_SERVICE );
	async resolve(route: ActivatedRouteSnapshot, state: RouterStateSnapshot):Promise<Connection[]>{
		let connections:Connection[];
		try{
			connections = await this.#appService.queryArray<Connection>( "connections(deleted:null){id instanceId programName instanceName hostName created status{memory values}}" );
		}
		catch( e ){//a rejected resolve reaches only the console - the Applications link would do nothing, silently (reviews/m3-closing.md #6)
			this.#snackbar.exception( "Could not load the applications.", e );
			throw e;
		}
		let urlMap:any = {};
		const pageHost = typeof location=="undefined" ? "" : location.hostname;
		connections.forEach( c=>{
			c.created = new Date( c.created );
			c.programName = c.programName.startsWith("Jde.") ? c.programName.substring(4) : c.programName;
			//the card title is what the process calls itself;  programName is normalised below into a *routing* key, which is a
			//different thing - keep the two apart or the hub loses its name on screen.  The plain gateway and the PLC emulator
			//are the exceptions:  labels, so only the hub reads as its process name.
			c.displayName = AppResolver.labels[c.programName] ?? c.programName;
			//the host an instance advertises is its web host - an OpcServer configured `http.host: "localhost"` says so - and a
			//loopback name means the page's host, as the connection itself reads it (resolveInstanceHost, install-issues #2)
			c.hostName = resolveInstanceHost( c.hostName, pageHost );
			if( c.programName=="OpcGateway" || c.programName=="OpcHub" )//the hub (AppServer + gateway in one process) registers once; its page is the gateway's - the connections, logs and levels there are its own
				c.programName = "Gateway";
			let childPath = uncapitalize( plural(c.programName) );
			c.urlSegments = [ childPath, c.instanceName];
			c.linked = !!matchConfig( this.#router.config, ['apps', ...c.urlSegments] );//an unrouted card was a link to NG04002
			if( !c.linked )
				return;
			if( !urlMap[childPath] )
				urlMap[childPath] = [];
			urlMap[childPath].push( new RouteItem({ path: c.instanceName, title: `${c.instanceName}` }) );
		});
		//the hub first - the process this site is served by - then by name, rather than the order the instances connected in
		connections.sort( (a,b)=>+(a.displayName!="OpcHub") - +(b.displayName!="OpcHub") || a.displayName.localeCompare(b.displayName) || a.instanceName.localeCompare(b.instanceName) );
		Object.keys(urlMap).forEach( key=>{
			this.routeStore.setChildren( 'apps/'+key, urlMap[key] );
		} );
		return connections;
	}
	//a program's instances, relative to /apps - the parent an instance page's sidenav renders them under
	static siblings( programSegment:string, routeStore:RouteStore ):RouteItem[]{
		return routeStore.getChildren( ['apps', programSegment] ).map( s=>new RouteItem({path: `${programSegment}/${s.path}`, title: s.title}) );
	}
	static readonly labels:Record<string,string> = { OpcGateway: "Gateway", "Opc.PlcEmulator": "PLC Emulator" };//program name (Jde. dropped) -> card title

	routeStore = inject( RouteStore );
	#router = inject( Router );
	#snackbar = inject( SnackbarService );
}