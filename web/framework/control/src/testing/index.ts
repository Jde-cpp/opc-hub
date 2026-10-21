//Spec helpers, imported as `jde-framework/testing` - see jde-spa/testing.
import { HttpClient, provideHttpClient } from '@angular/common/http';
import { Component, EnvironmentProviders, inject, InjectionToken, input, OnInit, Provider } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { ActivatedRoute, provideRouter, Router, Routes } from '@angular/router';
import { RouterTestingHarness } from '@angular/router/testing';
import { BehaviorSubject, Observable, of } from 'rxjs';
import { IENVIRONMENT, RecentVisits } from 'jde-spa';
import { pageTitleStub, routerStub } from 'jde-spa/testing';
import { TableSchema } from '../lib/model/ql/schema/table-schema';
import { SlugRow } from '../lib/model/ql/slug-row';
import { InstanceTabs } from '../lib/pages/apps/instance-tabs/instance-tabs';
import { LogDetail } from '../lib/pages/logs/detail/log-detail';
import { LogSettingsPanel } from '../lib/pages/logs/settings/log-settings-panel';
import { AUTH_STORE, AuthStore } from '../lib/services/auth-store';
import { ETransport, MessageCodec, ProtoService } from '../lib/services/proto-service';
import { SnackbarService } from '../lib/shared/snackbar/snackbar-service';
import { GoogleAuthService } from '../lib/services/google-auth-service';

//SnackbarService, every level recorded.
export const snackbarStub = ()=>({ exception: vi.fn(), error: vi.fn(), warn: vi.fn(), info: vi.fn() });

//MatDialog:  open() records the component and its config, and the ref's afterClosed() answers `answer` - or `answers.get( component )`
//for a component named there - the way a MatDialogRef does, once.  Set either between steps to change what the next dialog says.
export function dialogStub( answer?:unknown ){
	const dialog = {
		answer,
		answers: new Map<unknown,unknown>(),
		open: vi.fn( ( component:unknown, _config?:{data?:unknown} )=>({ afterClosed: ()=>of( dialog.answers.has(component) ? dialog.answers.get(component) : dialog.answer ) }) )
	};
	return dialog;
}

//A DetailPage on `row`:  the route's data is a BehaviorSubject of the resolver's pageData - next() opens another row on the page, as
//the router reusing it does - or `data` as given.  Configures TestBed with the page's providers and `providers` after them.
export function configureDetailPage( row:unknown, {gateway, data, providers=[]}:{gateway?:unknown; data?:Observable<unknown>; providers?:Provider[]}={} ){
	const pageData = new BehaviorSubject<any>( {pageData: {row, routing: {}, schema: {enums: new Map()}, gateway}} );
	const snackbar = snackbarStub(), router = routerStub();
	TestBed.configureTestingModule({ providers: [
		{ provide: ActivatedRoute, useValue: {data: data ?? pageData} },
		{ provide: Router, useValue: router },
		pageTitleStub,
		{ provide: SnackbarService, useValue: snackbar },
		...providers
	]});
	return { data: pageData, snackbar, router };
}

//A ProtoService with no socket and no http, for the base class's own logic.
export const stubCodec = ( create:()=>object = ()=>({}) ):MessageCodec<any>=>({ create, encode: ()=>({finish: ()=>new Uint8Array()}) } as unknown as MessageCodec<any>);
export const anonymousAuthStore = ()=>({ user: ()=>undefined, logout: vi.fn() }) as unknown as AuthStore;
export class TestProtoService extends ProtoService<any,any>{
	constructor( authStore:AuthStore=anonymousAuthStore(), {http={} as HttpClient, codec=stubCodec(), googleAuth}:{http?:HttpClient; codec?:MessageCodec<any>; googleAuth?:GoogleAuthService}={} ){
		super( codec, http, ETransport.Unsecure, authStore, googleAuth );
	}
	protected override processMessage():void{}
}
//What a service built on ProtoService injects:  http, no environment settings and no one signed in.
export const protoServiceProviders = ():(Provider|EnvironmentProviders)[]=>[
	provideHttpClient(),
	{ provide: IENVIRONMENT, useValue: {get: ()=>undefined} },
	{ provide: AUTH_STORE, useValue: anonymousAuthStore() }
];

//An instance page's two log tabs.  stubLogTabs() puts them in InstanceTabs and returns what each was created for:  `settings`
//is called with the instance id and `logs` with the service, once a tab is built.
type LogTabs = { settings:ReturnType<typeof vi.fn<( instanceId:number|undefined )=>void>>, logs:ReturnType<typeof vi.fn<( service:unknown )=>void>> };
const LOG_TABS = new InjectionToken<LogTabs>( 'LogTabs', {factory: ()=>({settings: vi.fn(), logs: vi.fn()})} );
@Component( {selector: 'log-settings', template: ''} )
class LogSettingsStub implements OnInit{
	service = input<unknown>(); instanceId = input<number>();
	#created = inject( LOG_TABS ).settings;
	ngOnInit(){ this.#created( this.instanceId() ); }
}
@Component( {selector: 'log-detail', template: ''} )
class LogDetailStub implements OnInit{
	service = input<unknown>();
	#created = inject( LOG_TABS ).logs;
	ngOnInit(){ this.#created( this.service() ); }
}
export function stubLogTabs():LogTabs{
	const tabs:LogTabs = { settings: vi.fn(), logs: vi.fn() };
	TestBed.overrideProvider( LOG_TABS, {useValue: tabs} );
	TestBed.overrideComponent( InstanceTabs, {remove: {imports: [LogDetail, LogSettingsPanel]}, add: {imports: [LogSettingsStub, LogDetailStub]}} );
	return tabs;
}

//A router test:  `routes` behind a RouterTestingHarness, the snackbar recorded and RecentVisits.forget a spy, `providers` after them.
//`url` is opened - once `before` has set up what the navigation reads - and waited past a resolver's redirect, which is a second
//navigation;  `leaf()` is the snapshot of the route it landed on.
export async function openRoute( {routes, providers=[], url, before}:{routes:Routes; providers?:Provider[]; url?:string; before?:()=>void} ){
	const snackbar = snackbarStub(), forget = vi.fn();
	TestBed.configureTestingModule( {providers: [
		provideRouter( routes ),
		{ provide: SnackbarService, useValue: snackbar },
		{ provide: RecentVisits, useValue: {forget} },
		...providers
	]} );
	const harness = await RouterTestingHarness.create();
	const router = TestBed.inject( Router );
	const leaf = ()=>{
		let route = router.routerState.snapshot.root;
		while( route.firstChild )
			route = route.firstChild;
		return route;
	};
	before?.();
	if( url ){
		await harness.navigateByUrl( url );
		await new Promise( r=>setTimeout(r, 20) );
	}
	return { harness, router, leaf, forget, snackbar, error: snackbar.error };
}

//The IGraphQL a DetailResolver reads one row through:  `collection`'s schema, its slug query and no sub-queries, and querySingle
//answering the row.
export const slugQl = ( querySingle:( ...args:any[] )=>Promise<unknown>, {collection='roles', type='Role', ...overrides}:{collection?:string; type?:string; [member:string]:unknown}={} )=>({
	toCollectionName: ( s:string )=>s,
	schemaWithEnums: async ()=>({collectionName: collection, type, fields: []}),
	slugQuery: ()=>`${type.toLowerCase()}(slug:"temp")`,
	subQueries: ()=>[],
	querySingle,
	...overrides
});

//A query/queryArray that answers from `table` as the server would:  the collection the text opens with, every `field:null` filter it
//names (deleted, criteria) and its skip/limit.  The texts are its calls.  `unwrapped` answers the rows alone, as queryArray does.
export function qlAnswering( table:Record<string, unknown>, {unwrapped=false}:{unwrapped?:boolean}={} ){
	return vi.fn( async ( text:string, _vars?:unknown ):Promise<any>=>{
		const [, collection, args=''] = /(\w+)\s*(?:\(([^)]*)\))?\s*\{/.exec( text )!;
		let rows = table[collection];
		if( Array.isArray(rows) ){
			const flat = args.replace( /\s/g, '' );
			for( const [, field] of flat.matchAll(/(\w+):null/g) )
				rows = (rows as Record<string,unknown>[]).filter( r=>r[field]==null );
			const skip = +(/skip:(\d+)/.exec(flat)?.[1] ?? 0), limit = /limit:(\d+)/.exec( flat );
			rows = (rows as unknown[]).slice( skip, limit ? skip + +limit[1] : undefined );
		}
		return unwrapped ? rows ?? [] : {[collection]: rows};
	} );
}

//Introspected fields, for a TableSchema built in a spec.
export type FieldJson = { name:string; type:object };
export const id = ( name='id' ):FieldJson=>({ name, type: {kind: "NON_NULL", name: null, ofType: {kind: "SCALAR", name: "ID"}} });
export const scalar = ( name:string, type="String" ):FieldJson=>({ name, type: {kind: "SCALAR", name: type} });
export const required = ( name:string, type="String" ):FieldJson=>({ name, type: {kind: "NON_NULL", name: null, ofType: {kind: "SCALAR", name: type}} });
export const object = ( name:string, type:string ):FieldJson=>({ name, type: {kind: "OBJECT", name: type} });
export const schemaOf = ( name:string, ...fields:FieldJson[] )=>new TableSchema( {name, enums: new Map(), fields} );
//A slug table:  id, a required name and slug, then `more`.
export const slugSchema = ( name:string, ...more:FieldJson[] )=>schemaOf( name, id(), required("name"), required("slug"), ...more );
//The gateway's ServerConnection:  introspected columns plus the grafted opcSessions object (config/introspection/serverConnection.jsonnet).
export const serverConnectionSchema = ()=>slugSchema( "ServerConnection", scalar("url"), scalar("deleted", "DateTime"), scalar("description"), object("provider", "Provider"), object("opcSessions", "OpcSessions") );

//`n` rows of a slug table numbered on from `from`, named for `noun`:  new objects every call, as every query builds.
export const slugRows = ( n:number, from=0, noun='row' )=>Array.from( {length: n}, (_, i)=>({id: from+i+1, name: `${noun} ${from+i+1}`, slug: `${noun}${from+i+1}`}) );

//A slug row that keeps whatever else it is given, for a page or a form to edit.
export class Widget extends SlugRow<Widget>{
	constructor( obj:any ){
		super( "Widget", obj ?? {} );
		for( const [key, value] of Object.entries(obj ?? {}) ){
			if( !(key in this) )
				this[key] = value;
		}
	}
	get properties():Partial<Widget>{ return new Widget( this ); }
	[key:string]:any;
}

//A log entry as the logs query answers it, `line` seconds before 2026-01-01 - newest first, as the default view sorts.
export const logTemplate = '00000000000000000000000000000001';
export const logEntry = ( line:number, extra:object={} )=>({ templateId: logTemplate, argIds: [], level: 'Information', tags: [], line, time: new Date(Date.UTC(2026, 0, 1)-line*1000).toISOString(), userId: 0, fileId: logTemplate, functionId: logTemplate, ...extra });