import { DatePipe } from '@angular/common';
import { Component, LOCALE_ID, computed, effect, inject, input, signal, untracked } from '@angular/core';
import { MatIconButton } from '@angular/material/button';
import { MatIcon } from '@angular/material/icon';
import { MatProgressBar } from '@angular/material/progress-bar';
import { MatToolbar } from '@angular/material/toolbar';
import { MatTooltip } from '@angular/material/tooltip';
import { IGraphQL, SnackbarService, errorMessage, isRefusal } from 'jde-framework';
import { ClientDiagnostics, clientDiagnosticsQuery, serverDiagnosticsIds, serverDiagnosticsQuery, ServerDiagnosticsRow, serverDiagnosticsSummaryId, serverStatusId, sessionDiagnosticsArrayId, subscriptionDiagnosticsArrayId, UaStructure } from '../../../../model/server';

export const refusedClientText = "You may not read the gateway's session telemetry.";
export const serverStates = ["Running", "Failed", "No configuration", "Suspended", "Shutdown", "Test", "Communication fault", "Unknown"];//ServerState, Part 5

//The members as the gateway hands them over - open62541's json encoding, reversible or not - read loosely:
//a structure member by its name
export const member = ( s:unknown, name:string ):unknown=>( s && typeof s=='object' && !Array.isArray(s) ) ? (s as UaStructure)[name] : undefined;
//an enum:  a number, or `Name_Value` in the non-reversible form - named from `names` by its value, else the name it came with
export const enumName = ( v:unknown, names:string[] ):string=>{
	if( typeof v=='number' )
		return names[v] ?? String( v );
	if( typeof v=='string' ){
		const m = /^(.*)_(\d+)$/.exec( v );
		return m ? ( names[+m[2]] ?? m[1] ) : v;
	}
	return v==null ? "" : String( v );
};
//a LocalizedText:  its text, or {Locale, Text}
export const localizedText = ( v:unknown ):string=>typeof v=='string' ? v : String( member(v, "Text") ?? "" );
//a NodeId:  a string, or {Id, Namespace, IdType} - rendered as its ns=…;i=… form
export const nodeIdText = ( v:unknown ):string=>{
	if( typeof v=='string' )
		return v;
	const id = member( v, "Id" );
	if( id===undefined )
		return v==null ? "" : String( v );
	const ns = member( v, "Namespace" ), idType = member( v, "IdType" );
	const prefix = typeof idType=='number' ? ["i", "s", "g", "b"][idType] ?? "i" : typeof id=='number' ? "i" : "s";
	return `${ns ? `ns=${ns};` : ""}${prefix}=${id}`;
};
//a count shown as text, blank for a member the server left out
export const countText = ( v:unknown ):string=>v==null ? "" : String( v );
//a `PascalCase` member name as a label:  "CumulatedSessionCount" → "Cumulated session count"
export const memberLabel = ( name:string ):string=>name.replace( /([a-z])([A-Z])/g, "$1 $2" ).replace( /([A-Z]+)([A-Z][a-z])/g, "$1 $2" ).toLowerCase().replace( /^./, c=>c.toUpperCase() );
//whole seconds as "1 h 2 m 3 s"
export const ageText = ( seconds:number ):string=>{
	const h = Math.floor( seconds/3600 ), m = Math.floor( seconds%3600/60 ), s = seconds%60;
	return [h ? `${h} h` : "", h || m ? `${m} m` : "", `${s} s`].filter( x=>x ).join( " " );
};
//a read's value:  the reading, unwrapped from an Uncertain {v, sc}, or undefined with the status for a Bad read ({sc} alone)
export const reading = ( value:unknown ):{ value?:unknown; sc?:number }=>{
	if( value && typeof value=='object' && !Array.isArray(value) ){
		const o = value as UaStructure;
		if( o.hasOwnProperty('v') )
			return { value: o['v'], sc: o['sc'] as number|undefined };
		if( o.hasOwnProperty('sc') )
			return { sc: o['sc'] as number };
	}
	return { value };
};
export type DiagnosticField = { label:string; value:string; title?:string };
const status = ( sc:number|undefined ):string=>`status 0x${(sc ?? 0).toString(16).padStart(8, '0')}`;

//The Server tab (docs/clients/spec.md *Diagnostics*, #427):  the server's ServerStatus and ServerDiagnosticsSummary, its session and
//subscription diagnostics arrays - which a server may leave empty, and the tab says so - and beside them the gateway's own client for
//the connection, the one this session uses, with its start, its breaks and what it monitors.  Both loads go at once, apart:  the server's
//reads need a session with it and fail where the connection does, while the client's telemetry answers whether or not it connects, and
//null when the gateway has no client for this session.  A refusal is a state, said in place.  Read when the tab is shown, and on Refresh.
@Component({
	selector: 'server-diagnostics',
	templateUrl: './server-diagnostics.html',
	styleUrl: './server-diagnostics.scss',
	imports: [MatIcon, MatIconButton, MatProgressBar, MatToolbar, MatTooltip]
})
export class ServerDiagnostics{
	#snackBar = inject( SnackbarService );
	constructor(){
		effect( ()=>{//the row's slug, and again for another row the router reuses the page on
			const slug = this.slug();
			untracked( ()=>this.load( slug ) );
		} );
	}
	async load( slug=this.slug() ){
		if( !slug ){//a connection not saved yet:  nothing to read under
			this.status.set( undefined ); this.summary.set( undefined ); this.sessions.set( [] ); this.subscriptions.set( [] ); this.client.set( undefined );
			this.serverError.set( null ); this.clientError.set( null );
			this.isLoading.set( false );
			return;
		}
		const load = ++this.#load;
		this.isLoading.set( true );
		this.serverError.set( null );
		this.clientError.set( null );
		const gateway = this.gateway();
		const [server, client] = await Promise.allSettled( [
			gateway.query<{nodes:ServerDiagnosticsRow[]}>( serverDiagnosticsQuery, {opc: slug, id: serverDiagnosticsIds} ),
			gateway.query<{serverConnection:{clientDiagnostics:ClientDiagnostics|null}|null}>( clientDiagnosticsQuery, {opc: slug} )
		] );
		if( load!=this.#load )
			return;//another row's, or a refresh's, since
		if( server.status=='fulfilled' ){
			const rows = server.value?.nodes ?? [];
			const read = ( i:number )=>reading( rows.find( r=>r.id?.i==i )?.value );
			const structure = ( i:number ):UaStructure|undefined=>{ const r = read( i ); return r.value && typeof r.value=='object' && !Array.isArray(r.value) ? r.value as UaStructure : undefined; };
			const array = ( i:number ):UaStructure[]=>{ const r = read( i ); return Array.isArray( r.value ) ? r.value as UaStructure[] : []; };
			this.status.set( structure(serverStatusId) );
			this.summary.set( structure(serverDiagnosticsSummaryId) );
			this.sessions.set( array(sessionDiagnosticsArrayId) );
			this.subscriptions.set( array(subscriptionDiagnosticsArrayId) );
			this.statusError.set( this.status() ? null : `The server did not answer ServerStatus (${status(read(serverStatusId).sc)}).` );
			this.summaryError.set( this.summary() ? null : `The server did not answer the diagnostics summary (${status(read(serverDiagnosticsSummaryId).sc)}).` );
		}
		else{
			this.status.set( undefined ); this.summary.set( undefined ); this.sessions.set( [] ); this.subscriptions.set( [] );
			this.serverError.set( isRefusal(server.reason) ? "You may not read this server." : errorMessage(server.reason) );//the reason alone:  the state's title says what failed
			if( !isRefusal(server.reason) )
				this.#snackBar.exception( "Could not read the server's diagnostics.", server.reason );
		}
		if( client.status=='fulfilled' )
			this.client.set( client.value?.serverConnection?.clientDiagnostics ?? null );
		else{
			this.client.set( undefined );
			this.clientError.set( isRefusal(client.reason) ? refusedClientText : errorMessage(client.reason) );
			if( !isRefusal(client.reason) )
				this.#snackBar.exception( "Could not read the gateway's client.", client.reason );
		}
		this.isLoading.set( false );
	}
	#load = 0;

	gateway = input.required<IGraphQL>();//the connection's gateway:  nodes(opc) and serverConnection{clientDiagnostics} are its custom queries
	slug = input<string|undefined>();//the connection's;  unset for one not saved yet
	status = signal<UaStructure|undefined>( undefined );
	summary = signal<UaStructure|undefined>( undefined );
	sessions = signal<UaStructure[]>( [] );
	subscriptions = signal<UaStructure[]>( [] );
	client = signal<ClientDiagnostics|null|undefined>( undefined );//null:  the gateway has no client for this session;  undefined:  not read, or the read failed
	isLoading = signal( true );
	serverError = signal<string|null>( null );//the four reads failed as one:  no session with the server, or a refusal
	statusError = signal<string|null>( null );//one read Bad where the others answered
	summaryError = signal<string|null>( null );
	clientError = signal<string|null>( null );

	//ServerStatusDataType:  the state named, the two times, the shutdown, then BuildInfo's members
	readonly statusFields = computed<DiagnosticField[]>( ()=>{
		const s = this.status();
		if( !s )
			return [];
		const build = member( s, "BuildInfo" );
		const text = ( v:unknown )=>v==null ? "" : String( v );
		return [
			{ label: "State", value: enumName( member(s, "State"), serverStates ) },
			{ label: "Start time", value: this.#time( member(s, "StartTime") ) },
			{ label: "Current time", value: this.#time( member(s, "CurrentTime") ) },
			{ label: "Seconds till shutdown", value: countText( member(s, "SecondsTillShutdown") ) },
			{ label: "Shutdown reason", value: localizedText( member(s, "ShutdownReason") ) },
			{ label: "Product name", value: text( member(build, "ProductName") ) },
			{ label: "Product URI", value: text( member(build, "ProductUri") ) },
			{ label: "Manufacturer", value: text( member(build, "ManufacturerName") ) },
			{ label: "Software version", value: text( member(build, "SoftwareVersion") ) },
			{ label: "Build number", value: text( member(build, "BuildNumber") ) },
			{ label: "Build date", value: this.#time( member(build, "BuildDate") ) }
		];
	} );
	//ServerDiagnosticsSummaryDataType:  every member, each a count, labelled from its name - a server's extra members show too
	readonly summaryFields = computed<DiagnosticField[]>( ()=>{
		const s = this.summary();
		return !s ? [] : Object.entries( s ).map( ([name, v])=>({ label: memberLabel( name ), value: countText( v ) }) );
	} );
	//the gateway's own client:  its start and breaks, then what it holds, then the connection's
	readonly clientFields = computed<DiagnosticField[]>( ()=>{
		const c = this.client();
		return !c ? [] : [
			{ label: "Connected since", value: this.#time( c.connectedSince ) },
			{ label: "Reconnects", value: String( c.reconnects ) },
			{ label: "Last break", value: c.lastBreak ? this.#time( c.lastBreak ) : "never" },
			{ label: "Session age", value: ageText( c.sessionAge ) },
			{ label: "Monitored items", value: String( c.monitoredItems ) },
			{ label: "Parked items", value: String( c.parkedItems ), title: "Items a lost connection is holding for its reconnect - on no client until it comes back." },
			{ label: "Reconnect waiting", value: c.reconnectWaiting ? "yes" : "no", title: "A reconnect of this connection is in its backoff wait." },
			{ label: "Clients", value: String( c.clients ), title: "The gateway's open clients for this connection, one per credential." }
		];
	} );
	//a DateTime member as the viewer's locale shows it - the Endpoint tab's `date:'medium'`, applied here since the rows are computed
	//- blank for none, the text as it came for one that is no date
	#time( v:unknown ):string{
		if( typeof v!='string' || !v )
			return "";
		const d = new Date( v );
		return isNaN( d.getTime() ) ? v : this.#date.transform( d, 'medium' ) ?? v;
	}
	#date = new DatePipe( inject(LOCALE_ID) );
	//the sessions table's cells, in the SessionDiagnosticsDataType's own order
	sessionName( s:UaStructure ):string{ return String( member(s, "SessionName") ?? "" ); }
	sessionId( s:UaStructure ):string{ return nodeIdText( member(s, "SessionId") ); }
	clientUri( s:UaStructure ):string{ return String( member(member(s, "ClientDescription"), "ApplicationUri") ?? "" ); }
	endpointUrl( s:UaStructure ):string{ return String( member(s, "EndpointUrl") ?? "" ); }
	connectionTime( s:UaStructure ):string{ return this.#time( member(s, "ClientConnectionTime") ); }
	lastContact( s:UaStructure ):string{ return this.#time( member(s, "ClientLastContactTime") ); }
	count( s:UaStructure, name:string ):string{ return countText( member(s, name) ); }
	interval( s:UaStructure ):string{ const v = member( s, "PublishingInterval" ); return typeof v=='number' ? `${v} ms` : countText( v ); }
	enabled( s:UaStructure ):string{ const v = member( s, "PublishingEnabled" ); return v==null ? "" : v ? "yes" : "no"; }
}