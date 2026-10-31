import { TestBed } from '@angular/core/testing';
import { provideRouter, Router, Routes } from '@angular/router';
import { RouteItem } from '../../pages/component-sidenav/route-item';
import { RouteStore } from '../route-store';
import { RouteSearchProvider } from './route-search-provider';
import { Blank } from 'jde-spa/testing';

const routes:Routes = [
	{ path: '', title: 'Home', component: Blank },
	{ path: 'login', component: Blank, data: {name: 'Login'} },
	{ path: 'gateways', title: 'Gateways', component: Blank, data: {summary: 'Available Gateways', icon: 'hub'} },
	{ path: 'gateways/:gateway', title: ':gateway', component: Blank },
	{ path: 'gateways/:gateway/:connection', component: Blank, children: [ { path: '**', component: Blank } ] },
	{ path: 'access', title: 'Access', component: Blank, data: {summary: 'Configure User Access', icon: 'admin_panel_settings'} },
	{ path: 'access', component: Blank, children: [
		{ path: 'users/:slug', component: Blank },
		{ path: ':collectionDisplay', component: Blank, data: { collections: [ {path: 'users', data: {icon: 'person'}}, 'roles' ] } },
	]},
	{ path: 'apps', title: 'Applications', component: Blank },
	{ path: 'apps/gateways/:instance', component: Blank, children: [ { path: '', component: Blank }, { path: ':connection', component: Blank } ] },
];

describe( 'RouteSearchProvider', ()=>{
	const create = ()=>{
		const stored = new Map<string,RouteItem[]>();
		TestBed.configureTestingModule({ providers: [ provideRouter(routes), { provide: RouteStore, useValue: { entries: ()=>stored } } ] });
		return { provider: TestBed.inject( RouteSearchProvider ), stored };
	};

	it( 'walks the static tree: titles, data, the collections behind :collectionDisplay; skips login, params, ** and duplicates', ()=>{
		const items = create().provider.items();
		const byRoute = new Map( items.map( i=>[i.route as string, i] ) );
		expect( [...byRoute.keys()].sort() ).toEqual( ['/', '/access', '/access/roles', '/access/users', '/apps', '/gateways'] );
		expect( byRoute.get('/gateways') ).toMatchObject( {title: 'Gateways', icon: 'hub', summary: 'Available Gateways'} );
		expect( byRoute.get('/access/users') ).toMatchObject( {title: 'Users', icon: 'person'} );
		expect( byRoute.get('/access/roles')?.title ).toBe( 'Roles' );
	});

	//the dynamic hits navigate by COMMANDS ARRAY, raw segments - router.navigate encodes each one, and a pre-encoded string
	//double-encoded them (angular-review3 #7).  The encoded url survives as the dedup key only.
	//reviews/angular-refactor.md B12:  a key is the parent's absolute url and a child's path is relative to it (RouteStore.key),
	//so the url is the two joined - no guessing the section of a bare key, or trimming a path that repeats its key's tail
	it( 'joins each key to its children\'s paths', ()=>{
		const { provider, stored } = create();
		stored.set( 'gateways/gw1', [ new RouteItem({title: 'Line 1', path: 'cn1'}) ] );
		stored.set( 'apps', [ new RouteItem({title: 'Gateway/Debug', path: 'gateways/Debug'}) ] );//a path of two segments
		stored.set( 'apps/gateways', [ new RouteItem({title: 'Debug', path: 'Debug'}) ] );
		stored.set( 'access/users', [ new RouteItem({title: 'Alice', path: 'alice'}) ] );
		stored.set( 'nowhere', [ new RouteItem({title: 'Lost', path: 'x'}) ] );//no route matches - dropped
		const items = provider.items();
		const find = ( title:string )=>items.find( i=>i.title==title );
		expect( find('Line 1') ).toMatchObject( {route: ['/gateways', 'gw1', 'cn1'], summary: 'gateways / gw1'} );
		expect( find('Gateway/Debug')?.route ).toEqual( ['/apps', 'gateways', 'Debug'] );
		expect( find('Debug') ).toBeUndefined();//same url as the Applications entry, which came first
		expect( find('Alice')?.route ).toEqual( ['/access', 'users', 'alice'] );
		expect( find('Lost') ).toBeUndefined();
	});

	//review3 #7: every Google-provisioned slug is '<provider>-<email>'.  Pre-encoding gave the router '%40', which it
	//encoded again to '%2540', so paramMap yielded '…%40…', slugQuery matched no row and the page bounced.
	it( 'hands the router raw segments, so an @ slug survives navigation', async ()=>{
		const { provider, stored } = create();
		stored.set( 'access/users', [ new RouteItem({title: 'John', path: 'Google-johnmduffy@gmail.com'}) ] );
		const item = provider.items().find( i=>i.title=='John' )!;
		expect( item.route ).toEqual( ['/access', 'users', 'Google-johnmduffy@gmail.com'] );
		const router = TestBed.inject( Router );
		await router.navigate( item.route as any[] );
		expect( router.url ).toBe( '/access/users/Google-johnmduffy@gmail.com' );//NOT %2540
		expect( decodeURIComponent(router.url.split('/').pop()!) ).toBe( 'Google-johnmduffy@gmail.com' );
	});

	it( 'ranks title starts-with ahead of contains, ignores scoped queries', async ()=>{
		const { provider, stored } = create();
		stored.set( 'gateways', [ new RouteItem({title: 'Plant gateway', path: 'plant'}) ] );
		const results = await provider.search( 'Gate', undefined, 10 );//as typed:  the rank is case-blind
		expect( results.map(r=>r.title) ).toEqual( ['Gateways', 'Plant gateway'] );
		expect( await provider.search('gate', 'user', 10) ).toEqual( [] );
		expect( await provider.search('', undefined, 10) ).toEqual( [] );
	});
});