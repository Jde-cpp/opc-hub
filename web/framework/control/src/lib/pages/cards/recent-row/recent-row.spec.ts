if( typeof globalThis.localStorage=="undefined" ){
	const backing = new Map<string,string>();
	(globalThis as any).localStorage = {
		getItem: ( k:string )=>backing.has(k) ? backing.get(k)! : null,
		setItem: ( k:string, v:string )=>{ backing.set(k, String(v)); },
		removeItem: ( k:string )=>{ backing.delete(k); },
		clear: ()=>backing.clear()
	};
}
import { Component, signal } from '@angular/core';
import { TestBed } from '@angular/core/testing';
import { provideRouter } from '@angular/router';
import { RecentVisits } from 'jde-spa';
import { RecentRow } from './recent-row';

@Component( {template: ''} ) class Dummy{}

//reviews/m3-closing.md #8:  a visit is stored as the url the router serialized - percent-encoded - and a string routerLink
//splits it on '/' and pushes each part as a raw segment, so the serializer encoded the '%' again:  a node under a browse
//name with a space (KEPServerEX's `Simulation Examples`) got a tile linking to `Simulation%2520Examples`, which is not found.
describe( 'RecentRow', ()=>{
	it( 'links a visited url with an encoded segment back to that url', ()=>{
		const url = '/gateways/gw/kep/Simulation%20Examples/Functions';
		TestBed.configureTestingModule( {providers: [
			provideRouter( [{ path: 'gateways/:gateway/:connection', component: Dummy, children: [{path: '**', component: Dummy}] }] ),
			{provide: RecentVisits, useValue: {visits: signal([{url, title: 'Functions', path: 'Gateways › gw', section: 'gateways', at: Date.now()}]), load: async ()=>{}, clear: ()=>{}}}
		]} );
		const fixture = TestBed.createComponent( RecentRow );
		fixture.detectChanges();
		expect( fixture.nativeElement.querySelector('a.recent-card').getAttribute('href') ).toBe( url );
	} );
} );
