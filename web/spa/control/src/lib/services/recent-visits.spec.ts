import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter, Router } from '@angular/router';
import { vi } from 'vitest';
import { RouteItem } from '../pages/component-sidenav/route-item';
import { IPROFILE_SERVICE } from './profile/profile-service';
import { ProfileStore } from './profile/profile-store';
import { RecentVisit, RecentVisits, SEGMENT_NAME, SegmentName, timeAgo } from './recent-visits';

@Component( {template: ''} )
class Blank {}

const crumbs = ( ...titles:string[] )=>[ new RouteItem({title: 'Home'}), ...titles.map( title=>new RouteItem({title}) ) ];
const saved = ( url:string ):RecentVisit=>({ url, title: url, path: '', section: 'gateways', at: 0 });

describe( 'RecentVisits', ()=>{
	let history:Record<string,RecentVisit[]>;//the profile row, per user
	let user:ReturnType<typeof signal<string|undefined>>;
	let store:{ load:ReturnType<typeof vi.fn>, save:ReturnType<typeof vi.fn> };
	let segmentName:SegmentName|undefined;//the site's SEGMENT_NAME, when a test gives it one
	beforeEach( ()=>{
		history = {};
		segmentName = undefined;
		user = signal<string|undefined>( 'alice' );
		store = {
			load: vi.fn( async ( _key:string, fallback:RecentVisit[] )=>history[user()!] ?? fallback ),
			save: vi.fn( async ( _key:string, value:RecentVisit[] )=>{ history[user()!] = value; } )
		};
		TestBed.configureTestingModule( {providers: [
			provideRouter( [
				{ path: 'gateways', data: {icon: 'hub'}, component: Blank },
				{ path: 'gateways/:gateway', component: Blank },
				{ path: 'gateways/:gateway/broken', component: Blank, resolve: {x: ()=>{ throw new Error('deleted'); }} }
			] ),
			{provide: ProfileStore, useValue: store},
			{provide: IPROFILE_SERVICE, useValue: {userKey: user, load: async ()=>null, save: async ()=>{}}},
			{provide: SEGMENT_NAME, useFactory: ()=>segmentName}
		]} );
	});
	const urls = ()=>TestBed.inject( RecentVisits ).visits().map( v=>v.url );

	it( 'names a page by its crumbs:  the last is the title, the two above it the path, the section icon when the crumb has none', async ()=>{
		const recent = TestBed.inject( RecentVisits );
		await recent.visit( '/gateways/gw1/local?x=1#top', crumbs('Gateways', 'gw1', 'Local OPC Server') );
		expect( recent.visits()[0] ).toMatchObject( {url: '/gateways/gw1/local', title: 'Local OPC Server', path: 'Gateways › gw1', section: 'gateways', icon: 'hub'} );
	});

	it( 'counts only pages below a section, newest first, without repeats, six at most', async ()=>{
		const recent = TestBed.inject( RecentVisits );
		await recent.visit( '/', crumbs() );
		await recent.visit( '/gateways', crumbs('Gateways') );
		await recent.visit( '/login', crumbs('Login') );
		for( let i=1; i<=7; ++i )
			await recent.visit( `/gateways/gw${i}`, crumbs('Gateways', `gw${i}`) );
		await recent.visit( '/gateways/gw5', crumbs('Gateways', 'gw5') );
		expect( urls() ).toEqual( ['/gateways/gw5', '/gateways/gw7', '/gateways/gw6', '/gateways/gw4', '/gateways/gw3', '/gateways/gw2'] );
	});

	it( 'puts the saved history behind the pages opened this session', async ()=>{
		history['alice'] = [ saved('/gateways/a'), saved('/gateways/b') ];
		await TestBed.inject( RecentVisits ).visit( '/gateways/c', crumbs('Gateways', 'c') );
		expect( urls() ).toEqual( ['/gateways/c', '/gateways/a', '/gateways/b'] );
	});

	it( 'swaps in the next user\'s history instead of merging the last one\'s', async ()=>{
		history['bob'] = [ saved('/gateways/bobs') ];
		const recent = TestBed.inject( RecentVisits );
		await recent.visit( '/gateways/alices', crumbs('Gateways', 'alices') );
		user.set( 'bob' );
		await recent.load();
		expect( urls() ).toEqual( ['/gateways/bobs'] );
	});

	it( 'drops a page that fails to open', async ()=>{
		const recent = TestBed.inject( RecentVisits );
		await recent.visit( '/gateways/gw1/broken', crumbs('Gateways', 'gw1', 'Broken') );
		await recent.visit( '/gateways/gw1', crumbs('Gateways', 'gw1') );
		await TestBed.inject( Router ).navigateByUrl( '/gateways/gw1/broken' ).catch( ()=>{} );
		expect( urls() ).toEqual( ['/gateways/gw1'] );
	});

	//reviews/m3-closing.md #25:  a deep link to a deleted page fails in its resolver, before anything has loaded the history;
	//forgetting from the empty list let the load bring the page straight back.
	it( 'forgets a saved page even before the history has loaded', async ()=>{
		history['alice'] = [ saved('/access/roles/temp'), saved('/access/roles/engineer') ];
		const recent = TestBed.inject( RecentVisits );
		recent.forget( '/access/roles/temp' );
		await recent.load();
		expect( urls() ).toEqual( ['/access/roles/engineer'] );
	});

	it( 'renames a saved entry\'s title and path by SEGMENT_NAME, as the crumbs would name them now', async ()=>{
		segmentName = ( segments, i )=>segments[0]=='gateways' && i>=3 ? segments[i].replace( /^\d+~/, '' ) : undefined;
		history['alice'] = [ {url: '/gateways/gw1/local/5~pump1/5~motorRpm', title: '5~motorRpm', path: 'Local OPC Server › 5~pump1', section: 'gateways', at: 0},
			{url: '/access/roles/engineer', title: 'Engineer', path: 'Access › Roles', section: 'access', at: 0} ];
		const recent = TestBed.inject( RecentVisits );
		await recent.load();
		expect( recent.visits().map(v=>[v.title, v.path]) ).toEqual( [['motorRpm', 'Local OPC Server › pump1'], ['Engineer', 'Access › Roles']] );
	});

	it( 'writes a burst of visits to the profile once', async ()=>{
		vi.useFakeTimers();
		try{
			const recent = TestBed.inject( RecentVisits );
			for( const gw of ['a', 'b', 'c'] )
				await recent.visit( `/gateways/${gw}`, crumbs('Gateways', gw) );
			expect( store.save ).not.toHaveBeenCalled();
			await vi.advanceTimersByTimeAsync( RecentVisits.saveDelay );
			expect( store.save ).toHaveBeenCalledTimes( 1 );
			expect( history['alice'].map(v=>v.url) ).toEqual( ['/gateways/c', '/gateways/b', '/gateways/a'] );
		}finally{
			vi.useRealTimers();
		}
	});
});

describe( 'timeAgo', ()=>{
	const now = Date.UTC( 2026, 8, 19, 12 );
	it( 'reads like a person would say it', ()=>{
		expect( [0, 59_000, 12*60_000, 3*3_600_000, 30*3_600_000, 3*86_400_000].map( ms=>timeAgo(now-ms, now) ) )
			.toEqual( ['just now', 'just now', '12 min ago', '3 h ago', 'yesterday', '3 days ago'] );
	});
});
